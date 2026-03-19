#pragma once

#include "CoreMinimal.h"
#include "HAL/CriticalSection.h"
#include "Components/ActorComponent.h"
// HTTP request typedef (FHttpRequestPtr)
#include "Interfaces/IHttpRequest.h"
#include "Audio/AudioStreamHttpWsSubsystem.h" // Include for AudioStreamPacket::FHeader
#include "Audio/AudioStreamSettings.h"
#include "Sound/SoundWaveProcedural.h"
#include "Components/AudioComponent.h"
#include "AudioStreamHttpWsComponent.generated.h"

class UAudioStreamHttpWsSubsystem;
class FJsonObject;

// Add a dynamic delegate for Viseme array update to be UHT-friendly
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnVisemeArrayUpdated);

struct FAudioPacketBuffer
{
    uint32 Seq;
    TArray<uint8> Data;

    bool operator<(const FAudioPacketBuffer& Other) const
    {
        return Seq < Other.Seq;
    }
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

    // 可选：预期注册用Key（为空则由子系统分配或生成）
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AudioStream|Routing")
    FString PreferredKey;

    // 注册后由子系统分配并保持的唯一识别码（UUID）
    UPROPERTY(BlueprintReadOnly, ReplicatedUsing=OnRep_RegisteredUuid, Category="AudioStream|Routing")
    FString RegisteredUuid;

    UFUNCTION()
    void OnRep_RegisteredUuid();

    // 查询此组件的注册 UUID
    UFUNCTION(BlueprintPure, Category="AudioStream|Routing")
    FString GetComponentUuid() const { return RegisteredUuid; }

    // ===== 网络控制（组件拥有） =====
    UFUNCTION(BlueprintCallable, Category="AudioStream")
    void StartRunAndConnect(const FString& ServerHostWithPort = TEXT("127.0.0.1:8001"),
                            const FString& CallbackUrl = TEXT(""),
                            const FString& TargetUuid = TEXT(""),
                            int32 SampleRate = 16000,
                            int32 Channels = 1,
                            bool bUseHttps = false,
                            const FString& HttpRunPath = TEXT("/run"),
                            const FString& WsPathPrefix = TEXT("/ws/"),
                            bool bSoftReconnect = false);

    UFUNCTION(BlueprintCallable, Category="AudioStream")
    void PostStreamText(const FString& Text);
    UFUNCTION(BlueprintCallable, Category="AudioStream")
    void PostEndStream();

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AudioStream|Network")
    EAudioStreamProtocolMode ProtocolMode = EAudioStreamProtocolMode::PureWebSocket;

    // Minimum interval between queued HTTP stream requests (/stream and /end-stream).
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AudioStream|Network", meta=(ClampMin="0.0", UIMin="0.0"))
    float MinStreamRequestIntervalSeconds = 0.05f;

    // Coalesce adjacent text chunks inside this window to reduce high-frequency /stream requests.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AudioStream|Network", meta=(ClampMin="0.0", UIMin="0.0"))
    float StreamTextCoalesceWindowSeconds = 0.12f;

    // Soft cap for pending text items; excess items are merged into the latest text request.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AudioStream|Network", meta=(ClampMin="1", UIMin="1"))
    int32 MaxPendingStreamTextItems = 8;

    // Coalesced text is flushed into the HTTP queue at this interval.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AudioStream|Network", meta=(ClampMin="0.02", UIMin="0.02"))
    float StreamTextFlushIntervalSeconds = 0.25f;

    // Flush pending text immediately when this character count is reached.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AudioStream|Network", meta=(ClampMin="1", UIMin="1"))
    int32 StreamTextMaxBatchChars = 48;

    // Base cooldown applied after a failed /stream or /end-stream request.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AudioStream|Network", meta=(ClampMin="0.0", UIMin="0.0"))
    float StreamFailureCooldownBaseSeconds = 0.2f;

    // Max cooldown cap for exponential backoff after consecutive failures.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AudioStream|Network", meta=(ClampMin="0.0", UIMin="0.0"))
    float StreamFailureCooldownMaxSeconds = 3.0f;

    // 强制关闭 WebSocket
    UFUNCTION(BlueprintCallable, Category="AudioStream")
    void CloseWebSocket(bool bKeepQueue = false);

    // 在组件里解析 WebSocket 文本消息（仅解析，后续的转发/播放先注释）
    void ProcessWebSocketMessage(const FString& Message);

    // 接收来自 Socket 的消息（由 Subsystem 分发）
    void ReceiveSocketMessage(const AudioStreamPacket::FHeader& Header, const TArray<uint8>& Payload);

    // 抖动缓冲包数量（收到多少包后开始播放）
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AudioStream|Playback")
    int32 JitterBufferThreshold = 3;

    // 目标缓冲时间（秒），用于控制向SoundWave投递的速度
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AudioStream|Playback")
    float TargetBufferedTime = 0.1f;

    // 起播所需的最小缓冲时长（秒），避免小包导致永远不满足；默认更小以保证尽快起播
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AudioStream|Playback")
    float MinStartDuration = 0.02f;

    // ===== Viseme 配置 =====
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AudioStream|Viseme")
    int32 VisemeStepMs = 8; // 每个Viseme步进时长，和上游一致

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AudioStream|Viseme")
    int32 VisemeKeyframeIntervalMs = 500; // 预留（暂未使用）

    // 消费 Viseme 时触发（BP 可实现）
    UFUNCTION(BlueprintImplementableEvent, Category="AudioStream|Viseme")
    void OnViseme(int32 Viseme, float Confidence);

    // 当前口型值数组（长度15），每次消费时根据index填充对应置信度，其他清零；队列耗尽时整体清零
    UPROPERTY(BlueprintReadOnly, Category="AudioStream|Viseme")
    TArray<float> CurrentVisemeArray;

    static constexpr int32 VisemeArraySize = 15; // 满足 AnimBP 访问 0..14

    // 获取当前口型数组（保证长度为15）
    UFUNCTION(BlueprintPure, Category="AudioStream|Viseme")
    const TArray<float>& GetCurrentVisemeArray();

    // 当口型数组更新时广播（AnimBP可绑定复制到自己的变量）
    UPROPERTY(BlueprintAssignable, Category="AudioStream|Viseme")
    FOnVisemeArrayUpdated OnVisemeArrayUpdated;

    // 工具：确保数组长度为指定大小并全部清零
    void EnsureAndZeroVisemeArray(int32 Size = VisemeArraySize);

    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
    // 注册态
    bool bRegistered = false;

    // 注册/注销到子系统（使用UE RPC约束）
    UFUNCTION(Server, Reliable)
    void RegisterToSubsystem();


    void UnregisterFromSubsystem();

    // ===== 组件持有的会话状态 =====
    TSharedPtr<class IWebSocket> WebSocket;
    FString ActiveTaskId;
    FString ActiveHttpHost; // host:port
    bool bActiveUseHttps = false;
    int32 ActiveWsSampleRate = 16000;
    int32 ActiveWsChannels = 1;
    // TSharedPtr<IHttpRequest, ESPMode::ThreadSafe> CurrentHttpRequest; // Removing unused member to avoid confusion/issues

    // 保存当前会话使用的路径（由 StartRunAndConnect 设置），用于重连时重用
    FString ActiveHttpRunPath = TEXT("/run");
    FString ActiveWsPathPrefix = TEXT("/ws/");
    bool bPureWsTaskStarted = false;
    bool bPureWsAwaitingTaskStart = false;

    // 在下一次 CloseWebSocket 时保留队列（用于软重连避免丢数据）
    bool bPreserveQueuesNextClose = false;

    // Internal helpers
    void ConnectWebSocket(const FString& Url);
    FString BuildActiveWebSocketUrl() const;
    void BeginPureWsTask(bool bGenerateNewTaskId);
    bool SendPureWsMessage(const FString& Action, const TSharedPtr<FJsonObject>& Payload, const FString& TaskIdOverride = FString());
    void HandleIncomingAudioChunk(const FString& Base64Audio, int32 SampleRate, int32 Channels);
    void HandleIncomingVisemeChunk(const TArray<int32>& Visemes, const TArray<float>& Confidence);
    void HandleIncomingTextChunk(const FString& Text);
    void ApplySettingsDefaultsFromProject();
    void EnqueuePendingWebSocketMessage(const FString& Message);
    void ProcessPendingWebSocketMessages(int32 MaxMessagesToProcess);
    void ResetPendingWebSocketMessages();
    void ResetAudioPacketQueue();
    float GetBufferedAudioDurationSeconds() const;
    void LogStreamPushDispatch(const FString& ProtocolLabel, const FString& Text);
    void LogIncomingAudioReturn(int32 Base64AudioLen);
    // 发起 /run POST 并解析 task_id（从 StartRunAndConnect 中抽取的实现）
    void RequestRunTask(const FString& ServerHostWithPort, const FString& CallbackUrl, const FString& TargetUuid, int32 SampleRate, int32 Channels, bool bUseHttps, const FString& HttpRunPath, const FString& WsPathPrefix);

    // Queue handling
    struct FStreamQueueItem
    {
        enum class EType : uint8 { Text, EndStream };
        EType Type;
        FString TextContent;
        double EnqueueTimeSeconds = 0.0;
    };
    TArray<FStreamQueueItem> StreamQueue;
    bool bIsProcessingStreamQueue = false;
    void ProcessNextStreamQueueItem();
    void ScheduleProcessNextStreamQueueItem(float DelaySeconds = 0.0f);
    bool TryCoalesceQueuedText(const FString& Text, double NowSeconds);
    void FlushPendingStreamText(bool bForce);
    void SchedulePendingStreamTextFlush(float DelaySeconds = 0.0f);
    void CancelActiveStreamRequest(const TCHAR* Reason);

    FTimerHandle StreamQueueProcessTimerHandle;
    FTimerHandle PendingStreamTextFlushTimerHandle;
    double LastStreamRequestDispatchTime = 0.0;
    double NextStreamRequestAllowedTime = 0.0;

    FString PendingStreamTextBuffer;
    int32 ConsecutiveStreamFailures = 0;

    // Keep one in-flight /stream or /end-stream request owned by this component.
    TSharedPtr<IHttpRequest, ESPMode::ThreadSafe> ActiveStreamRequest;
    uint32 ActiveStreamRequestId = 0;
    uint32 StreamRequestIdCounter = 0;
    uint32 StreamPushRequestSequence = 0;
    uint32 StreamAudioReturnSequence = 0;
    double LastStreamPushRequestTimeSeconds = 0.0;

    // Reconnect logic
    FTimerHandle ReconnectTimerHandle;
    int32 ReconnectAttempts = 0;
    float ReconnectBaseDelaySeconds = 1.0f;
    float ReconnectMaxDelaySeconds = 30.0f;

    // 状态标记：是否曾成功连接（仅在曾经连接后，断开时才触发重连）
    bool bHasEverConnected = false;
    // 手动关闭标记：如果组件主动关闭 WS（例如 EndPlay 或 用户调用 CloseWebSocket），则不要触发重连
    bool bManualClose = false;

    // Schedule cancelable reconnect attempt
    void ScheduleReconnect();
    void CancelReconnect();

    // 音频缓冲队列
    TArray<FAudioPacketBuffer> AudioPacketQueue;
    int32 QueuedAudioBytes = 0;
    uint32 LastPlayedSeq = 0;

    FCriticalSection PendingWebSocketMessagesCS;
    TArray<FString> PendingWebSocketMessages;

    static constexpr uint8 LocalDecodedPcmFlag = 0x02;
    static constexpr int32 MaxPendingWebSocketMessagesPerTick = 4;
    static constexpr int32 MaxAudioPacketsToFeedPerTick = 6;
    static constexpr int32 MaxAudioBytesToFeedPerTick = 256 * 1024;
    
    // 播放时间追踪
    double PlaybackStartTime = 0.0;
    double TotalAudioFedDuration = 0.0;
    bool bIsPlaying = false;
    
    // Viseme 播放头（按实际播放的 DeltaTime 累加，用于对齐口型与音频）
    double VisemePlayheadSec = 0.0;
    float LastDeltaTime = 0.0f;
    
    // 标记是否需要重置音频流（在Tick中处理）
    bool bPendingStreamReset = false;

    // 强制下一次写入音频数据时应用淡入（用于平滑句子间或其他不连续处的衔接）
    bool bForceNextFadeIn = false;

    // 本地生成的音频序列号（用于WebSocket接收到的无序包）
    uint32 LocalAudioSeq = 0;

    UPROPERTY()
    USoundWaveProcedural* SoundStream;

    UPROPERTY()
    UAudioComponent* AudioPlayer;

    void FeedAudio();
    void InitAudioComponents();

#if defined(CUSTOMINPUT_USE_OPUS)
    // Opus 解码器实例（单组件按当前会话的采样率/声道初始化）
    void* OpusDecoderHandle = nullptr; // 实际类型为 OpusDecoder*
    void InitOpusDecoder(int32 SampleRate, int32 Channels);
    void DestroyOpusDecoder();
#endif

    // ===== Viseme 队列与消费进度 =====
    TArray<int32> VisemeQueue;
    TArray<float> VisemeConfQueue;
    int32 VisemeStepsEmitted = 0; // 已消费步数（从TotalAudioFedDuration推导）

    // 标记：本帧 viseme 数组是否更新
    bool bVisemeArrayDirty = false;
};
