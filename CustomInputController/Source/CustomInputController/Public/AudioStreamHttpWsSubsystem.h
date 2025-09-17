#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "NivaNetworkCoreSettings.h"
#include "NetworkCoreSubsystem.h"
#include "HAL/CriticalSection.h"
#include "IPAddress.h"
#include "Interfaces/IPv4/IPv4Endpoint.h"
#include "Containers/Ticker.h"
#include "TimerManager.h" // FTimerHandle
#include "AudioStreamSettings.h"
#include "AudioStreamHttpWsSubsystem.generated.h"

class UAudioStreamHttpWsComponent;
class UUDPHandler;

UCLASS()
class CUSTOMINPUTCONTROLLER_API UAudioStreamHttpWsSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()
public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    UFUNCTION(BlueprintCallable, Category="AudioStream")
    bool StartHttpListener(int32 Port = 0);

    UFUNCTION(BlueprintCallable, Category="AudioStream")
    void StopHttpListener();

    bool RegisterComponent(UAudioStreamHttpWsComponent* Comp, FString& OutKey, const FString& PreferredKey = TEXT(""));
    void UnregisterComponent(UAudioStreamHttpWsComponent* Comp);

    UFUNCTION(BlueprintCallable, Category="AudioStream")
    void StopStreaming();

    UFUNCTION(BlueprintCallable, Category="AudioStream|Stats")
    void ResetAudioStats();

    UFUNCTION(BlueprintCallable, Category="AudioStream|Stats")
    void GetAudioStats(int64& OutTotalBytes, int64& OutTotalFrames, double& OutTotalSeconds) const;

    UFUNCTION(BlueprintCallable, Category="AudioStream|Stats")
    void GetAudioStatsEx(int64& OutTotalBytes, int64& OutTotalFrames, double& OutTotalSeconds, int64& OutTotalVisemes) const;

    UFUNCTION(BlueprintCallable, Category="AudioStream|Stats")
    void PrintAudioStatsToLog(const FString& Reason = TEXT("Manual"));

    UFUNCTION(BlueprintCallable, Category="AudioStream|Stats")
    void SetStatsLiveLog(bool bEnable);

    // 新增：一键输出当前状态，便于排障
    UFUNCTION(BlueprintCallable, Category="AudioStream|Debug")
    void DumpState(const FString& Reason = TEXT("Manual")) const;

    // 测试接口
    UFUNCTION(BlueprintCallable, Category="AudioStream|Test", meta=(CallInEditor="true"))
    bool StartTestStream(const FString& TargetKey, int32 SampleRate = 16000, int32 Channels = 1, float FrequencyHz = 440.0f, float DurationSeconds = 5.0f);
    UFUNCTION(BlueprintCallable, Category="AudioStream|Test", meta=(CallInEditor="true"))
    void StopTestStream();
    UFUNCTION(BlueprintCallable, Category="AudioStream|Test")
    void PushTestAudioChunk(const FString& TargetKey, int32 SampleRate = 16000, int32 Channels = 1, float FrequencyHz = 440.0f, float ChunkDurationMs = 100.0f);
    UFUNCTION(BlueprintCallable, Category="AudioStream|Test")
    void PushTestText(const FString& TargetKey, const FString& Text);
    UFUNCTION(BlueprintCallable, Category="AudioStream|Test")
    void PushTestViseme(const FString& TargetKey, const TArray<int32>& VisemeIndices);
    UFUNCTION(BlueprintCallable, Category="AudioStream|Test")
    TArray<uint8> GenerateTestSineWave(int32 SampleRate, int32 Channels, float FrequencyHz, float DurationSeconds);

protected:
    bool TickSync(float DeltaTime);
    FDelegateHandle TickHandle;
    // 改用 FTSTicker 句柄，匹配 UE5.5 接口
    FTSTicker::FDelegateHandle MediaTickerHandle;
private:
    bool bHttpStarted = false;
    // 是否作为本机“媒体服务器”角色（唯一实例）
    bool bActAsMediaServer = false;
    TMap<FString, TWeakObjectPtr<UAudioStreamHttpWsComponent>> ComponentMap;

    // HTTP路由
    UFUNCTION()
    FNivaHttpResponse HandleAudioPush_NCP(FNivaHttpRequest Request);
    UFUNCTION()
    FNivaHttpResponse HandleTaskStart_NCP(FNivaHttpRequest Request);
    UFUNCTION()
    FNivaHttpResponse HandleAudioStats_NCP(FNivaHttpRequest Request);

    // WebSocket
    TSharedPtr<class IWebSocket> WebSocket;
    FString ActiveWsTargetKey;
    int32 ActiveWsSampleRate = 16000;
    int32 ActiveWsChannels = 1;
    void ConnectWebSocket(const FString& Url);
    void CloseWebSocket();

    // 统计
    void UpdateStats(int32 PcmBytes, int32 SampleRate, int32 Channels);
    void UpdateVisemeStats(int32 Count);
    void LogFinalStats(const TCHAR* Reason) const;
    void LogCurrentStats(const TCHAR* Reason) const;

    mutable FCriticalSection StatsCS;
    int64 TotalPcmBytes = 0;
    int64 TotalFrames   = 0;
    double TotalSeconds = 0.0;
    int64 TotalVisemes  = 0;
    bool bStatsLiveLog = false;

    // ===== 设置（来自项目设置） =====
    int32 MediaUdpPort = 18500;      // 本地监听端口（客户端可能改为随机端口）
    int32 ServerUdpPort = 18500;     // 服务器固定监听端口（HELLO/上行使用）
    int32 FrameDurationMs = 20;
    int32 TargetPreRollMs = 180;
    int32 TargetJitterMs = 180;
    int32 VisemeStepMs = 8;
    int32 VisemeKeyframeIntervalMs = 500;
    int32 HeartbeatIntervalMs = 1000;
    float OffsetLerpAlpha = 0.1f;
    bool bBroadcastWhenNoSubscribers = true; // 无订阅者时是否退回广播

    // 新增：从项目设置加载并打印
    void LoadSettings();

    // ===== 媒体同步 =====
    UPROPERTY() UUDPHandler* MediaUdpHandler = nullptr; // 避免GC
    FSocket* MediaSendSocket = nullptr;
    // 兼容接收器：始终监听18500，仅处理hello（用于客户端仍向18500发HELLO的情况）
    UPROPERTY() UUDPHandler* HelloCompatUdpHandler = nullptr;
    uint32 MediaSeq = 0;

    // 服务器-客户端映射
    TSet<FIPv4Endpoint> MediaClients; // hello 注册的客户端池
    // 订阅（按流与按key的待分配）
    TMap<uint16, TSet<FIPv4Endpoint>> StreamSubscribers; // 已有stream的精确订阅
    TMap<FString, TSet<FIPv4Endpoint>> PendingKeySubscribers; // 尚未分配streamId的key订阅
    TMap<uint16, FString> StreamIdToKey;
    TMap<FString, uint16> KeyToStreamId;
    uint16 NextStreamId = 1;

    // 时间同步
    double EstimatedOffsetUs = 0.0; // server_time = local + offset
    bool bHasOffset = false;

    // 心跳
    double LastHeartbeatSendSec = 0.0;

    // 服务器流信息
    struct FServerVisPoint { uint64 PtsUs; uint8 Id; uint8 Conf; };
    struct FServerStreamInfo
    {
        TArray<uint8> Tail;     // 音频拼帧残留
        int32 SampleRate = 16000;
        int32 Channels = 1;
        bool bSentFormat = false;
        // Viseme
        TArray<FServerVisPoint> PendingVis;
        TArray<uint8> LastKFWeights; // 15 长度，0..255
        uint64 NextVisPtsUs = 0;     // 下一步viseme的PTS
        uint64 NextKFTimeUs = 0;     // 下次关键帧发送时间
    };
    TMap<uint16, FServerStreamInfo> ServerStreams;

    // 客户端流缓冲
    struct FPendingAudioFrame { uint32 Seq; uint64 PtsUs; TArray<uint8> Payload; };
    struct FClientVisPoint { uint64 PtsUs; uint8 Id; uint8 Conf; };
    struct FClientStreamState
    {
        int32 SampleRate = 16000;
        int32 Channels = 1;
        bool bHasFormat = false;
        bool bPreRollReady = false;
        TArray<FPendingAudioFrame> Frames; // 音频
        TArray<FClientVisPoint> VisemePoints; // 嘴型点
    };
    TMap<uint16, FClientStreamState> ClientStreams;

    FCriticalSection StreamCS; // 保护映射
    FCriticalSection JitterCS; // 保护帧与viseme缓冲

    // 出队节流
    double LastDequeueTimeSec = 0.0;

    // ==== UDP处理 ====
    void InitMediaUdp();
    void ShutdownMediaUdp();
    void HandleUdpBinary(const TArray<uint8>& Data, const FIPv4Endpoint& Remote);

    // 兼容：仅处理hello的UDP入口（监听18500）
    void HandleHelloUdp(const TArray<uint8>& Data, const FIPv4Endpoint& Remote);
    // 选取收件人（优先按流订阅，否则按全局）
    void CollectRecipients(uint16 StreamId, TArray<FIPv4Endpoint>& OutRecipients) const;

    // 服务器：音频分发
    void ServerDistributeAudio(const FString& Key, const TArray<uint8>& PcmBytes, int32 InSR, int32 InCH);
    void ServerSendFrame(uint16 StreamId, const uint8* FrameData, int32 FrameBytes, uint64 PtsUs, bool bKeyframe);

    // 服务器：viseme分发
    void ServerQueueVisemes(const FString& Key, const TArray<int32>& VisIdx, const TArray<float>& Confidence);
    void ServerSendVisemeBatch(uint16 StreamId, uint64 FramePtsUs, const TArray<FServerVisPoint>& PointsInFrame);
    void ServerMaybeSendVisemeKeyframe(uint16 StreamId, FServerStreamInfo& Info, uint64 NowUs);

    // 客户端：插入/出队
    void ClientInsertFrame(uint16 StreamId, uint32 Seq, uint64 PtsUs, const uint8* Payload, int32 PayloadLen);
    void ClientDrainFrames(double NowSec);
    void ClientInsertVisemePoints(uint16 StreamId, uint64 BatchPtsUs, const uint8* Payload, int32 PayloadLen);
    void ClientApplyVisemeKeyframe(uint16 StreamId, const uint8* Payload, int32 PayloadLen);
    void ClientDrainVisemes(double NowSec);

    // 工具
    bool IsServer() const;
    
    // 仅用于日志/调试：返回当前媒体角色字符串
    FString GetMediaRoleTag() const { return bActAsMediaServer ? TEXT("MediaServer") : TEXT("MediaClient"); }

public:
    // 客户端/服务器控制：注册与订阅
    UFUNCTION(BlueprintCallable, Category="AudioStream|Sync")
    void ClientRegisterToServer(const FString& ServerIp);

    // 触发一次自动注册（客户端）：尝试从当前NetDriver解析服务器IP并发送hello
    UFUNCTION(BlueprintCallable, Category="AudioStream|Sync")
    void AutoRegisterClient();

    // 服务器主动添加一个客户端（不经hello）。Port<=0则用默认MediaUdpPort
    UFUNCTION(BlueprintCallable, Category="AudioStream|Sync")
    void ServerAddClient(const FString& ClientIp, int32 Port/*=0*/);

    // 服务器为指定Key添加/移除订阅者（主动多播控制）
    UFUNCTION(BlueprintCallable, Category="AudioStream|Sync")
    void ServerAddSubscriberForKey(const FString& Key, const FString& ClientIp, int32 Port/*=0*/);

    UFUNCTION(BlueprintCallable, Category="AudioStream|Sync")
    void ServerRemoveSubscriberForKey(const FString& Key, const FString& ClientIp, int32 Port/*=0*/);

    UFUNCTION(BlueprintCallable, Category="AudioStream|Sync")
    void ServerClearSubscribersForKey(const FString& Key);

private:
    // 自动化 hello
    bool bAutoHelloDone = false;
    double LastAutoHelloAttemptSec = 0.0;
    // 最近一次成功HELLO的服务器IP（用于换房间/跨服务器时触发重新注册）
    FString LastHelloServerIp;
    bool TryAutoHello();

    // ===== 测试流状态 =====
    FTimerHandle TestStreamTimer;
    bool bTestStreamActive = false;
    FString TestTargetKey;
    int32 TestSampleRate = 16000;
    int32 TestChannels = 1;
    float TestFrequency = 440.0f;
    float TestDuration = 5.0f;
    float TestCurrentTime = 0.0f;
    void TestStreamTick();
};
