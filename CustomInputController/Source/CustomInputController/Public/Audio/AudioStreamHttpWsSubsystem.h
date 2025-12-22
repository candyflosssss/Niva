#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "NivaNetworkCoreSettings.h"
#include "NetworkCoreSubsystem.h"
#include "HAL/CriticalSection.h"
#include "Interfaces/IPv4/IPv4Endpoint.h"
#include "AudioStreamSettings.h"
#include "AudioStreamHttpWsSubsystem.generated.h"

DECLARE_LOG_CATEGORY_EXTERN(LogAudioStreamWs, Log, All);

class UAudioStreamHttpWsComponent;

UCLASS()
class CUSTOMINPUTCONTROLLER_API UAudioStreamHttpWsSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()
public:
    // ===== 生命周期 =====
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;
    
    // ===== 组件注册 / UUID 管理 =====
    UFUNCTION(BlueprintCallable, Category="AudioStream|Registry")
    bool RegisterComponent(UAudioStreamHttpWsComponent* Comp, FString& OutUuid);
    UFUNCTION(BlueprintCallable, Category="AudioStream|Registry")
    void UnregisterComponent(UAudioStreamHttpWsComponent* Comp);

    UFUNCTION(BlueprintCallable, Category="AudioStream|Registry")
    UAudioStreamHttpWsComponent* FindComponentByUuid(const FString& Uuid) const;
    
    // NOTE: 网络发送/连接逻辑已迁移到每个组件实例，子系统仅保留解析/统计/路由相关的可复用方法。
    // 组件在接收 WebSocket 文本消息后可调用此方法以复用子系统的解析与统计实现。
    void ProcessWebSocketMessage(const FString& Message, const FString& MsgUuidOverride = FString(), int32 SampleRateOverride = -1, int32 ChannelsOverride = -1);

public:
    // ===== 同步：客户端注册 =====
    UFUNCTION(BlueprintCallable, Category="AudioStream|Sync")
    void ClientRegisterToServer(const FString& ServerIp);

    UFUNCTION(BlueprintCallable, Category="AudioStream|Sync")
    void AutoRegisterClient();

private:
    // ===== 组件映射 / 路由 =====
    TMap<FString, TWeakObjectPtr<UAudioStreamHttpWsComponent>> UuidComponentMap;
    // Protects UuidComponentMap for concurrent access from components
    mutable FCriticalSection UuidMapCS;


    // ===== 内部：统计实现 =====
    void UpdateStats(int32 PcmBytes, int32 SampleRate, int32 Channels);
    void UpdateVisemeStats(int32 Count);
    void LogFinalStats(const TCHAR* Reason) const;
    void LogCurrentStats(const TCHAR* Reason) const;

    // Retrieve accumulated audio stats (thread-safe)
    void GetAudioStatsEx(int64& OutBytes, int64& OutFrames, double& OutSeconds, int64& OutVisemes) const;

    // ===== 统计数据 =====
    mutable FCriticalSection StatsCS;
    int64 TotalPcmBytes = 0;
    int64 TotalFrames   = 0;
    double TotalSeconds = 0.0;
    int64 TotalVisemes  = 0;
    bool bStatsLiveLog = false;

    // ===== 设置（从 UAudioStreamSettings 加载） =====
    int32 MediaUdpPort = 18500;      // 仍保留配置打印
    int32 FrameDurationMs = 20;
    int32 TargetPreRollMs = 180;
    int32 TargetJitterMs = 180;
    int32 VisemeStepMs = 8;
    int32 VisemeKeyframeIntervalMs = 500;
    int32 HeartbeatIntervalMs = 1000;
    float OffsetLerpAlpha = 0.1f;

    void LoadSettings();

    // ===== 角色判断 =====
    bool IsServer() const;

    // ===== 自动注册状态 =====
    bool bAutoHelloDone = false;
    double LastAutoHelloAttemptSec = 0.0;
    FString LastHelloServerIp;

    // ===== HTTP 监听状态 =====
    bool bHttpStarted = false;
};
