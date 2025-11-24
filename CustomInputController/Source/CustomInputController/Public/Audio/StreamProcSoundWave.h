#pragma once

#include "CoreMinimal.h"
#include "Sound/SoundWaveProcedural.h"
#include <atomic>
#include "Containers/Queue.h"
#include "StreamProcSoundWave.generated.h"

UCLASS()
class CUSTOMINPUTCONTROLLER_API UStreamProcSoundWave : public USoundWaveProcedural
{
	GENERATED_BODY()
public:
	// 公开设置采样率和声道，避免外部访问受保护成员
	UFUNCTION()
	void SetParams(int32 InSampleRate, int32 InNumChannels)
	{
		SampleRate = InSampleRate;
		NumChannels = InNumChannels;
	}

	// 渲染线程调用：累计已消费字节
	virtual int32 OnGeneratePCMAudio(TArray<uint8>& OutAudio, int32 NumSamples) override;
	virtual void OnBeginGenerate() override;
	virtual void OnEndGenerate() override;

	// 供游戏线程读取
	UFUNCTION(BlueprintCallable)
	int64 GetConsumedBytes() const { return ConsumedBytes.load(std::memory_order_relaxed); }

	UFUNCTION(BlueprintCallable)
	void ResetCounters() { ConsumedBytes.store(0, std::memory_order_relaxed); }

	// 外部入队PCM（任意线程安全）
	// 非UFUNCTION：指针参数不暴露给UHT/蓝图
	void EnqueuePcm(const uint8* Data, int32 NumBytes);

	UFUNCTION()
	void EnqueuePcmArray(const TArray<uint8>& Data) { EnqueuePcm(Data.GetData(), Data.Num()); }

	// 欠载平滑开关与参数
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Audio")
	bool bEnableUnderRunFade = false; // 默认关闭：避免Push瞬时抖动引起的可闻起伏

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Audio", meta=(ClampMin="0", ClampMax="20"))
	int32 FadeMs = 3; // 2~5ms 足够抹掉“呲啦”；关闭时忽略

	// 可配置：压缩阈值（字节）。到达阈值且未读数据较少时进行内存压缩
	UFUNCTION(BlueprintCallable, Category="Audio")
	void SetCompactThreshold(int32 InBytes) { CompactThreshold = FMath::Max(4096, InBytes); }

	UFUNCTION(BlueprintPure, Category="Audio")
	int32 GetCompactThreshold() const { return CompactThreshold; }

private:
	std::atomic<int64> ConsumedBytes{0};
	std::atomic<bool> bInGenerate{false};

	// 生产者(音频线程命令) → 渲染线程：无锁队列
	TQueue<TArray<uint8>, EQueueMode::Mpsc> PendingChunks;

	TArray<uint8> QueueData;
	int32 ReadOffset = 0;
	int32 CompactThreshold = 1 << 16; // 64KB后触发一次压缩

	bool  bNeedFadeIn = false; // 上一帧欠载 → 下一帧淡入（仅在 bEnableUnderRunFade 时生效）
};
