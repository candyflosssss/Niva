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
    // 服务器分配UUID的注册（仅服务器允许）
    UFUNCTION(BlueprintCallable, Category="AudioStream|Registry")
    bool RegisterServerAllocateUuid(UAudioStreamHttpWsComponent* Comp, FString& OutUuid);
    // 携带UUID的注册（客户端/服务器皆可，子系统将映射到该UUID）
    UFUNCTION(BlueprintCallable, Category="AudioStream|Registry")
    bool RegisterComponentWithUuid(UAudioStreamHttpWsComponent* Comp, const FString& InUuid, FString& OutUuid);

    // 兼容入口：若组件已有UUID则用之，否则走服务器分配
    UFUNCTION(BlueprintCallable, Category="AudioStream|Registry")
    bool RegisterComponent(UAudioStreamHttpWsComponent* Comp, FString& OutUuid);
    UFUNCTION(BlueprintCallable, Category="AudioStream|Registry")
    void UnregisterComponent(UAudioStreamHttpWsComponent* Comp);

    UFUNCTION(BlueprintCallable, Category="AudioStream|Registry")
    UAudioStreamHttpWsComponent* FindComponentByUuid(const FString& Uuid) const;
    
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



