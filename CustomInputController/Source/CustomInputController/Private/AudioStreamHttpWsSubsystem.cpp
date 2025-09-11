#include "AudioStreamHttpWsSubsystem.h"
#include "AudioStreamHttpWsComponent.h"

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

static TMap<FString, TArray<uint8>> GStreamTails; // key -> 上一包残留字节

static void ClearTail_GT(const FString& Key)
{
    check(IsInGameThread());
    GStreamTails.Remove(Key);
}
static void ClearAllTails_GT()
{
    check(IsInGameThread());
    GStreamTails.Empty();
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
    // 统一HTTP监听（由NetworkCorePlugin接口控制）
    StartHttpListener(0);
}

void UAudioStreamHttpWsSubsystem::Deinitialize()
{
    StopStreaming();
    StopHttpListener();
    ComponentMap.Empty();
    Super::Deinitialize();
}

bool UAudioStreamHttpWsSubsystem::StartHttpListener(int32 /*Port*/)
{
    if (bHttpStarted)
    {
        return true;
    }

    UGameInstance* GI = GetGameInstance();
    if (!GI)
    {
        return false;
    }
    UNetworkCoreSubsystem* Ncp = GI->GetSubsystem<UNetworkCoreSubsystem>();
    if (!Ncp)
    {
        return false;
    }

    // 绑定 /audio/push
    {
        FNetworkCoreHttpServerDelegate Delegate;
        Delegate.BindUFunction(this, FName("HandleAudioPush_NCP"));
        Ncp->BindRoute(TEXT("/audio/push"), ENivaHttpRequestVerbs::POST, Delegate);
    }

    // 兼容：保留 /task/start（可选）
    {
        FNetworkCoreHttpServerDelegate Delegate;
        Delegate.BindUFunction(this, FName("HandleTaskStart_NCP"));
        Ncp->BindRoute(TEXT("/task/start"), ENivaHttpRequestVerbs::POST, Delegate);
    }

    // 新增：/audio/stats 查询
    {
        FNetworkCoreHttpServerDelegate Delegate;
        Delegate.BindUFunction(this, FName("HandleAudioStats_NCP"));
        Ncp->BindRoute(TEXT("/audio/stats"), ENivaHttpRequestVerbs::GET, Delegate);
    }

    bHttpStarted = true;
    return true;
}

void UAudioStreamHttpWsSubsystem::StopHttpListener()
{
    // 由 NetworkCorePlugin 管理监听与路由解绑
    bHttpStarted = false;
}

bool UAudioStreamHttpWsSubsystem::RegisterComponent(UAudioStreamHttpWsComponent* Comp, FString& OutKey, const FString& PreferredKey)
{
    if (!Comp)
    {
        return false;
    }

    FString Key = PreferredKey;
    if (Key.IsEmpty() || ComponentMap.Contains(Key))
    {
        // 生成唯一Key
        Key = FString::Printf(TEXT("aud-%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits));
        while (ComponentMap.Contains(Key))
        {
            Key = FString::Printf(TEXT("aud-%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits));
        }
    }

    ComponentMap.Add(Key, Comp);
    OutKey = Key;
    UE_LOG(LogTemp, Log, TEXT("Audio component registered: %s"), *Key);
    return true;
}

void UAudioStreamHttpWsSubsystem::UnregisterComponent(UAudioStreamHttpWsComponent* Comp)
{
    if (!Comp) return;

    FString FoundKey;
    for (auto It = ComponentMap.CreateIterator(); It; ++It)
    {
        if (It.Value() == Comp)
        {
            FoundKey = It.Key();
            It.RemoveCurrent();
            break;
        }
    }
    if (!FoundKey.IsEmpty())
    {
        UE_LOG(LogTemp, Log, TEXT("Audio component unregistered: %s"), *FoundKey);
        // 清理对应 Key 的尾巴
        AsyncTask(ENamedThreads::GameThread, [K=FoundKey]() { ClearTail_GT(K); });
    }
}

void UAudioStreamHttpWsSubsystem::StopStreaming()
{
    // 广播停止（若需要）
    for (auto& Pair : ComponentMap)
    {
        if (Pair.Value.IsValid())
        {
            if (UAudioStreamHttpWsComponent* C = Pair.Value.Get())
            {
                AsyncTask(ENamedThreads::GameThread, [C]()
                {
                    if (IsValid(C))
                    {
                        C->StopStreaming();
                    }
                });
            }
        }
    }
    // 清空所有流的尾巴
    AsyncTask(ENamedThreads::GameThread, [](){ ClearAllTails_GT(); });

    // 输出最终统计
    LogFinalStats(TEXT("StopStreaming"));
    CloseWebSocket();
}

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
            UE_LOG(LogTemp, Warning, TEXT("/audio/push JSON parse failed"));
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

    // 统计与推送在 GameThread 执行，并进行“尾巴进位”
    TWeakObjectPtr<UAudioStreamHttpWsComponent>* Found = ComponentMap.Find(Key);
    if (!Found || !Found->IsValid())
    {
        return UNetworkCoreSubsystem::MakeResponse(TEXT("Component not found"), TEXT("text/plain"), 404);
    }

    UAudioStreamHttpWsComponent* Target = Found->Get();
    AsyncTask(ENamedThreads::GameThread, [this, Key, Target, Data = MoveTemp(Pcm), UseSR, UseCH]() mutable
    {
        if (!IsValid(Target)) return;
        TArray<uint8> Bytes = MoveTemp(Data);
        AppendWithCarry_GT(Key, Bytes, UseCH);
        UpdateStats(Bytes.Num(), UseSR, UseCH);
        LogCurrentStats(TEXT("HTTP"));
        Target->PushPcmData(Bytes, UseSR, UseCH);
    });

    TSharedRef<FJsonObject> OkObj = MakeShared<FJsonObject>();
    OkObj->SetStringField(TEXT("status"), TEXT("ok"));
    OkObj->SetStringField(TEXT("key"), Key);
    OkObj->SetNumberField(TEXT("bytes"), (double)Request.BodyBytes.Num());
    OkObj->SetNumberField(TEXT("decoded"), (double)Pcm.Num());
    OkObj->SetNumberField(TEXT("sample_rate"), UseSR);
    OkObj->SetNumberField(TEXT("channels"), UseCH);

    FString JsonOut;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonOut);
    FJsonSerializer::Serialize(OkObj, Writer);
    return UNetworkCoreSubsystem::MakeResponse(JsonOut, TEXT("application/json"), 200);
}

// 兼容保留：/task/start 仍可用于WS直连
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
            UE_LOG(LogTemp, Warning, TEXT("/task/start JSON parse failed"));
        }
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
            // Fallback: ws://127.0.0.1:8000/ws/<task_id>
            WsUrl = FString::Printf(TEXT("ws://10.1.10.93:8000/ws/%s"), *TaskId);
        }
    }

    if (WsUrl.IsEmpty())
    {
        return UNetworkCoreSubsystem::MakeResponse(TEXT("Missing ws_url or task_id"), TEXT("text/plain"), 400);
    }
    if (TargetKey.IsEmpty())
    {
        return UNetworkCoreSubsystem::MakeResponse(TEXT("Missing key (or role_id)"), TEXT("text/plain"), 400);
    }

    TWeakObjectPtr<UAudioStreamHttpWsComponent>* Found = ComponentMap.Find(TargetKey);
    if (!Found || !Found->IsValid())
    {
        return UNetworkCoreSubsystem::MakeResponse(TEXT("Component not found"), TEXT("text/plain"), 404);
    }

    ActiveWsTargetKey = TargetKey;
    ActiveWsSampleRate = InSampleRate;
    ActiveWsChannels = InChannels;

    AsyncTask(ENamedThreads::GameThread, [this, WsUrl]()
    {
        CloseWebSocket();
        ConnectWebSocket(WsUrl);
    });

    UE_LOG(LogTemp, Log, TEXT("/task/start ok: key=%s task_id=%s ws_url=%s sr=%d ch=%d"), *TargetKey, *TaskId, *WsUrl, ActiveWsSampleRate, ActiveWsChannels);

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
            auto Found = P->ComponentMap.Find(Key);
            if (!Found || !Found->IsValid()) { UE_LOG(LogTemp, Warning, TEXT("WS audio JSON dropped: component not found for key=%s"), *Key); return; }
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

            // 不再在网络线程做对齐；在 GameThread 做“尾巴进位”再推送
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
    ActiveWsTargetKey.Reset();
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

// ============== 测试节点实现 ==============

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
    
    // 启动定时器，每100ms推送一次音频块
    UGameInstance* GI = GetGameInstance();
    if (GI && GI->GetWorld())
    {
        GI->GetWorld()->GetTimerManager().SetTimer(
            TestStreamTimer,
            this,
            &UAudioStreamHttpWsSubsystem::TestStreamTick,
            0.1f, // 100ms间隔
            true  // 重复
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
    
    // 检查是否超过持续时间
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
    PushTestAudioChunk(TestTargetKey, TestSampleRate, TestChannels, TestFrequency, 300.0f); // 改为200ms音频块
    
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
    
    // 生成测试音频数据
    float ChunkDurationSec = ChunkDurationMs / 1000.0f;
    TArray<uint8> AudioData = GenerateTestSineWave(SampleRate, Channels, FrequencyHz, ChunkDurationSec);
    
    if (AudioData.Num() > 0)
    {
        UAudioStreamHttpWsComponent* Target = Found->Get();
        AsyncTask(ENamedThreads::GameThread, [this, TargetKey, Target, Data = MoveTemp(AudioData), SampleRate, Channels]() mutable
        {
            if (!IsValid(Target)) return;
            
            // 进行尾巴进位处理
            AppendWithCarry_GT(TargetKey, Data, Channels);
            
            // 更新统计
            UpdateStats(Data.Num(), SampleRate, Channels);
            LogCurrentStats(TEXT("TestAudio"));
            
            // 推送到组件
            Target->PushPcmData(Data, SampleRate, Channels);
        });
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
    LogCurrentStats(TEXT("TestViseme"));
    
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

