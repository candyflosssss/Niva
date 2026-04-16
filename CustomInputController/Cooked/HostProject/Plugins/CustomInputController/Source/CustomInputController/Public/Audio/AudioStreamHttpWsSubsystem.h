#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "HAL/CriticalSection.h"
#include "Interfaces/IPv4/IPv4Endpoint.h"
#include "AudioStreamSettings.h"
#include "Engine/TimerHandle.h"
#include "AudioStreamHttpWsSubsystem.generated.h"

DECLARE_LOG_CATEGORY_EXTERN(LogAudioStreamWs, Log, All);

class UAudioStreamHttpWsComponent;
class FSocket;

// --- Packet Protocol Definitions ---
namespace AudioStreamPacket
{
    enum EType : uint8
    {
        Text = 1,
        Audio = 2,
        Image = 3,
        Control = 4,
        Viseme = 5
    };

    struct FHeader
    {
        uint8 Type = 0;
        uint8 Flags = 0;
        uint32 Seq = 0;
        uint32 Timestamp = 0;
        FGuid Uuid; 

        bool HasUuid() const { return (Flags & 0x01) != 0; }

        // 加个方法，能把type以文本形式返回，方便日志打印
        const TCHAR* GetTypeName() const
        {
            switch (Type)
            {
            case Text:    return TEXT("Text");
            case Audio:   return TEXT("Audio");
            case Image:   return TEXT("Image");
            case Control: return TEXT("Control");
            case Viseme:  return TEXT("Viseme");
            default:      return TEXT("Unknown");
            }
        }
        void BuildPacket(uint8 Type, const TArray<uint8>& Payload, TArray<uint8>& OutPacket, const FGuid* InUuid = nullptr);
        bool ParsePacket(const TArray<uint8>& InData, FHeader& OutHeader, TArray<uint8>& OutPayload);
    };
}
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
    // ===== Socket Server/Client =====
    // 通用
    // 启动Socket服务器（自动请求端口）
    UFUNCTION(BlueprintCallable, Category="AudioStream|Socket")
    void StartSocketServer();

    // 停止Socket服务
    UFUNCTION(BlueprintCallable, Category="AudioStream|Socket")
    void StopSocketServer();

    // 客户端连接到Socket服务器
    UFUNCTION(BlueprintCallable, Category="AudioStream|Socket")
    void ConnectToSocketServer(const FString& Ip, int32 Port);

    // 接收服务器端口 (Client Only)
    UFUNCTION(BlueprintCallable, Category="AudioStream|Socket")
    void Client_OnServerPortReceived(int32 Port);

    // 通用发送方法：封装包头并发送
    // Server: 广播给所有客户端
    // Client: 发送给Server
    void SendPacket(uint8 Type, const TArray<uint8>& Payload, const FGuid& Uuid = FGuid());

    // 发送测试包给所有客户端 (PacketType: 1=Text, 2=Audio)
    UFUNCTION(BlueprintCallable, Category="AudioStream|Socket")
    void BroadcastTestPacket(uint8 PacketType = 1);

    // 获取当前监听端口
    UFUNCTION(BlueprintPure, Category="AudioStream|Socket")
    int32 GetSocketServerPort() const { return CurrentSocketPort; }

public:
    // ===== 同步：客户端注册 =====
    UFUNCTION(BlueprintCallable, Category="AudioStream|Sync")
    void ClientRegisterToServer(const FString& ServerIp);

    UFUNCTION(BlueprintCallable, Category="AudioStream|Sync")
    void AutoRegisterClient();


    void UpdateStats(int32 PcmBytes, int32 SampleRate, int32 Channels);
    void UpdateVisemeStats(int32 Count);

private:
    // 转发原始数据包给其他客户端（Server Only）
    void ForwardPacket(const TArray<uint8>& RawData, const FString& ExcludeClientKey);
    void SendBuiltPacket(const TArray<uint8>& Packet);
    void ScheduleAudioDrain(float DelaySeconds = 0.001f);
    void DrainPendingAudioFrames();
    bool DequeueAudioFrameForUuid(const FGuid& Uuid, int32 FrameBytes, TArray<uint8>& OutFramePayload);
    bool HasPendingAudioFrames(int32 FrameBytes) const;
    bool TryConsumeOpusEncodeBudget(int32 FramesToConsume, double& OutRetryDelaySeconds);
    void TrimAudioBufferForUuid(const FGuid& Uuid, TArray<uint8>& Buffer, int32 FrameBytes);

    // ===== 组件映射 / 路由 =====
    TMap<FString, TWeakObjectPtr<UAudioStreamHttpWsComponent>> UuidComponentMap;
    // Protects UuidComponentMap for concurrent access from components
    mutable FCriticalSection UuidMapCS;


    // ===== 内部：统计实现 =====
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

    // ===== 音频分帧缓存（按UUID） =====
    mutable FCriticalSection AudioBufCS;
    TMap<FGuid, TArray<uint8>> AudioBufferMap;
    FGuid LastDrainedAudioUuid;
    int32 MaxDrainFramesPerTick = 8;
    int32 MaxBufferedFramesPerUuid = 12;

    // ===== Opus 限流（避免同一帧/同一时间窗内编码过量导致卡顿） =====
    mutable FCriticalSection OpusBudgetCS;
    double OpusBudgetWindowStartSec = 0.0;
    int32 OpusFramesEncodedInWindow = 0;
    int32 MaxOpusFramesPerWindow = 8;

    // ===== 可选：Opus 编码器缓存（按UUID） =====
    struct FOpusEncoderState
    {
        // 编码器指针（外部库类型）；为避免外部头依赖，这里用 void* 保存并在 .cpp 中转换
        void* Encoder = nullptr;
        int32 LastSampleRate = 0;
        int32 LastChannels = 0;
    };
    mutable FCriticalSection OpusCS;
    TMap<FGuid, FOpusEncoderState> OpusEncoders;

private:
    // ===== Socket 成员 =====
    FSocket* ListenSocket = nullptr;
    // UDP: Store client addresses instead of sockets
    TMap<FString, TSharedRef<FInternetAddr>> ClientMap;
    FSocket* ClientConnectionSocket = nullptr;
    
    int32 CurrentSocketPort = 0;
    int32 CachedServerPort = 0;
    FTimerHandle SocketServerTimerHandle;
    FTimerHandle SocketClientTimerHandle;
    FTimerHandle AudioDrainTimerHandle;

    void SocketServerTick();
    void SocketClientTick();
    
    // 辅助：处理接收到的数据
    void HandleSocketData(const TArray<uint8>& Data, const FString& SenderInfo, bool bIsServer);
};

