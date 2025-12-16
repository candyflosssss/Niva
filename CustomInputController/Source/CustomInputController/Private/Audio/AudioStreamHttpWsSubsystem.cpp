#include "Audio/AudioStreamHttpWsSubsystem.h"
#include "Audio/AudioStreamHttpWsComponent.h"
#include "Input/UUDPHandler.h"
#include "Audio/MediaStreamPacket.h"

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

// CoreManager logging
#include "Log/CoreLogSubsystem.h"
#include "Log/CoreLogTypes.h"

// 进程级：最近一次成功HELLO的服务器IP（用于换房间/跨服务器时触发重新注册）
static FString GLastHelloServerIp;

static TMap<FString, TArray<uint8>> GStreamTails; // key -> 上一包残留字节

// 适配 CoreManager 日志的便捷函数（本地文件私有）
static void CoreLog(const UAudioStreamHttpWsSubsystem* Self, ECoreLogSeverity Severity, const FString& Message)
{
    if (!Self) return;
    if (UCoreLogSubsystem* LogSS = UCoreLogSubsystem::Get(Self))
    {
        LogSS->Log(TEXT("StreamRegistry"), TEXT("RegistrationProcess"), Severity, Message);
    }
}

static void CoreLog(const UAudioStreamHttpWsSubsystem* Self, ECoreLogSeverity Severity, const FString& Message, const TMap<FString,FString>& Data)
{
    if (!Self) return;
    if (UCoreLogSubsystem* LogSS = UCoreLogSubsystem::Get(Self))
    {
        LogSS->Log(TEXT("StreamRegistry"), TEXT("RegistrationProcess"), Severity, Message, Data);
    }
}

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
    CoreLog(this, ECoreLogSeverity::Debug, FString::Printf(TEXT("HTTP listener %s"), bHttpOk?TEXT("started"):TEXT("not available")));

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
    CoreLog(this, ECoreLogSeverity::Warn, TEXT("Deinitialize begin"));
    if (MediaTickerHandle.IsValid())
    {
        FTSTicker::GetCoreTicker().RemoveTicker(MediaTickerHandle);
        MediaTickerHandle = FTSTicker::FDelegateHandle();
    }
    ShutdownMediaUdp();
    StopStreaming();
    StopHttpListener();
    UuidComponentMap.Empty();
    Super::Deinitialize();
    UE_LOG(LogTemp, Log, TEXT("[AudioStream] Deinitialize end"));
    CoreLog(this, ECoreLogSeverity::Warn, TEXT("Deinitialize end"));
}

// 新增：HTTP 路由绑定/解绑与组件注册
bool UAudioStreamHttpWsSubsystem::StartHttpListener(int32 /*Port*/)
{
    // 客户端不绑定HTTP路由，避免大厅/多PIE时的全局路由冲突
    if (!IsServer())
    {
        UE_LOG(LogTemp, Verbose, TEXT("[AudioStream] Skip HTTP routes on client"));
        CoreLog(this, ECoreLogSeverity::Warn, TEXT("Skip HTTP routes on client"));
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
        CoreLog(this, ECoreLogSeverity::Debug, TEXT("HTTP routes bound: /audio/push, /audio/stats (client handles /run as POST)"));
        return true;
    }
    UE_LOG(LogTemp, Verbose, TEXT("[AudioStream] NetworkCoreSubsystem not found, skip HTTP routes"));
    CoreLog(this, ECoreLogSeverity::Warn, TEXT("NetworkCoreSubsystem not found, skip HTTP routes"));
    return false;
}

void UAudioStreamHttpWsSubsystem::StopHttpListener()
{
    if (bHttpStarted)
    {
        UE_LOG(LogTemp, Log, TEXT("[AudioStream] HTTP listener stopped"));
        CoreLog(this, ECoreLogSeverity::Warn, TEXT("HTTP listener stopped"));
    }
    bHttpStarted = false;
}

bool UAudioStreamHttpWsSubsystem::RegisterComponent(UAudioStreamHttpWsComponent* Comp, FString& OutUuid)
{
    if (!IsValid(Comp)) return false;

    // 生成 UUID（使用 FGuid）
    FGuid G = FGuid::NewGuid();
    const FString Uuid = G.ToString(EGuidFormats::DigitsWithHyphens);
    UuidComponentMap.Add(Uuid, Comp);

    OutUuid = Uuid;
    UE_LOG(LogTemp, Log, TEXT("[AudioStream] Component registered uuid=%s total=%d"), *Uuid, UuidComponentMap.Num());
    CoreLog(this, ECoreLogSeverity::Info, FString::Printf(TEXT("Component registered uuid=%s total=%d"), *Uuid, UuidComponentMap.Num()));

    // If this is the first component, auto start the TTS flow using the UUID as target
    if (UuidComponentMap.Num() == 1)
    {
        const UAudioStreamSettings* S = GetDefault<UAudioStreamSettings>();
        const FString Host = S ? S->DefaultWsHost : TEXT("127.0.0.1:8001");
        const bool bHttps = (S && S->DefaultWsScheme.Equals(TEXT("wss"), ESearchCase::IgnoreCase));
        const int32 SR = S ? S->DefaultSampleRate : 16000;
        const int32 CH = S ? S->DefaultChannels : 1;
        const FString RunPath = TEXT("/run");
        const FString WsPrefix = S ? S->DefaultWsPathPrefix : TEXT("/ws/");
        UE_LOG(LogTemp, Log, TEXT("[AudioStream] First component added -> auto StartRunAndConnect (host=%s, scheme=%s, sr=%d, ch=%d, uuid=%s)"), *Host, bHttps?TEXT("wss"):TEXT("ws"), SR, CH, *Uuid);
        StartRunAndConnect(Host, TEXT(""), Uuid, SR, CH, bHttps, RunPath, WsPrefix);
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
}

// Helper: resolve target UUID or fallback to active or single-entry if available
static FString ResolveTargetUuidOrFallback(const TMap<FString, TWeakObjectPtr<UAudioStreamHttpWsComponent>>& Map, const FString& Candidate, const FString& Active)
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
        CoreLog(this, ECoreLogSeverity::Error, FString::Printf(TEXT("Failed to create WebSocket for %s"), *Url));
        return;
    }

    CoreLog(this, ECoreLogSeverity::Debug, FString::Printf(TEXT("WS connect begin -> url=%s"), *Url));

    TWeakObjectPtr<UAudioStreamHttpWsSubsystem> Self = this;

    WebSocket->OnConnected().AddLambda([Self]()
    {
        if (!Self.IsValid()) return;
        UAudioStreamHttpWsSubsystem* P = Self.Get();
        CoreLog(P, ECoreLogSeverity::Info, FString::Printf(TEXT("WS connected -> host=%s task=%s uuid=%s"), *P->ActiveHttpHost, *P->ActiveTaskId, *ResolveTargetUuidOrFallback(P->UuidComponentMap, FString(), P->ActiveWsTargetUuid)));
        // 连接建立时，清空目标组件缓冲，避免残留导致起始噪音
        const FString Uuid = ResolveTargetUuidOrFallback(P->UuidComponentMap, FString(), P->ActiveWsTargetUuid);
        if (!Uuid.IsEmpty())
        {
            ClearTail_GT(Uuid);
            CoreLog(P, ECoreLogSeverity::Debug, FString::Printf(TEXT("Cleared stream tail for uuid=%s on WS connected"), *Uuid));
        }
    });

    WebSocket->OnConnectionError().AddLambda([Self](const FString& Error)
    {
        if (!Self.IsValid()) return;
        UAudioStreamHttpWsSubsystem* P = Self.Get();
        CoreLog(P, ECoreLogSeverity::Error, FString::Printf(TEXT("WS connection error: %s"), *Error));
    });

    WebSocket->OnClosed().AddLambda([Self](const EWebSocketCloseCode CloseCode, const FString& Reason)
    {
        if (!Self.IsValid()) return;
        UAudioStreamHttpWsSubsystem* P = Self.Get();
        CoreLog(P, ECoreLogSeverity::Warn, FString::Printf(TEXT("WS closed: %s (code=%d)"), *Reason, (int32)CloseCode));
    });

    // ... existing lambdas updated similarly to use UuidComponentMap and Uuid variable instead of Key ...

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
            if (P) CoreLog(P, ECoreLogSeverity::Warn, TEXT("WS text parse failed"));
            return;
        }

        FString StatusStr;
        if (RootObj->TryGetStringField(TEXT("status"), StatusStr))
        {
            if (StatusStr.Equals(TEXT("completed"), ESearchCase::IgnoreCase))
            {
                UE_LOG(LogTemp, Log, TEXT("WS status=completed -> closing WebSocket"));
                CoreLog(P, ECoreLogSeverity::Debug, TEXT("WS status=completed -> closing WebSocket"));
                AsyncTask(ENamedThreads::GameThread, [P]()
                {
                    if (P) { P->CloseWebSocket(); }
                });
                return;
            }
        }

        FString Type; RootObj->TryGetStringField(TEXT("type"), Type);
        FString MsgUuid; RootObj->TryGetStringField(TEXT("key"), MsgUuid); if (MsgUuid.IsEmpty()) RootObj->TryGetStringField(TEXT("role_id"), MsgUuid);
        const FString Uuid = ResolveTargetUuidOrFallback(P->UuidComponentMap, MsgUuid, P->ActiveWsTargetUuid);

        if (Type.Equals(TEXT("audio"), ESearchCase::IgnoreCase))
        {
            int32 SR = P->ActiveWsSampleRate, CH = P->ActiveWsChannels, Tmp;
            if (RootObj->TryGetNumberField(TEXT("sample_rate"), Tmp)) SR = Tmp;
            if (RootObj->TryGetNumberField(TEXT("channels"), Tmp)) CH = Tmp;
            CH = FMath::Clamp(CH, 1, 8);

            FString Base64; RootObj->TryGetStringField(TEXT("data"), Base64);

            if (Uuid.IsEmpty()) { UE_LOG(LogTemp, Warning, TEXT("WS audio JSON dropped: empty uuid")); return; }

            TWeakObjectPtr<UAudioStreamHttpWsComponent>* Found = P->UuidComponentMap.Find(Uuid);
            const bool bNeedLocalPlay = !P->IsServer();
            if (bNeedLocalPlay && (!Found || !Found->IsValid()))
            {
                UE_LOG(LogTemp, Warning, TEXT("WS audio JSON dropped (client mode): component not found for uuid=%s"), *Uuid);
                return;
            }
            if (Base64.IsEmpty()) { UE_LOG(LogTemp, Warning, TEXT("WS audio JSON dropped: empty base64")); return; }

            TArray<uint8> Decoded; if (!FBase64::Decode(Base64, Decoded)) { UE_LOG(LogTemp, Warning, TEXT("WS audio JSON base64 decode failed (len=%d)"), Base64.Len()); return; }

            TArray<uint8> Pcm; int32 UseSR = SR, UseCH = CH;
            if (ExtractPcmFromMaybeWav(Decoded, Pcm, UseSR, UseCH))
            {
                if (P) CoreLog(P, ECoreLogSeverity::Trace, FString::Printf(TEXT("WS audio received -> kind=WAV uuid=%s pcmBytes=%d sr=%d ch=%d"), *Uuid, Pcm.Num(), UseSR, UseCH));
            }
            else
            {
                Pcm = MoveTemp(Decoded);
                if (P) CoreLog(P, ECoreLogSeverity::Trace, FString::Printf(TEXT("WS audio received -> kind=RAW uuid=%s bytes=%d sr=%d ch=%d"), *Uuid, Pcm.Num(), UseSR, UseCH));
            }

            if (P->IsServer())
            {
                TArray<uint8> DataToSend = Pcm;
                AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, [P, Uuid, Data=MoveTemp(DataToSend), UseSR, UseCH]() mutable
                {
                    if (P)
                    {
                        P->ServerDistributeAudio(Uuid, Data, UseSR, UseCH);
                    }
                });
            }
            else
            {
                // 客户端：统计并记录；组件目前不直接播放
                CoreLog(P, ECoreLogSeverity::Trace, FString::Printf(TEXT("WS audio received for uuid=%s bytes=%d sr=%d ch=%d (client mode)"), *Uuid, Pcm.Num(), UseSR, UseCH));
                P->UpdateStats(Pcm.Num(), UseSR, UseCH);
                P->LogCurrentStats(TEXT("WSAudio"));
            }
        }
        else if (Type.Equals(TEXT("text"), ESearchCase::IgnoreCase))
        {
            FString Text; RootObj->TryGetStringField(TEXT("data"), Text);
            if (Uuid.IsEmpty())
            {
                UE_LOG(LogTemp, Warning, TEXT("WS text dropped: empty uuid"));
                if (P) CoreLog(P, ECoreLogSeverity::Warn, FString::Printf(TEXT("WS text dropped: empty uuid; text=%s"), *Text));
                return;
            }
            auto Found = P->UuidComponentMap.Find(Uuid);
            if (!Found || !Found->IsValid())
            {
                UE_LOG(LogTemp, Warning, TEXT("WS text dropped: component not found for uuid=%s"), *Uuid);
                if (P) CoreLog(P, ECoreLogSeverity::Warn, FString::Printf(TEXT("WS text dropped: component not found for uuid=%s; text=%s"), *Uuid, *Text));
                return;
            }
            // 组件已简化为仅注册信息，记录收到的文本并通过 CoreLog 通知
            CoreLog(P, ECoreLogSeverity::Info, FString::Printf(TEXT("WS text for uuid=%s: %s"), *Uuid, *Text));
        }
        else if (Type.Equals(TEXT("viseme"), ESearchCase::IgnoreCase))
        {
            const TArray<TSharedPtr<FJsonValue>>* ArrPtr = nullptr;
            if (!RootObj->TryGetArrayField(TEXT("data"), ArrPtr) || !ArrPtr)
            {
                UE_LOG(LogTemp, Warning, TEXT("WS viseme dropped: no array"));
                if (P) CoreLog(P, ECoreLogSeverity::Warn, TEXT("WS viseme dropped: no array"));
                return;
            }
            TArray<int32> Vis; Vis.Reserve(ArrPtr->Num());
            for (const auto& V : *ArrPtr) { int32 Val=0; if (V->TryGetNumber(Val)) Vis.Add(Val); }

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

            if (Uuid.IsEmpty())
            {
                UE_LOG(LogTemp, Warning, TEXT("WS viseme dropped: empty uuid"));
                if (P) CoreLog(P, ECoreLogSeverity::Warn, TEXT("WS viseme dropped: empty uuid"));
                return;
            }
            auto Found = P->UuidComponentMap.Find(Uuid);
            if (!Found || !Found->IsValid())
            {
                UE_LOG(LogTemp, Warning, TEXT("WS viseme dropped: component not found for uuid=%s"), *Uuid);
                if (P) CoreLog(P, ECoreLogSeverity::Warn, FString::Printf(TEXT("WS viseme dropped: component not found for uuid=%s"), *Uuid));
                return;
            }
            UE_LOG(LogTemp, Log, TEXT("WS viseme -> n=%d uuid=%s confN=%d"), Vis.Num(), *Uuid, Confidence.Num());
            if (P) CoreLog(P, ECoreLogSeverity::Trace, FString::Printf(TEXT("WS viseme -> n=%d uuid=%s confN=%d"), Vis.Num(), *Uuid, Confidence.Num()));

            // 统计 viseme 数
            P->UpdateVisemeStats(Vis.Num());
            P->LogCurrentStats(TEXT("WSViseme"));

            // 组件现在仅保留注册信息；把 viseme 事件记录到 CoreLog 中，后续系统可通过 UUID 查询组件并处理
            CoreLog(P, ECoreLogSeverity::Info, FString::Printf(TEXT("WS viseme for uuid=%s count=%d"), *Uuid, Vis.Num()));

        }
        else
        {
            UE_LOG(LogTemp, Verbose, TEXT("WS text ignored: type=%s"), *Type);
        }
    });

    // 二进制帧：仅打印关键/截断信息，避免输出完整原始数据
    WebSocket->OnRawMessage().AddLambda([Self](const void* Data, SIZE_T Size, SIZE_T BytesRemaining)
    {
        if (!Self.IsValid() || Data == nullptr) return;

        const uint8* Bytes = static_cast<const uint8*>(Data);
        const int32 MaxPreview = 32; // 截断预览长度（字节）
        const int32 PreviewLen = static_cast<int32>(FMath::Min<SIZE_T>(Size, MaxPreview));

        FString HexPreview;
        HexPreview.Reserve(PreviewLen * 3);
        for (int32 i = 0; i < PreviewLen; ++i)
        {
            HexPreview += FString::Printf(TEXT("%02X"), Bytes[i]);
            if (i + 1 < PreviewLen) HexPreview += TEXT(" ");
        }

        const bool bTruncated = Size > static_cast<SIZE_T>(MaxPreview);
        const uint64 Total = static_cast<uint64>(Size);
        const uint64 Rem = static_cast<uint64>(BytesRemaining);


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
