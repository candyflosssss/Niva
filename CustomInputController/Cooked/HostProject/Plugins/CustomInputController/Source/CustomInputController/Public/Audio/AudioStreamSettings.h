#pragma once
#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "AudioStreamSettings.generated.h"

UENUM(BlueprintType)
enum class EAudioStreamProtocolMode : uint8
{
    LegacyHttpWs UMETA(DisplayName="Legacy HTTP + WS"),
    PureWebSocket UMETA(DisplayName="Pure WebSocket")
};

UCLASS(Config=Game, DefaultConfig, meta=(DisplayName="Audio Stream Settings"))
class CUSTOMINPUTCONTROLLER_API UAudioStreamSettings : public UDeveloperSettings
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, Config, Category="Protocol", meta=(DisplayName="默认服务模型", ToolTip="音频流组件默认协议模式；切换后仅显示当前协议对应的服务配置"))
    EAudioStreamProtocolMode DefaultProtocolMode = EAudioStreamProtocolMode::PureWebSocket;

    UPROPERTY(EditAnywhere, Config, Category="LegacyService|WebSocket", meta=(ToolTip="旧版 HTTP+WS 协议的 WebSocket 协议：ws/wss", EditCondition="DefaultProtocolMode == EAudioStreamProtocolMode::LegacyHttpWs", EditConditionHides))
    FString LegacyWsScheme = TEXT("ws");

    UPROPERTY(EditAnywhere, Config, Category="LegacyService|WebSocket", meta=(ToolTip="旧版 HTTP+WS 协议的主机:端口", EditCondition="DefaultProtocolMode == EAudioStreamProtocolMode::LegacyHttpWs", EditConditionHides))
    FString LegacyWsHost = TEXT("127.0.0.1:8000");

    UPROPERTY(EditAnywhere, Config, Category="LegacyService|WebSocket", meta=(ToolTip="旧版 HTTP+WS 协议的 WebSocket 路径前缀，通常为 /ws/", EditCondition="DefaultProtocolMode == EAudioStreamProtocolMode::LegacyHttpWs", EditConditionHides))
    FString LegacyWsPathPrefix = TEXT("/ws/");

    // 网络（HTTP 路径仅由 UAudioStreamHttpWsComponent 使用）
    UPROPERTY(EditAnywhere, Config, Category="LegacyService|Http", meta=(ToolTip="组件HTTP启动任务路径 /run", EditCondition="DefaultProtocolMode == EAudioStreamProtocolMode::LegacyHttpWs", EditConditionHides))
    FString DefaultHttpRunPath = TEXT("/run");

    UPROPERTY(EditAnywhere, Config, Category="LegacyService|Http", meta=(ToolTip="组件HTTP推流路径 /stream", EditCondition="DefaultProtocolMode == EAudioStreamProtocolMode::LegacyHttpWs", EditConditionHides))
    FString DefaultHttpStreamPath = TEXT("/stream");

    UPROPERTY(EditAnywhere, Config, Category="LegacyService|Http", meta=(ToolTip="组件HTTP结束推流路径 /end-stream", EditCondition="DefaultProtocolMode == EAudioStreamProtocolMode::LegacyHttpWs", EditConditionHides))
    FString DefaultHttpEndStreamPath = TEXT("/end-stream");

    UPROPERTY(EditAnywhere, Config, Category="PureWebSocketService|WebSocket", meta=(ToolTip="新协议 Pure WebSocket 的协议：ws/wss", EditCondition="DefaultProtocolMode == EAudioStreamProtocolMode::PureWebSocket", EditConditionHides))
    FString PureWebSocketScheme = TEXT("ws");

    UPROPERTY(EditAnywhere, Config, Category="PureWebSocketService|WebSocket", meta=(ToolTip="新协议 Pure WebSocket 的主机:端口，例如 localhost:8023", EditCondition="DefaultProtocolMode == EAudioStreamProtocolMode::PureWebSocket", EditConditionHides))
    FString PureWebSocketHost = TEXT("127.0.0.1:8023");

    UPROPERTY(EditAnywhere, Config, Category="PureWebSocketService|WebSocket", meta=(ToolTip="新协议 Pure WebSocket 的路径，通常为 /ws", EditCondition="DefaultProtocolMode == EAudioStreamProtocolMode::PureWebSocket", EditConditionHides))
    FString PureWebSocketPath = TEXT("/ws");


    // ========== 兼容旧配置（建议迁移到 LegacyService / PureWebSocketService） ==========
    UPROPERTY(EditAnywhere, Config, Category="Compatibility", meta=(DisplayName="显示兼容旧配置回退项", ToolTip="默认隐藏旧版兼容回退项，只有迁移旧配置时才需要打开"))
    bool bShowCompatibilityFallbackSettings = false;

    UPROPERTY(EditAnywhere, Config, Category="Compatibility", meta=(ToolTip="兼容旧配置：未设置分离配置时作为回退 WebSocket 协议", EditCondition="bShowCompatibilityFallbackSettings", EditConditionHides, AdvancedDisplay))
    FString DefaultWsScheme = TEXT("ws"); // 或 "wss"

    UPROPERTY(EditAnywhere, Config, Category="Compatibility", meta=(ToolTip="兼容旧配置：未设置分离配置时作为回退 WebSocket 主机:端口", EditCondition="bShowCompatibilityFallbackSettings", EditConditionHides, AdvancedDisplay))
    FString DefaultWsHost = TEXT("127.0.0.1:8000"); // host:port

    UPROPERTY(EditAnywhere, Config, Category="Compatibility", meta=(ToolTip="兼容旧配置：未设置分离配置时作为回退 WebSocket 路径", EditCondition="bShowCompatibilityFallbackSettings", EditConditionHides, AdvancedDisplay))
    FString DefaultWsPathPrefix = TEXT("/ws/"); // 以/开头，以/结尾

    // ========== 组件默认参数（用于 UAudioStreamHttpWsComponent 构造 & 分帧） ==========
    // 默认采样率/声道（分帧计算、播放兜底，组件/上游可覆盖）
    UPROPERTY(EditAnywhere, Config, Category="ComponentDefaults", meta=(ToolTip="默认采样率（Hz），用于分帧与播放兜底"))
    int32 DefaultSampleRate = 16000;

    UPROPERTY(EditAnywhere, Config, Category="ComponentDefaults", meta=(ToolTip="默认声道数，用于分帧与播放兜底"))
    int32 DefaultChannels = 1;

    UPROPERTY(EditAnywhere, Config, Category="ComponentDefaults", meta=(ToolTip="组件默认抖动缓冲包数量"))
    int32 DefaultJitterBufferThreshold = 3;

    UPROPERTY(EditAnywhere, Config, Category="ComponentDefaults", meta=(ToolTip="组件默认目标缓冲时长（秒）"))
    float DefaultTargetBufferedTime = 0.1f;

    UPROPERTY(EditAnywhere, Config, Category="ComponentDefaults", meta=(ToolTip="组件默认起播最小缓冲时长（秒）"))
    float DefaultMinStartDuration = 0.02f;

    UPROPERTY(EditAnywhere, Config, Category="ComponentDefaults", meta=(ToolTip="最小流请求间隔（秒）"))
    float DefaultMinStreamRequestIntervalSeconds = 0.05f;

    UPROPERTY(EditAnywhere, Config, Category="ComponentDefaults", meta=(ToolTip="文本合并窗口（秒）"))
    float DefaultStreamTextCoalesceWindowSeconds = 0.12f;

    UPROPERTY(EditAnywhere, Config, Category="ComponentDefaults", meta=(ToolTip="最大待发送文本项数"))
    int32 DefaultMaxPendingStreamTextItems = 8;

    UPROPERTY(EditAnywhere, Config, Category="ComponentDefaults", meta=(ToolTip="文本缓冲刷新间隔（秒）"))
    float DefaultStreamTextFlushIntervalSeconds = 0.25f;

    UPROPERTY(EditAnywhere, Config, Category="ComponentDefaults", meta=(ToolTip="文本达到该字符数后立即刷新"))
    int32 DefaultStreamTextMaxBatchChars = 48;

    UPROPERTY(EditAnywhere, Config, Category="ComponentDefaults", meta=(ToolTip="流请求失败退避基础时间（秒）"))
    float DefaultStreamFailureCooldownBaseSeconds = 0.2f;

    UPROPERTY(EditAnywhere, Config, Category="ComponentDefaults", meta=(ToolTip="流请求失败退避最大时间（秒）"))
    float DefaultStreamFailureCooldownMaxSeconds = 3.0f;

    UPROPERTY(EditAnywhere, Config, Category="ComponentDefaults", meta=(ToolTip="默认 Viseme 步进时长（毫秒）"))
    int32 DefaultVisemeStepMs = 8;

    UPROPERTY(EditAnywhere, Config, Category="ComponentDefaults", meta=(ToolTip="默认 Viseme 关键帧间隔（毫秒）"))
    int32 DefaultVisemeKeyframeIntervalMs = 500;

    // 单帧PCM时长（毫秒），用于分帧组包（已使用于 SendPacket 分帧逻辑）
    UPROPERTY(EditAnywhere, Config, Category="Sync", meta=(ToolTip="单帧PCM时长（毫秒），影响分帧与编码（Opus）"))
    int32 FrameDurationMs = 20;

    // ========== Opus 编码（可选）：若启用则分帧后进行压缩编码 ==========
    // 注意：需要在工程中正确集成 libopus，并定义宏 CUSTOMINPUT_USE_OPUS 或相应编译标志。
    UPROPERTY(EditAnywhere, Config, Category="Opus", meta=(ToolTip="启用 Opus 编码（未集成库时会自动回退到PCM）"))
    bool bEnableOpus = true;

    UPROPERTY(EditAnywhere, Config, Category="Opus", meta=(EditCondition="bEnableOpus", ToolTip="Opus 目标比特率（bps），例如 16000~64000"))
    int32 OpusBitrate = 24000;

    UPROPERTY(EditAnywhere, Config, Category="Opus", meta=(EditCondition="bEnableOpus", ToolTip="Opus 复杂度（0-10），越高质量越好但更耗 CPU"))
    int32 OpusComplexity = 5;

    UPROPERTY(EditAnywhere, Config, Category="Opus", meta=(EditCondition="bEnableOpus", ToolTip="启用前向纠错（FEC），在丢包场景下更稳"))
    bool bOpusUseFEC = false;

    UPROPERTY(EditAnywhere, Config, Category="Opus", meta=(EditCondition="bEnableOpus", ToolTip="预估丢包率百分比（0-100），用于编码器丢包优化"))
    int32 OpusPacketLossPct = 0;


    // ========== 调试与日志 ==========
    UPROPERTY(EditAnywhere, Config, Category="Debug", meta=(ToolTip="子系统统计日志默认开关（已使用）"))
    bool bStatsLiveLogDefault = false;

    // ========== NetMic 默认连接配置（供 UNetMicWsComponent 自动连接使用） ==========
    UPROPERTY(EditAnywhere, Config, Category="NetMic", meta=(ToolTip="网麦 WebSocket 服务器地址（例如 ws://127.0.0.1:8780）"))
    FString DefaultNetMicWsUrl = TEXT("");


    // 静态访问器，方便统一读取设置
    static const UAudioStreamSettings* Get();
    FString GetEffectiveWsScheme(EAudioStreamProtocolMode ProtocolMode) const;
    FString GetEffectiveWsHost(EAudioStreamProtocolMode ProtocolMode) const;
    FString GetEffectiveWsPath(EAudioStreamProtocolMode ProtocolMode) const;

    // 确保在对象初始化时加载 Config（便于在早期读取）
    virtual void PostInitProperties() override;

#if WITH_EDITOR
    virtual bool CanEditChange(const FProperty* InProperty) const override;
#endif
};