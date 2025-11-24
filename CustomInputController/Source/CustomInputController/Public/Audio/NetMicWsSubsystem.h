#pragma once
#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "IWebSocket.h"
#include "NetMicWsSubsystem.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnNetMicAudioBinary, const TArray<uint8>&, Data);

// 一个最小可用的“网络麦克风”子系统：
// - 通过 HTTP POST 获取目标 WebSocket 地址并连接
// - 接收 WebSocket 二进制音频帧并按时间长度做环形缓存（默认15秒）
// - 暂不实现真正的转发，只保留 EnableForward/SetForwardTargets 接口
UCLASS()
class CUSTOMINPUTCONTROLLER_API UNetMicWsSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()
public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// 连接：通过 HTTP POST 获取 wsUrl 后再连接；每次调用都会重置暂存区
	UFUNCTION(BlueprintCallable, Category="NetMic")
	void StartByPost(const FString& HttpUrl, const FString& JsonBody = TEXT("{}"));

	// 直接连接指定 WebSocket（有时外部已拿到 wsUrl）；每次调用都会重置暂存区
	UFUNCTION(BlueprintCallable, Category="NetMic")
	void StartDirect(const FString& WsUrl);

	// 关闭麦克风（断开 WebSocket 并清空暂存）
	UFUNCTION(BlueprintCallable, Category="NetMic")
	void StopMic();

	// 暂存上限（秒），运行时可调整
	UFUNCTION(BlueprintCallable, Category="NetMic")
	void SetMaxBufferSeconds(float InSeconds) { MaxBufferSeconds = FMath::Max(0.f, InSeconds); }

	UFUNCTION(BlueprintCallable, Category="NetMic")
	float GetMaxBufferSeconds() const { return MaxBufferSeconds; }

	UFUNCTION(BlueprintCallable, Category="NetMic")
	float GetBufferedSeconds() const;

	// 转发开关与目标（占位，暂不实际发送）
	UFUNCTION(BlueprintCallable, Category="NetMic|Forward")
	void EnableForward(bool bEnable) { bForwardEnabled = bEnable; }

	UFUNCTION(BlueprintCallable, Category="NetMic|Forward")
	void SetForwardTargets(const TArray<FString>& Targets) { ForwardTargets = Targets; }

	// 最新一帧到达事件（蓝图）
	UPROPERTY(BlueprintAssignable, Category="NetMic")
	FOnNetMicAudioBinary OnAudioBinary;

private:
	void ResetBuffer();
	void ConnectWebSocket(const FString& Url);
	void CloseWebSocket();

	void OnWsConnected();
	void OnWsError(const FString& Error);
	void OnWsClosed(int32 StatusCode, const FString& Reason, bool bWasClean);
	void OnWsText(const FString& Message);
	void OnWsBinary(const void* Data, SIZE_T Size, SIZE_T BytesRemaining);

private:
	TSharedPtr<IWebSocket> Socket;
	struct FPacket { double TimeSec = 0.0; TArray<uint8> Bytes; };
	mutable FCriticalSection BufferCS;
	TArray<FPacket> Ring;
	float MaxBufferSeconds = 15.f; // 默认15秒
	bool bForwardEnabled = false;
	TArray<FString> ForwardTargets;
};
