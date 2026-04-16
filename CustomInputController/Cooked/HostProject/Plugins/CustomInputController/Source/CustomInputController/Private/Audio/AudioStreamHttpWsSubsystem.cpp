#include "Audio/AudioStreamHttpWsSubsystem.h"
#include "Audio/AudioStreamHttpWsComponent.h"
#include "Core/CICRuntimeSettings.h"
#include "Subsystems/SubsystemCollection.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "UObject/Object.h"
// #include "Input/UUDPHandler.h" // UDP removed for now
// #include "Audio/MediaStreamPacket.h"

#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonReader.h"
#include "Dom/JsonObject.h"
#include "Misc/Base64.h"

#include "Modules/ModuleManager.h"
#include "Engine/GameInstance.h"
#include "Async/Async.h"
#include "HAL/ThreadSafeCounter.h"
#include "HAL/PlatformProcess.h"
#include "Engine/World.h"
#include "TimerManager.h"

#include "Engine/NetConnection.h"
#include "Engine/NetDriver.h"

#include "Sockets.h"
#include "SocketSubsystem.h"

// CoreManager logging

#include "CICLogWrapper.h"

// 可选：Opus 编码集成（需要在工程中集成 libopus 并定义 CUSTOMINPUT_USE_OPUS）
#if defined(CUSTOMINPUT_USE_OPUS)
#include "opus.h"
static inline bool UE_OpusInitEncoder(int32 SampleRate, int32 Channels, int32 Bitrate, int32 Complexity, bool bUseFEC, int32 PacketLossPct, OpusEncoder** Out)
{
    int Error = 0;
    OpusEncoder* Enc = opus_encoder_create(SampleRate, Channels, OPUS_APPLICATION_AUDIO, &Error);
    if (!Enc || Error != OPUS_OK) return false;
    opus_encoder_ctl(Enc, OPUS_SET_BITRATE(Bitrate));
    opus_encoder_ctl(Enc, OPUS_SET_COMPLEXITY(Complexity));
    opus_encoder_ctl(Enc, OPUS_SET_INBAND_FEC(bUseFEC ? 1 : 0));
    opus_encoder_ctl(Enc, OPUS_SET_PACKET_LOSS_PERC(FMath::Clamp(PacketLossPct, 0, 100)));
    *Out = Enc;
    return true;
}
static inline void UE_OpusDestroyEncoder(OpusEncoder* Enc){ if (Enc) opus_encoder_destroy(Enc); }
#endif

// Helper: compute PCM16 bytes per ms for framing
static inline int32 ComputeBytesPerMs(int32 SampleRate, int32 Channels)
{
    if (SampleRate <= 0 || Channels <= 0) return 0;
    const int64 BytesPerSec = (int64)SampleRate * (int64)Channels * 2; // PCM16
    return (int32)FMath::Max<int64>(1, BytesPerSec / 1000);
}

// --- Packet Protocol Definitions ---
namespace AudioStreamPacket
{
    static uint32 Swap32(uint32 Val)
    {
        return ((Val >> 24) & 0xff) | ((Val >> 8) & 0xff00) | ((Val << 8) & 0xff0000) | ((Val << 24) & 0xff000000);
    }

    void BuildPacket(uint8 Type, const TArray<uint8>& Payload, TArray<uint8>& OutPacket, const FGuid* InUuid)
    {
        static uint32 GlobalSeq = 0;

        FHeader H;
        H.Type = Type;
        H.Flags = (InUuid != nullptr) ? 1 : 0;
        H.Seq = ++GlobalSeq;
        // Simple timestamp in ms
        H.Timestamp = (uint32)(FPlatformTime::Seconds() * 1000.0);
        if (InUuid) H.Uuid = *InUuid;

        OutPacket.Reset();
        OutPacket.Add(H.Type);
        OutPacket.Add(H.Flags);

        // Network Byte Order (Big Endian)
        uint32 SeqBE = H.Seq;
        uint32 TimeBE = H.Timestamp;
        #if PLATFORM_LITTLE_ENDIAN
            SeqBE = Swap32(SeqBE);
            TimeBE = Swap32(TimeBE);
        #endif
        
        OutPacket.Append((uint8*)&SeqBE, 4);
        OutPacket.Append((uint8*)&TimeBE, 4);

        if (H.HasUuid())
        {
            OutPacket.Append((uint8*)&H.Uuid, 16);
        }

        OutPacket.Append(Payload);
    }

    bool ParsePacket(const TArray<uint8>& InData, FHeader& OutHeader, TArray<uint8>& OutPayload)
    {
        if (InData.Num() < 10) return false;

        const uint8* Ptr = InData.GetData();
        OutHeader.Type = Ptr[0];
        OutHeader.Flags = Ptr[1];

        uint32 SeqNet = 0;
        uint32 TimeNet = 0;
        FMemory::Memcpy(&SeqNet, Ptr + 2, 4);
        FMemory::Memcpy(&TimeNet, Ptr + 6, 4);

        OutHeader.Seq = SeqNet;
        OutHeader.Timestamp = TimeNet;
        #if PLATFORM_LITTLE_ENDIAN
            OutHeader.Seq = Swap32(OutHeader.Seq);
            OutHeader.Timestamp = Swap32(OutHeader.Timestamp);
        #endif

        int32 Offset = 10;
        if (OutHeader.HasUuid())
        {
            if (InData.Num() < Offset + 16) return false;
            FMemory::Memcpy(&OutHeader.Uuid, Ptr + Offset, 16);
            Offset += 16;
        }

        int32 PayloadSize = InData.Num() - Offset;
        if (PayloadSize > 0)
        {
            OutPayload.Reset(PayloadSize);
            OutPayload.Append(Ptr + Offset, PayloadSize);
        }
        else
        {
            OutPayload.Empty();
        }
        return true;
    }
}

DEFINE_LOG_CATEGORY(LogAudioStreamWs);

// 进程级：最近一次成功HELLO的服务器IP（用于换房间/跨服务器时触发重新注册）
static FString GLastHelloServerIp;

void UAudioStreamHttpWsSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    // 配置加载
    LoadSettings();
    UE_LOG(LogTemp, Log, TEXT("[AudioStream] Initialize: mode=%s, UDP=%d, frame_ms=%d, viseme_step=%d, kf_ms=%d, hb_ms=%d, offsetAlpha=%.3f, statsLive=%d"),
        IsServer()?TEXT("Server"):TEXT("Client"), MediaUdpPort, FrameDurationMs, VisemeStepMs, VisemeKeyframeIntervalMs, HeartbeatIntervalMs, OffsetLerpAlpha, bStatsLiveLog?1:0);
    
    // if (IsServer())
    // {
    //     StartSocketServer();
    // }
}

void UAudioStreamHttpWsSubsystem::Deinitialize()
{
    UE_LOG(LogTemp, Log, TEXT("[AudioStream] Deinitialize begin"));
    FCICLogHelpers::CoreLog(this, ECoreLogSeverity::Info, TEXT("音频流子系统"), TEXT("开始销毁"), TEXT("Deinitialize begin"));

    StopSocketServer();

    {
        // Clear audio buffers
        FScopeLock L(&AudioBufCS);
        AudioBufferMap.Empty();
    }

    // 释放 Opus 编码器
#if defined(CUSTOMINPUT_USE_OPUS)
    {
        FScopeLock L(&OpusCS);
        for (auto& Pair : OpusEncoders)
        {
            OpusEncoder* Enc = (OpusEncoder*)Pair.Value.Encoder;
            UE_OpusDestroyEncoder(Enc);
        }
        OpusEncoders.Empty();
    }
#endif

    UuidComponentMap.Empty();
    Super::Deinitialize();
    UE_LOG(LogTemp, Log, TEXT("[AudioStream] Deinitialize end"));
    FCICLogHelpers::CoreLog(this, ECoreLogSeverity::Info, TEXT("音频流子系统"), TEXT("销毁成功"), TEXT("Deinitialize end"));
}



bool UAudioStreamHttpWsSubsystem::RegisterComponent(UAudioStreamHttpWsComponent* Comp, FString& OutUuid)
{
    if (!IsValid(Comp)) return false;

    // 生成 UUID（使用 FGuid）
    FGuid G = FGuid::NewGuid();
    FString Uuid = G.ToString(EGuidFormats::DigitsWithHyphens);
    const int32 RegisteredCount = [&]() -> int32
    {
        FScopeLock Lock(&UuidMapCS);
        // 确保 UUID 唯一（理论上 NewGuid 已足够，但额外检查以防)
        while (UuidComponentMap.Contains(Uuid))
        {
            FGuid NG = FGuid::NewGuid();
            Uuid = NG.ToString(EGuidFormats::DigitsWithHyphens);
        }
        UuidComponentMap.Add(Uuid, Comp);
        return UuidComponentMap.Num();
    }();

    OutUuid = Uuid;
    FCICLogHelpers::CoreLog(this, ECoreLogSeverity::Info, TEXT("流组件注册"), TEXT("组件注册"), FString::Printf(TEXT("Component registered uuid=%s total=%d"), *Uuid, RegisteredCount));

    // If this is the first component, log auto-start suggestion (networking now handled per-component)
    if (RegisteredCount == 1)
    {
        const UAudioStreamSettings* S = GetDefault<UAudioStreamSettings>();
        const EAudioStreamProtocolMode ProtocolMode = S ? S->DefaultProtocolMode : EAudioStreamProtocolMode::LegacyHttpWs;
        const FString Host = S ? S->GetEffectiveWsHost(ProtocolMode) : TEXT("127.0.0.1:8001");
        const bool bHttps = (S && S->GetEffectiveWsScheme(ProtocolMode).Equals(TEXT("wss"), ESearchCase::IgnoreCase));
        const int32 SR = S ? S->DefaultSampleRate : 16000;
        const int32 CH = S ? S->DefaultChannels : 1;
        UE_LOG(LogTemp, Log, TEXT("[AudioStream] First component added -> networking should be started by the component (host=%s, scheme=%s, sr=%d, ch=%d, uuid=%s)"), *Host, bHttps?TEXT("wss"):TEXT("ws"), SR, CH, *Uuid);
    }

    return true;
}

void UAudioStreamHttpWsSubsystem::UnregisterComponent(UAudioStreamHttpWsComponent* Comp)
{
    if (!Comp) return;

    // 从 UuidComponentMap 中移除该组件的所有 uuid 条目
    TArray<FString> ToRemove;
    for (const auto& Pair : UuidComponentMap)
    {
        if (Pair.Value.Get() == Comp) { ToRemove.Add(Pair.Key); }
    }
    for (const FString& K : ToRemove) { UuidComponentMap.Remove(K); UE_LOG(LogTemp, Log, TEXT("[AudioStream] Uuid entry removed: %s"), *K); }

    // 同时清理该组件相关的音频缓存（按UUID）
    if (ToRemove.Num() > 0)
    {
        FScopeLock L(&AudioBufCS);
        for (const FString& K : ToRemove)
        {
            FGuid G; FGuid::Parse(K, G);
            AudioBufferMap.Remove(G);
        }
    }
}


// ======= 统计实现 =======
void UAudioStreamHttpWsSubsystem::UpdateStats(int32 PcmBytes, int32 SampleRate, int32 Channels)
{
    FScopeLock _l(&StatsCS);
    TotalPcmBytes += PcmBytes;
    const int32 BytesPerFrame = 2 * FMath::Max(1, Channels); // PCM16 per sample
    if (BytesPerFrame > 0)
    {
        const int64 Frames = PcmBytes / BytesPerFrame;
        TotalFrames += Frames;
        if (SampleRate > 0)
        {
            TotalSeconds += double(Frames) / double(SampleRate);
        }
    }
}

void UAudioStreamHttpWsSubsystem::UpdateVisemeStats(int32 Count)
{
    FScopeLock _l(&StatsCS);
    TotalVisemes += Count;
}
void UAudioStreamHttpWsSubsystem::LogFinalStats(const TCHAR* Reason) const
{
    int64 B=0,F=0; double S=0; int64 V=0; GetAudioStatsEx(B,F,S,V);
    UE_LOG(LogTemp, Log, TEXT("[AudioStream] Final stats (%s) -> bytes=%lld frames=%lld sec=%.3f vis=%lld"), Reason, B, F, S, V);
}

void UAudioStreamHttpWsSubsystem::LogCurrentStats(const TCHAR* Reason) const
{
    if (!bStatsLiveLog) return;
    int64 B=0,F=0; double S=0; int64 V=0; GetAudioStatsEx(B,F,S,V);
    UE_LOG(LogTemp, Verbose, TEXT("[AudioStream] Stats (%s) -> bytes=%lld frames=%lld sec=%.3f vis=%lld"), Reason, B, F, S, V);
}


// ======= Find/Stop =======

UAudioStreamHttpWsComponent* UAudioStreamHttpWsSubsystem::FindComponentByUuid(const FString& Uuid) const
{
    FScopeLock Lock(&UuidMapCS);
    if (const TWeakObjectPtr<UAudioStreamHttpWsComponent>* Found = UuidComponentMap.Find(Uuid))
    {
        return Found->Get();
    }
    return nullptr;
}


// ======= HTTP server handlers =======

void UAudioStreamHttpWsSubsystem::GetAudioStatsEx(int64& OutBytes, int64& OutFrames, double& OutSeconds, int64& OutVisemes) const
{
    FScopeLock _l(&StatsCS);
    OutBytes = TotalPcmBytes;
    OutFrames = TotalFrames;
    OutSeconds = TotalSeconds;
    OutVisemes = TotalVisemes;
}

// ======= Client register / auto =======

void UAudioStreamHttpWsSubsystem::ClientRegisterToServer(const FString& ServerIp)
{
    // 记忆最后成功的服务器IP
    GLastHelloServerIp = ServerIp;
    LastHelloServerIp = ServerIp;
    FCICLogHelpers::CoreLog(this, ECoreLogSeverity::Info, TEXT("客户端注册"), TEXT("获取服务器"), FString::Printf(TEXT("Registered to server %s"), *ServerIp));
}

void UAudioStreamHttpWsSubsystem::Client_OnServerPortReceived(int32 Port)
{
    if (IsServer()) return;
    if (Port <= 0) return;

    // [防重] 如果端口没变，且当前Socket连接正常，则忽略此次请求
    if (CachedServerPort == Port)
    {
        if (ClientConnectionSocket && ClientConnectionSocket->GetConnectionState() == SCS_Connected)
        {
            return;
        }
    }

    UE_LOG(LogAudioStreamWs, Log, TEXT("[Socket] Client received server port via RPC: %d"), Port);
    
    // 缓存端口
    CachedServerPort = Port;

    // 尝试触发连接（如果 IP 已知）
    AutoRegisterClient();
}

void UAudioStreamHttpWsSubsystem::AutoRegisterClient()
{
    if (IsServer()) return;

    // 1. 尝试解析服务器 IP (通过 UE 原生网络连接)
    FString TargetIp = GLastHelloServerIp;
    
    if (TargetIp.IsEmpty())
    {
        if (UWorld* W = GetWorld())
        {
            if (UNetDriver* ND = W->GetNetDriver())
            {
                // 只有当连接建立后 ServerConnection 才有效
                if (ND->ServerConnection)
                {
                    FString Remote = ND->ServerConnection->LowLevelGetRemoteAddress(true);
                    FString Ip, P;
                    if (Remote.Split(TEXT(":"), &Ip, &P))
                    {
                        TargetIp = Ip;
                        ClientRegisterToServer(TargetIp); // 缓存 IP
                    }
                }
            }
        }
    }

    // 2. 决策连接：必须同时拥有 IP 和 Port
    if (!TargetIp.IsEmpty())
    {
        if (CachedServerPort > 0)
        {
            // IP 和 端口 都有了，发起连接
            ConnectToSocketServer(TargetIp, CachedServerPort);
        }
        else
        {
            // 只有 IP 没有端口，说明 RPC 还没到，等待 RPC
            UE_LOG(LogAudioStreamWs, Log, TEXT("[Socket] Client IP resolved (%s), waiting for Server Port via RPC..."), *TargetIp);
            FCICLogHelpers::CoreLog(this, ECoreLogSeverity::Info, TEXT("客户端注册"), TEXT("等待端口"), FString::Printf(TEXT("Client IP resolved (%s), waiting for Server Port via RPC..."), *TargetIp));
        }
    }
    else
    {
        // 还没连上游戏服务器，无法获取 IP
        // UE_LOG(LogAudioStreamWs, Verbose, TEXT("[Socket] AutoRegisterClient waiting for NetDriver connection..."));
    }
}

// ======= Settings and role =======

bool UAudioStreamHttpWsSubsystem::IsServer() const
{
    const UWorld* W = GetWorld();
    if (!W) return true;
    ENetMode M = W->GetNetMode();
    return M == NM_Standalone || M == NM_ListenServer || M == NM_DedicatedServer;
}

void UAudioStreamHttpWsSubsystem::LoadSettings()
{
    const UAudioStreamSettings* S = GetDefault<UAudioStreamSettings>();
    if (!S) return;
    FrameDurationMs = S->FrameDurationMs;
    bStatsLiveLog = S->bStatsLiveLogDefault;
}

// Previously, network initiation (StartRunAndConnect/PostStreamText/PostEndStream) and WebSocket management
// were implemented here in the subsystem. Those responsibilities have been migrated to individual components.
// The subsystem now only provides registry, routing, and parsing/statistics helpers.

bool UAudioStreamHttpWsSubsystem::RegisterServerAllocateUuid(UAudioStreamHttpWsComponent* Comp, FString& OutUuid)
{
    if (!IsValid(Comp)) return false;
    if (!IsServer()) return false;
    FString Uuid;
    {
        FScopeLock Lock(&UuidMapCS);
        FGuid G = FGuid::NewGuid();
        Uuid = G.ToString(EGuidFormats::DigitsWithHyphens);
        while (UuidComponentMap.Contains(Uuid))
        {
            Uuid = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens);
        }
        UuidComponentMap.Add(Uuid, Comp);
    }
    OutUuid = Uuid;
    return true;
}

bool UAudioStreamHttpWsSubsystem::RegisterComponentWithUuid(UAudioStreamHttpWsComponent* Comp, const FString& InUuid, FString& OutUuid)
{
    if (!IsValid(Comp)) return false;
    if (InUuid.IsEmpty()) return false;
    {
        FScopeLock Lock(&UuidMapCS);
        UuidComponentMap.Add(InUuid, Comp);
    }
    OutUuid = InUuid;
    return true;
}


// ======= Socket Server Implementation =======

void UAudioStreamHttpWsSubsystem::StartSocketServer()
{
    if (!IsServer()) return;
    if (ListenSocket) return;

    ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
    if (!SocketSubsystem) return;

      const UCICRuntimeSettings* RuntimeSettings = UCICRuntimeSettings::Get();
      int32 PortMin = RuntimeSettings ? RuntimeSettings->AudioSocketServerPortMin : 19001;
      int32 PortMax = RuntimeSettings ? RuntimeSettings->AudioSocketServerPortMax : 19010;
      if (PortMin > PortMax)
      {
        Swap(PortMin, PortMax);
      }

      // Try the configured port range to find a valid one
    int32 BoundPort = 0;
      for (int32 Port = PortMin; Port <= PortMax; Port++)
    {
        // UDP: NAME_DGram
        FSocket* NewSocket = SocketSubsystem->CreateSocket(NAME_DGram, TEXT("AudioStreamServerSocket"), false);
        if (!NewSocket) break;

        NewSocket->SetReuseAddr(true);
        NewSocket->SetNonBlocking(true); // UDP should be non-blocking
        
        TSharedRef<FInternetAddr> Addr = SocketSubsystem->CreateInternetAddr();
        Addr->SetAnyAddress();
        Addr->SetPort(Port);

        if (NewSocket->Bind(*Addr))
        {
            // UDP: No Listen()
            ListenSocket = NewSocket;
            BoundPort = Port;
            break;
        }
        
        NewSocket->Close();
        SocketSubsystem->DestroySocket(NewSocket);
    }

    if (ListenSocket && BoundPort > 0)
    {
        CurrentSocketPort = BoundPort;
        UE_LOG(LogAudioStreamWs, Log, TEXT("[Socket] UDP Server bound to port %d"), CurrentSocketPort);
        FCICLogHelpers::CoreLog(this, ECoreLogSeverity::Info, TEXT("SocketServer"), TEXT("启动成功"), FString::Printf(TEXT("UDP Bound to port %d"), CurrentSocketPort));
        
        // Start Tick
        if (UWorld* World = GetWorld())
        {
            World->GetTimerManager().SetTimer(SocketServerTimerHandle, this, &UAudioStreamHttpWsSubsystem::SocketServerTick, 0.01f, true); // Faster tick for UDP
        }
    }
    else
    {
        UE_LOG(LogAudioStreamWs, Error, TEXT("[Socket] Failed to bind UDP to any configured port %d-%d"), PortMin, PortMax);
        FCICLogHelpers::CoreLog(this, ECoreLogSeverity::Error, TEXT("SocketServer"), TEXT("启动失败"), FString::Printf(TEXT("Failed to bind UDP port %d-%d"), PortMin, PortMax));
    }
}

void UAudioStreamHttpWsSubsystem::StopSocketServer()
{
    ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
    
    // Stop Server
    if (ListenSocket)
    {
        ListenSocket->Close();
        if (SocketSubsystem) SocketSubsystem->DestroySocket(ListenSocket);
        ListenSocket = nullptr;
    }
    
    // Clear Clients Map
    ClientMap.Empty();
    
    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(SocketServerTimerHandle);
    }

    // Stop Client (if we were a client)
    if (ClientConnectionSocket)
    {
        ClientConnectionSocket->Close();
        if (SocketSubsystem) SocketSubsystem->DestroySocket(ClientConnectionSocket);
        ClientConnectionSocket = nullptr;
    }
    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(SocketClientTimerHandle);
    }
}

void UAudioStreamHttpWsSubsystem::ConnectToSocketServer(const FString& Ip, int32 Port)
{
    if (IsServer()) return;
    
    // 如果已经连接，且目标一致，则跳过
    if (ClientConnectionSocket)
    {
        // UDP doesn't have "Connected" state in the same way, but we check if socket exists
        return; 
    }

    FCICLogHelpers::CoreLog(this, ECoreLogSeverity::Debug, TEXT("SocketClient"), TEXT("尝试连接"), FString::Printf(TEXT("Subsystem UDP Connecting to %s:%d"), *Ip, Port));
    
    if (Ip.IsEmpty() || Port <= 0) return;

    ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
    if (!SocketSubsystem) return;

    // UDP: NAME_DGram
    ClientConnectionSocket = SocketSubsystem->CreateSocket(NAME_DGram, TEXT("AudioStreamClientSocket"), false);
    if (!ClientConnectionSocket) return;

    ClientConnectionSocket->SetNonBlocking(true);

    TSharedRef<FInternetAddr> Addr = SocketSubsystem->CreateInternetAddr();
    bool bValid;
    Addr->SetIp(*Ip, bValid);
    Addr->SetPort(Port);
    
    if (bValid)
    {
        // UDP Connect sets default destination
        bool bConnected = ClientConnectionSocket->Connect(*Addr);
        if (bConnected)
        {
             UE_LOG(LogAudioStreamWs, Log, TEXT("[Socket] UDP Client connected to %s:%d"), *Ip, Port);
             FCICLogHelpers::CoreLog(this, ECoreLogSeverity::Info, TEXT("SocketClient"), TEXT("连接成功"), FString::Printf(TEXT("Subsystem UDP Connected to %s:%d"), *Ip, Port));
             
             // Send Hello Packet to register with server
             FString HelloMsg = TEXT("HELLO_SERVER");
             FTCHARToUTF8 Utf8(*HelloMsg);
             TArray<uint8> Payload;
             Payload.Append((const uint8*)Utf8.Get(), Utf8.Length());
             SendPacket(AudioStreamPacket::Text, Payload);

             if (UWorld* World = GetWorld())
             {
                 World->GetTimerManager().SetTimer(SocketClientTimerHandle, this, &UAudioStreamHttpWsSubsystem::SocketClientTick, 0.01f, true);
             }
        }
        else
        {
             UE_LOG(LogAudioStreamWs, Warning, TEXT("[Socket] UDP Client failed to connect to %s:%d"), *Ip, Port);
             ClientConnectionSocket->Close();
             SocketSubsystem->DestroySocket(ClientConnectionSocket);
             ClientConnectionSocket = nullptr;
        }
    }
}

void UAudioStreamHttpWsSubsystem::SocketServerTick()
{
    if (!ListenSocket) return;
    
    uint32 PendingDataSize = 0;
    while (ListenSocket->HasPendingData(PendingDataSize) && PendingDataSize > 0)
    {
        TArray<uint8> Buffer;
        Buffer.SetNumUninitialized(PendingDataSize);
        int32 BytesRead = 0;
        
        TSharedRef<FInternetAddr> SenderAddr = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->CreateInternetAddr();
        
        if (ListenSocket->RecvFrom(Buffer.GetData(), PendingDataSize, BytesRead, *SenderAddr))
        {
            if (BytesRead > 0)
            {
                Buffer.SetNum(BytesRead);
                
                // Register Client if new
                // [Hello 处理逻辑] 实际上，服务器收到任何来自新地址的 UDP 包都会将其视为“注册”
                // 客户端发送 "HELLO_SERVER" 主要是为了触发这里的逻辑
                FString SenderKey = SenderAddr->ToString(true);
                if (!ClientMap.Contains(SenderKey))
                {
                    ClientMap.Add(SenderKey, SenderAddr);
                    UE_LOG(LogAudioStreamWs, Log, TEXT("[Socket] New UDP Client registered: %s"), *SenderKey);
                }

                HandleSocketData(Buffer, SenderKey, true);
            }
        }
    }
}

void UAudioStreamHttpWsSubsystem::SocketClientTick()
{
    if (!ClientConnectionSocket) return;
    
    uint32 PendingDataSize = 0;
    while (ClientConnectionSocket->HasPendingData(PendingDataSize) && PendingDataSize > 0)
    {
        TArray<uint8> Buffer;
        Buffer.SetNumUninitialized(PendingDataSize);
        int32 BytesRead = 0;
        
        // Since we used Connect(), we can use Recv, or RecvFrom. Recv is simpler.
        if (ClientConnectionSocket->Recv(Buffer.GetData(), PendingDataSize, BytesRead))
        {
            if (BytesRead > 0)
            {
                Buffer.SetNum(BytesRead);
                HandleSocketData(Buffer, TEXT("Server"), false);
            }
        }
    }
}

void UAudioStreamHttpWsSubsystem::HandleSocketData(const TArray<uint8>& Data, const FString& SenderInfo, bool bIsServer)
{
    // 定义包头和负载变量
    AudioStreamPacket::FHeader Header;
    TArray<uint8> Payload;
    
    // 尝试解析数据包，如果解析失败则记录警告并返回
    if (!AudioStreamPacket::ParsePacket(Data, Header, Payload))
    {
        UE_LOG(LogAudioStreamWs, Warning, TEXT("[Socket] Failed to parse packet from %s (len=%d)"), *SenderInfo, Data.Num());
        FCICLogHelpers::CoreLog(this, ECoreLogSeverity::Warn, TEXT("SocketData"), TEXT("解析失败"), FString::Printf(TEXT("Failed to parse packet from %s (len=%d)"), *SenderInfo, Data.Num()));
        return;
    }
    FCICLogHelpers::CoreLog(this, ECoreLogSeverity::Trace, TEXT("SocketData"), TEXT("子系统接收"), FString::Printf(/*type用实际文本*/TEXT("Parsed packet from %s (Type=%s, Seq=%d, Size=%d)"), 
        *SenderInfo, Header.GetTypeName(), Header.Seq, Payload.Num()));
    // 标记是否需要转发该消息（仅在作为服务器时有效）
    bool bShouldForward = false;

    // 处理文本或控制类型的消息
    if (Header.Type == AudioStreamPacket::Text || Header.Type == AudioStreamPacket::Control)
    {
        // 确保负载数据以 null 结尾，以便安全转换为字符串
        Payload.Add(0);
        // 将 UTF8 编码的负载转换为 TCHAR 字符串
        FString ReceivedMsg = UTF8_TO_TCHAR((const char*)Payload.GetData());
        
        // 记录详细日志和核心日志
        UE_LOG(LogAudioStreamWs, Verbose, TEXT("[Socket] %s received from %s (Type=%d, Seq=%d): %s"), 
            bIsServer ? TEXT("Server") : TEXT("Client"), *SenderInfo, Header.Type, Header.Seq, *ReceivedMsg);
        FCICLogHelpers::CoreLog(this, ECoreLogSeverity::Trace, TEXT("SocketData"), TEXT("文本消息"), FString::Printf(TEXT("From Subsystem %s: %s"), *SenderInfo, *ReceivedMsg));

        // 处理特定的握手消息 "HELLO_SERVER"
        if (ReceivedMsg.StartsWith(TEXT("HELLO_SERVER")))
        {
            if (bIsServer)
            {
                // 服务器收到 Hello 消息，记录日志。客户端通常已通过 UDP 数据包到达注册。
                UE_LOG(LogAudioStreamWs, Log, TEXT("[Socket] Server received HELLO from %s. Client is already registered by UDP packet arrival."), *SenderInfo);
                FCICLogHelpers::CoreLog(this, ECoreLogSeverity::Trace, TEXT("SocketServer"), TEXT("收到HELLO"), FString::Printf(TEXT("Received HELLO from Subsystem %s"), *SenderInfo));
                // Optional: Send Welcome back
                // SendToClient(SenderInfo, "WELCOME_CLIENT"); 
            }
        }
        else 
        {
            // 处理测试包消息
            if (ReceivedMsg.StartsWith(TEXT("TEST_PACKET")))
            {
                UE_LOG(LogAudioStreamWs, Log, TEXT("[Socket] Test packet received: %s"), *ReceivedMsg);
                FCICLogHelpers::CoreLog(this, ECoreLogSeverity::Trace, TEXT("SocketData"), TEXT("测试包"), FString::Printf(TEXT("Test packet received: %s"), *ReceivedMsg));
            }
            
            // 如果是服务器，标记需要转发此消息給其他客户端
            if (bIsServer)
            {
                bShouldForward = true;
            }
        }
    }
    // 处理音频或图像类型的消息
    else if (Header.Type == AudioStreamPacket::Audio || Header.Type == AudioStreamPacket::Image || Header.Type == AudioStreamPacket::Viseme)
    {
        // 音频/图像处理占位符，记录日志
        UE_LOG(LogAudioStreamWs, Verbose, TEXT("[Socket] Binary packet received from %s (Seq=%d, Size=%d)"), *SenderInfo, Header.Seq, Payload.Num());
        // 在message里写明收到的类型，和来源uuid
        FCICLogHelpers::CoreLog(this, ECoreLogSeverity::Trace, TEXT("SocketData"),TEXT("收到二进制数据"), FString::Printf(TEXT("UUID=%s, Type=%d, Size=%d from %s"), 
            Header.HasUuid() ? /*只保留前几位*/ *Header.Uuid.ToString(EGuidFormats::DigitsWithHyphens).Left(8) : TEXT("None"), Header.Type, Payload.Num(), *SenderInfo));
        // 如果是服务器，标记需要转发此二进制数据
        if (bIsServer)
        {
            bShouldForward = true;
        }
    }

    // 如果是服务器且标记为需要转发，并且监听 socket 有效
    if (bIsServer && bShouldForward && ListenSocket)
    {
        ForwardPacket(Data, SenderInfo);
    }

    // 如果有 UUID，尝试分发给组件
    if (Header.HasUuid())
    {
        FString UuidStr = Header.Uuid.ToString(EGuidFormats::DigitsWithHyphens);
        if (UAudioStreamHttpWsComponent* Comp = FindComponentByUuid(UuidStr))
        {
            Comp->ReceiveSocketMessage(Header, Payload);
        }
    }
}

void UAudioStreamHttpWsSubsystem::SendPacket(uint8 Type, const TArray<uint8>& Payload, const FGuid& Uuid)
{
    // Audio: pool by UUID, emit full frames, cache leftovers
    if (Type == AudioStreamPacket::Audio)
    {
        const UAudioStreamSettings* S = GetDefault<UAudioStreamSettings>();
        const int32 SampleRate = S ? S->DefaultSampleRate : 16000;
        const int32 Channels   = S ? S->DefaultChannels   : 1;
        const int32 FrameMs    = FMath::Max(1, FrameDurationMs);
        const int32 BytesPerMs = ComputeBytesPerMs(SampleRate, Channels);
        const int32 FrameBytes = BytesPerMs * FrameMs;

        if (!Uuid.IsValid())
        {
            // 没有UUID无法为其建立独立缓冲，直接走单包发送（仍更新统计）
            FCICLogHelpers::CoreLog(this, ECoreLogSeverity::Warn, TEXT("SendPacket"), TEXT("无UUID"), TEXT("音频包无UUID，降级为单包发送"));
            UpdateStats(Payload.Num(), SampleRate, Channels);
        }
        else if (FrameBytes > 0)
        {
            // 追加到该UUID的缓冲池
            {
                FScopeLock L(&AudioBufCS);
                TArray<uint8>& Buf = AudioBufferMap.FindOrAdd(Uuid);
                Buf.Append(Payload);
            }

            // 循环取整帧
            while (true)
            {
                int32 Available = 0;
                bool bHasBuf = false;
                {
                    FScopeLock L(&AudioBufCS);
                    if (TArray<uint8>* BufPtr = AudioBufferMap.Find(Uuid))
                    {
                        Available = BufPtr->Num();
                        bHasBuf = true;
                    }
                }
                if (!bHasBuf || Available < FrameBytes) break;

                // 取一帧 PCM16 数据
                TArray<uint8> FramePayload;
                FramePayload.SetNumUninitialized(FrameBytes);
                {
                    FScopeLock L(&AudioBufCS);
                    if (TArray<uint8>* BufPtr = AudioBufferMap.Find(Uuid))
                    {
                        FMemory::Memcpy(FramePayload.GetData(), BufPtr->GetData(), FrameBytes);
                        // 移除已消费数据
                        BufPtr->RemoveAt(0, FrameBytes, EAllowShrinking::No);
                    }
                    else
                    {
                        break; // 缓冲在期间被移除，退出
                    }
                }

                // 统计（按原始PCM字节）
                UpdateStats(FrameBytes, SampleRate, Channels);

                // 若启用 Opus，则进行编码
                bool bEncodedWithOpus = false;
#if defined(CUSTOMINPUT_USE_OPUS)
                if (S && S->bEnableOpus)
                {
                    // 准备/获取编码器
                    OpusEncoder* Encoder = [&]() -> OpusEncoder*
                    {
                        FScopeLock L(&OpusCS);
                        FOpusEncoderState& ES = OpusEncoders.FindOrAdd(Uuid);
                        bool bNeedInit = (ES.Encoder == nullptr) || (ES.LastSampleRate != SampleRate) || (ES.LastChannels != Channels);
                        if (bNeedInit)
                        {
                            if (ES.Encoder)
                            {
                                UE_OpusDestroyEncoder((OpusEncoder*)ES.Encoder);
                                ES.Encoder = nullptr;
                            }
                            OpusEncoder* NewEnc = nullptr;
                            const bool bOk = UE_OpusInitEncoder(SampleRate, Channels, S->OpusBitrate, S->OpusComplexity, S->bOpusUseFEC, S->OpusPacketLossPct, &NewEnc);
                            if (bOk)
                            {
                                ES.Encoder = NewEnc;
                                ES.LastSampleRate = SampleRate;
                                ES.LastChannels = Channels;
                            }
                            else
                            {
                                UE_LOG(LogAudioStreamWs, Warning, TEXT("[Opus] Failed to init encoder (sr=%d ch=%d), fallback to PCM"), SampleRate, Channels);
                            }
                        }
                        return (OpusEncoder*)ES.Encoder;
                    }();

                    if (Encoder)
                    {
                        // Opus 输入为 16-bit samples，计算样本数
                        const int32 SamplesPerFrame = (FrameBytes / 2) / FMath::Max(1, Channels); // per channel? Opus expects interleaved total samples per channel
                        // 实际上传递总样本数（每通道样本数），输入缓冲为 interleaved PCM16
                        const opus_int16* Pcm = (const opus_int16*)FramePayload.GetData();
                        // Opus 输出最大长度建议：帧时长相关，这里给出一个安全上限
                        // 由官方建议：最大包 ~1275 bytes；我们给 4096 以确保安全
                        TArray<uint8> EncBuf;
                        EncBuf.SetNumUninitialized(4096);
                        const int32 EncBytes = opus_encode(Encoder, Pcm, SamplesPerFrame, EncBuf.GetData(), EncBuf.Num());
                        if (EncBytes > 0)
                        {
                            EncBuf.SetNum(EncBytes, EAllowShrinking::Yes);
                            TArray<uint8> Packet;
                            const FGuid* UuidPtr = &Uuid;
                            // 为了与解码端区分，这里仍用 Audio 类型，但需要在组件侧知道负载是 Opus
                            // 可选：在 Flags 中扩展标志位；当前先走约定：组件按设置 bEnableOpus 解码
                            AudioStreamPacket::BuildPacket(Type, EncBuf, Packet, UuidPtr);
                            FCICLogHelpers::CoreLog(this, ECoreLogSeverity::Trace, TEXT("SendPacket"), TEXT("发送Opus帧"), FString::Printf(TEXT("UUID=%s EncBytes=%d PCM=%d"), *Uuid.ToString(), EncBytes, FrameBytes));

                            if (IsServer())
                            {
                                ForwardPacket(Packet, TEXT(""));
                            }
                            else if (ClientConnectionSocket)
                            {
                                int32 Sent = 0;
                                ClientConnectionSocket->Send(Packet.GetData(), Packet.Num(), Sent);
                            }
                            bEncodedWithOpus = true;
                        }
                        else
                        {
                            UE_LOG(LogAudioStreamWs, Warning, TEXT("[Opus] encode failed (%d), fallback to PCM"), EncBytes);
                        }
                    }
                }
#endif
                if (!bEncodedWithOpus)
                {
                    // 打包并发送原始 PCM 帧
                    TArray<uint8> Packet;
                    const FGuid* UuidPtr = &Uuid;
                    FCICLogHelpers::CoreLog(this, ECoreLogSeverity::Trace, TEXT("SendPacket"), TEXT("发送整帧音频"), FString::Printf(TEXT("Sending full audio frame (Size=%d bytes) for UUID=%s"), FrameBytes, *Uuid.ToString(EGuidFormats::DigitsWithHyphens)));
                    AudioStreamPacket::BuildPacket(Type, FramePayload, Packet, UuidPtr);

                    if (IsServer())
                    {
                        ForwardPacket(Packet, TEXT(""));
                    }
                    else if (ClientConnectionSocket)
                    {
                        int32 Sent = 0;
                        ClientConnectionSocket->Send(Packet.GetData(), Packet.Num(), Sent);
                    }
                }
            }
            return; // 已经按整帧发送，剩余不足帧的已缓存
        }
        else
        {
            // 无效帧配置，降级为单包发送
            UpdateStats(Payload.Num(), SampleRate, Channels);
        }
    }

    // 非音频或无法分帧：单包发送
    TArray<uint8> Packet;
    // 如果 UUID 有效（非零），则传递指针，否则传 nullptr
    const FGuid* UuidPtr = Uuid.IsValid() ? &Uuid : nullptr;
    AudioStreamPacket::BuildPacket(Type, Payload, Packet, UuidPtr);

    if (IsServer())
    {
        // Server: 广播给所有客户端 (不排除任何人)
        ForwardPacket(Packet, TEXT(""));
    }
    else
    {
        // Client: 发送给 Server
        if (ClientConnectionSocket)
        {
            int32 Sent = 0;
            ClientConnectionSocket->Send(Packet.GetData(), Packet.Num(), Sent);
        }
    }
}

void UAudioStreamHttpWsSubsystem::ForwardPacket(const TArray<uint8>& RawData, const FString& ExcludeClientKey)
{
    if (!IsServer() || !ListenSocket) return;

    for (auto& Pair : ClientMap)
    {
        // 如果指定了排除的客户端 Key，则跳过
        if (!ExcludeClientKey.IsEmpty() && Pair.Key == ExcludeClientKey) continue;

        TSharedRef<FInternetAddr> ClientAddr = Pair.Value;
        int32 Sent = 0;
        ListenSocket->SendTo(RawData.GetData(), RawData.Num(), Sent, *ClientAddr);
    }
}

void UAudioStreamHttpWsSubsystem::BroadcastTestPacket(uint8 PacketType)
{
    FGuid TestUuid = FGuid::NewGuid();
    bool bUseUuid = false;

    // If Audio, try to pick a registered component UUID
    if (PacketType == AudioStreamPacket::Audio)
    {
        bUseUuid = true;
        FScopeLock Lock(&UuidMapCS);
        if (UuidComponentMap.Num() > 0)
        {
            // Pick random
            int32 Index = FMath::RandRange(0, UuidComponentMap.Num() - 1);
            int32 i = 0;
            for (const auto& Pair : UuidComponentMap)
            {
                if (i == Index)
                {
                    FGuid::Parse(Pair.Key, TestUuid);
                    break;
                }
                i++;
            }
        }
    }

    TArray<uint8> Payload;
    
    if (PacketType == AudioStreamPacket::Audio)
    {
        // Generate fake audio (100ms of sine wave)
        // 16kHz, 1ch, 16bit -> 32000 bytes/sec -> 32 bytes/ms
        // Let's send 100ms -> 3200 bytes
        int32 SampleCount = 1600; // 100ms at 16kHz
        Payload.SetNumUninitialized(SampleCount * 2);
        int16* Ptr = (int16*)Payload.GetData();
        for(int32 i=0; i<SampleCount; ++i)
        {
            // Simple sine wave 440Hz
            float t = (float)i / 16000.0f;
            float v = FMath::Sin(t * 440.0f * 2.0f * PI);
            Ptr[i] = (int16)(v * 10000.0f);
        }
    }
    else // Text or others
    {
        FString TestMsg;
        if (IsServer())
            TestMsg = FString::Printf(TEXT("TEST_PACKET_SERVER_TIME_%lld"), FDateTime::Now().ToUnixTimestamp());
        else
            TestMsg = FString::Printf(TEXT("TEST_PACKET_CLIENT_TIME_%lld"), FDateTime::Now().ToUnixTimestamp());
            
        FTCHARToUTF8 Utf8(*TestMsg);
        Payload.Append((const uint8*)Utf8.Get(), Utf8.Length());
    }

    // 使用新的 SendPacket 方法
    SendPacket(PacketType, Payload, bUseUuid ? TestUuid : FGuid());

    FString UuidStr = bUseUuid ? TestUuid.ToString() : TEXT("None");
    if (IsServer())
    {
        UE_LOG(LogAudioStreamWs, Log, TEXT("[Socket] Server broadcasted test packet (Type=%d, Size=%d, UUID=%s)"), PacketType, Payload.Num(), *UuidStr);
        FCICLogHelpers::CoreLog(this, ECoreLogSeverity::Debug, TEXT("SocketServer"), TEXT("广播测试包"), FString::Printf(TEXT("Type: %d, Size: %d, UUID: %s"), PacketType, Payload.Num(), *UuidStr));
    }
    else
    {
        UE_LOG(LogAudioStreamWs, Log, TEXT("[Socket] Client sent test packet (Type=%d, Size=%d, UUID=%s)"), PacketType, Payload.Num(), *UuidStr);
        FCICLogHelpers::CoreLog(this, ECoreLogSeverity::Debug, TEXT("SocketClient"), TEXT("发送测试包"), FString::Printf(TEXT("Type: %d, Size: %d, UUID: %s"), PacketType, Payload.Num(), *UuidStr));
    }
}




