#pragma once
#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "AudioStreamSettings.generated.h"

UCLASS(Config=Game, DefaultConfig, meta=(DisplayName="Audio Stream Settings"))
class CUSTOMINPUTCONTROLLER_API UAudioStreamSettings : public UDeveloperSettings
{
    GENERATED_BODY()
public:
    // 网络（HTTP 路径仅由 UAudioStreamHttpWsComponent 使用）
    UPROPERTY(EditAnywhere, Config, Category="HttpPaths", meta=(ToolTip="组件HTTP启动任务路径 /run"))
    FString DefaultHttpRunPath = TEXT("/run");

    UPROPERTY(EditAnywhere, Config, Category="HttpPaths", meta=(ToolTip="组件HTTP推流路径 /stream"))
    FString DefaultHttpStreamPath = TEXT("/stream");

    UPROPERTY(EditAnywhere, Config, Category="HttpPaths", meta=(ToolTip="组件HTTP结束推流路径 /end-stream"))
    FString DefaultHttpEndStreamPath = TEXT("/end-stream");


    // ========== WebSocket 连接默认配置（组件使用） ==========
    // 下列三个字段仅被 UAudioStreamHttpWsComponent 在主动建立 WS 连接时使用
    UPROPERTY(EditAnywhere, Config, Category="WebSocket", meta=(ToolTip="WebSocket 协议：ws/wss（组件连接使用）"))
    FString DefaultWsScheme = TEXT("ws"); // 或 "wss"

    UPROPERTY(EditAnywhere, Config, Category="WebSocket", meta=(ToolTip="WebSocket 主机:端口（组件连接使用）"))
    FString DefaultWsHost = TEXT("127.0.0.1:8000"); // host:port

    UPROPERTY(EditAnywhere, Config, Category="WebSocket", meta=(ToolTip="WebSocket 路径前缀（组件连接使用）"))
    FString DefaultWsPathPrefix = TEXT("/ws/"); // 以/开头，以/结尾

    // ========== 组件默认参数（用于 UAudioStreamHttpWsComponent 构造 & 分帧） ==========
    // 默认采样率/声道（分帧计算、播放兜底，组件/上游可覆盖）
    UPROPERTY(EditAnywhere, Config, Category="ComponentDefaults", meta=(ToolTip="默认采样率（Hz），用于分帧与播放兜底"))
    int32 DefaultSampleRate = 16000;

    UPROPERTY(EditAnywhere, Config, Category="ComponentDefaults", meta=(ToolTip="默认声道数，用于分帧与播放兜底"))
    int32 DefaultChannels = 1;

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

    // 确保在对象初始化时加载 Config（便于在早期读取）
    virtual void PostInitProperties() override;
};