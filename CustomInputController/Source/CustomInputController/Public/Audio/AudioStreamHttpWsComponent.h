#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
// HTTP request typedef (FHttpRequestPtr)
#include "Interfaces/IHttpRequest.h"
#include "Audio/AudioStreamHttpWsSubsystem.h" // Include for AudioStreamPacket::FHeader
#include "Sound/SoundWaveProcedural.h"
#include "Components/AudioComponent.h"
#include "AudioStreamHttpWsComponent.generated.h"

class UAudioStreamHttpWsSubsystem;

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

    // 抖动缓冲阈值（包数量）
    // UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AudioStream|Playback")
    // int32 JitterBufferThreshold = 3;  // Duplicate removed

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

    // Internal helpers
    void ConnectWebSocket(const FString& Url);
    // 发起 /run POST 并解析 task_id（从 StartRunAndConnect 中抽取的实现）
    void RequestRunTask(const FString& ServerHostWithPort, const FString& CallbackUrl, const FString& TargetUuid, int32 SampleRate, int32 Channels, bool bUseHttps, const FString& HttpRunPath, const FString& WsPathPrefix);

    // Queue handling
    struct FStreamQueueItem
    {
        enum class EType : uint8 { Text, EndStream };
        EType Type;
        FString TextContent;
    };
    TArray<FStreamQueueItem> StreamQueue;
    bool bIsProcessingStreamQueue = false;
    void ProcessNextStreamQueueItem();

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
    uint32 LastPlayedSeq = 0;
    
    // 播放时间追踪
    double PlaybackStartTime = 0.0;
    double TotalAudioFedDuration = 0.0;
    bool bIsPlaying = false;
    
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
};
