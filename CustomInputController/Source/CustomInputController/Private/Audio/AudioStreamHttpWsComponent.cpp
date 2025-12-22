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

UAudioStreamHttpWsComponent::UAudioStreamHttpWsComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
}

void UAudioStreamHttpWsComponent::BeginPlay()
{
    Super::BeginPlay();
    RegisterToSubsystem();

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
    // 尝试优雅结束
    PostEndStream();
    CloseWebSocket();
    UnregisterFromSubsystem();
    Super::EndPlay(EndPlayReason);
}

void UAudioStreamHttpWsComponent::RegisterToSubsystem()
{
    if (bRegistered) return;
    UGameInstance* GI = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
    if (!GI) return;
    UAudioStreamHttpWsSubsystem* Subsys = GI->GetSubsystem<UAudioStreamHttpWsSubsystem>();
    if (!Subsys) return;

    FString OutUuid;
    if (Subsys->RegisterComponent(this, OutUuid))
    {
        RegisteredUuid = OutUuid;
        bRegistered = true;
        const FString Msg = FString::Printf(TEXT("Component registered uuid=%s"), *RegisteredUuid);
        UE_LOG(LogTemp, Log, TEXT("[AudioStream][Registry] %s"), *Msg);
        FCoreLogHelpers::CoreLog(this, ECoreLogSeverity::Info, TEXT("流组件注册"), TEXT("组件注册"), Msg);
    }
    else
    {
        const FString Msg = FString(TEXT("Component registration failed"));
        UE_LOG(LogTemp, Warning, TEXT("[AudioStream][Registry] %s"), *Msg);
        FCoreLogHelpers::CoreLog(this, ECoreLogSeverity::Warn, TEXT("流组件注册"), TEXT("组件注册"), Msg);
    }
}

void UAudioStreamHttpWsComponent::UnregisterFromSubsystem()
{
    if (!bRegistered) return;
    UGameInstance* GI = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
    if (!GI) return;
    UAudioStreamHttpWsSubsystem* Subsys = GI->GetSubsystem<UAudioStreamHttpWsSubsystem>();
    if (!Subsys) return;

    Subsys->UnregisterComponent(this);
    const FString Msg = FString::Printf(TEXT("Component unregistered uuid=%s"), *RegisteredUuid);
    UE_LOG(LogTemp, Log, TEXT("[AudioStream][Registry] %s"), *Msg);
    FCoreLogHelpers::CoreLog(this, ECoreLogSeverity::Info, TEXT("流组件注册"), TEXT("组件注销"), Msg);

    bRegistered = false;
    RegisteredUuid.Reset();
}

// ===== 网络逻辑：StartRunAndConnect / POST / WS =====

void UAudioStreamHttpWsComponent::StartRunAndConnect(const FString& ServerHostWithPort, const FString& CallbackUrl, const FString& TargetUuid, int32 SampleRate, int32 Channels, bool bUseHttps, const FString& HttpRunPath, const FString& WsPathPrefix)
{
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
    const FString Scheme = bUseHttps ? TEXT("https") : TEXT("http");
    const FString RunUrl = FString::Printf(TEXT("%s://%s%s"), *Scheme, *ServerHostWithPort, *HttpRunPath);

    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Req = FHttpModule::Get().CreateRequest();
    Req->SetURL(RunUrl);
    Req->SetVerb(TEXT("POST"));
    Req->SetHeader(TEXT("Content-Type"), TEXT("application/json"));

    TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
    if (!CallbackUrl.IsEmpty()) Body->SetStringField(TEXT("callback"), CallbackUrl);
    const FString UseKey = !TargetUuid.IsEmpty() ? TargetUuid : RegisteredUuid;
    Body->SetStringField(TEXT("key"), UseKey);
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
            FCoreLogHelpers::CoreLog(P, ECoreLogSeverity::Error, TEXT("音频流组件"), TEXT("连接握手"), TEXT("/run request failed"), Data);
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
            // FCoreLogHelpers::CoreLog(P, ECoreLogSeverity::Debug, FString::Printf(TEXT("Connecting WS -> %s"), *WsUrl));
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
        // 将解析委托给子系统的 ProcessWebSocketMessage（在 GameThread 调用以确保线程安全）
        AsyncTask(ENamedThreads::GameThread, [P, Message]() {
            if (!P) return;
            if (UGameInstance* GI = P->GetWorld()?P->GetWorld()->GetGameInstance():nullptr)
            {
                if (UAudioStreamHttpWsSubsystem* SS = GI->GetSubsystem<UAudioStreamHttpWsSubsystem>())
                {
                    SS->ProcessWebSocketMessage(Message, P->RegisteredUuid, P->ActiveWsSampleRate, P->ActiveWsChannels);
                }
            }
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
                // 使用上次的 host 和注册 uuid 发起 /run 并重连
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
