#include "Audio/AudioStreamHttpWsComponent.h"
#include "Audio/AudioStreamHttpWsSubsystem.h"
#include "Audio/AudioStreamSettings.h" // ensure settings declared
#include "Engine/GameInstance.h"
#include "Engine/World.h"

// CoreManager logging
#include "Log/CoreLogSubsystem.h"
#include "Log/CoreLogTypes.h"
#include "Log/CoreLogHelpers.h"

#include "WebSocketsModule.h"
#include "IWebSocket.h"
#include "Modules/ModuleManager.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Async/Async.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonReader.h"
#include "Dom/JsonObject.h"
#include "Misc/Base64.h"

// Helper: Sanitize a string so it contains no newline characters (replace with spaces) and optionally truncate.
static FString SanitizeNoNewline(const FString& In, int32 MaxLen = 1024)
{
    if (In.IsEmpty()) return FString();
    FString Out = In;
    Out.ReplaceInline(TEXT("\r"), TEXT(" "));
    Out.ReplaceInline(TEXT("\n"), TEXT(" "));
    if (MaxLen > 0 && Out.Len() > MaxLen) { Out = Out.Left(MaxLen); }
    return Out;
}

// Helper: whether this component should initiate network operations (server-only)
static bool ComponentHasServerAuthority(const UAudioStreamHttpWsComponent* Comp)
{
    if (!Comp) return false;
    const AActor* Owner = Comp->GetOwner();
    return Owner && Owner->HasAuthority();
}

// Helper: role label for logs
static const TCHAR* GetRoleLabel(const UObject* Obj)
{
    if (!Obj) return TEXT("Unknown");
    const UWorld* W = Obj->GetWorld();
    if (!W) return TEXT("Unknown");
    switch (W->GetNetMode())
    {
        case NM_Standalone:      return TEXT("Server");
        case NM_ListenServer:    return TEXT("Server");
        case NM_DedicatedServer: return TEXT("Server");
        case NM_Client:          return TEXT("Client");
        default:                 return TEXT("Unknown");
    }
}

// Minimal WAV extractor: if data is RIFF/WAVE, extract PCM16 or convert float32->PCM16
static bool ExtractPcmFromMaybeWav_Local(const TArray<uint8>& InBytes, TArray<uint8>& OutPcm, int32& InOutSR, int32& InOutCH)
{
    auto ReadLE16 = [](const uint8* p) -> uint16 { return (uint16)p[0] | ((uint16)p[1] << 8); };
    auto ReadLE32 = [](const uint8* p) -> uint32 { return (uint32)p[0] | ((uint32)p[1] << 8) | ((uint32)p[2] << 16) | ((uint32)p[3] << 24); };
    if (InBytes.Num() < 44) return false;
    const uint8* B = InBytes.GetData();
    if (!(B[0]=='R' && B[1]=='I' && B[2]=='F' && B[3]=='F' && B[8]=='W' && B[9]=='A' && B[10]=='V' && B[11]=='E')) return false;

    int32 sr = InOutSR;
    int32 ch = InOutCH;
    uint16 fmtTag = 1; // 1=PCM, 3=IEEE_FLOAT
    uint16 bps = 16;

    int32 dataOffset = -1, dataSize = 0;
    int32 ofs = 12, N = InBytes.Num();
    while (ofs + 8 <= N)
    {
        const uint8* H = B + ofs;
        const uint32 sz = ReadLE32(H + 4);
        const int32 payloadBegin = ofs + 8;
        const int32 payloadEnd = payloadBegin + (int32)sz;
        if (payloadBegin < 0 || sz > (uint32)FMath::Max(0, N - payloadBegin)) return false;
        const bool isFmt = (H[0]=='f' && H[1]=='m' && H[2]=='t' && H[3]==' ');
        const bool isData = (H[0]=='d' && H[1]=='a' && H[2]=='t' && H[3]=='a');
        if (isFmt && sz >= 16)
        {
            fmtTag = ReadLE16(B + payloadBegin + 0);
            ch     = ReadLE16(B + payloadBegin + 2);
            sr     = (int32)ReadLE32(B + payloadBegin + 4);
            bps    = ReadLE16(B + payloadBegin + 14);
        }
        else if (isData)
        {
            dataOffset = payloadBegin;
            dataSize   = FMath::Min<int32>((int32)sz, N - dataOffset);
        }
        ofs = payloadEnd + ((sz & 1) ? 1 : 0);
    }
    if (dataOffset < 0 || dataSize <= 0) return false;
    const uint8* pd = B + dataOffset;
    if (fmtTag == 1 && bps == 16)
    {
        OutPcm.Reset(); OutPcm.Append(pd, dataSize);
    }
    else if (fmtTag == 3 && bps == 32)
    {
        const int32 samples = dataSize / 4;
        OutPcm.Reset(); OutPcm.AddUninitialized(samples * 2);
        int16* pi = reinterpret_cast<int16*>(OutPcm.GetData());
        for (int32 i=0;i<samples;++i)
        {
            float v; FMemory::Memcpy(&v, pd + i*4, 4);
            v = FMath::Clamp(v, -1.0f, 1.0f);
            pi[i] = (int16)FMath::RoundToInt(v * 32767.0f);
        }
    }
    else { return false; }
    InOutSR = sr; InOutCH = ch; return true;
}

void UAudioStreamHttpWsComponent::ProcessWebSocketMessage(const FString& Message)
{
    TSharedPtr<FJsonObject> RootObj;
    const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Message);
    if (!FJsonSerializer::Deserialize(Reader, RootObj) || !RootObj.IsValid())
    {
        FCoreLogHelpers::CoreLog(this, ECoreLogSeverity::Warn, TEXT("流数据处理"), TEXT("接收处理"), TEXT("WS text parse failed (component)"));
        return;
    }

    FString StatusStr;
    if (RootObj->TryGetStringField(TEXT("status"), StatusStr))
    {
        if (StatusStr.Equals(TEXT("completed"), ESearchCase::IgnoreCase))
        {
            FCoreLogHelpers::CoreLog(this, ECoreLogSeverity::Info, TEXT("流数据处理"), TEXT("流程更新"), TEXT("WS status=completed (component)"));
            return;
        }
    }

    FString Type; RootObj->TryGetStringField(TEXT("type"), Type);
    FString MsgUuid; RootObj->TryGetStringField(TEXT("key"), MsgUuid); if (MsgUuid.IsEmpty()) RootObj->TryGetStringField(TEXT("role_id"), MsgUuid);
    const FString Uuid = !MsgUuid.IsEmpty() ? MsgUuid : RegisteredUuid;

    if (Type.Equals(TEXT("audio"), ESearchCase::IgnoreCase))
    {
        int32 SR = ActiveWsSampleRate;
        int32 CH = ActiveWsChannels;
        int32 Tmp;
        if (RootObj->TryGetNumberField(TEXT("sample_rate"), Tmp)) SR = Tmp;
        if (RootObj->TryGetNumberField(TEXT("channels"), Tmp)) CH = FMath::Clamp(Tmp, 1, 8);

        FString Base64; RootObj->TryGetStringField(TEXT("data"), Base64);
        if (Base64.IsEmpty()) { FCoreLogHelpers::CoreLog(this, ECoreLogSeverity::Warn, TEXT("流数据处理"), TEXT("接收处理"), TEXT("WS audio dropped: empty base64 (component)")); return; }
        TArray<uint8> Decoded; if (!FBase64::Decode(Base64, Decoded)) { FCoreLogHelpers::CoreLog(this, ECoreLogSeverity::Warn, TEXT("流数据处理"), TEXT("接收处理"), TEXT("WS audio base64 decode failed (component)")); return; }
        TArray<uint8> Pcm; int32 UseSR = SR, UseCH = CH; const bool bWav = ExtractPcmFromMaybeWav_Local(Decoded, Pcm, UseSR, UseCH);
        const int32 Bytes = bWav ? Pcm.Num() : Decoded.Num();
        FCoreLogHelpers::CoreLog(this, ECoreLogSeverity::Trace, TEXT("流数据处理"), TEXT("接收处理"), FString::Printf(TEXT("WS audio parsed (component) -> kind=%s uuid=%s bytes=%d sr=%d ch=%d"), bWav?TEXT("WAV"):TEXT("RAW"), *Uuid, Bytes, UseSR, UseCH));
        // 后续：转发到播放/队列/统计，先注释
        if (UGameInstance* GI = GetWorld()?GetWorld()->GetGameInstance():nullptr) { if (auto* SS = GI->GetSubsystem<UAudioStreamHttpWsSubsystem>()) { SS->UpdateStats(Bytes, UseSR, UseCH); } }
    }
    else if (Type.Equals(TEXT("text"), ESearchCase::IgnoreCase))
    {
        FString Text; RootObj->TryGetStringField(TEXT("data"), Text);
        FCoreLogHelpers::CoreLog(this, ECoreLogSeverity::Info, TEXT("流数据处理"), TEXT("接收处理"), FString::Printf(TEXT("WS text (component) for uuid=%s: %s"), *Uuid, *Text));
        // 后续：事件分发，先注释
        // ...
    }
    else if (Type.Equals(TEXT("viseme"), ESearchCase::IgnoreCase))
    {
        const TArray<TSharedPtr<FJsonValue>>* ArrPtr = nullptr;
        if (!RootObj->TryGetArrayField(TEXT("data"), ArrPtr) || !ArrPtr)
        {
            FCoreLogHelpers::CoreLog(this, ECoreLogSeverity::Warn, TEXT("流数据处理"), TEXT("接收处理"), TEXT("WS viseme dropped: no array (component)"));
            return;
        }
        TArray<int32> Vis; Vis.Reserve(ArrPtr->Num());
        for (const auto& V : *ArrPtr) { int32 Val=0; if (V->TryGetNumber(Val)) Vis.Add(Val); }
        TArray<float> Confidence;
        const TArray<TSharedPtr<FJsonValue>>* ConfPtr = nullptr;
        if (RootObj->TryGetArrayField(TEXT("confidence"), ConfPtr) && ConfPtr)
        {
            Confidence.Reserve(ConfPtr->Num());
            for (const auto& C : *ConfPtr) { double D=0.0; if (C->TryGetNumber(D)) Confidence.Add((float)D); }
        }
        FCoreLogHelpers::CoreLog(this, ECoreLogSeverity::Trace, TEXT("流数据处理"), TEXT("接收处理"), FString::Printf(TEXT("WS viseme (component) -> n=%d uuid=%s confN=%d"), Vis.Num(), *Uuid, Confidence.Num()));
        // 后续：统计/驱动动画，先注释
        // if (UGameInstance* GI = GetWorld()?GetWorld()->GetGameInstance():nullptr) { if (auto* SS = GI->GetSubsystem<UAudioStreamHttpWsSubsystem>()) { SS->UpdateVisemeStats(Vis.Num()); } }
    }
    else
    {
        FCoreLogHelpers::CoreLog(this, ECoreLogSeverity::Trace, TEXT("流数据处理"), TEXT("接收处理"), FString::Printf(TEXT("WS ignored (component): type=%s"), *Type));
    }
}

UAudioStreamHttpWsComponent::UAudioStreamHttpWsComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
    SetIsReplicatedByDefault(true);
}

void UAudioStreamHttpWsComponent::BeginPlay()
{
    Super::BeginPlay();
    // 仅在服务器上触发服务器RPC（客户端无需主动调用）
    if (ComponentHasServerAuthority(this))
    {
        RegisterToSubsystem();
    }

    // Only the server should attempt to run/connect
    if (!ComponentHasServerAuthority(this))
    {
        FCoreLogHelpers::CoreLog(this, ECoreLogSeverity::Debug, TEXT("音频流组件"), TEXT("权限"), TEXT("Skip StartRunAndConnect on client (server-only)"));
        return;
    }

    // 使用设置中的默认值（如果存在），优先使用 settings 的 DefaultWsHost / DefaultWsScheme / DefaultWsPathPrefix
    const UAudioStreamSettings* S = GetDefault<UAudioStreamSettings>();
    FString Host = S ? S->DefaultWsHost : TEXT("127.0.0.1:8001");
    const bool bHttps = S ? S->DefaultWsScheme.Equals(TEXT("wss"), ESearchCase::IgnoreCase) : false;
    const int32 SR = S ? S->DefaultSampleRate : ActiveWsSampleRate;
    const int32 CH = S ? S->DefaultChannels : ActiveWsChannels;
    const FString RunPath = S ? S->DefaultHttpRunPath : TEXT("/run");
    const FString WsPrefix = S ? S->DefaultWsPathPrefix : TEXT("/ws/");

    // 自动在 BeginPlay 尝试发起 /run 并连接 WebSocket（使用 PreferredKey 作为 key，如果为空则让子系统分配）
    StartRunAndConnect(Host, FString(), PreferredKey, SR, CH, bHttps, RunPath, WsPrefix);
}

void UAudioStreamHttpWsComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    // 尝试优雅结束（这些调用在客户端上会早退，无副作用）
    PostEndStream();
    CloseWebSocket();
    UnregisterFromSubsystem();
    Super::EndPlay(EndPlayReason);
}

void UAudioStreamHttpWsComponent::RegisterToSubsystem_Implementation()
{
    if (bRegistered) return;
    UGameInstance* GI = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
    if (!GI) return;
    UAudioStreamHttpWsSubsystem* Subsys = GI->GetSubsystem<UAudioStreamHttpWsSubsystem>();
    if (!Subsys) return;

    const TCHAR* Role = GetRoleLabel(this);

    // 服务器上：如果已存在UUID，尝试携带UUID注册，否则由服务器分配
    FString OutUuid;
    bool bOk = false;
    if (!RegisteredUuid.IsEmpty())
    {
        bOk = Subsys->RegisterComponentWithUuid(this, RegisteredUuid, OutUuid);
        FCoreLogHelpers::CoreLog(this, ECoreLogSeverity::Info, TEXT("音频流组件"), TEXT("服务器注册"), FString::Printf(TEXT("Register with existing uuid on %s: %s"), Role, *RegisteredUuid));
    }
    else
    {
        bOk = Subsys->RegisterServerAllocateUuid(this, OutUuid);
        FCoreLogHelpers::CoreLog(this, ECoreLogSeverity::Info, TEXT("音频流组件"), TEXT("服务器注册"), FString::Printf(TEXT("Allocated uuid on %s: %s"), Role, *OutUuid));
    }

    if (bOk)
    {
        RegisteredUuid = OutUuid;
        bRegistered = true;
        FCoreLogHelpers::CoreLog(this, ECoreLogSeverity::Info, TEXT("音频流组件"), TEXT("服务器注册"), FString::Printf(TEXT("RegisterToSubsystem OK (%s) uuid=%s"), Role, *RegisteredUuid));
        // 多播给客户端，同步UUID并在各端注册
        MulticastAssignAndRegisterUuid(RegisteredUuid);
    }
    else
    {
        FCoreLogHelpers::CoreLog(this, ECoreLogSeverity::Error, TEXT("音频流组件"), TEXT("服务器注册"), FString::Printf(TEXT("RegisterToSubsystem FAILED (%s)"), Role));
    }
}

void UAudioStreamHttpWsComponent::MulticastAssignAndRegisterUuid_Implementation(const FString& InUuid)
{
    const TCHAR* Role = GetRoleLabel(this);
    FCoreLogHelpers::CoreLog(this, ECoreLogSeverity::Debug, TEXT("音频流组件"), TEXT("客户端注册"), FString::Printf(TEXT("Multicast assign uuid on %s: %s"), Role, *InUuid));

    // 在所有端执行：设置UUID并在本地子系统进行携带UUID注册
    RegisteredUuid = InUuid;

    // 若本端已注册则跳过（避免服务器端重复注册）
    if (bRegistered) return;

    UGameInstance* GI = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
    if (!GI) return;
    if (UAudioStreamHttpWsSubsystem* Subsys = GI->GetSubsystem<UAudioStreamHttpWsSubsystem>())
    {
        FString Dummy;
        if (Subsys->RegisterComponentWithUuid(this, RegisteredUuid, Dummy))
        {
            bRegistered = true;
            FCoreLogHelpers::CoreLog(this, ECoreLogSeverity::Info, TEXT("音频流组件"), TEXT("客户端注册"), FString::Printf(TEXT("Client registered (%s) uuid=%s"), Role, *RegisteredUuid));
        }
        else
        {
            FCoreLogHelpers::CoreLog(this, ECoreLogSeverity::Error, TEXT("音频流组件"), TEXT("客户端注册"), FString::Printf(TEXT("Client register FAILED (%s) uuid=%s"), Role, *RegisteredUuid));
        }
    }
}

// ===== 网络逻辑：StartRunAndConnect / POST / WS =====

void UAudioStreamHttpWsComponent::StartRunAndConnect(const FString& ServerHostWithPort, const FString& CallbackUrl, const FString& TargetUuid, int32 SampleRate, int32 Channels, bool bUseHttps, const FString& HttpRunPath, const FString& WsPathPrefix)
{
    // Server-authority gating
    if (!ComponentHasServerAuthority(this))
    {
        FCoreLogHelpers::CoreLog(this, ECoreLogSeverity::Debug, TEXT("音频流组件"), TEXT("权限"), TEXT("Skip StartRunAndConnect on client (server-only)"));
        return;
    }

    // 记住会话信息
    ActiveHttpHost = ServerHostWithPort;
    bActiveUseHttps = bUseHttps;
    ActiveWsSampleRate = SampleRate;
    ActiveWsChannels = Channels;

    // 保存当前会话的路径，以便重连时复用
    ActiveHttpRunPath = HttpRunPath;
    ActiveWsPathPrefix = WsPathPrefix;

    // 新的会话显式开始：这不是手动关闭，允许后续重连逻辑如有需要
    bManualClose = false;
    // 从新启动时重置尝试计数
    ReconnectAttempts = 0;

    // 把 /run 请求逻辑分离为 RequestRunTask
    RequestRunTask(ServerHostWithPort, CallbackUrl, TargetUuid, SampleRate, Channels, bUseHttps, HttpRunPath, WsPathPrefix);
}

// 新方法：执行 /run POST 并解析 task_id，成功后发起 WebSocket 连接
void UAudioStreamHttpWsComponent::RequestRunTask(const FString& ServerHostWithPort, const FString& CallbackUrl, const FString& TargetUuid, int32 SampleRate, int32 Channels, bool bUseHttps, const FString& HttpRunPath, const FString& WsPathPrefix)
{
    // Server-authority gating
    if (!ComponentHasServerAuthority(this))
    {
        FCoreLogHelpers::CoreLog(this, ECoreLogSeverity::Trace, TEXT("音频流组件"), TEXT("权限"), TEXT("Skip RequestRunTask on client (server-only)"));
        return;
    }

    const FString Scheme = bUseHttps ? TEXT("https") : TEXT("http");
    const FString RunUrl = FString::Printf(TEXT("%s://%s%s"), *Scheme, *ServerHostWithPort, *HttpRunPath);

    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Req = FHttpModule::Get().CreateRequest();
    Req->SetURL(RunUrl);
    Req->SetVerb(TEXT("POST"));
    Req->SetHeader(TEXT("Content-Type"), TEXT("application/json"));

    TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
    if (!CallbackUrl.IsEmpty()) Body->SetStringField(TEXT("callback"), CallbackUrl);
    Body->SetStringField(TEXT("key"), RegisteredUuid);
    Body->SetNumberField(TEXT("sample_rate"), SampleRate);
    Body->SetNumberField(TEXT("channels"), Channels);
    FString BodyStr; const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&BodyStr);
    FJsonSerializer::Serialize(Body, Writer);
    Req->SetContentAsString(BodyStr);

    // Log the request URL and sanitized body
    {
        TMap<FString, FString> Data;
        Data.Add(TEXT("url"), SanitizeNoNewline(RunUrl));
        Data.Add(TEXT("body"), SanitizeNoNewline(BodyStr));
        // 把data写进日志的message
        FCoreLogHelpers::CoreLog(this, ECoreLogSeverity::Debug, TEXT("音频流组件"), TEXT("连接握手"), TEXT("StartRunAndConnect /run request, url = ") + SanitizeNoNewline(RunUrl), Data);
    }

    TWeakObjectPtr<UAudioStreamHttpWsComponent> Self = this;
    Req->OnProcessRequestComplete().BindLambda([Self, ServerHostWithPort, WsPathPrefix, bUseHttps](FHttpRequestPtr, FHttpResponsePtr Resp, bool bOK)
    {
        if (!Self.IsValid()) return;
        UAudioStreamHttpWsComponent* P = Self.Get();
        if (!bOK || !Resp.IsValid())
        {
            TMap<FString,FString> Data;
            Data.Add(TEXT("host"), ServerHostWithPort);
            return;
        }
        FString Content = Resp->GetContentAsString();
        TSharedPtr<FJsonObject> Obj; const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Content);
        FJsonSerializer::Deserialize(Reader, Obj);
        FString TaskId; if (Obj.IsValid()) { Obj->TryGetStringField(TEXT("task_id"), TaskId); }
        if (TaskId.IsEmpty())
        {
            // Fallback: try "id"
            Obj.Reset(); FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Content), Obj);
            if (Obj.IsValid()) Obj->TryGetStringField(TEXT("id"), TaskId);
        }
        if (!TaskId.IsEmpty())
        {
            P->ActiveTaskId = TaskId;
            const FString WsScheme = bUseHttps ? TEXT("wss") : TEXT("ws");
            const FString WsUrl = FString::Printf(TEXT("%s://%s%s%s"), *WsScheme, *ServerHostWithPort, *WsPathPrefix, *TaskId);
            // 尝试连接到websocket
            AsyncTask(ENamedThreads::GameThread, [P, WsUrl]() { if (P) P->ConnectWebSocket(WsUrl); });
        }
        else
        {
            TMap<FString,FString> Data;
            Data.Add(TEXT("host"), ServerHostWithPort);
            Data.Add(TEXT("response"), Content.Left(256));
            FCoreLogHelpers::CoreLog(P, ECoreLogSeverity::Error, TEXT("音频流组件"), TEXT("连接握手"), TEXT("/run response missing task_id"), Data);
        }
    });
    Req->ProcessRequest();
}

void UAudioStreamHttpWsComponent::PostStreamText(const FString& Text)
{
    // Server-authority gating
    if (!ComponentHasServerAuthority(this))
    {
        FCoreLogHelpers::CoreLog(this, ECoreLogSeverity::Trace, TEXT("音频流组件"), TEXT("权限"), TEXT("Skip PostStreamText on client (server-only)"));
        return;
    }

    if (ActiveTaskId.IsEmpty() || ActiveHttpHost.IsEmpty()) return;
    const FString Scheme = bActiveUseHttps ? TEXT("https") : TEXT("http");
    const UAudioStreamSettings* S = GetDefault<UAudioStreamSettings>();
    const FString StreamPath = S ? S->DefaultHttpStreamPath : TEXT("/stream");
    FString Url = FString::Printf(TEXT("%s://%s%s/%s"), *Scheme, *ActiveHttpHost, *StreamPath, *ActiveTaskId);

    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Req = FHttpModule::Get().CreateRequest();
    Req->SetURL(Url);
    Req->SetVerb(TEXT("POST"));
    Req->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
    TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
    Obj->SetStringField(TEXT("text"), Text);
    FString BodyStr; const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&BodyStr);
    FJsonSerializer::Serialize(Obj, Writer);
    Req->SetContentAsString(BodyStr);

    // Log the request URL and sanitized body
    {
        TMap<FString, FString> Data;
        Data.Add(TEXT("url"), SanitizeNoNewline(Url));
        Data.Add(TEXT("body"), SanitizeNoNewline(BodyStr));
        // 在message里显示核心数据
        FCoreLogHelpers::CoreLog(this, ECoreLogSeverity::Trace, TEXT("音频流组件"), TEXT("信息流"), TEXT("Url => ") + SanitizeNoNewline(Url) + TEXT(" Body => ") + SanitizeNoNewline(BodyStr, 8), Data);
    }

    Req->OnProcessRequestComplete().BindLambda([Self=TWeakObjectPtr<UAudioStreamHttpWsComponent>(this)](FHttpRequestPtr, FHttpResponsePtr Resp, bool bOK)
    {
        if (!Self.IsValid()) return;
        if (!bOK || !Resp.IsValid())
        {
            TMap<FString,FString> Data; Data.Add(TEXT("action"), TEXT("PostStreamText"));
            FCoreLogHelpers::CoreLog(Self.Get(), ECoreLogSeverity::Warn, TEXT("音频流组件"), TEXT("信息流"), TEXT("PostStreamText failed"), Data);
        }
    });
    Req->ProcessRequest();
}

void UAudioStreamHttpWsComponent::PostEndStream()
{
    // Server-authority gating
    if (!ComponentHasServerAuthority(this))
    {
        FCoreLogHelpers::CoreLog(this, ECoreLogSeverity::Trace, TEXT("音频流组件"), TEXT("权限"), TEXT("Skip PostEndStream on client (server-only)"));
        return;
    }

    if (ActiveTaskId.IsEmpty() || ActiveHttpHost.IsEmpty()) return;
    const FString Scheme = bActiveUseHttps ? TEXT("https") : TEXT("http");
    const UAudioStreamSettings* S = GetDefault<UAudioStreamSettings>();
    const FString EndPath = S ? S->DefaultHttpEndStreamPath : TEXT("/end-stream");
    const FString Url = FString::Printf(TEXT("%s://%s%s/%s"), *Scheme, *ActiveHttpHost, *EndPath, *ActiveTaskId);

    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Req = FHttpModule::Get().CreateRequest();
    Req->SetURL(Url);
    Req->SetVerb(TEXT("POST"));

    // Log the request URL (no body) sanitized
    {
        TMap<FString, FString> Data;
        Data.Add(TEXT("url"), SanitizeNoNewline(Url));
        Data.Add(TEXT("body"), TEXT(""));
        FCoreLogHelpers::CoreLog(this, ECoreLogSeverity::Debug, TEXT("音频流组件"), TEXT("信息流"), TEXT("PostEndStream"), Data);
    }

    Req->OnProcessRequestComplete().BindLambda([Self=TWeakObjectPtr<UAudioStreamHttpWsComponent>(this)](FHttpRequestPtr, FHttpResponsePtr, bool)
    {
        if (!Self.IsValid()) return;
        TMap<FString,FString> Data; Data.Add(TEXT("action"), TEXT("PostEndStream"));
        FCoreLogHelpers::CoreLog(Self.Get(), ECoreLogSeverity::Info, TEXT("音频流组件"), TEXT("信息流"), TEXT("EndStream posted"), Data);
    });
    Req->ProcessRequest();
}

void UAudioStreamHttpWsComponent::ConnectWebSocket(const FString& Url)
{
    // Server-authority gating
    if (!ComponentHasServerAuthority(this))
    {
        FCoreLogHelpers::CoreLog(this, ECoreLogSeverity::Trace, TEXT("音频流组件"), TEXT("权限"), TEXT("Skip ConnectWebSocket on client (server-only)"));
        return;
    }

    CloseWebSocket();

    if (!FModuleManager::Get().IsModuleLoaded("WebSockets"))
    {
        FModuleManager::Get().LoadModule("WebSockets");
    }

    FWebSocketsModule& WS = FWebSocketsModule::Get();
    WebSocket = WS.CreateWebSocket(Url);

    if (!WebSocket.IsValid())
    {
        TMap<FString,FString> Data; Data.Add(TEXT("url"), Url);
        FCoreLogHelpers::CoreLog(this, ECoreLogSeverity::Error, TEXT("音频流组件"), TEXT("连接握手"), FString::Printf(TEXT("Failed to create WebSocket for %s"), *Url), Data);
        return;
    }

    FCoreLogHelpers::CoreLog(this, ECoreLogSeverity::Debug, TEXT("音频流组件"), TEXT("连接握手"), FString::Printf(TEXT("WS connect begin -> url=%s"), *Url));

    TWeakObjectPtr<UAudioStreamHttpWsComponent> Self = this;

    WebSocket->OnConnected().AddLambda([Self]()
    {
        if (!Self.IsValid()) return;
        UAudioStreamHttpWsComponent* P = Self.Get();
        // 标记已至少成功连接一次
        P->bHasEverConnected = true;
        // 成功连接时取消后续重试
        P->CancelReconnect();
        TMap<FString,FString> Data; Data.Add(TEXT("host"), P->ActiveHttpHost); Data.Add(TEXT("task"), P->ActiveTaskId);
        FCoreLogHelpers::CoreLog(P, ECoreLogSeverity::Info, TEXT("音频流组件"), TEXT("连接状态"), FString::Printf(TEXT("WS connected -> host=%s task=%s uuid=%s"), *P->ActiveHttpHost, *P->ActiveTaskId, *P->RegisteredUuid), Data);
    });

    WebSocket->OnConnectionError().AddLambda([Self](const FString& Error)
    {
        if (!Self.IsValid()) return;
        UAudioStreamHttpWsComponent* P = Self.Get();
        TMap<FString,FString> Data; Data.Add(TEXT("error"), Error);
        FCoreLogHelpers::CoreLog(P, ECoreLogSeverity::Error, TEXT("音频流组件"), TEXT("连接恢复"), FString::Printf(TEXT("WS connection error: %s"), *Error), Data);
        // 仅在之前已经成功过连接且不是手动关闭的情况下才安排重试
        if (P->bHasEverConnected && !P->bManualClose)
        {
            AsyncTask(ENamedThreads::GameThread, [P]() { if (P) P->ScheduleReconnect(); });
        }
    });

    WebSocket->OnClosed().AddLambda([Self](int32 StatusCode, const FString& Reason, bool bWasClean)
    {
        if (!Self.IsValid()) return;
        UAudioStreamHttpWsComponent* P = Self.Get();
        TMap<FString,FString> Data; Data.Add(TEXT("reason"), Reason); Data.Add(TEXT("code"), FString::FromInt(StatusCode));
        FCoreLogHelpers::CoreLog(P, ECoreLogSeverity::Warn, TEXT("音频流组件"), TEXT("连接恢复"), FString::Printf(TEXT("WS closed: %s (code=%d clean=%d)"), *Reason, StatusCode, bWasClean ? 1 : 0), Data);
        // 只有在曾成功连接并且不是手动关闭时才安排重试
        if (P->bHasEverConnected && !P->bManualClose)
        {
            AsyncTask(ENamedThreads::GameThread, [P]() { if (P) P->ScheduleReconnect(); });
        }
    });

    WebSocket->OnMessage().AddLambda([Self](const FString& Message)
    {
        if (!Self.IsValid()) return;
        UAudioStreamHttpWsComponent* P = Self.Get();
        // 在组件内解析（仅解析；后续转发/播放暂时注释）
        AsyncTask(ENamedThreads::GameThread, [P, Message]()
        {
            if (!P) return;
            P->ProcessWebSocketMessage(Message);
        });
    });

    WebSocket->Connect();
}

void UAudioStreamHttpWsComponent::CloseWebSocket()
{
    // 标记为手动关闭，避免自动重连被触发
    bManualClose = true;
    // 取消任何计划的重连
    CancelReconnect();
    if (WebSocket.IsValid())
    {
        WebSocket->OnConnected().Clear();
        WebSocket->OnConnectionError().Clear();
        WebSocket->OnClosed().Clear();
        WebSocket->OnMessage().Clear();
        WebSocket->Close();
        WebSocket.Reset();
    }
}

// Implement reconnect scheduling and cancellation (definitions required for linkage)
void UAudioStreamHttpWsComponent::ScheduleReconnect()
{
    // If there's an existing timer, clear it (do not reset attempt count)
    if (UWorld* W = GetWorld())
    {
        if (ReconnectTimerHandle.IsValid())
        {
            W->GetTimerManager().ClearTimer(ReconnectTimerHandle);
            ReconnectTimerHandle.Invalidate();
        }
    }

    ReconnectAttempts++;
    const float Delay = FMath::Min(ReconnectMaxDelaySeconds, ReconnectBaseDelaySeconds * FMath::Pow(2.0f, ReconnectAttempts - 1));

    FCoreLogHelpers::CoreLog(this, ECoreLogSeverity::Info, TEXT("音频流组件"), TEXT("连接状态"), FString::Printf(TEXT("Schedule reconnect attempt %d in %.1f sec"), ReconnectAttempts, Delay));

    if (UWorld* W2 = GetWorld())
    {
        TWeakObjectPtr<UAudioStreamHttpWsComponent> WeakThis(this);
        W2->GetTimerManager().SetTimer(ReconnectTimerHandle, [WeakThis]() {
            if (UAudioStreamHttpWsComponent* P = WeakThis.Get())
            {
                // 使用上次的 host 和注册 uuid 发起 /run 并重连（仅服务器侧会执行 RequestRunTask 自身的权限检查）
                P->RequestRunTask(P->ActiveHttpHost, FString(), P->RegisteredUuid, P->ActiveWsSampleRate, P->ActiveWsChannels, P->bActiveUseHttps, P->ActiveHttpRunPath, P->ActiveWsPathPrefix);
            }
        }, Delay, false);
    }
}

void UAudioStreamHttpWsComponent::CancelReconnect()
{
    ReconnectAttempts = 0;
    if (UWorld* W = GetWorld())
    {
        if (ReconnectTimerHandle.IsValid())
        {
            W->GetTimerManager().ClearTimer(ReconnectTimerHandle);
            ReconnectTimerHandle.Invalidate();
        }
    }
}

void UAudioStreamHttpWsComponent::UnregisterFromSubsystem()
{
    if (!bRegistered) return;
    const TCHAR* Role = GetRoleLabel(this);
    UGameInstance* GI = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
    if (!GI) return;
    if (UAudioStreamHttpWsSubsystem* Subsys = GI->GetSubsystem<UAudioStreamHttpWsSubsystem>())
    {
        Subsys->UnregisterComponent(this);
        FCoreLogHelpers::CoreLog(this, ECoreLogSeverity::Info, TEXT("音频流组件"), TEXT("组件注销"), FString::Printf(TEXT("Unregistered from subsystem (%s) uuid=%s"), Role, *RegisteredUuid));
    }
    bRegistered = false;
}
