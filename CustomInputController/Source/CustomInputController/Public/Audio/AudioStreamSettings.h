#pragma once
#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "AudioStreamSettings.generated.h"

UCLASS(Config=Game, DefaultConfig, meta=(DisplayName="Audio Stream Settings"))
class CUSTOMINPUTCONTROLLER_API UAudioStreamSettings : public UDeveloperSettings
{
    GENERATED_BODY()
public:
    // 网络
    UPROPERTY(EditAnywhere, Config, Category="Network")
    int32 MediaUdpPort = 18500;

    // 默认服务器IP（用于 Standalone/ListenServer 的 AutoHello），仅支持 IPv4 字符串
    UPROPERTY(EditAnywhere, Config, Category="Network")
    FString DefaultServerIp = TEXT("127.0.0.1");

    // 音频
    UPROPERTY(EditAnywhere, Config, Category="Audio")
    int32 UdpRecvBufferBytes = 4*1024*1024; // 4MB

    // ========== 新增：默认 WebSocket 配置（用于 task/start 回退拼接） ==========
    UPROPERTY(EditAnywhere, Config, Category="WebSocket")
    FString DefaultWsScheme = TEXT("ws"); // 或 "wss"

    UPROPERTY(EditAnywhere, Config, Category="WebSocket")
    FString DefaultWsHost = TEXT("127.0.0.1:8000"); // host:port

    UPROPERTY(EditAnywhere, Config, Category="WebSocket")
    FString DefaultWsPathPrefix = TEXT("/ws/"); // 以/开头，以/结尾

    // ========== 新增：组件默认参数（用于 UAudioStreamHttpWsComponent 构造） ==========
    // 默认采样率/声道（可被上游数据覆盖）
    UPROPERTY(EditAnywhere, Config, Category="ComponentDefaults")
    int32 DefaultSampleRate = 16000;

    UPROPERTY(EditAnywhere, Config, Category="ComponentDefaults")
    int32 DefaultChannels = 1;

    // Viseme 出队策略
    UPROPERTY(EditAnywhere, Config, Category="ComponentDefaults")
    bool bPopVisemeByAudioProgress = true;

    UPROPERTY(EditAnywhere, Config, Category="ComponentDefaults")
    bool bAutoPopViseme = false;

    // 嘴型步长/预热与格式切换低水位
    UPROPERTY(EditAnywhere, Config, Category="Viseme")
    int32 VisemeStepMs = 8; // 步长（毫秒）

    UPROPERTY(EditAnywhere, Config, Category="Audio", meta=(ClampMin="0.0", ClampMax="1000.0"))
    float WarmupMs = 120.0f; // 播放前预热（毫秒）

    UPROPERTY(EditAnywhere, Config, Category="Audio", meta=(ClampMin="10.0", ClampMax="500.0"))
    float FormatSwitchLowWaterMs = 40.0f; // 低于该缓冲（毫秒）再切格式

    // 欠载保护
    UPROPERTY(EditAnywhere, Config, Category="Audio")
    bool bPadSilenceOnUnderflow = false;

    UPROPERTY(EditAnywhere, Config, Category="Audio", meta=(ClampMin="1", ClampMax="64"))
    int32 UnderflowLowWaterSteps = 4;

    UPROPERTY(EditAnywhere, Config, Category="Audio", meta=(ClampMin="1", ClampMax="128"))
    int32 UnderflowPadSteps = 8;

    // 中性嘴型索引 + 输出float长度
    UPROPERTY(EditAnywhere, Config, Category="Viseme", meta=(ClampMin="0", ClampMax="63"))
    int32 NeutralVisemeIndex = 0;

    UPROPERTY(EditAnywhere, Config, Category="Viseme", meta=(ClampMin="1", ClampMax="64"))
    int32 VisemeFloatCount = 15;

    // 组件调试日志默认开关
    UPROPERTY(EditAnywhere, Config, Category="Debug")
    bool bComponentDebugLogsDefault = false;

    // ========== 新增：流程音频（UStreamProcSoundWave）默认配置 ==========
    UPROPERTY(EditAnywhere, Config, Category="Audio")
    bool bEnableUnderRunFadeDefault = false;

    UPROPERTY(EditAnywhere, Config, Category="Audio", meta=(ClampMin="0", ClampMax="20"))
    int32 FadeMsDefault = 3;

    // 到达阈值时对队列进行压缩以控制内存占用
    UPROPERTY(EditAnywhere, Config, Category="Audio", meta=(ClampMin="4096"))
    int32 ProcCompactThresholdBytes = 65536; // 64KB

    // ========== 新增：子系统节奏与同步 ==========
    UPROPERTY(EditAnywhere, Config, Category="Sync")
    int32 TargetPreRollMs = 180; // 服务器分配客户端预热

    UPROPERTY(EditAnywhere, Config, Category="Sync")
    int32 TargetJitterMs = 180; // 客户端目标缓冲

    // 单帧PCM时长（毫秒），影响帧组包/对齐
    UPROPERTY(EditAnywhere, Config, Category="Sync")
    int32 FrameDurationMs = 20;

    // Viseme 关键帧周期
    UPROPERTY(EditAnywhere, Config, Category="Viseme")
    int32 VisemeKeyframeIntervalMs = 500; // 关键帧周期

    // 对时心跳
    UPROPERTY(EditAnywhere, Config, Category="Sync")
    int32 HeartbeatIntervalMs = 1000; // 心跳周期

    UPROPERTY(EditAnywhere, Config, Category="Sync", meta=(ClampMin="0.0", ClampMax="1.0"))
    float OffsetLerpAlpha = 0.1f; // 心跳融合系数

    // 日志
    UPROPERTY(EditAnywhere, Config, Category="Debug")
    bool bStatsLiveLogDefault = false;
};
