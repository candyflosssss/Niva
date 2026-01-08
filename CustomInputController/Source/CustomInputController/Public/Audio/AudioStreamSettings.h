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
    // HTTP 路径（UAudioStreamHttpWsComponent 使用）
    UPROPERTY(EditAnywhere, Config, Category="HttpPaths", meta=(ToolTip="组件HTTP启动任务路径 /run"))
    FString DefaultHttpRunPath = TEXT("/run");

    UPROPERTY(EditAnywhere, Config, Category="HttpPaths", meta=(ToolTip="组件HTTP推流路径 /stream"))
    FString DefaultHttpStreamPath = TEXT("/stream");

    UPROPERTY(EditAnywhere, Config, Category="HttpPaths", meta=(ToolTip="组件HTTP结束推流路径 /end-stream"))
    FString DefaultHttpEndStreamPath = TEXT("/end-stream");

    // 音频
    // UUDPHandler 使用的UDP接收缓冲区大小（仍在用）
    UPROPERTY(EditAnywhere, Config, Category="Audio", meta=(AdvancedDisplay, ToolTip="UDP接收缓冲区大小（UUDPHandler 使用）"))
    int32 UdpRecvBufferBytes = 4*1024*1024; // 4MB

    // ========== WebSocket 连接默认配置（组件使用） ==========
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

    // ========== 子系统节奏与同步 ==========
    UPROPERTY(EditAnywhere, Config, Category="Sync", meta=(AdvancedDisplay, ToolTip="服务器分配客户端预热目标（ms）；当前主要用于日志/参数打印"))
    int32 TargetPreRollMs = 180; // 服务器分配客户端预热

    UPROPERTY(EditAnywhere, Config, Category="Sync", meta=(AdvancedDisplay, ToolTip="客户端目标抖动缓冲（ms）；当前主要用于日志/参数打印"))
    int32 TargetJitterMs = 180; // 客户端目标缓冲

    // 单帧PCM时长（毫秒），用于分帧组包（已使用）
    UPROPERTY(EditAnywhere, Config, Category="Sync", meta=(ToolTip="单帧PCM时长（毫秒），影响分帧组包"))
    int32 FrameDurationMs = 50;
    
    // ========== 调试与日志 ==========
    // 日志
    UPROPERTY(EditAnywhere, Config, Category="Debug", meta=(ToolTip="子系统统计日志默认开关（已使用）"))
    bool bStatsLiveLogDefault = false;

    // 静态访问器，方便统一读取设置
    static const UAudioStreamSettings* Get();

    // 确保在对象初始化时加载 Config（便于在早期读取）
    virtual void PostInitProperties() override;
};
