#include "AudioStreamHttpWsSubsystem.h"
#include "AudioStreamHttpWsComponent.h"
#include "UUDPHandler.h"
#include "MediaStreamPacket.h"

#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonReader.h"
#include "Dom/JsonObject.h"
#include "Misc/Base64.h"

#include "WebSocketsModule.h"
#include "IWebSocket.h"
#include "Modules/ModuleManager.h"
#include "Engine/GameInstance.h"
#include "Async/Async.h"
#include "HAL/ThreadSafeCounter.h"
#include "HAL/PlatformProcess.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "Sockets.h"
#include "IPAddress.h"
#include "Containers/Ticker.h"

#include "Engine/NetConnection.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"

// 进程级：最近一次成功HELLO的服务器IP（用于换房间/跨服务器时触发重新注册）
static FString GLastHelloServerIp;

static TMap<FString, TArray<uint8>> GStreamTails; // key -> 上一包残留字节

static void ClearTail_GT(const FString& Key)
{
    check(IsInGameThread());
    GStreamTails.Remove(Key);
}

static void AppendWithCarry_GT(const FString& Key, TArray<uint8>& InOut, int32 Channels)
{
    check(IsInGameThread());
    Channels = FMath::Clamp(Channels, 1, 8);
    const int32 FrameSize = 2 * Channels; // PCM16LE

    // 1) 把上一包残留先拼到开头
    if (TArray<uint8>* TailPtr = GStreamTails.Find(Key))
    {
        if (TailPtr->Num() > 0)
        {
            InOut.Insert(TailPtr->GetData(), TailPtr->Num(), 0);
            TailPtr->Reset();
        }
    }

    // 2) 把本包末尾不足一帧的字节留到下一包
    const int32 r = InOut.Num() % FrameSize;
    if (r != 0)
    {
        const int32 start = InOut.Num() - r;
        TArray<uint8>& Tail = GStreamTails.FindOrAdd(Key);
        Tail.SetNum(r, EAllowShrinking::No);
        FMemory::Memcpy(Tail.GetData(), InOut.GetData() + start, r);
        InOut.SetNum(start, EAllowShrinking::No);
    }
}

// --- WAV 提取工具：若是 RIFF/WAVE，就提取 data 块 PCM；支持 PCM16 与 Float32 转 S16 ---
static bool ExtractPcmFromMaybeWav(const TArray<uint8>& InBytes, TArray<uint8>& OutPcm, int32& InOutSR, int32& InOutCH)
{
    auto ReadLE16 = [](const uint8* p) -> uint16 { return (uint16)p[0] | ((uint16)p[1] << 8); };
    auto ReadLE32 = [](const uint8* p) -> uint32 { return (uint32)p[0] | ((uint32)p[1] << 8) | ((uint32)p[2] << 16) | ((uint32)p[3] << 24); };

    if (InBytes.Num() < 44) return false;
    const uint8* B = InBytes.GetData();
    // 仅支持 RIFF/WAVE
    if (!(B[0]=='R' && B[1]=='I' && B[2]=='F' && B[3]=='F' && B[8]=='W' && B[9]=='A' && B[10]=='V' && B[11]=='E'))
    {
        return false; // 非 WAV，按原始PCM处理
    }

    int32 sr = InOutSR;
    int32 ch = InOutCH;
    uint16 fmtTag = 1; // 1=PCM, 3=IEEE_FLOAT
    uint16 bps = 16;

    int32 dataOffset = -1;
    int32 dataSize = 0;

    int32 ofs = 12; // 从第一个chunk开始
    const int32 N = InBytes.Num();
    while (ofs + 8 <= N)
    {
        const uint8* H = B + ofs;
        const uint32 sz = ReadLE32(H + 4);
        const int32 payloadBegin = ofs + 8;
        const int32 payloadEnd = payloadBegin + (int32)sz;

        // 边界保护：长度非法直接停止/返回
        if (payloadBegin < 0 || sz > (uint32)FMath::Max(0, N - payloadBegin))
        {
            // 格式损坏
            return false;
        }

        const bool isFmt  = (H[0]=='f' && H[1]=='m' && H[2]=='t' && H[3]==' ');
        const bool isData = (H[0]=='d' && H[1]=='a' && H[2]=='t' && H[3]=='a');

        if (isFmt && sz >= 16)
        {
            fmtTag = ReadLE16(B + payloadBegin + 0);
            ch     = ReadLE16(B + payloadBegin + 2);
            sr     = (int32)ReadLE32(B + payloadBegin + 4);
            bps    = ReadLE16(B + payloadBegin + 14);

            // 可选：基本一致性检查
            const uint16 blockAlign = ReadLE16(B + payloadBegin + 12);
            const uint16 expectAlign = (uint16)(FMath::Max(1, ch) * (bps / 8));
            if (blockAlign != expectAlign)
            {
                UE_LOG(LogTemp, Warning, TEXT("WAV fmt mismatch: blockAlign=%u, expect=%u (ch=%d, bps=%u)"), blockAlign, expectAlign, ch, bps);
            }

            if (fmtTag == 0xFFFE)
            {
                // WAVE_FORMAT_EXTENSIBLE 暂不支持
                UE_LOG(LogTemp, Warning, TEXT("WAVE_FORMAT_EXTENSIBLE not supported"));
                return false;
            }
        }
        else if (isData)
        {
            dataOffset = payloadBegin;
            dataSize   = FMath::Min<int32>((int32)sz, N - dataOffset);
        }

        // RIFF padding：chunk size 为奇数时需要补一个 pad 字节
        ofs = payloadEnd + ((sz & 1) ? 1 : 0);
    }

    if (dataOffset < 0 || dataSize <= 0)
    {
        return false;
    }

    const uint8* pd = B + dataOffset;

    if (fmtTag == 1 && bps == 16)
    {
        // 直接拷贝PCM16LE
        OutPcm.Reset();
        OutPcm.Append(pd, dataSize);
    }
    else if (fmtTag == 3 && bps == 32)
    {
        // Float32 -> PCM16LE（避免未对齐访问）
        const int32 samples = dataSize / 4;
        OutPcm.Reset();
        OutPcm.AddUninitialized(samples * 2);
        int16* pi = reinterpret_cast<int16*>(OutPcm.GetData());
        for (int32 i = 0; i < samples; ++i)
        {
            float v;
            FMemory::Memcpy(&v, pd + i * 4, 4);
            v = FMath::Clamp(v, -1.0f, 1.0f);
            pi[i] = (int16)FMath::RoundToInt(v * 32767.0f);
        }
    }
    else
    {
        // 不支持的格式
        return false;
    }

    InOutSR = sr;
    InOutCH = ch;
    return true;
}

void UAudioStreamHttpWsSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    // 配置加载
    LoadSettings();
    UE_LOG(LogTemp, Log, TEXT("[AudioStream] Initialize: mode=%s, UDP=%d, frame_ms=%d, preroll_ms=%d, jitter_ms=%d, viseme_step=%d, kf_ms=%d, hb_ms=%d, offsetAlpha=%.3f, statsLive=%d"),
        IsServer()?TEXT("Server"):TEXT("Client"), MediaUdpPort, FrameDurationMs, TargetPreRollMs, TargetJitterMs, VisemeStepMs, VisemeKeyframeIntervalMs, HeartbeatIntervalMs, OffsetLerpAlpha, bStatsLiveLog?1:0);

    // 统一HTTP监听（由NetworkCorePlugin接口控制）
    const bool bHttpOk = StartHttpListener(0);
    UE_LOG(LogTemp, Log, TEXT("[AudioStream] HTTP listener %s"), bHttpOk?TEXT("started"):TEXT("not available"));

    InitMediaUdp();

    // 尝试自动向服务器 hello（客户端自动打洞/报到）
    AutoRegisterClient();

    // 注册ticker，每10ms出队一次
    if (!MediaTickerHandle.IsValid())
    {
        MediaTickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UAudioStreamHttpWsSubsystem::TickSync), 0.01f);
        UE_LOG(LogTemp, Verbose, TEXT("[AudioStream] Tick registered (10ms)"));
    }
}

void UAudioStreamHttpWsSubsystem::Deinitialize()
{
    UE_LOG(LogTemp, Log, TEXT("[AudioStream] Deinitialize begin"));
    if (MediaTickerHandle.IsValid())
    {
        FTSTicker::GetCoreTicker().RemoveTicker(MediaTickerHandle);
        MediaTickerHandle = FTSTicker::FDelegateHandle();
    }
    ShutdownMediaUdp();
    StopStreaming();
    StopHttpListener();
    ComponentMap.Empty();
    Super::Deinitialize();
    UE_LOG(LogTemp, Log, TEXT("[AudioStream] Deinitialize end"));
}

// 新增：HTTP 路由绑定/解绑与组件注册
bool UAudioStreamHttpWsSubsystem::StartHttpListener(int32 /*Port*/)
{
    // 客户端不绑定HTTP路由，避免大厅/多PIE时的全局路由冲突
    if (!IsServer())
    {
        UE_LOG(LogTemp, Verbose, TEXT("[AudioStream] Skip HTTP routes on client"));
        return false;
    }
    if (bHttpStarted) return true;
    UGameInstance* GI = GetGameInstance(); if (!GI) return false;
    if (UNetworkCoreSubsystem* Core = GI->GetSubsystem<UNetworkCoreSubsystem>())
    {
        FNetworkCoreHttpServerDelegate D1; D1.BindUFunction(this, FName(TEXT("HandleAudioPush_NCP")));
        Core->BindRoute(TEXT("/audio/push"), ENivaHttpRequestVerbs::POST, D1);

        FNetworkCoreHttpServerDelegate D3; D3.BindUFunction(this, FName(TEXT("HandleAudioStats_NCP")));
        Core->BindRoute(TEXT("/audio/stats"), ENivaHttpRequestVerbs::GET, D3);
        bHttpStarted = true;
        UE_LOG(LogTemp, Log, TEXT("[AudioStream] HTTP routes bound: /audio/push, /audio/stats (client handles /run as POST)"));
        return true;
    }
    UE_LOG(LogTemp, Verbose, TEXT("[AudioStream] NetworkCoreSubsystem not found, skip HTTP routes"));
    return false;
}

void UAudioStreamHttpWsSubsystem::StopHttpListener()
{
    if (bHttpStarted)
    {
        UE_LOG(LogTemp, Log, TEXT("[AudioStream] HTTP listener stopped"));
    }
    bHttpStarted = false;
}

bool UAudioStreamHttpWsSubsystem::RegisterComponent(UAudioStreamHttpWsComponent* Comp, FString& OutKey, const FString& PreferredKey)
{
    if (!IsValid(Comp)) return false;

    FString UseKey = PreferredKey;
    if (UseKey.IsEmpty())
    {
        int32 Idx = 1;
        do { UseKey = FString::Printf(TEXT("npc_%d"), Idx++); } while (ComponentMap.Contains(UseKey));
    }
    else if (ComponentMap.Contains(UseKey))
    {
        const FString Base = UseKey;
        int32 Suffix = 2;
        do { UseKey = FString::Printf(TEXT("%s_%d"), *Base, Suffix++); } while (ComponentMap.Contains(UseKey));
    }

    ComponentMap.Add(UseKey, Comp);
    OutKey = UseKey;
    UE_LOG(LogTemp, Log, TEXT("[AudioStream] Component registered key=%s, total=%d"), *UseKey, ComponentMap.Num());

    // If this is the first component, auto start the TTS flow: POST /run then connect WS using settings
    if (ComponentMap.Num() == 1)
    {
        const UAudioStreamSettings* S = GetDefault<UAudioStreamSettings>();
        const FString Host = S ? S->DefaultWsHost : TEXT("127.0.0.1:8001");
        const bool bHttps = (S && S->DefaultWsScheme.Equals(TEXT("wss"), ESearchCase::IgnoreCase));
        const int32 SR = S ? S->DefaultSampleRate : 16000;
        const int32 CH = S ? S->DefaultChannels : 1;
        const FString RunPath = TEXT("/run");
        const FString WsPrefix = S ? S->DefaultWsPathPrefix : TEXT("/ws/");
        UE_LOG(LogTemp, Log, TEXT("[AudioStream] First component added -> auto StartRunAndConnect (host=%s, scheme=%s, sr=%d, ch=%d, key=%s)"), *Host, bHttps?TEXT("wss"):TEXT("ws"), SR, CH, *UseKey);
        StartRunAndConnect(Host, TEXT(""), UseKey, SR, CH, bHttps, RunPath, WsPrefix);
    }

    return true;
}

void UAudioStreamHttpWsSubsystem::UnregisterComponent(UAudioStreamHttpWsComponent* Comp)
{
    if (!Comp) return;
    for (auto It = ComponentMap.CreateIterator(); It; ++It)
    {
        if (It.Value().Get() == Comp)
        {
            UE_LOG(LogTemp, Log, TEXT("[AudioStream] Component unregistered key=%s"), *It.Key());
            It.RemoveCurrent();
            break;
        }
    }
}

void UAudioStreamHttpWsSubsystem::StopStreaming()
{
    UE_LOG(LogTemp, Log, TEXT("[AudioStream] StopStreaming: closing WS if any"));
    CloseWebSocket();
    // 在显式停止流时再清空路由键，避免重连期间丢失
    ActiveWsTargetKey.Reset();
}

// ======= 媒体同步实现 =======

bool UAudioStreamHttpWsSubsystem::IsServer() const
{
    UWorld* W = GetWorld();
    if (!W) return false; // 无World时默认按客户端，避免误绑端口
    const ENetMode Mode = W->GetNetMode();
    // 仅把真正的联机服务器视为Server；Standalone一律按Client处理（大厅/单机阶段）
    return (Mode == NM_ListenServer || Mode == NM_DedicatedServer);
}

void UAudioStreamHttpWsSubsystem::InitMediaUdp()
{
    // 客户端使用动态空闲端口监听，避免多PIE/大厅阶段端口冲突；服务端保持配置端口
    if (!IsServer())
    {
        ISocketSubsystem* SSS = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
        if (SSS)
        {
            FSocket* Probe = SSS->CreateSocket(NAME_DGram, TEXT("MediaUDP-Probe"), false);
            if (Probe)
            {
                TSharedRef<FInternetAddr> AnyAddr = SSS->CreateInternetAddr();
                AnyAddr->SetAnyAddress();
                AnyAddr->SetPort(0); // 让系统分配空闲端口
                if (Probe->Bind(*AnyAddr))
                {
                    TSharedRef<FInternetAddr> Bound = SSS->CreateInternetAddr();
                    Probe->GetAddress(*Bound);
                    const int32 Chosen = Bound->GetPort();
                    if (Chosen > 0)
                    {
                        MediaUdpPort = Chosen;
                        UE_LOG(LogTemp, Log, TEXT("[AudioStream] Client picked free UDP port %d"), MediaUdpPort);
                    }
                }
                Probe->Close();
                SSS->DestroySocket(Probe);
            }
        }
    }

    if (!MediaUdpHandler)
    {
        MediaUdpHandler = NewObject<UUDPHandler>(this);
        MediaUdpHandler->OnBinaryReceived.AddUObject(this, &UAudioStreamHttpWsSubsystem::HandleUdpBinary);
        MediaUdpHandler->StartUDPReceiver(MediaUdpPort);
        UE_LOG(LogTemp, Log, TEXT("[AudioStream] UDP listen on %d"), MediaUdpPort);
    }

    // 兼容监听：服务器端额外在18500端口只接收HELLO，避免客户端仍向固定端口发HELLO而丢包
    if (IsServer() && MediaUdpPort != 18500 && !HelloCompatUdpHandler)
    {
        HelloCompatUdpHandler = NewObject<UUDPHandler>(this);
        HelloCompatUdpHandler->OnBinaryReceived.AddUObject(this, &UAudioStreamHttpWsSubsystem::HandleHelloUdp);
        HelloCompatUdpHandler->StartUDPReceiver(18500);
        UE_LOG(LogTemp, Log, TEXT("[AudioStream] HelloCompat listen on 18500 (main=%d)"), MediaUdpPort);
    }

    // 创建发送socket
    if (!MediaSendSocket)
    {
        ISocketSubsystem* SSS = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
        MediaSendSocket = SSS->CreateSocket(NAME_DGram, TEXT("MediaSend"), false);
        if (MediaSendSocket)
        {
            // 绑定到任意本地端口，便于 SendTo
            TSharedRef<FInternetAddr> Local = SSS->CreateInternetAddr();
            Local->SetAnyAddress();
            Local->SetPort(0);
            MediaSendSocket->Bind(*Local);
            int32 Desired = 2*1024*1024, Applied=0;
            MediaSendSocket->SetSendBufferSize(Desired, Applied);
            UE_LOG(LogTemp, Log, TEXT("[AudioStream] UDP send socket ready (buf=%d)"), Applied);
        }
        else
        {
            UE_LOG(LogTemp, Error, TEXT("[AudioStream] Failed to create UDP send socket"));
        }
    }

    // 服务器将本机回环加入客户端集，确保本机也经UDP管线播放
    if (IsServer())
    {
        FIPv4Endpoint Loop(FIPv4Address(127,0,0,1), MediaUdpPort);
        MediaClients.Add(Loop);
        UE_LOG(LogTemp, Verbose, TEXT("[AudioStream] Add loopback client %s"), *Loop.ToString());
    }
}

void UAudioStreamHttpWsSubsystem::HandleHelloUdp(const TArray<uint8>& Data, const FIPv4Endpoint& Remote)
{
    // 只处理控制包
    FMediaPacketHeader H;
    if (!MSP_ParseHeader(Data, H)) return;
    if ((EMediaPacketType)H.MediaType != EMediaPacketType::Control) return;

    // 解析 JSON
    const uint8* Payload = Data.GetData() + sizeof(FMediaPacketHeader);
    const int32 Len = (int32)H.PayloadLen;
    FString JsonStr = (Len > 0) ? FString(FUTF8ToTCHAR(reinterpret_cast<const ANSICHAR*>(Payload), Len)) : FString();
    TSharedPtr<FJsonObject> Obj; TSharedRef<TJsonReader<>> R = TJsonReaderFactory<>::Create(JsonStr);
    if (!FJsonSerializer::Deserialize(R, Obj) || !Obj.IsValid()) return;

    FString Op; Obj->TryGetStringField(TEXT("op"), Op);
    if (Op != TEXT("hello")) return;      // 兼容端口只认 hello
    if (!IsServer()) return;              // 只有服务器需要登记客户端

    int32 PortFromClient = 0;
    if (Obj->TryGetNumberField(TEXT("port"), PortFromClient) && PortFromClient > 0)
    {
        MediaClients.Add(FIPv4Endpoint(Remote.Address, (uint16)PortFromClient));
        UE_LOG(LogTemp, Log, TEXT("[MediaSync] HELLO(compat) from %s (port=%d)"),
               *Remote.Address.ToString(), PortFromClient);
    }
    else
    {
        MediaClients.Add(Remote);
        UE_LOG(LogTemp, Log, TEXT("[MediaSync] HELLO(compat) from %s"), *Remote.ToString());
    }
}


void UAudioStreamHttpWsSubsystem::ShutdownMediaUdp()
{
    if (MediaUdpHandler)
    {
        MediaUdpHandler->OnBinaryReceived.Clear();
        MediaUdpHandler->StopUDPReceiver();
        MediaUdpHandler = nullptr;
        UE_LOG(LogTemp, Log, TEXT("[AudioStream] UDP listener shutdown"));
    }
    if (MediaSendSocket)
    {
        MediaSendSocket->Close();
        ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(MediaSendSocket);
        MediaSendSocket = nullptr;
        UE_LOG(LogTemp, Log, TEXT("[AudioStream] UDP send socket destroyed"));
    }
    MediaClients.Reset();

    if (HelloCompatUdpHandler)
    {
        HelloCompatUdpHandler->OnBinaryReceived.Clear();
        HelloCompatUdpHandler->StopUDPReceiver();
        HelloCompatUdpHandler = nullptr;
        UE_LOG(LogTemp, Log, TEXT("[AudioStream] HelloCompat UDP listener shutdown"));
    }
}

static void SendPacketToAll(FSocket* Sock, const TSet<FIPv4Endpoint>& Clients, const TArray<uint8>& Packet)
{
    if (!Sock || Packet.Num() <= 0) return;
    for (const FIPv4Endpoint& Ep : Clients)
    {
        TSharedRef<FInternetAddr> Addr = Ep.ToInternetAddr();
        int32 Sent = 0;
        Sock->SendTo(Packet.GetData(), Packet.Num(), Sent, *Addr);
    }
}

void UAudioStreamHttpWsSubsystem::ServerSendFrame(uint16 StreamId, const uint8* FrameData, int32 FrameBytes, uint64 PtsUs, bool bKeyframe)
{
    if (!MediaSendSocket) return;
    FMediaPacketHeader H; MSP_FillHeader(H, EMediaPacketType::Audio, StreamId, ++MediaSeq, PtsUs, bKeyframe ? EMediaPacketFlags::Keyframe : 0, (uint32)FrameBytes);
    TArray<uint8> Packet; Packet.AddUninitialized(sizeof(H) + FrameBytes);
    FMemory::Memcpy(Packet.GetData(), &H, sizeof(H));
    FMemory::Memcpy(Packet.GetData()+sizeof(H), FrameData, FrameBytes);
    SendPacketToAll(MediaSendSocket, MediaClients, Packet);
}

static void ServerSendControlJson(FSocket* Sock, const TSet<FIPv4Endpoint>& Clients, uint16 StreamId, const TSharedRef<FJsonObject>& Obj)
{
    FString S; TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&S); FJsonSerializer::Serialize(Obj, W);
    FTCHARToUTF8 Conv(*S);
    const int32 N = Conv.Length();
    FMediaPacketHeader H; MSP_FillHeader(H, EMediaPacketType::Control, StreamId, 0, MSP_NowMicroseconds(), EMediaPacketFlags::Keyframe, (uint32)N);
    TArray<uint8> P; P.AddUninitialized(sizeof(H)+N);
    FMemory::Memcpy(P.GetData(), &H, sizeof(H));
    FMemory::Memcpy(P.GetData()+sizeof(H), Conv.Get(), N);
    SendPacketToAll(Sock, Clients, P);
}

void UAudioStreamHttpWsSubsystem::ServerDistributeAudio(const FString& Key, const TArray<uint8>& PcmBytes, int32 InSR, int32 InCH)
{
    if (!IsServer()) return; // 客户端禁止处理上游
    const int32 SR = FMath::Clamp(InSR>0?InSR:16000, 8000, 48000);
    const int32 CH = FMath::Clamp(InCH>0?InCH:1, 1, 8);

    // 安全兜底：若当前尚无任何收件人，强制加入本机回环，确保至少服务器本机可播放
    if (MediaClients.Num() == 0)
    {
        FIPv4Endpoint Loop(FIPv4Address(127,0,0,1), MediaUdpPort);
        MediaClients.Add(Loop);
        UE_LOG(LogTemp, Warning, TEXT("[MediaSync] No clients registered; add loopback %s"), *Loop.ToString());
    }

    uint16 StreamId = 0;
    {
        FScopeLock L(&StreamCS);
        uint16* Found = KeyToStreamId.Find(Key);
        if (!Found)
        {
            StreamId = NextStreamId++;
            KeyToStreamId.Add(Key, StreamId);
            StreamIdToKey.Add(StreamId, Key);
            FServerStreamInfo& Info = ServerStreams.Add(StreamId);
            Info.SampleRate = SR; Info.Channels = CH; Info.bSentFormat = false;
            UE_LOG(LogTemp, Log, TEXT("[MediaSync] New stream: key=%s -> id=%u (sr=%d ch=%d)"), *Key, (unsigned)StreamId, SR, CH);
        }
        else
        {
            StreamId = *Found;
        }
    }

    FServerStreamInfo* SInfoPtr = ServerStreams.Find(StreamId);
    if (!SInfoPtr) return;
    FServerStreamInfo& SInfo = *SInfoPtr;

    // 若首次发送或格式变化，发送format控制包
    if (!SInfo.bSentFormat || SInfo.SampleRate!=SR || SInfo.Channels!=CH)
    {
        SInfo.SampleRate = SR; SInfo.Channels = CH; SInfo.bSentFormat = true;
        TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
        Obj->SetStringField(TEXT("op"), TEXT("format"));
        Obj->SetStringField(TEXT("key"), Key);
        Obj->SetNumberField(TEXT("stream_id"), (double)StreamId);
        Obj->SetNumberField(TEXT("sr"), (double)SR);
        Obj->SetNumberField(TEXT("ch"), (double)CH);
        Obj->SetNumberField(TEXT("lead_ms"), (double)TargetPreRollMs);
        Obj->SetNumberField(TEXT("frame_ms"), (double)FrameDurationMs);
        Obj->SetNumberField(TEXT("server_time_us"), (double)MSP_NowMicroseconds());
        ServerSendControlJson(MediaSendSocket, MediaClients, StreamId, Obj);
        // 将Tail清空以免跨格式
        SInfo.Tail.Reset();
    }

    // 切帧
    const int32 SamplesPerFrame = FMath::Max(1, (int32)FMath::RoundToInt((double)SR * ((double)FrameDurationMs/1000.0)));
    const int32 FrameBytes = SamplesPerFrame * CH * 2; // S16

    TArray<uint8> Buf;
    Buf.Reserve(SInfo.Tail.Num() + PcmBytes.Num());
    Buf.Append(SInfo.Tail);
    Buf.Append(PcmBytes);
    SInfo.Tail.Reset();

    // 计算首帧PTS：对每流用一个静态表记录next PTS
    static TMap<uint16, uint64> GNextPts;
    uint64* PNext = GNextPts.Find(StreamId);
    if (!PNext)
    {
        uint64 StartPts = MSP_NowMicroseconds() + (uint64)TargetPreRollMs * 1000ULL;
        GNextPts.Add(StreamId, StartPts);
        PNext = GNextPts.Find(StreamId);
    }

    int32 Offset = 0;
    int32 FramesSent = 0;
    while (Buf.Num() - Offset >= FrameBytes)
    {
        const uint8* Ptr = Buf.GetData() + Offset;
        const uint64 Pts = *PNext;
        ServerSendFrame(StreamId, Ptr, FrameBytes, Pts, false);
        Offset += FrameBytes;
        *PNext += (uint64)FrameDurationMs * 1000ULL;
        ++FramesSent;
    }

    const int32 Rem = Buf.Num() - Offset;
    if (Rem > 0)
    {
        SInfo.Tail.Append(Buf.GetData()+Offset, Rem);
    }
    UE_LOG(LogTemp, Verbose, TEXT("[MediaSync] Distribute key=%s id=%u bytes=%d -> frames=%d rem=%d clients=%d"), *Key, (unsigned)StreamId, PcmBytes.Num(), FramesSent, Rem, MediaClients.Num());
}

void UAudioStreamHttpWsSubsystem::HandleUdpBinary(const TArray<uint8>& Data, const FIPv4Endpoint& Remote)
{
    FMediaPacketHeader H;
    if (!MSP_ParseHeader(Data, H)) return;
    const uint8* Payload = Data.GetData() + sizeof(FMediaPacketHeader);

    if ((EMediaPacketType)H.MediaType == EMediaPacketType::Control)
    {
        const int32 Len = (int32)H.PayloadLen;
        FString JsonStr;
        if (Len > 0)
        {
            // 使用长度安全的UTF8转换，避免未0终止导致解析失败
            JsonStr = FString(FUTF8ToTCHAR(reinterpret_cast<const ANSICHAR*>(Payload), Len));
        }
        TSharedPtr<FJsonObject> Obj; TSharedRef<TJsonReader<>> R = TJsonReaderFactory<>::Create(JsonStr);
        if (FJsonSerializer::Deserialize(R, Obj) && Obj.IsValid())
        {
            FString Op; Obj->TryGetStringField(TEXT("op"), Op);
            if (Op == TEXT("hello"))
            {
                if (IsServer())
                {
                    int32 PortFromClient = 0; if (Obj->TryGetNumberField(TEXT("port"), PortFromClient) && PortFromClient > 0)
                    {
                        FIPv4Endpoint Ep(Remote.Address, (uint16)PortFromClient);
                        MediaClients.Add(Ep);
                        UE_LOG(LogTemp, Log, TEXT("[MediaSync] HELLO from %s (port=%d)"), *Ep.ToString(), PortFromClient);
                    }
                    else
                    {
                        MediaClients.Add(Remote);
                        UE_LOG(LogTemp, Log, TEXT("[MediaSync] HELLO from %s"), *Remote.ToString());
                    }
                }
            }
            else if (Op == TEXT("format"))
            {
                const int32 SR = (int32)Obj->GetNumberField(TEXT("sr"));
                const int32 CH = (int32)Obj->GetNumberField(TEXT("ch"));
                const int32 StreamId = (int32)Obj->GetNumberField(TEXT("stream_id"));
                const int64 ServerUs = (int64)Obj->GetNumberField(TEXT("server_time_us"));
                FString Key; Obj->TryGetStringField(TEXT("key"), Key);
                {
                    FScopeLock L(&StreamCS);
                    StreamIdToKey.FindOrAdd((uint16)StreamId) = Key;
                    FClientStreamState& CS = ClientStreams.FindOrAdd((uint16)StreamId);
                    CS.SampleRate = SR; CS.Channels = CH; CS.bHasFormat = true; CS.bPreRollReady = false; CS.Frames.Reset();
                }
                const double LocalUs = FPlatformTime::Seconds()*1000000.0;
                const double Off = (double)ServerUs - LocalUs;
                if (!bHasOffset) { EstimatedOffsetUs = Off; bHasOffset = true; }
                else { EstimatedOffsetUs = FMath::Lerp(EstimatedOffsetUs, Off, OffsetLerpAlpha); }
                
                // 客户端侧：标记已建立媒体控制，停止重复HELLO
                if (!IsServer()) { bAutoHelloDone = true; }

                UE_LOG(LogTemp, Log, TEXT("[MediaSync] FORMAT stream=%d key=%s sr=%d ch=%d offsetUs=%.0f"), StreamId, *Key, SR, CH, EstimatedOffsetUs);
            }
        }
        else
        {
            UE_LOG(LogTemp, Warning, TEXT("[MediaSync] Control JSON parse failed (len=%d)"), Len);
        }
        return;
    }
    else if ((EMediaPacketType)H.MediaType == EMediaPacketType::Audio)
    {
        ClientInsertFrame(H.StreamId, H.Seq, H.PtsUs, Payload, H.PayloadLen);
        return;
    }
}

void UAudioStreamHttpWsSubsystem::ClientInsertFrame(uint16 StreamId, uint32 Seq, uint64 PtsUs, const uint8* Payload, int32 PayloadLen)
{
    if (PayloadLen <= 0 || !Payload) return;

    FClientStreamState* CS0 = nullptr;
    {
        FScopeLock L(&StreamCS);
        CS0 = &ClientStreams.FindOrAdd(StreamId);
    }
    if (!CS0) return; // 防御性检查（理论上 FindOrAdd 总是返回有效引用）

    FPendingAudioFrame F; F.Seq = Seq; F.PtsUs = PtsUs; F.Payload.SetNum(PayloadLen); FMemory::Memcpy(F.Payload.GetData(), Payload, PayloadLen);

    {
        FScopeLock JL(&JitterCS);
        FClientStreamState& CS = *CS0;
        int32 InsertIdx = 0;
        while (InsertIdx < CS.Frames.Num() && (int64)(CS.Frames[InsertIdx].PtsUs - PtsUs) <= 0) { ++InsertIdx; }
        CS.Frames.Insert(MoveTemp(F), InsertIdx);

        if (!CS.bPreRollReady && CS.Frames.Num() >= 2)
        {
            const uint64 OldestPts = CS.Frames[0].PtsUs;
            const uint64 NewestPts = CS.Frames.Last().PtsUs;
            const int64 DepthUs = (int64)(NewestPts - OldestPts);
            if (DepthUs >= (int64)TargetPreRollMs * 1000)
            {
                CS.bPreRollReady = true;
                UE_LOG(LogTemp, Log, TEXT("[MediaSync] PreRoll ready: stream=%u depthUs=%lld"), (unsigned)StreamId, (long long)DepthUs);
            }
        }
    }
}

// 选取收件人（当前简单回退至全局）
void UAudioStreamHttpWsSubsystem::CollectRecipients(uint16 StreamId, TArray<FIPv4Endpoint>& OutRecipients) const
{
    OutRecipients.Reset();
    if (const TSet<FIPv4Endpoint>* S = StreamSubscribers.Find(StreamId))
    {
        for (const FIPv4Endpoint& Ep : *S) { OutRecipients.Add(Ep); }
        if (OutRecipients.Num() > 0) return;
    }
    // 退回全局
    for (const FIPv4Endpoint& Ep : MediaClients) { OutRecipients.Add(Ep); }
}

// 服务器主动登记客户端
void UAudioStreamHttpWsSubsystem::ServerAddClient(const FString& ClientIp, int32 Port)
{
    if (!IsServer()) return;
    const int32 UsePort = (Port > 0 ? Port : MediaUdpPort);
    FIPv4Address Addr; if (!FIPv4Address::Parse(ClientIp, Addr)) { UE_LOG(LogTemp, Warning, TEXT("ServerAddClient: bad ip %s"), *ClientIp); return; }
    FIPv4Endpoint Ep(Addr, UsePort);
    MediaClients.Add(Ep);
    UE_LOG(LogTemp, Log, TEXT("[MediaSync] Add client %s"), *Ep.ToString());
}

void UAudioStreamHttpWsSubsystem::ServerAddSubscriberForKey(const FString& Key, const FString& ClientIp, int32 Port)
{
    if (!IsServer()) return;
    const int32 UsePort = (Port > 0 ? Port : MediaUdpPort);
    FIPv4Address Addr; if (!FIPv4Address::Parse(ClientIp, Addr)) { UE_LOG(LogTemp, Warning, TEXT("ServerAddSubscriberForKey: invalid ip %s"), *ClientIp); return; }
    FIPv4Endpoint Ep(Addr, UsePort);

    MediaClients.Add(Ep); // 确保在全局池

    FScopeLock L(&StreamCS);
    if (uint16* Sid = KeyToStreamId.Find(Key))
    {
        TSet<FIPv4Endpoint>& Set = StreamSubscribers.FindOrAdd(*Sid);
        Set.Add(Ep);
        UE_LOG(LogTemp, Log, TEXT("[MediaSync] Subscribe key=%s stream=%u -> %s"), *Key, (unsigned)*Sid, *Ep.ToString());
    }
    else
    {
        TSet<FIPv4Endpoint>& Pend = PendingKeySubscribers.FindOrAdd(Key);
        Pend.Add(Ep);
        UE_LOG(LogTemp, Log, TEXT("[MediaSync] Pending subscribe key=%s -> %s (await stream start)"), *Key, *Ep.ToString());
    }
}

void UAudioStreamHttpWsSubsystem::ServerRemoveSubscriberForKey(const FString& Key, const FString& ClientIp, int32 Port)
{
    if (!IsServer()) return;
    const int32 UsePort = (Port > 0 ? Port : MediaUdpPort);
    FIPv4Address Addr; if (!FIPv4Address::Parse(ClientIp, Addr)) { UE_LOG(LogTemp, Warning, TEXT("ServerRemoveSubscriberForKey: invalid ip %s"), *ClientIp); return; }
    FIPv4Endpoint Ep(Addr, UsePort);

    FScopeLock L(&StreamCS);
    if (uint16* Sid = KeyToStreamId.Find(Key))
    {
        if (TSet<FIPv4Endpoint>* Set = StreamSubscribers.Find(*Sid))
        {
            Set->Remove(Ep);
            UE_LOG(LogTemp, Log, TEXT("[MediaSync] Unsubscribe key=%s stream=%u <- %s"), *Key, (unsigned)*Sid, *Ep.ToString());
        }
    }
    if (TSet<FIPv4Endpoint>* P = PendingKeySubscribers.Find(Key))
    {
        P->Remove(Ep);
        UE_LOG(LogTemp, Log, TEXT("[MediaSync] Remove pending subscribe key=%s <- %s"), *Key, *Ep.ToString());
    }
}

void UAudioStreamHttpWsSubsystem::ServerClearSubscribersForKey(const FString& Key)
{
    if (!IsServer()) return;
    FScopeLock L(&StreamCS);
    if (uint16* Sid = KeyToStreamId.Find(Key))
    {
        StreamSubscribers.Remove(*Sid);
        UE_LOG(LogTemp, Log, TEXT("[MediaSync] Clear subscribers for key=%s stream=%u"), *Key, (unsigned)*Sid);
    }
    PendingKeySubscribers.Remove(Key);
}

// 客户端 viseme 相关（占位，后续可实现）
void UAudioStreamHttpWsSubsystem::ClientInsertVisemePoints(uint16 /*StreamId*/, uint64 /*BatchPtsUs*/, const uint8* /*Payload*/, int32 /*PayloadLen*/)
{
}
void UAudioStreamHttpWsSubsystem::ClientApplyVisemeKeyframe(uint16 /*StreamId*/, const uint8* /*Payload*/, int32 /*PayloadLen*/)
{
}
void UAudioStreamHttpWsSubsystem::ClientDrainVisemes(double /*NowSec*/)
{
}

// 修复 PushTestViseme（移除被错误插入的重复 TryAutoHello 定义）
void UAudioStreamHttpWsSubsystem::PushTestViseme(const FString& TargetKey, const TArray<int32>& VisemeIndices)
{
    TWeakObjectPtr<UAudioStreamHttpWsComponent>* Found = ComponentMap.Find(TargetKey);
    if (!Found || !Found->IsValid())
    {
        UE_LOG(LogTemp, Warning, TEXT("PushTestViseme: Component not found for key=%s"), *TargetKey);
        return;
    }

    UAudioStreamHttpWsComponent* Target = Found->Get();

    // 更新 viseme 统计
    UpdateVisemeStats(VisemeIndices.Num());

    AsyncTask(ENamedThreads::GameThread, [Target, VisemeIndices]()
    {
        if (IsValid(Target))
        {
            Target->PushViseme(VisemeIndices);
        }
    });

    UE_LOG(LogTemp, Log, TEXT("PushTestViseme: key=%s, visemes=%d"), *TargetKey, VisemeIndices.Num());
}

TArray<uint8> UAudioStreamHttpWsSubsystem::GenerateTestSineWave(int32 SampleRate, int32 Channels, float FrequencyHz, float DurationSeconds)
{
    TArray<uint8> Result;

    // 参数验证
    SampleRate = FMath::Clamp(SampleRate, 8000, 48000);
    Channels = FMath::Clamp(Channels, 1, 2);
    FrequencyHz = FMath::Clamp(FrequencyHz, 100.0f, 2000.0f);
    DurationSeconds = FMath::Clamp(DurationSeconds, 0.001f, 10.0f);

    // 计算样本数
    const int32 TotalSamples = FMath::RoundToInt(SampleRate * DurationSeconds);
    const int32 NumFrames = TotalSamples; // 重命名避免与类成员变量冲突
    const int32 BytesPerSample = 2; // PCM16LE
    const int32 TotalBytes = NumFrames * Channels * BytesPerSample;

    if (TotalBytes <= 0)
    {
        return Result;
    }

    Result.AddUninitialized(TotalBytes);
    int16* SamplePtr = reinterpret_cast<int16*>(Result.GetData());

    // 生成正弦波
    const float AngularFreq = 2.0f * PI * FrequencyHz;
    const float Amplitude = 0.3f; // 30% 音量，避免过大

    for (int32 Frame = 0; Frame < NumFrames; ++Frame) // 使用重命名后的变量
    {
        const float Time = (float)Frame / (float)SampleRate;
        const float SineValue = FMath::Sin(AngularFreq * Time);
        const int16 Sample = (int16)FMath::RoundToInt(SineValue * Amplitude * 32767.0f);

        // 为所有声道填充相同的样本值
        for (int32 Channel = 0; Channel < Channels; ++Channel)
        {
            SamplePtr[Frame * Channels + Channel] = Sample;
        }
    }

    UE_LOG(LogTemp, Verbose, TEXT("GenerateTestSineWave: sr=%d, ch=%d, freq=%.1fHz, dur=%.3fs, bytes=%d"),
           SampleRate, Channels, FrequencyHz, DurationSeconds, TotalBytes);

    return Result;
}

// ======== 自动注册（客户端） ========
bool UAudioStreamHttpWsSubsystem::TryAutoHello()
{
    UWorld* W = GetWorld(); if (!W) return false;
    const ENetMode Mode = W->GetNetMode();

    // 真正的服务器（Listen/Dedicated）无需向自己注册；Standalone不视为server以便后续联机后还能注册
    if (Mode == NM_ListenServer || Mode == NM_DedicatedServer)
    {
        bAutoHelloDone = true;
        return true;
    }

    const double Now = FPlatformTime::Seconds();
    if (Now - LastAutoHelloAttemptSec < 1.0) return false; // 节流：1秒一次
    LastAutoHelloAttemptSec = Now;

    FString ServerIp;
    // 仅在真正成为客户端后从NetDriver解析服务器IP
    if (Mode == NM_Client)
    {
        if (APlayerController* PC = W->GetFirstPlayerController())
        {
            if (UNetConnection* Conn = PC->GetNetConnection())
            {
                FString Addr = Conn->LowLevelGetRemoteAddress(false); // 形如 "IP:Port"
                int32 ColonIdx = INDEX_NONE;
                if (Addr.FindChar(':', ColonIdx)) ServerIp = Addr.Left(ColonIdx); else ServerIp = Addr;
            }
        }
        if (ServerIp.IsEmpty())
        {
            const FURL& Url = W->URL;
            if (Url.Host.Len() > 0) ServerIp = Url.Host;
        }
    }

    if (ServerIp.IsEmpty())
    {
        UE_LOG(LogTemp, Verbose, TEXT("[MediaSync] AutoHello: server IP unresolved (mode=%d), will retry"), (int32)Mode);
        return false;
    }

    // 改进：持续重发HELLO直到真正建立媒体控制(收到FORMAT后置位 bAutoHelloDone)
    ClientRegisterToServer(ServerIp);
    GLastHelloServerIp = ServerIp;
    UE_LOG(LogTemp, Log, TEXT("[MediaSync] AutoHello sent to %s (udp=%d)"), *ServerIp, MediaUdpPort);
    return true;
}

void UAudioStreamHttpWsSubsystem::AutoRegisterClient()
{
    bAutoHelloDone = false; // 允许重试
    LastAutoHelloAttemptSec = 0.0;
    TryAutoHello();
}

bool UAudioStreamHttpWsSubsystem::TickSync(float DeltaTime)
{
    const double NowSec = FPlatformTime::Seconds();

    // 服务器兜底：若初始化时未识别为服务器，确保此时已在 18500 开启兼容 HELLO 监听
    if (IsServer() && MediaUdpPort != 18500 && HelloCompatUdpHandler == nullptr)
    {
        HelloCompatUdpHandler = NewObject<UUDPHandler>(this);
        HelloCompatUdpHandler->OnBinaryReceived.AddUObject(this, &UAudioStreamHttpWsSubsystem::HandleHelloUdp);
        HelloCompatUdpHandler->StartUDPReceiver(18500);
        UE_LOG(LogTemp, Log, TEXT("[AudioStream] HelloCompat listen(on-tick) on 18500 (main=%d)"), MediaUdpPort);
    }

    // 新增：成为服务器后自动绑定HTTP端点（初始化过早处于大厅/Standalone会跳过，此处补绑定）
    if (IsServer() && !bHttpStarted)
    {
        const bool bOk = StartHttpListener(0);
        UE_LOG(LogTemp, Log, TEXT("[AudioStream] Tick ensure HTTP listener: %s"), bOk?TEXT("started/bound"):TEXT("not available (will retry)"));
    }

    // 新增：服务器每tick确保本机回环在收件人列表中，统一走UDP/mediaClients回放
    if (IsServer())
    {
        const FIPv4Endpoint Loop(FIPv4Address(127,0,0,1), MediaUdpPort);
        if (!MediaClients.Contains(Loop))
        {
            MediaClients.Add(Loop);
            UE_LOG(LogTemp, Log, TEXT("[MediaSync] Ensure loopback client %s (tick)"), *Loop.ToString());
        }
    }

    // 客户端侧：若尚未完成 hello，尝试重试
    if (!bAutoHelloDone)
    {
        TryAutoHello();
    }

    ClientDrainFrames(NowSec);
    return true;
}

void UAudioStreamHttpWsSubsystem::ClientDrainFrames(double NowSec)
{
    const double ServerNowUs = NowSec*1000000.0 + (bHasOffset?EstimatedOffsetUs:0.0);

    TArray<uint16> Streams;
    {
        FScopeLock L(&StreamCS);
        ClientStreams.GetKeys(Streams);
    }

    for (uint16 Sid : Streams)
    {
        FString Key;
        FClientStreamState* CS0=nullptr;
        {
            FScopeLock L(&StreamCS);
            Key = StreamIdToKey.FindRef(Sid);
            CS0 = ClientStreams.Find(Sid);
        }
        if (!CS0) continue;

        // 拷贝必要状态以减小持锁时间
        int32 SR = CS0->SampleRate; int32 CH = CS0->Channels; bool bReady = CS0->bPreRollReady;
        if (!bReady) continue; // 还未预热

        // 出队符合时间的帧
        TArray<FPendingAudioFrame> ToPlay;
        {
            FScopeLock JL(&JitterCS);
            FClientStreamState& CS = *CS0;
            while (CS.Frames.Num() > 0)
            {
                const uint64 PtsUs = CS.Frames[0].PtsUs;
                if ((double)PtsUs <= ServerNowUs)
                {
                    ToPlay.Add(MoveTemp(CS.Frames[0]));
                    CS.Frames.RemoveAt(0);
                }
                else break;
            }
        }

        if (ToPlay.Num() > 0)
        {
            // 分发到组件
            TWeakObjectPtr<UAudioStreamHttpWsComponent>* Found = ComponentMap.Find(Key);
            if (Found && Found->IsValid())
            {
                UAudioStreamHttpWsComponent* Target = Found->Get();
                for (FPendingAudioFrame& F : ToPlay)
                {
                    TArray<uint8> Bytes = MoveTemp(F.Payload);
                    AsyncTask(ENamedThreads::GameThread, [Target, Bytes=MoveTemp(Bytes), SR, CH]() mutable
                    {
                        if (IsValid(Target))
                        {
                            Target->PushPcmData(Bytes, SR, CH);
                        }
                    });
                }
            }
        }
    }
}

void UAudioStreamHttpWsSubsystem::ClientRegisterToServer(const FString& ServerIp)
{
    // 发一个 HELLO 控制包给服务器，便于其记录本端地址
    if (!MediaSendSocket) return;
    FIPv4Address Addr; if (!FIPv4Address::Parse(ServerIp, Addr)) { UE_LOG(LogTemp, Warning, TEXT("ClientRegisterToServer: invalid ip %s"), *ServerIp); return; }
    const uint16 MainPort = (uint16)ServerUdpPort;
    FIPv4Endpoint Ep(Addr, MainPort);

    TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
    Obj->SetStringField(TEXT("op"), TEXT("hello"));
    Obj->SetNumberField(TEXT("server_time_us"), (double)MSP_NowMicroseconds());
    // 告知服务器我方的 UDP 监听端口，便于同机双进程的端口区分
    Obj->SetNumberField(TEXT("port"), (double)MediaUdpPort);

    FString S; TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&S); FJsonSerializer::Serialize(Obj, W);
    FTCHARToUTF8 Conv(*S); const int32 N = Conv.Length();
    FMediaPacketHeader H; MSP_FillHeader(H, EMediaPacketType::Control, 0 /*broadcast*/, 0, MSP_NowMicroseconds(), 0, (uint32)N);
    TArray<uint8> P; P.AddUninitialized(sizeof(H)+N);
    FMemory::Memcpy(P.GetData(), &H, sizeof(H));
    FMemory::Memcpy(P.GetData()+sizeof(H), Conv.Get(), N);

    int32 Sent=0; {
        TSharedRef<FInternetAddr> A = Ep.ToInternetAddr();
        MediaSendSocket->SendTo(P.GetData(), P.Num(), Sent, *A);
        UE_LOG(LogTemp, Log, TEXT("[MediaSync] HELLO sent bytes=%d to %s (localUdp=%d)"), P.Num(), *Ep.ToString(), MediaUdpPort);
    }

    // 兼容：若服务器主端口非18500，再向18500也发送一份HELLO
    if ((int32)MainPort != 18500)
    {
        FIPv4Endpoint Ep18500(Addr, 18500);
        TSharedRef<FInternetAddr> B = Ep18500.ToInternetAddr();
        MediaSendSocket->SendTo(P.GetData(), P.Num(), Sent, *B);
        UE_LOG(LogTemp, Log, TEXT("[MediaSync] HELLO (compat18500) sent to %s"), *Ep18500.ToString());
    }
}

// ======= WebSocket =======

static FString ResolveTargetKeyOrFallback(const TMap<FString, TWeakObjectPtr<UAudioStreamHttpWsComponent>>& Map, const FString& Candidate, const FString& Active)
{
    if (!Candidate.IsEmpty()) return Candidate;
    if (!Active.IsEmpty()) return Active;
    if (Map.Num() == 1)
    {
        for (const auto& Pair : Map) { return Pair.Key; }
    }
    return FString();
}

void UAudioStreamHttpWsSubsystem::ConnectWebSocket(const FString& Url)
{
    CloseWebSocket();

    if (!FModuleManager::Get().IsModuleLoaded("WebSockets"))
    {
        FModuleManager::Get().LoadModule("WebSockets");
    }

    FWebSocketsModule& WS = FWebSocketsModule::Get();
    WebSocket = WS.CreateWebSocket(Url);

    if (!WebSocket.IsValid())
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to create WebSocket for %s"), *Url);
        return;
    }

    UE_LOG(LogTemp, Log, TEXT("Connecting WS: %s"), *Url);

    TWeakObjectPtr<UAudioStreamHttpWsSubsystem> Self = this;

    WebSocket->OnConnected().AddLambda([Self]()
    {
        if (!Self.IsValid()) return;
        UAudioStreamHttpWsSubsystem* P = Self.Get();
        UE_LOG(LogTemp, Log, TEXT("WS connected"));
        // 连接建立时，清空目标组件缓冲，避免残留导致起始噪音
        const FString Key = ResolveTargetKeyOrFallback(P->ComponentMap, FString(), P->ActiveWsTargetKey);
        if (!Key.IsEmpty())
        {
            auto Found = P->ComponentMap.Find(Key);
            if (Found && Found->IsValid())
            {
                if (UAudioStreamHttpWsComponent* Target = Found->Get())
                {
                    AsyncTask(ENamedThreads::GameThread, [Target, Key]()
                    {
                        if (IsValid(Target)) { Target->StopStreaming(); }
                        ClearTail_GT(Key);
                    });
                }
            }
        }
    });

    WebSocket->OnConnectionError().AddLambda([Self](const FString& Error)
    {
        if (!Self.IsValid()) return;
        UE_LOG(LogTemp, Error, TEXT("WS error: %s"), *Error);
    });

    WebSocket->OnClosed().AddLambda([Self](int32 Status, const FString& Reason, bool /*bWasClean*/)
    {
        if (!Self.IsValid()) return;
        UAudioStreamHttpWsSubsystem* P = Self.Get();
        UE_LOG(LogTemp, Log, TEXT("WS closed (%d) %s"), Status, *Reason);
        // 输出最终统计
        P->LogFinalStats(TEXT("WSClosed"));
    });

    // 文本帧：解析 JSON（audio/text/viseme）
    WebSocket->OnMessage().AddLambda([Self](const FString& Message)
    {
        if (!Self.IsValid()) return;
        UAudioStreamHttpWsSubsystem* P = Self.Get();

        FString Preview = Message.Left(128);
        UE_LOG(LogTemp, Log, TEXT("[WS onMessage] raw text: %d chars -> %s%s"), Message.Len(), *Preview, Message.Len() > 128 ? TEXT("...") : TEXT(""));

        TSharedPtr<FJsonObject> RootObj;
        const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Message);
        if (!FJsonSerializer::Deserialize(Reader, RootObj) || !RootObj.IsValid())
        {
            UE_LOG(LogTemp, Warning, TEXT("WS text parse failed"));
            return;
        }

        // Handle terminal status message: { "status": "completed" }
        FString StatusStr;
        if (RootObj->TryGetStringField(TEXT("status"), StatusStr))
        {
            if (StatusStr.Equals(TEXT("completed"), ESearchCase::IgnoreCase))
            {
                UE_LOG(LogTemp, Log, TEXT("WS status=completed -> closing WebSocket"));
                AsyncTask(ENamedThreads::GameThread, [P]()
                {
                    if (P) { P->CloseWebSocket(); }
                });
                return;
            }
        }

        FString Type; RootObj->TryGetStringField(TEXT("type"), Type);
        FString MsgKey; RootObj->TryGetStringField(TEXT("key"), MsgKey); if (MsgKey.IsEmpty()) RootObj->TryGetStringField(TEXT("role_id"), MsgKey);
        const FString Key = ResolveTargetKeyOrFallback(P->ComponentMap, MsgKey, P->ActiveWsTargetKey);

        if (Type.Equals(TEXT("audio"), ESearchCase::IgnoreCase))
        {
            int32 SR = P->ActiveWsSampleRate, CH = P->ActiveWsChannels, Tmp;
            if (RootObj->TryGetNumberField(TEXT("sample_rate"), Tmp)) SR = Tmp;
            if (RootObj->TryGetNumberField(TEXT("channels"), Tmp)) CH = Tmp;
            CH = FMath::Clamp(CH, 1, 8);

            FString Base64; RootObj->TryGetStringField(TEXT("data"), Base64);

            if (Key.IsEmpty()) { UE_LOG(LogTemp, Warning, TEXT("WS audio JSON dropped: empty key")); return; }

            // 注意：服务器分发无需本地组件存在；客户端本地播放才需要组件
            TWeakObjectPtr<UAudioStreamHttpWsComponent>* Found = P->ComponentMap.Find(Key);
            const bool bNeedLocalPlay = !P->IsServer();
            if (bNeedLocalPlay && (!Found || !Found->IsValid()))
            {
                UE_LOG(LogTemp, Warning, TEXT("WS audio JSON dropped (client mode): component not found for key=%s"), *Key);
                return;
            }
            if (Base64.IsEmpty()) { UE_LOG(LogTemp, Warning, TEXT("WS audio JSON dropped: empty base64")); return; }

            TArray<uint8> Decoded; if (!FBase64::Decode(Base64, Decoded)) { UE_LOG(LogTemp, Warning, TEXT("WS audio JSON base64 decode failed (len=%d)"), Base64.Len()); return; }

            TArray<uint8> Pcm; int32 UseSR = SR, UseCH = CH;
            if (ExtractPcmFromMaybeWav(Decoded, Pcm, UseSR, UseCH))
            {
                UE_LOG(LogTemp, Log, TEXT("WS audio JSON (WAV) -> pcm=%d key=%s sr=%d ch=%d"), Pcm.Num(), *Key, UseSR, UseCH);
            }
            else
            {
                Pcm = MoveTemp(Decoded);
                UE_LOG(LogTemp, Log, TEXT("WS audio JSON (RAW) -> bytes=%d key=%s sr=%d ch=%d"), Pcm.Num(), *Key, UseSR, UseCH);
            }

            if (P->IsServer())
            {
                // 服务器：仅切片并经UDP广播给客户端（包括本机回环），不直接本地播放；无需组件存在
                TArray<uint8> DataToSend = Pcm;
                AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, [P, Key, Data=MoveTemp(DataToSend), UseSR, UseCH]() mutable
                {
                    if (P)
                    {
                        P->ServerDistributeAudio(Key, Data, UseSR, UseCH);
                    }
                });
            }
            else
            {
                // 客户端：本机组件播放
                UAudioStreamHttpWsComponent* Target = Found->Get();
                AsyncTask(ENamedThreads::GameThread, [P, Key, Target, Data = MoveTemp(Pcm), UseSR, UseCH]() mutable
                {
                    if (!IsValid(Target)) return;
                    TArray<uint8> Bytes = MoveTemp(Data);
                    AppendWithCarry_GT(Key, Bytes, UseCH);
                    P->UpdateStats(Bytes.Num(), UseSR, UseCH);
                    P->LogCurrentStats(TEXT("WSAudio"));
                    Target->PushPcmData(Bytes, UseSR, UseCH);
                });
            }
        }
        else if (Type.Equals(TEXT("text"), ESearchCase::IgnoreCase))
        {
            FString Text; RootObj->TryGetStringField(TEXT("data"), Text);
            if (Key.IsEmpty()) { UE_LOG(LogTemp, Warning, TEXT("WS text dropped: empty key")); return; }
            auto Found = P->ComponentMap.Find(Key); if (!Found || !Found->IsValid()) { UE_LOG(LogTemp, Warning, TEXT("WS text dropped: component not found for key=%s"), *Key); return; }
            UAudioStreamHttpWsComponent* Target = Found->Get();
            AsyncTask(ENamedThreads::GameThread, [Target, Text]()
            {
                if (IsValid(Target)) { Target->PushText(Text); }
            });
        }
        else if (Type.Equals(TEXT("viseme"), ESearchCase::IgnoreCase))
        {
            const TArray<TSharedPtr<FJsonValue>>* ArrPtr = nullptr;
            if (!RootObj->TryGetArrayField(TEXT("data"), ArrPtr) || !ArrPtr) { UE_LOG(LogTemp, Warning, TEXT("WS viseme dropped: no array")); return; }
            TArray<int32> Vis; Vis.Reserve(ArrPtr->Num());
            for (const auto& V : *ArrPtr) { int32 Val=0; if (V->TryGetNumber(Val)) Vis.Add(Val); }

            // 解析可选的 confidence 数组
            TArray<float> Confidence;
            const TArray<TSharedPtr<FJsonValue>>* ConfPtr = nullptr;
            if (RootObj->TryGetArrayField(TEXT("confidence"), ConfPtr) && ConfPtr)
            {
                Confidence.Reserve(ConfPtr->Num());
                for (const auto& C : *ConfPtr)
                {
                    double D = 0.0; // JSON数字默认 double
                    if (C->TryGetNumber(D))
                    {
                        Confidence.Add((float)D);
                    }
                }
            }

            if (Key.IsEmpty()) { UE_LOG(LogTemp, Warning, TEXT("WS viseme dropped: empty key")); return; }
            auto Found = P->ComponentMap.Find(Key); if (!Found || !Found->IsValid()) { UE_LOG(LogTemp, Warning, TEXT("WS viseme dropped: component not found for key=%s"), *Key); return; }
            UAudioStreamHttpWsComponent* Target = Found->Get();
            UE_LOG(LogTemp, Log, TEXT("WS viseme -> n=%d key=%s confN=%d"), Vis.Num(), *Key, Confidence.Num());

            // 统计 viseme 数
            P->UpdateVisemeStats(Vis.Num());
            P->LogCurrentStats(TEXT("WSViseme"));

            // 直接使用配对入队，确保同步
            AsyncTask(ENamedThreads::GameThread, [Target, Vis=MoveTemp(Vis), Confidence=MoveTemp(Confidence)]() mutable
            {
                if (!IsValid(Target)) return;
                Target->PushVisemeEx(Vis, Confidence);
            });
        }
        else
        {
            UE_LOG(LogTemp, Verbose, TEXT("WS text ignored: type=%s"), *Type);
        }
    });

    WebSocket->Connect();
}

void UAudioStreamHttpWsSubsystem::CloseWebSocket()
{
    if (WebSocket.IsValid())
    {
        // 在关闭前先清理所有委托，避免在对象生命周期结束后回调仍访问 this
        WebSocket->OnConnected().Clear();
        WebSocket->OnConnectionError().Clear();
        WebSocket->OnClosed().Clear();
        WebSocket->OnMessage().Clear();

        WebSocket->Close();
        WebSocket.Reset();
    }
    // 移除：不要在这里清空 ActiveWsTargetKey，避免重连时丢失路由键
    // ActiveWsTargetKey.Reset();
}

// ======= 统计实现 =======
void UAudioStreamHttpWsSubsystem::UpdateStats(int32 PcmBytes, int32 SampleRate, int32 Channels)
{
    if (PcmBytes <= 0) return;
    if (Channels <= 0) Channels = 1;
    if (SampleRate <= 0) SampleRate = 16000;

    const int32 FrameSize = 2 * Channels; // PCM16LE
    const int64 Frames = (FrameSize > 0) ? (PcmBytes / FrameSize) : 0;
    const double Sec = (SampleRate > 0) ? (double)Frames / (double)SampleRate : 0.0;

    FScopeLock Lock(&StatsCS);
    TotalPcmBytes += PcmBytes;
    TotalFrames   += Frames;
    TotalSeconds  += Sec;
}

void UAudioStreamHttpWsSubsystem::UpdateVisemeStats(int32 Count)
{
    if (Count <= 0) return;
    FScopeLock Lock(&StatsCS);
    TotalVisemes += Count;
}

void UAudioStreamHttpWsSubsystem::ResetAudioStats()
{
    FScopeLock Lock(&StatsCS);
    TotalPcmBytes = 0;
    TotalFrames = 0;
    TotalSeconds = 0.0;
    TotalVisemes = 0;
}

void UAudioStreamHttpWsSubsystem::GetAudioStats(int64& OutTotalBytes, int64& OutTotalFrames, double& OutTotalSeconds) const
{
    FScopeLock Lock(&StatsCS);
    OutTotalBytes = TotalPcmBytes;
    OutTotalFrames = TotalFrames;
    OutTotalSeconds = TotalSeconds;
}

void UAudioStreamHttpWsSubsystem::GetAudioStatsEx(int64& OutTotalBytes, int64& OutTotalFrames, double& OutTotalSeconds, int64& OutTotalVisemes) const
{
    FScopeLock Lock(&StatsCS);
    OutTotalBytes = TotalPcmBytes;
    OutTotalFrames = TotalFrames;
    OutTotalSeconds = TotalSeconds;
    OutTotalVisemes = TotalVisemes;
}

void UAudioStreamHttpWsSubsystem::PrintAudioStatsToLog(const FString& Reason)
{
    LogFinalStats(*Reason);
}

void UAudioStreamHttpWsSubsystem::LogFinalStats(const TCHAR* Reason) const
{
    FScopeLock Lock(&StatsCS);
    UE_LOG(LogTemp, Log, TEXT("[AudioStats][%s] seconds=%.3f, frames=%lld, bytes=%lld, visemes=%lld"), Reason, TotalSeconds, TotalFrames, TotalPcmBytes, TotalVisemes);
}

void UAudioStreamHttpWsSubsystem::LogCurrentStats(const TCHAR* Reason) const
{
    FScopeLock Lock(&StatsCS);
    if (bStatsLiveLog)
    {
        UE_LOG(LogTemp, Log, TEXT("[AudioStats][%s][Now] seconds=%.3f, visemes=%lld (frames=%lld, bytes=%lld)"), Reason, TotalSeconds, TotalVisemes, TotalFrames, TotalPcmBytes);
    }
    else
    {
        UE_LOG(LogTemp, Verbose, TEXT("[AudioStats][%s][Now] seconds=%.3f, visemes=%lld (frames=%lld, bytes=%lld)"), Reason, TotalSeconds, TotalVisemes, TotalFrames, TotalPcmBytes);
    }
}

void UAudioStreamHttpWsSubsystem::SetStatsLiveLog(bool bEnable)
{
    // 简单起见：这里也加锁，避免并发读写
    FScopeLock Lock(&StatsCS);
    bStatsLiveLog = bEnable;
}

FNivaHttpResponse UAudioStreamHttpWsSubsystem::HandleAudioStats_NCP(FNivaHttpRequest /*Request*/)
{
    int64 Bytes=0, Frames=0; double Sec=0.0; int64 Vis=0;
    GetAudioStatsEx(Bytes, Frames, Sec, Vis);

    TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
    Obj->SetNumberField(TEXT("bytes"), (double)Bytes);
    Obj->SetNumberField(TEXT("frames"), (double)Frames);
    Obj->SetNumberField(TEXT("seconds"), Sec);
    Obj->SetNumberField(TEXT("visemes"), (double)Vis);

    FString JsonOut;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonOut);
    FJsonSerializer::Serialize(Obj, Writer);
    return UNetworkCoreSubsystem::MakeResponse(JsonOut, TEXT("application/json"), 200);
}

// ======= HTTP 处理 =======
FNivaHttpResponse UAudioStreamHttpWsSubsystem::HandleAudioPush_NCP(FNivaHttpRequest Request)
{
    FString BodyString = Request.Body;
    UE_LOG(LogTemp, Verbose, TEXT("/audio/push body size: %d chars"), BodyString.Len());

    FString Key;
    FString Base64;
    int32 InSampleRate = -1;
    int32 InChannels = -1;

    {
        TSharedPtr<FJsonObject> RootObj;
        const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(BodyString);
        if (FJsonSerializer::Deserialize(Reader, RootObj) && RootObj.IsValid())
        {
            RootObj->TryGetStringField(TEXT("key"), Key);
            // 兼容 role_id 作为 key
            if (Key.IsEmpty())
            {
                RootObj->TryGetStringField(TEXT("role_id"), Key);
            }
            RootObj->TryGetStringField(TEXT("base64"), Base64);
            int32 Tmp;
            if (RootObj->TryGetNumberField(TEXT("sample_rate"), Tmp))
            {
                InSampleRate = Tmp;
            }
            if (RootObj->TryGetNumberField(TEXT("channels"), Tmp))
            {
                InChannels = Tmp;
            }
        }
        else
        {
            UE_LOG(LogTemp, Warning, TEXT("/audio/push JSON parse失败"));
        }
    }

    if (Key.IsEmpty() || Base64.IsEmpty())
    {
        return UNetworkCoreSubsystem::MakeResponse(TEXT("Missing key (or role_id) or base64"), TEXT("text/plain"), 400);
    }

    TArray<uint8> Decoded;
    if (!FBase64::Decode(Base64, Decoded))
    {
        return UNetworkCoreSubsystem::MakeResponse(TEXT("Invalid base64"), TEXT("text/plain"), 400);
    }

    // WAV 嗅探与提取（不再截尾）
    int32 UseSR = (InSampleRate > 0 ? InSampleRate : 16000);
    int32 UseCH = (InChannels > 0 ? InChannels : 1);
    UseCH = FMath::Clamp(UseCH, 1, 8);

    TArray<uint8> Pcm;
    if (!ExtractPcmFromMaybeWav(Decoded, Pcm, UseSR, UseCH))
    {
        Pcm = MoveTemp(Decoded);
    }


    UE_LOG(LogTemp, Log, TEXT("[/audio/push] key=%s bytes=%d sr=%d ch=%d (server=%d)"), *Key, Pcm.Num(), UseSR, UseCH, IsServer()?1:0);

    // 仅服务器：切帧并经UDP分发；客户端直接忽略
    if (IsServer())
    {
        AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, [this, Key, Pcm=MoveTemp(Pcm), UseSR, UseCH]() mutable
        {
            ServerDistributeAudio(Key, Pcm, UseSR, UseCH);
        });
    }
    //
    // // 本地播放（主线程）
    // if (TWeakObjectPtr<UAudioStreamHttpWsComponent>* Found = ComponentMap.Find(Key))
    // {
    //     if (Found->IsValid())
    //     {
    //         UAudioStreamHttpWsComponent* Target = Found->Get();
    //         TArray<uint8> LocalBytes = Pcm; // 拷贝一份用于本地播放
    //         AsyncTask(ENamedThreads::GameThread, [this, Target, Key, Data=MoveTemp(LocalBytes), UseSR, UseCH]() mutable
    //         {
    //             if (!IsValid(Target)) return;
    //             TArray<uint8> Bytes = MoveTemp(Data);
    //             AppendWithCarry_GT(Key, Bytes, UseCH);
    //             UpdateStats(Bytes.Num(), UseSR, UseCH);
    //             LogCurrentStats(TEXT("HTTPPushLocal"));
    //             Target->PushPcmData(Bytes, UseSR, UseCH);
    //         });
    //     }
    // }
    // 返回结果（不代表本地播放）
    TSharedRef<FJsonObject> OkObj = MakeShared<FJsonObject>();
    OkObj->SetStringField(TEXT("status"), TEXT("ok"));
    OkObj->SetStringField(TEXT("key"), Key);
    OkObj->SetNumberField(TEXT("decoded"), (double)Pcm.Num());
    OkObj->SetNumberField(TEXT("sample_rate"), UseSR);
    OkObj->SetNumberField(TEXT("channels"), UseCH);

    FString JsonOut;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonOut);
    FJsonSerializer::Serialize(OkObj, Writer);
    return UNetworkCoreSubsystem::MakeResponse(JsonOut, TEXT("application/json"), 200);
}

FNivaHttpResponse UAudioStreamHttpWsSubsystem::HandleTaskStart_NCP(FNivaHttpRequest Request)
{
    FString BodyString = Request.Body;
    UE_LOG(LogTemp, Verbose, TEXT("/task/start body size: %d chars"), BodyString.Len());

    FString TaskId;
    FString WsUrl;
    FString WsBase;
    FString WsHost;
    FString WsScheme = TEXT("ws");
    FString WsPathPrefix = TEXT("/ws/");
    FString TargetKey;
    int32 InSampleRate = 16000;
    int32 InChannels = 1;

    {
        TSharedPtr<FJsonObject> RootObj;
        const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(BodyString);
        if (FJsonSerializer::Deserialize(Reader, RootObj) && RootObj.IsValid())
        {
            RootObj->TryGetStringField(TEXT("task_id"), TaskId);
            RootObj->TryGetStringField(TEXT("ws_url"), WsUrl);
            RootObj->TryGetStringField(TEXT("ws_base"), WsBase);
            RootObj->TryGetStringField(TEXT("ws_host"), WsHost);
            // 兼容别名
            if (WsHost.IsEmpty()) RootObj->TryGetStringField(TEXT("host"), WsHost);
            if (WsBase.IsEmpty()) RootObj->TryGetStringField(TEXT("base"), WsBase);

            RootObj->TryGetStringField(TEXT("ws_scheme"), WsScheme);
            RootObj->TryGetStringField(TEXT("ws_path_prefix"), WsPathPrefix);

            RootObj->TryGetStringField(TEXT("key"), TargetKey);
            // 兼容 role_id 作为 key
            if (TargetKey.IsEmpty()) RootObj->TryGetStringField(TEXT("role_id"), TargetKey);

            int32 Tmp;
            if (RootObj->TryGetNumberField(TEXT("sample_rate"), Tmp)) InSampleRate = Tmp;
            if (RootObj->TryGetNumberField(TEXT("channels"), Tmp)) InChannels = Tmp;    
        }
        else
        {
            UE_LOG(LogTemp, Warning, TEXT("/task/start JSON parse失败"));
        }
    }

    // 设置默认 WS 参数（从项目设置回退）
    if (const UAudioStreamSettings* S = GetDefault<UAudioStreamSettings>())
    {
        if (WsScheme.IsEmpty()) WsScheme = S->DefaultWsScheme;
        if (WsPathPrefix.IsEmpty()) WsPathPrefix = S->DefaultWsPathPrefix;
        if (WsHost.IsEmpty()) WsHost = S->DefaultWsHost;
    }

    // Compose ws_url when only task_id is given
    if (WsUrl.IsEmpty() && !TaskId.IsEmpty())
    {
        if (!WsBase.IsEmpty())
        {
            WsUrl = WsBase.EndsWith(TEXT("/")) ? (WsBase + TaskId) : (WsBase + TEXT("/") + TaskId);
        }
        else if (!WsHost.IsEmpty())
        {
            if (!WsPathPrefix.StartsWith(TEXT("/"))) WsPathPrefix = TEXT("/") + WsPathPrefix;
            if (!WsPathPrefix.EndsWith(TEXT("/"))) WsPathPrefix += TEXT("/");
            WsUrl = FString::Printf(TEXT("%s://%s%s%s"), *WsScheme, *WsHost, *WsPathPrefix, *TaskId);
        }
        else
        {
            // Fallback: 通过项目设置拼接
            const UAudioStreamSettings* S = GetDefault<UAudioStreamSettings>();
            const FString Scheme = S ? S->DefaultWsScheme : TEXT("ws");
            const FString Host   = S ? S->DefaultWsHost   : TEXT("127.0.0.1:8000");
            FString PathPrefix   = S ? S->DefaultWsPathPrefix : TEXT("/ws/");
            if (!PathPrefix.StartsWith(TEXT("/"))) PathPrefix = TEXT("/") + PathPrefix;
            if (!PathPrefix.EndsWith(TEXT("/")))  PathPrefix += TEXT("/");
            WsUrl = FString::Printf(TEXT("%s://%s%s%s"), *Scheme, *Host, *PathPrefix, *TaskId);
            // 也更新用于状态记录的字段
            WsScheme = Scheme;
            WsHost = Host;
            WsPathPrefix = PathPrefix;
        }
    }
    if (WsUrl.IsEmpty())
    {
        return UNetworkCoreSubsystem::MakeResponse(TEXT("Missing ws_url or task_id"), TEXT("text/plain"), 400);
    }
    // 如果未提供 key，则回退为当前活动/唯一组件键
    if (TargetKey.IsEmpty())
    {
        const FString FallbackKey = ResolveTargetKeyOrFallback(ComponentMap, FString(), ActiveWsTargetKey);
        if (!FallbackKey.IsEmpty())
        {
            TargetKey = FallbackKey;
        }
        else
        {
            return UNetworkCoreSubsystem::MakeResponse(TEXT("Missing key (or role_id) and no fallback available"), TEXT("text/plain"), 400);
        }
    }

    // 放宽：组件不存在也允许启动WS，用于服务器仅分发/回环自播
    TWeakObjectPtr<UAudioStreamHttpWsComponent>* Found = ComponentMap.Find(TargetKey);
    if (!Found || !Found->IsValid())
    {
        UE_LOG(LogTemp, Warning, TEXT("/task/start(parse): Component not found for key=%s, will still start WS for UDP-only distribution"), *TargetKey);
    }

    ActiveWsTargetKey = TargetKey;
    ActiveWsSampleRate = InSampleRate;
    ActiveWsChannels = InChannels;
    ActiveTaskId = TaskId;
    ActiveHttpHost = WsHost;
    bActiveUseHttps = WsScheme.Equals(TEXT("wss"), ESearchCase::IgnoreCase);

    AsyncTask(ENamedThreads::GameThread, [this, WsUrl]()
    {
        CloseWebSocket();
        ConnectWebSocket(WsUrl);
    });

    UE_LOG(LogTemp, Log, TEXT("/task/start ok(parse): key=%s task_id=%s ws_url=%s sr=%d ch=%d"), *TargetKey, *TaskId, *WsUrl, ActiveWsSampleRate, ActiveWsChannels);

    TSharedRef<FJsonObject> OkObj = MakeShared<FJsonObject>();
    OkObj->SetStringField(TEXT("status"), TEXT("starting"));
    OkObj->SetStringField(TEXT("ws_url"), WsUrl);
    if (!TaskId.IsEmpty()) OkObj->SetStringField(TEXT("task_id"), TaskId);
    OkObj->SetStringField(TEXT("key"), TargetKey);
    OkObj->SetNumberField(TEXT("sample_rate"), InSampleRate);
    OkObj->SetNumberField(TEXT("channels"), InChannels);

    FString JsonOut;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonOut);
    FJsonSerializer::Serialize(OkObj, Writer);

    return UNetworkCoreSubsystem::MakeResponse(JsonOut, TEXT("application/json"), 200);
}

// ===== 测试流 =====
bool UAudioStreamHttpWsSubsystem::StartTestStream(const FString& TargetKey, int32 SampleRate, int32 Channels, float FrequencyHz, float DurationSeconds)
{
    // 停止当前测试流
    StopTestStream();

    // 检查目标组件是否存在
    TWeakObjectPtr<UAudioStreamHttpWsComponent>* Found = ComponentMap.Find(TargetKey);
    if (!Found || !Found->IsValid())
    {
        UE_LOG(LogTemp, Warning, TEXT("StartTestStream: Component not found for key=%s"), *TargetKey);
        return false;
    }

    // 设置测试参数
    TestTargetKey = TargetKey;
    TestSampleRate = FMath::Clamp(SampleRate, 8000, 48000);
    TestChannels = FMath::Clamp(Channels, 1, 2);
    TestFrequency = FMath::Clamp(FrequencyHz, 100.0f, 2000.0f);
    TestDuration = FMath::Clamp(DurationSeconds, 1.0f, 60.0f);
    TestCurrentTime = 0.0f;
    bTestStreamActive = true;

    UE_LOG(LogTemp, Log, TEXT("StartTestStream: key=%s, sr=%d, ch=%d, freq=%.1fHz, duration=%.1fs"),
           *TargetKey, TestSampleRate, TestChannels, TestFrequency, TestDuration);

    // 首包推送200ms，帮助快速越过Warmup，缩短起播延迟
    PushTestAudioChunk(TestTargetKey, TestSampleRate, TestChannels, TestFrequency, 200.0f);

    // 启动定时器，每100ms推送一次音频块
    UGameInstance* GI = GetGameInstance();
    if (GI && GI->GetWorld())
    {
        GI->GetWorld()->GetTimerManager().SetTimer(
            TestStreamTimer,
            this,
            &UAudioStreamHttpWsSubsystem::TestStreamTick,
            0.1f,
            true
        );

        return true;
    }

    return false;
}

void UAudioStreamHttpWsSubsystem::StopTestStream()
{
    if (!bTestStreamActive)
    {
        return;
    }

    bTestStreamActive = false;

    // 停止定时器
    UGameInstance* GI = GetGameInstance();
    if (GI && GI->GetWorld())
    {
        GI->GetWorld()->GetTimerManager().ClearTimer(TestStreamTimer);
    }

    UE_LOG(LogTemp, Log, TEXT("StopTestStream: test stream stopped for key=%s"), *TestTargetKey);
    TestTargetKey.Empty();
}

void UAudioStreamHttpWsSubsystem::TestStreamTick()
{
    if (!bTestStreamActive || TestTargetKey.IsEmpty())
    {
        StopTestStream();
        return;
    }

    if (TestCurrentTime >= TestDuration)
    {
        UE_LOG(LogTemp, Log, TEXT("TestStreamTick: reached duration limit, stopping"));
        StopTestStream();
        return;
    }

    // 检查目标组件是否仍然有效
    TWeakObjectPtr<UAudioStreamHttpWsComponent>* Found = ComponentMap.Find(TestTargetKey);
    if (!Found || !Found->IsValid())
    {
        UE_LOG(LogTemp, Warning, TEXT("TestStreamTick: target component lost, stopping"));
        StopTestStream();
        return;
    }

    // 推送100ms的音频块
    PushTestAudioChunk(TestTargetKey, TestSampleRate, TestChannels, TestFrequency, 100.0f);

    // 更新时间
    TestCurrentTime += 0.1f;
}

void UAudioStreamHttpWsSubsystem::PushTestAudioChunk(const FString& TargetKey, int32 SampleRate, int32 Channels, float FrequencyHz, float ChunkDurationMs)
{
    TWeakObjectPtr<UAudioStreamHttpWsComponent>* Found = ComponentMap.Find(TargetKey);
    if (!Found || !Found->IsValid())
    {
        UE_LOG(LogTemp, Warning, TEXT("PushTestAudioChunk: Component not found for key=%s"), *TargetKey);
        return;
    }
    float ChunkDurationSec = ChunkDurationMs / 1000.0f;
    TArray<uint8> AudioData = GenerateTestSineWave(SampleRate, Channels, FrequencyHz, ChunkDurationSec);
    if (AudioData.Num() <= 0) return;

    if (IsServer())
    {
        // 服务器：统一经UDP分发（包含本机回环），不做直接本地播放兜底
        AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, [this, K=TargetKey, Data=AudioData, SampleRate, Channels]() mutable
        {
            ServerDistributeAudio(K, Data, SampleRate, Channels);
        });
    }
    else
    {
        // 非服务器：直接本地推给目标组件，便于 Standalone/大厅阶段自测
        UAudioStreamHttpWsComponent* Target = Found->Get();
        TArray<uint8> LocalData = MoveTemp(AudioData);
        AsyncTask(ENamedThreads::GameThread, [Target, Data=MoveTemp(LocalData), SampleRate, Channels]() mutable
        {
            if (IsValid(Target))
            {
                Target->PushPcmData(Data, SampleRate, Channels);
            }
        });
        UE_LOG(LogTemp, Log, TEXT("PushTestAudioChunk: local play on client key=%s, sr=%d ch=%d dur=%.1fms"), *TargetKey, SampleRate, Channels, ChunkDurationMs);
    }
}

void UAudioStreamHttpWsSubsystem::PushTestText(const FString& TargetKey, const FString& Text)
{
    TWeakObjectPtr<UAudioStreamHttpWsComponent>* Found = ComponentMap.Find(TargetKey);
    if (!Found || !Found->IsValid())
    {
        UE_LOG(LogTemp, Warning, TEXT("PushTestText: Component not found for key=%s"), *TargetKey);
        return;
    }

    UAudioStreamHttpWsComponent* Target = Found->Get();
    AsyncTask(ENamedThreads::GameThread, [Target, Text]()
    {
        if (IsValid(Target))
        {
            Target->PushText(Text);
        }
    });

    UE_LOG(LogTemp, Log, TEXT("PushTestText: key=%s, text=%s"), *TargetKey, *Text);
}

// ===== 新增：从项目设置加载并打印 =====
void UAudioStreamHttpWsSubsystem::LoadSettings()
{
    const UAudioStreamSettings* S = GetDefault<UAudioStreamSettings>();
    if (!S)
    {
        UE_LOG(LogTemp, Warning, TEXT("[AudioStream] Settings not found, using defaults"));
        return;
    }
    MediaUdpPort = S->MediaUdpPort;
    ServerUdpPort = S->MediaUdpPort; // 新增：服务器端口默认等于配置端口
    FrameDurationMs = S->FrameDurationMs;
    TargetPreRollMs = S->TargetPreRollMs;
    TargetJitterMs = S->TargetJitterMs;
    VisemeStepMs = S->VisemeStepMs;
    VisemeKeyframeIntervalMs = S->VisemeKeyframeIntervalMs;
    HeartbeatIntervalMs = S->HeartbeatIntervalMs;
    OffsetLerpAlpha = S->OffsetLerpAlpha;
    bStatsLiveLog = S->bStatsLiveLogDefault;

    UE_LOG(LogTemp, Log, TEXT("[AudioStream] Settings loaded: UDPPort=%d, ServerUdpPort=%d, FrameDurationMs=%d, TargetPreRollMs=%d, TargetJitterMs=%d, VisemeStepMs=%d, VisemeKeyframeIntervalMs=%d, HeartbeatIntervalMs=%d, OffsetLerpAlpha=%.3f, bStatsLiveLog=%d"),
        MediaUdpPort, ServerUdpPort, FrameDurationMs, TargetPreRollMs, TargetJitterMs, VisemeStepMs, VisemeKeyframeIntervalMs, HeartbeatIntervalMs, OffsetLerpAlpha, bStatsLiveLog?1:0);
}

// ===== 新增：一键转储当前状态 =====
void UAudioStreamHttpWsSubsystem::DumpState(const FString& Reason) const
{
    FString Mode = IsServer()?TEXT("Server"):TEXT("Client");

    // 组件键列表
    TArray<FString> Keys; Keys.Reserve(ComponentMap.Num());
    for (const auto& Pair : ComponentMap)
    {
        Keys.Add(FString::Printf(TEXT("%s%s"), *Pair.Key, (Pair.Value.IsValid()?TEXT(""):TEXT("(invalid)"))));
    }

    // 客户端列表
    TArray<FString> ClientStrs; ClientStrs.Reserve(MediaClients.Num());
    for (const FIPv4Endpoint& Ep : MediaClients)
    {
        ClientStrs.Add(Ep.ToString());
    }

    // 流统计
    int32 ServerStreamCount = ServerStreams.Num();
    int32 ClientStreamCount = ClientStreams.Num();

    // 订阅概览
    int32 SubStreamCount = StreamSubscribers.Num();
    int32 PendingKeySubCount = PendingKeySubscribers.Num();

    // WS 状态
    const bool bWs = WebSocket.IsValid();

    // 统计复制
    int64 Bytes=0, Frames=0; double Sec=0.0; int64 Vis=0;
    GetAudioStatsEx(Bytes, Frames, Sec, Vis);

    UE_LOG(LogTemp, Log, TEXT("[AudioStream][Dump][%s] mode=%s UDPPort=%d WS=%d ActiveKey=%s sr=%d ch=%d offsetUs=%.0f(has=%d)"),
        *Reason, *Mode, MediaUdpPort, bWs?1:0, *ActiveWsTargetKey, ActiveWsSampleRate, ActiveWsChannels, EstimatedOffsetUs, bHasOffset?1:0);
    UE_LOG(LogTemp, Log, TEXT("[AudioStream][Dump] components=%d -> [%s]"), ComponentMap.Num(), *FString::Join(Keys, TEXT(", ")));
    UE_LOG(LogTemp, Log, TEXT("[AudioStream][Dump] mediaClients=%d -> [%s]"), MediaClients.Num(), *FString::Join(ClientStrs, TEXT(", ")));
    UE_LOG(LogTemp, Log, TEXT("[AudioStream][Dump] streams(server=%d, client=%d) nextStreamId=%u subStreams=%d pendingKeySubs=%d"), ServerStreamCount, ClientStreamCount, (unsigned)NextStreamId, SubStreamCount, PendingKeySubCount);
    UE_LOG(LogTemp, Log, TEXT("[AudioStream][Dump] stats: sec=%.3f frames=%lld bytes=%lld visemes=%lld liveLog=%d"), Sec, Frames, Bytes, Vis, bStatsLiveLog?1:0);
}


// 主动向 TTS 服务 /run 发送 POST，获取 task_id 后连接 WebSocket /ws/<task_id>
void UAudioStreamHttpWsSubsystem::StartRunAndConnect(const FString& ServerHostWithPort,
                                                    const FString& CallbackUrl,
                                                    const FString& TargetKey,
                                                    int32 SampleRate,
                                                    int32 Channels,
                                                    bool bUseHttps,
                                                    const FString& HttpRunPath,
                                                    const FString& WsPathPrefix)
{
    // Merge with settings
    const UAudioStreamSettings* S = GetDefault<UAudioStreamSettings>();

    FString Host = ServerHostWithPort;
    if (Host.IsEmpty() && S) Host = S->DefaultWsHost;
    if (Host.IsEmpty()) Host = TEXT("127.0.0.1:8001");

    FString RunPath = HttpRunPath.IsEmpty() ? TEXT("/run") : HttpRunPath;
    if (!RunPath.StartsWith(TEXT("/"))) RunPath = TEXT("/") + RunPath;

    FString WsPrefix = WsPathPrefix;
    if (WsPrefix.IsEmpty() && S) WsPrefix = S->DefaultWsPathPrefix;
    if (WsPrefix.IsEmpty()) WsPrefix = TEXT("/ws/");

    int32 SR = (SampleRate > 0) ? SampleRate : (S ? S->DefaultSampleRate : 16000);
    int32 CH = (Channels > 0) ? Channels : (S ? S->DefaultChannels : 1);

    // If caller didn't explicitly request HTTPS, follow settings' scheme
    const bool bUseHttpsFinal = bUseHttps || (S && S->DefaultWsScheme.Equals(TEXT("wss"), ESearchCase::IgnoreCase));
    const FString HttpScheme = bUseHttpsFinal ? TEXT("https") : TEXT("http");

    // Key fallback
    FString KeyCopy = TargetKey;
    if (KeyCopy.IsEmpty())
    {
        KeyCopy = ResolveTargetKeyOrFallback(ComponentMap, FString(), ActiveWsTargetKey);
    }

    const FString HttpUrl = FString::Printf(TEXT("%s://%s%s"), *HttpScheme, *Host, *RunPath);

    TSharedRef<FJsonObject> BodyObj = MakeShared<FJsonObject>();
    if (!CallbackUrl.IsEmpty())
    {
        BodyObj->SetStringField(TEXT("callback_url"), CallbackUrl);
    }
    FString BodyStr;
    {
        TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&BodyStr);
        FJsonSerializer::Serialize(BodyObj, Writer);
    }

    TWeakObjectPtr<UAudioStreamHttpWsSubsystem> Self = this;

    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Req = FHttpModule::Get().CreateRequest();
    Req->SetURL(HttpUrl);
    Req->SetVerb(TEXT("POST"));
    Req->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
    Req->SetContentAsString(BodyStr.IsEmpty() ? TEXT("{}") : BodyStr);
    UE_LOG(LogTemp, Log, TEXT("[AudioStream] POST %s ... body=%s"), *HttpUrl, *BodyStr);

    Req->OnProcessRequestComplete().BindLambda([Self, Host, WsPrefix, SR, CH, KeyCopy, bUseHttpsFinal](FHttpRequestPtr /*Request*/, FHttpResponsePtr Response, bool bSucceeded)
    {
        if (!Self.IsValid()) return;
        if (!bSucceeded || !Response.IsValid())
        {
            UE_LOG(LogTemp, Error, TEXT("[AudioStream] /run POST failed (request error)"));
            return;
        }
        const int32 Code = Response->GetResponseCode();
        const FString Content = Response->GetContentAsString();
        if (Code < 200 || Code >= 300)
        {
            UE_LOG(LogTemp, Error, TEXT("[AudioStream] /run POST non-2xx: %d content=%s"), Code, *Content);
            return;
        }

        TSharedPtr<FJsonObject> Root;
        const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Content);
        if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
        {
            UE_LOG(LogTemp, Error, TEXT("[AudioStream] /run POST JSON parse failed: %s"), *Content);
            return;
        }
        FString TaskId;
        if (!Root->TryGetStringField(TEXT("task_id"), TaskId) || TaskId.IsEmpty())
        {
            UE_LOG(LogTemp, Error, TEXT("[AudioStream] /run POST missing task_id in response: %s"), *Content);
            return;
        }

        // 通过 HandleTaskStart_NCP 统一解析与连接
        TSharedRef<FJsonObject> Synthetic = MakeShared<FJsonObject>();
        Synthetic->SetStringField(TEXT("task_id"), TaskId);
        Synthetic->SetStringField(TEXT("ws_host"), Host);
        Synthetic->SetStringField(TEXT("ws_scheme"), bUseHttpsFinal ? TEXT("wss") : TEXT("ws"));
        Synthetic->SetStringField(TEXT("ws_path_prefix"), WsPrefix);
        Synthetic->SetStringField(TEXT("key"), KeyCopy);
        Synthetic->SetNumberField(TEXT("sample_rate"), SR);
        Synthetic->SetNumberField(TEXT("channels"), CH);

        FString BodyForParser;
        {
            TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&BodyForParser);
            FJsonSerializer::Serialize(Synthetic, W);
        }

        FNivaHttpRequest FakeReq; FakeReq.Body = BodyForParser;
        Self->HandleTaskStart_NCP(FakeReq);
    });

    Req->ProcessRequest();
}


// 第3步：向 /stream/{task_id} 发送文本块
void UAudioStreamHttpWsSubsystem::PostStreamText(const FString& Text)
{
    FString TaskId = ActiveTaskId;
    if (TaskId.IsEmpty())
    {
        UE_LOG(LogTemp, Warning, TEXT("[AudioStream] PostStreamText: missing task_id. Call StartRunAndConnect first or pass TaskIdOverride."));
        return;
    }

    FString Host = ActiveHttpHost;
    const UAudioStreamSettings* S = GetDefault<UAudioStreamSettings>();
    if (Host.IsEmpty() && S) Host = S->DefaultWsHost;
    if (Host.IsEmpty()) Host = TEXT("127.0.0.1:8001");

    bool bHttps = bActiveUseHttps;
    if (!bHttps && S)
    {
        bHttps = S->DefaultWsScheme.Equals(TEXT("wss"), ESearchCase::IgnoreCase);
    }
    const FString HttpScheme = bHttps ? TEXT("https") : TEXT("http");

    FString Prefix = TEXT("/stream/");
    if (!Prefix.StartsWith(TEXT("/"))) Prefix = TEXT("/") + Prefix;
    if (!Prefix.EndsWith(TEXT("/"))) Prefix += TEXT("/");

    const FString Url = FString::Printf(TEXT("%s://%s%s%s"), *HttpScheme, *Host, *Prefix, *TaskId);

    TSharedRef<FJsonObject> BodyObj = MakeShared<FJsonObject>();
    BodyObj->SetStringField(TEXT("text"), Text);

    FString BodyStr;
    {
        TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&BodyStr);
        FJsonSerializer::Serialize(BodyObj, Writer);
    }

    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Req = FHttpModule::Get().CreateRequest();
    Req->SetURL(Url);
    Req->SetVerb(TEXT("POST"));
    Req->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
    Req->SetContentAsString(BodyStr);

    UE_LOG(LogTemp, Log, TEXT("[AudioStream] POST %s ... textLen=%d"), *Url, Text.Len());

    Req->OnProcessRequestComplete().BindLambda([](FHttpRequestPtr /*Request*/, FHttpResponsePtr Response, bool bSucceeded)
    {
        if (!bSucceeded || !Response.IsValid())
        {
            UE_LOG(LogTemp, Warning, TEXT("[AudioStream] /stream POST failed (request error)"));
            return;
        }
        const int32 Code = Response->GetResponseCode();
        if (Code < 200 || Code >= 300)
        {
            UE_LOG(LogTemp, Warning, TEXT("[AudioStream] /stream POST non-2xx: %d, resp=%s"), Code, *Response->GetContentAsString());
        }
    });

    Req->ProcessRequest();
}
