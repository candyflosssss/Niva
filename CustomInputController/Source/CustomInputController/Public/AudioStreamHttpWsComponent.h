#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "AudioStreamHttpWsComponent.generated.h"

class UAudioStreamHttpWsSubsystem;
class UStreamProcSoundWave; // 新增前置声明

// 文本与viseme事件
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnAudioTextReceived, const FString&, Text);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnVisemeReceived, const TArray<int32>&, Visemes);

// 统计快照
USTRUCT(BlueprintType)
struct FAudioVisemeStats
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category="AudioStream|Stats")
    double AudioMsReceived = 0.0;   // 来自服务器的音频累计时长（不含补零）

    UPROPERTY(BlueprintReadOnly, Category="AudioStream|Stats")
    double AudioMsPadded = 0.0;     // 欠载等本地补零累计时长

    UPROPERTY(BlueprintReadOnly, Category="AudioStream|Stats")
    double AudioMsTotal = 0.0;      // 总计（Received + Padded）

    UPROPERTY(BlueprintReadOnly, Category="AudioStream|Stats")
    int32 VisemeStepsReceived = 0;  // 来自服务器的viseme步数

    UPROPERTY(BlueprintReadOnly, Category="AudioStream|Stats")
    int32 VisemeStepsPadded = 0;    // 本地补入的中性步数

    UPROPERTY(BlueprintReadOnly, Category="AudioStream|Stats")
    int32 VisemeStepsTotal = 0;     // 总计（Received + Padded）

    UPROPERTY(BlueprintReadOnly, Category="AudioStream|Stats")
    double VisemeMsReceived = 0.0;  // = VisemeStepsReceived * VisemeStepMs

    UPROPERTY(BlueprintReadOnly, Category="AudioStream|Stats")
    double VisemeMsPadded = 0.0;    // = VisemeStepsPadded * VisemeStepMs

    UPROPERTY(BlueprintReadOnly, Category="AudioStream|Stats")
    double VisemeMsTotal = 0.0;     // = VisemeStepsTotal * VisemeStepMs

    UPROPERTY(BlueprintReadOnly, Category="AudioStream|Stats")
    double DeltaMsReceived = 0.0;   // VisemeMsReceived - AudioMsReceived

    UPROPERTY(BlueprintReadOnly, Category="AudioStream|Stats")
    double DeltaMsTotal = 0.0;      // VisemeMsTotal - AudioMsTotal
};

/**
 * 组件版：每个挂载它的角色都可独立播放自己的流式音频。
 * 默认使用 PCM S16LE，16kHz，单声道；POST可覆盖。
 * POST JSON: { "ws_url": "wss://...", "sample_rate": 16000, "channels": 1 }
 */
UCLASS(ClassGroup=(Audio), meta=(BlueprintSpawnableComponent))
class CUSTOMINPUTCONTROLLER_API UAudioStreamHttpWsComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UAudioStreamHttpWsComponent();

    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override; // 新增

    // 可选：预期注册用Key（为空则自动生成）
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AudioStream|Routing")
    FString PreferredKey;

    // 默认音频参数（可被Push时的参数覆盖）
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AudioStream|Audio")
    int32 DefaultSampleRate = 16000;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AudioStream|Audio")
    int32 DefaultChannels = 1;

    // 方式一：按播放进度出队（推荐）
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AudioStream|Viseme")
    bool bPopVisemeByAudioProgress = true;

    // 方式二：定时8ms出队（备选）
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AudioStream|Viseme")
    bool bAutoPopViseme = false; // 默认关闭：推荐用按音频进度出队，避免与音频不同步

    // 嘴型步长（毫秒，默认8ms）；与服务端约定一致更稳
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AudioStream|Viseme", meta=(ClampMin="1.0", ClampMax="100.0"))
    float VisemeStepMs = 8.0f;

    // 播放前预热时长（毫秒）：累计足够缓冲再Play，降低起始噪音（默认120ms）
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AudioStream|Audio", meta=(ClampMin="0.0", ClampMax="1000.0"))
    float WarmupMs = 120.0f;

    // 欠载保护：缓冲过低时自动填充静音，避免爆音（默认开启）
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AudioStream|Audio")
    bool bPadSilenceOnUnderflow = false;

    // 欠载低水位阈值（以步数计），低于该步数认为欠载（默认提升到4步）
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AudioStream|Audio", meta=(ClampMin="1", ClampMax="64"))
    int32 UnderflowLowWaterSteps = 4;

    // 欠载发生时一次填充的静音步数（默认提升到8步，等于64ms@8ms步长）
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AudioStream|Audio", meta=(ClampMin="1", ClampMax="128"))
    int32 UnderflowPadSteps = 8;

    // 读取当前估算缓冲时长（毫秒）
    UFUNCTION(BlueprintPure, Category="AudioStream|Audio")
    float GetBufferedMilliseconds();

    // ���性嘴型索引（用于静音/预热/欠载填充时），默认0
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AudioStream|Viseme", meta=(ClampMin="0", ClampMax="14"))
    int32 NeutralVisemeIndex = 0;

    // 调试开关
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AudioStream|Debug")
    bool bDebugLogs = true;

    UFUNCTION(BlueprintCallable, Category="AudioStream|Viseme")
    void StartVisemeAutoPop();

    UFUNCTION(BlueprintCallable, Category="AudioStream|Viseme")
    void StopVisemeAutoPop();

    // 主动停止播放并清理缓冲
    UFUNCTION(BlueprintCallable, Category="AudioStream")
    void StopStreaming();

    // 直接推送PCM数据（供子系统或蓝图调���）
    UFUNCTION(BlueprintCallable, Category="AudioStream")
    void PushPcmData(const TArray<uint8>& Data, int32 InSampleRate = 16000, int32 InChannels = -1);

    // 供子系统调用：文本/viseme
    UFUNCTION(BlueprintCallable, Category="AudioStream|Text")
    void PushText(const FString& InText);

    UFUNCTION(BlueprintCallable, Category="AudioStream|Viseme")
    void PushViseme(const TArray<int32>& InViseme);

    // 新增：成对推送 viseme+confidence（长度可不同，按最短对齐，缺失的权重回退1.0）
    UFUNCTION(BlueprintCallable, Category="AudioStream|Viseme")
    void PushVisemeEx(const TArray<int32>& InViseme, const TArray<float>& InConfidence);

    // 新增：设置最近一次收到的置信度向量（兼容旧接口，不再作为入队依据）
    UFUNCTION(BlueprintCallable, Category="AudioStream|Viseme")
    void PushVisemeConfidence(const TArray<float>& InConfidence);

    // 文本/viseme 历史容器（全量保存）
    UPROPERTY(BlueprintReadOnly, Category="AudioStream|Text")
    TArray<FString> TextHistory;

    UPROPERTY(BlueprintReadOnly, Category="AudioStream|Viseme")
    TArray<int32> VisemeHistory; // 一维容器（对外保留）

    // 当前14维嘴型权重（one-hot）
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AudioStream|Viseme", meta=(ClampMin="1", ClampMax="64"))
    int32 VisemeFloatCount = 15; // 默认15，兼容上游可能输出0..14

    UPROPERTY(BlueprintReadOnly, Category="AudioStream|Viseme")
    TArray<float> VisemeFloat14; // 实际长度=VisemeFloatCount

    // 广播事件
    UPROPERTY(BlueprintAssignable, Category="AudioStream|Text")
    FOnAudioTextReceived OnTextReceived;

    UPROPERTY(BlueprintAssignable, Category="AudioStream|Viseme")
    FOnVisemeReceived OnVisemeReceived;

    // 查询此组件的注册Key
    UFUNCTION(BlueprintPure, Category="AudioStream|Routing")
    FString GetComponentKey() const { return RegisteredKey; }

    // 统计接口
    UFUNCTION(BlueprintCallable, Category="AudioStream|Stats")
    void ResetAudioVisemeStats();

    UFUNCTION(BlueprintPure, Category="AudioStream|Stats")
    FAudioVisemeStats GetAudioVisemeStats() const;

    // 便捷：判断时长是否一致
    UFUNCTION(BlueprintPure, Category="AudioStream|Stats")
    bool IsDurationConsistent(bool bUseTotal, float ToleranceMs, float& OutDeltaMs) const;

private:
    // 注册态
    bool bRegistered = false;
    FString RegisteredKey;

    // 播放状态
    bool bPlayStarted = false;
    double LastBufferedLogTimeSec = 0.0;

    // 待应用的格式变更
    bool bHasPendingFormatChange = false;
    int32 PendingSampleRate = -1;
    int32 PendingChannels = -1;
    // 低水位判定（毫秒）：缓冲低于该时长才切换SR/Ch
    UPROPERTY(EditAnywhere, Category="AudioStream|Audio", meta=(ClampMin="10.0", ClampMax="500.0"))
    float FormatSwitchLowWaterMs = 40.0f;

    // 挂起的新格式PCM缓冲
    TArray<uint8> PendingPcmBuffer;

    // 音频
    UPROPERTY()
    UStreamProcSoundWave* ProcSound = nullptr; // 自定义流程音频

    UPROPERTY()
    class UAudioComponent* AudioComp = nullptr;

    int32 SampleRate;
    int32 NumChannels;

    // 按播放进度出队相关统计
    int64 TotalQueuedBytes = 0;              // 总入队字节（会因重置而清零）
    int64 LastConsumedBytes = 0;             // 上次用于出队的累计消费（平滑后）
    int64 AccumConsumedForVisemeBytes = 0;   // 用于步长出队的累计
    int32 BytesPerStep = 256;                // = SR * Ch * 2 * (VisemeStepMs/1000)
    int64 WarmupBytes = 0;                   // = SR * Ch * 2 * (WarmupMs/1000)

    // 统计累计（不随播放重置）
    double TotalAudioMsReceived = 0.0;       // 仅统计来自服务器的PCM
    double TotalAudioMsPadded = 0.0;         // 本地补零的PCM
    int32  TotalVisemeStepsReceived = 0;     // 仅统计来自服务器的viseme步数
    int32  TotalVisemeStepsPadded = 0;       // 本地补入的中性步数

    // 平滑消费统计（解决渲染层计数被重置导致的抖动）
    int64 ConsumedBaseBytes = 0;             // 聚合偏移量
    int64 LastRawConsumedBytes = 0;          // 上一帧读取的原始值
    int64 LastSmoothedConsumedBytes = 0;     // 上一帧计算得到的平滑累计（单调不减）
    int64 SynthConsumedBytes = 0;            // 合成推进���累计（用于底层读数不变时按时间推进）
    double LastConsumeProgressTimeSec = 0.0; // 上次真实进度推进的时间戳

    // 成对队列：每步一个 (visemeIndex, confidence)；出队与播放进度一致
    TArray<TPair<int32, float>> VisemePairQueue;

    // 兼容：最近一帧置信度（已不再用于入队，仅保留避免外部依赖崩）
    TArray<float> LastVisemeConfidence;

    void EnsureAudioObjects();
    void ResetAudio();

    // 便捷：注册/注销到子系统
    void RegisterToSubsystem();
    void UnregisterFromSubsystem();

    // Viseme定时器（备选）
    FTimerHandle VisemePopTimer;
    void VisemePopTick();

    // 根据队首更新14维float数组
    void UpdateVisemeFloatFromHead();
    void SetNeutralVisemeFloat();

    // 参数变化时计算 BytesPerStep / WarmupBytes
    void UpdateTimingParams();

    // 读取“平滑后的已消费字节”（单调不减），并内部维护基线
    int64 GetSmoothedConsumedBytes();

    // 将PCM数据投递到音频线程入队，避免与渲染线程锁竞争
    void QueueAudioOnAudioThread(TArray<uint8>&& Bytes);
};
