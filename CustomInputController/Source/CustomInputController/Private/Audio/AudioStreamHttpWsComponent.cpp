#include "Audio/AudioStreamHttpWsComponent.h"
#include "Audio/AudioStreamHttpWsSubsystem.h"
#include "Audio/AudioStreamSettings.h" // ensure settings declared
#include "Engine/GameInstance.h"
#include "Engine/World.h"

// CoreManager logging
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

// TTS生成后的首次WebSocket消息处理
void UAudioStreamHttpWsComponent::ProcessWebSocketMessage(const FString& Message)
{
    TSharedPtr<FJsonObject> RootObj;
    const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Message);
    if (!FJsonSerializer::Deserialize(Reader, RootObj) || !RootObj.IsValid())
    {
        FCoreLogHelpers::CoreLog(this, ECoreLogSeverity::Warn, TEXT("流数据处理"), TEXT("首次接收处理"), TEXT("WS parse failed (component)"));
        return;
    }
    else
    {
        FCoreLogHelpers::CoreLog(this, ECoreLogSeverity::Trace, TEXT("流数据处理"), TEXT("首次接收处理"), TEXT("WS parsed (component)"));
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

    // 获取 type 字段
    FString Type; RootObj->TryGetStringField(TEXT("type"), Type);
    // 尝试从 key 或 role_id 字段获取 UUID,如果都没有则使用 RegisteredUuid
    // FString MsgUuid; RootObj->TryGetStringField(TEXT("key"), MsgUuid); if (MsgUuid.IsEmpty()) RootObj->TryGetStringField(TEXT("role_id"), MsgUuid);
    // const FString Uuid = !MsgUuid.IsEmpty() ? MsgUuid : RegisteredUuid;

    // 暂时只用 RegisteredUuid
    const FString Uuid = RegisteredUuid;
    
    // 准备 UUID (用于转发)
    FGuid Guid;
    FGuid::Parse(RegisteredUuid, Guid);

    if (Type.Equals(TEXT("audio"), ESearchCase::IgnoreCase))
    {
        int32 SR = ActiveWsSampleRate;
        int32 CH = ActiveWsChannels;
        int32 Tmp;
        if (RootObj->TryGetNumberField(TEXT("sample_rate"), Tmp)) SR = Tmp;
        if (RootObj->TryGetNumberField(TEXT("channels"), Tmp)) CH = FMath::Clamp(Tmp, 1, 8);

        FString Base64; RootObj->TryGetStringField(TEXT("data"), Base64);
        // Base64 数据为空
        if (Base64.IsEmpty())
        {
            FCoreLogHelpers::CoreLog(this, ECoreLogSeverity::Warn, TEXT("流数据处理"), TEXT("接收处理"), TEXT("WS audio dropped: empty base64 (component)"));
            return;
        }
        // 解码 Base64 失败
        TArray<uint8> Decoded;
        if (!FBase64::Decode(Base64, Decoded))
        {
            FCoreLogHelpers::CoreLog(this, ECoreLogSeverity::Warn, TEXT("流数据处理"), TEXT("接收处理"), TEXT("WS audio base64 decode failed (component)"));
            return;
        }
        TArray<uint8> Pcm;
        int32 UseSR = SR, UseCH = CH;
        // 先尝试从 WAV 中提取 PCM
        const bool bWav = ExtractPcmFromMaybeWav_Local(Decoded, Pcm, UseSR, UseCH);
        const int32 Bytes = bWav ? Pcm.Num() : Decoded.Num();
        FCoreLogHelpers::CoreLog(this, ECoreLogSeverity::Trace, TEXT("流数据处理"), TEXT("接收处理"), FString::Printf(TEXT("WS audio parsed (component) -> kind=%s uuid=%s bytes=%d sr=%d ch=%d"), bWav?TEXT("WAV"):TEXT("RAW"), *Uuid, Bytes, UseSR, UseCH));

        const TArray<uint8>& FinalPayload = bWav ? Pcm : Decoded;

        // 转发到 Socket
        if (UGameInstance* GI = GetWorld() ? GetWorld()->GetGameInstance() : nullptr)
        {
            if (UAudioStreamHttpWsSubsystem* SS = GI->GetSubsystem<UAudioStreamHttpWsSubsystem>())
            {
                // 发送解码后的 PCM 数据
                SS->SendPacket(AudioStreamPacket::Audio, FinalPayload, Guid);
                SS->UpdateStats(Bytes, UseSR, UseCH);

                // 临时移除，数据应经由子系统处理后再分发
                
                // // 额外转发给自己一份
                // AudioStreamPacket::FHeader Header;
                // Header.Type = AudioStreamPacket::Audio;
                // Header.Flags = 0x01; // HasUuid
                // Header.Uuid = Guid;
                // Header.Seq = ++LocalAudioSeq;
                // ReceiveSocketMessage(Header, FinalPayload);
            }
        }
    }
    else if (Type.Equals(TEXT("text"), ESearchCase::IgnoreCase))
    {
        FString Text; RootObj->TryGetStringField(TEXT("data"), Text);
        FCoreLogHelpers::CoreLog(this, ECoreLogSeverity::Info, TEXT("流数据处理"), TEXT("接收处理"), FString::Printf(TEXT("WS text (component) for uuid=%s: %s"), *Uuid, *Text));
        
        // 转发到 Socket
        if (UGameInstance* GI = GetWorld() ? GetWorld()->GetGameInstance() : nullptr)
        {
            if (UAudioStreamHttpWsSubsystem* SS = GI->GetSubsystem<UAudioStreamHttpWsSubsystem>())
            {
                FTCHARToUTF8 Utf8(*Text);
                TArray<uint8> Payload;
                Payload.Append((const uint8*)Utf8.Get(), Utf8.Length());
                
                SS->SendPacket(AudioStreamPacket::Text, Payload, Guid);

                // 额外转发给自己一份
                // AudioStreamPacket::FHeader Header;
                // Header.Type = AudioStreamPacket::Text;
                // Header.Flags = 0x01; // HasUuid
                // Header.Uuid = Guid;
                // ReceiveSocketMessage(Header, Payload);
            }
        }
    }
    else if (Type.Equals(TEXT("viseme"), ESearchCase::IgnoreCase))
    {
        // 第一步：解析 viseme 数组
        TArray<int32> Vis; 
        const TArray<TSharedPtr<FJsonValue>>* ArrPtr = nullptr;
        if (!RootObj->TryGetArrayField(TEXT("data"), ArrPtr) || !ArrPtr)
        {
            FCoreLogHelpers::CoreLog(this, ECoreLogSeverity::Warn, TEXT("流数据处理"), TEXT("Audio处理"), TEXT("WS viseme dropped: no array"));
            return;
        }
        // 从 JSON 数组中提取整数 viseme 数据
        Vis.Reserve(ArrPtr->Num());
        for (const auto& V : *ArrPtr) { int32 Val=0; if (V->TryGetNumber(Val)) Vis.Add(Val); }
        
        // 第二步：解析 confidence 数组
        TArray<float> Confidence;
        const TArray<TSharedPtr<FJsonValue>>* ConfPtr = nullptr;
        if (RootObj->TryGetArrayField(TEXT("confidence"), ConfPtr) && ConfPtr)
        {
            Confidence.Reserve(ConfPtr->Num());
            for (const auto& C : *ConfPtr)
            {
                double D=0.0;
                if (C->TryGetNumber(D)) Confidence.Add((float)D);
            }
        }
        //控制uuid输出为前8位
        FCoreLogHelpers::CoreLog(this, ECoreLogSeverity::Trace, TEXT("流数据处理"), TEXT("Viseme处理"), FString::Printf(TEXT("WS viseme -> n=%d confN=%d uuid=%s "), Vis.Num(), Confidence.Num(),/*取前八位*/ *Uuid.Left(8)));
        // 转发到 Socket
        if (UGameInstance* GI = GetWorld() ? GetWorld()->GetGameInstance() : nullptr)
        {
            if (UAudioStreamHttpWsSubsystem* SS = GI->GetSubsystem<UAudioStreamHttpWsSubsystem>())
            {
                // TODO:感觉这个viseme的序列化有点问题,后续需要重新设计viseme的标准格式
                // 序列化 Viseme 数据: [NumVis:4][VisData...][NumConf:4][ConfData...]
                TArray<uint8> Payload;
                FMemoryWriter Writer(Payload);
                Writer << Vis;
                Writer << Confidence;
                
                SS->SendPacket(AudioStreamPacket::Viseme, Payload, Guid);
                SS->UpdateVisemeStats(Vis.Num());

                // // 额外转发给自己一份
                // AudioStreamPacket::FHeader Header;
                // Header.Type = AudioStreamPacket::Viseme;
                // Header.Flags = 0x01; // HasUuid
                // Header.Uuid = Guid;
                // ReceiveSocketMessage(Header, Payload);
            }
        }
    }
    else
    {
        FCoreLogHelpers::CoreLog(this, ECoreLogSeverity::Trace, TEXT("流数据处理"), TEXT("接收处理"), FString::Printf(TEXT("WS ignored (component): type=%s"), *Type));
    }
}

UAudioStreamHttpWsComponent::UAudioStreamHttpWsComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    SetIsReplicatedByDefault(true);
}

void UAudioStreamHttpWsComponent::BeginPlay()
{
    Super::BeginPlay();
    
    InitAudioComponents();

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
    bool bOk;
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
    ActiveWsSampleRate = (SampleRate > 0) ? SampleRate : 16000;
    ActiveWsChannels = (Channels > 0) ? Channels : 1;

    // 总是重新初始化音频组件以清除旧缓冲
    // 停止播放
    if (AudioPlayer)
    {
        AudioPlayer->Stop();
        AudioPlayer->SetSound(nullptr);
    }
    
    // 重新创建 SoundStream
    SoundStream = NewObject<USoundWaveProcedural>(this);
    SoundStream->SetSampleRate(ActiveWsSampleRate);
    SoundStream->NumChannels = ActiveWsChannels;
    SoundStream->Duration = 1000000.0f;
    SoundStream->bLooping = false;
    SoundStream->bProcedural = true;

    // 更新 AudioPlayer
    if (AudioPlayer)
    {
        AudioPlayer->SetSound(SoundStream);
    }

    // 保存当前会话的路径，以便重连时复用
    ActiveHttpRunPath = HttpRunPath;
    ActiveWsPathPrefix = WsPathPrefix;

    // 新的会话显式开始：这不是手动关闭，允许后续重连逻辑如有需要
    bManualClose = false;
    // 从新启动时重置尝试计数
    ReconnectAttempts = 0;

    // 重置播放状态
    bIsPlaying = false;
    LastPlayedSeq = 0;
    LocalAudioSeq = 0;
    AudioPacketQueue.Empty();
    TotalAudioFedDuration = 0.0;

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

void UAudioStreamHttpWsComponent::ReceiveSocketMessage(const AudioStreamPacket::FHeader& Header, const TArray<uint8>& Payload)
{
    if (Header.Type == AudioStreamPacket::Audio)
    {
        // type用文本写明，而不是数字！
        auto GetPacketTypeName = [](uint8 InType) -> const TCHAR*
        {
            switch (InType)
            {
            case AudioStreamPacket::Text:    return TEXT("Text");
            case AudioStreamPacket::Audio:   return TEXT("Audio");
            case AudioStreamPacket::Image:   return TEXT("Image");
            case AudioStreamPacket::Control: return TEXT("Control");
            case AudioStreamPacket::Viseme:  return TEXT("Viseme");
            default:                         return TEXT("Unknown");
            }
        };
        FCoreLogHelpers::CoreLog(this, ECoreLogSeverity::Trace, TEXT("流数据处理"), TEXT("Socket接收"), FString::Printf(TEXT("received socket msg type=%s size=%d"), GetPacketTypeName(Header.Type), Payload.Num()));

        // 自动重置逻辑更新：
        // 如果收到 Seq=0 或者极小的 Seq (<=1) 且当前播放进度明显靠后 (>10)，则认为是新流开始
        // 注意：Websocket 接收时现已使用 LocalAudioSeq (从1开始)，所以 Seq=1 也是可能的重置信号
        bool bIsRestart = (Header.Seq == 0) || (Header.Seq == 1 && LastPlayedSeq > 10);

        if (bIsPlaying && !bPendingStreamReset && bIsRestart)
        {
            // 标记重置
            bPendingStreamReset = true;
            
            // 立即清空队列
            AudioPacketQueue.Empty();
            
            FCoreLogHelpers::CoreLog(this, ECoreLogSeverity::Info, TEXT("流数据处理"), TEXT("Socket接收"), 
                FString::Printf(TEXT("Stream reset detected (Seq=%d), scheduled reset. Previous LastSeq=%d"), Header.Seq, LastPlayedSeq));
        }

        // 丢弃过时包 (仅当已开始播放且有历史记录时才检查)
        // 如果正在等待重置，则不丢弃任何包（视为新流的开始）
        if (bIsPlaying && !bPendingStreamReset && Header.Seq <= LastPlayedSeq)
        {
            FCoreLogHelpers::CoreLog(this, ECoreLogSeverity::Warn, TEXT("调试"), TEXT("Discard"),
                FString::Printf(TEXT("Discarding Seq=%d because <= LastPlayedSeq=%d"), Header.Seq, LastPlayedSeq));
            return;
        }

        // 插入排序
        FAudioPacketBuffer NewPacket;
        NewPacket.Seq = Header.Seq;
        NewPacket.Data = Payload;

        bool bInserted = false;
        for (int32 i = 0; i < AudioPacketQueue.Num(); ++i)
        {
            if (AudioPacketQueue[i].Seq == NewPacket.Seq)
            {
                // 重复包，忽略
                FCoreLogHelpers::CoreLog(this, ECoreLogSeverity::Warn, TEXT("调试"), TEXT("Duplicate"), FString::Printf(TEXT("Ignoring Duplicate Seq=%d"), NewPacket.Seq));
                return;
            }
            if (AudioPacketQueue[i].Seq > NewPacket.Seq)
            {
                AudioPacketQueue.Insert(NewPacket, i);
                bInserted = true;
                break;
            }
        }
        if (!bInserted)
        {
            AudioPacketQueue.Add(NewPacket);
        }

        FCoreLogHelpers::CoreLog(this, ECoreLogSeverity::Info, TEXT("调试"), TEXT("QueueStatus"), FString::Printf(TEXT("QueueSize=%d AddedSeq=%d"), AudioPacketQueue.Num(), NewPacket.Seq));
    }
}

void UAudioStreamHttpWsComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    // 处理延迟的流重置
    if (bPendingStreamReset)
    {
        bPendingStreamReset = false;

        bIsPlaying = false;
        LastPlayedSeq = 0;
        TotalAudioFedDuration = 0.0;

        // 安全地重建 SoundStream
        if (AudioPlayer)
        {
            AudioPlayer->Stop();
            AudioPlayer->SetSound(nullptr);
        }

        SoundStream = NewObject<USoundWaveProcedural>(this);
        SoundStream->SetSampleRate(ActiveWsSampleRate);
        SoundStream->NumChannels = ActiveWsChannels;
        SoundStream->Duration = 1000000.0f;
        SoundStream->bLooping = false;
        SoundStream->bProcedural = true;

        if (AudioPlayer)
        {
            AudioPlayer->SetSound(SoundStream);
        }

        FCoreLogHelpers::CoreLog(this, ECoreLogSeverity::Info, TEXT("流数据处理"), TEXT("播放控制"), TEXT("Stream reset executed in Tick"));
    }

    FeedAudio();
}

void UAudioStreamHttpWsComponent::InitAudioComponents()
{
    if (ActiveWsSampleRate <= 0) ActiveWsSampleRate = 16000;
    if (ActiveWsChannels <= 0) ActiveWsChannels = 1;

    if (!SoundStream)
    {
        SoundStream = NewObject<USoundWaveProcedural>(this);
        SoundStream->SetSampleRate(ActiveWsSampleRate);
        SoundStream->NumChannels = ActiveWsChannels;
        SoundStream->Duration = 1000000.0f; // Very long duration
        SoundStream->bLooping = false;
        SoundStream->bProcedural = true;
    }

    if (!AudioPlayer)
    {
        AudioPlayer = NewObject<UAudioComponent>(this);
        AudioPlayer->bAutoActivate = false;
        AudioPlayer->SetSound(SoundStream);
        AudioPlayer->RegisterComponent();
    }
    else if (AudioPlayer->GetSound() != SoundStream)
    {
        AudioPlayer->SetSound(SoundStream);
    }
}

void UAudioStreamHttpWsComponent::FeedAudio()
{
    if (!SoundStream || !AudioPlayer) return;

    // 1. Check if we need to start playing
    if (!bIsPlaying)
    {
        if (AudioPacketQueue.Num() == 0) return;

        // Calculate total duration in queue
        int32 TotalBytes = 0;
        for (const auto& Pkt : AudioPacketQueue)
        {
            TotalBytes += Pkt.Data.Num();
        }

        const int32 BytesPerSample = sizeof(int16); // Assuming 16-bit
        const int32 NumSamples = TotalBytes / (ActiveWsChannels * BytesPerSample);
        const float Duration = (float)NumSamples / (float)ActiveWsSampleRate;

        // Condition: Packet count threshold OR Duration threshold
        // If packets are large, Duration threshold will trigger first.
        // If packets are small, Packet count threshold might trigger first.
        // We use a small minimum duration to avoid starting with just a tiny blip.
        const float MinStartDuration = 0.1f;

        bool bCanStart = (AudioPacketQueue.Num() >= JitterBufferThreshold) || (Duration >= TargetBufferedTime) || (Duration >= MinStartDuration && AudioPacketQueue.Num() >= 1);

        // Debug info for buffering (only periodically or on change to avoid spam, but for deep debug print every few frames or use Info)
        // Here we just print if it fails to start but has queue
        if (!bCanStart)
        {
            static double LastLogTime = 0;
            double Now = FPlatformTime::Seconds();
            if (Now - LastLogTime > 1.0)
            {
                LastLogTime = Now;
                FCoreLogHelpers::CoreLog(this, ECoreLogSeverity::Info, TEXT("调试"), TEXT("Buffering"),
                    FString::Printf(TEXT("Buffering... Queue=%d Duration=%.3f (Req: Thresh=%d or Time=%.2f)"),
                    AudioPacketQueue.Num(), Duration, JitterBufferThreshold, TargetBufferedTime));
            }
        }

        if (bCanStart)
        {
            bIsPlaying = true;
            PlaybackStartTime = FPlatformTime::Seconds();
            TotalAudioFedDuration = 0.0;

            if (!AudioPlayer->IsPlaying())
            {
                AudioPlayer->Play();
            }
            FCoreLogHelpers::CoreLog(this, ECoreLogSeverity::Info, TEXT("音频流组件"), TEXT("播放控制"), FString::Printf(TEXT("Start playing: buffered %d pkts, %.2fs"), AudioPacketQueue.Num(), Duration));
        }
        else
        {
            // Buffering...
            return;
        }
    }

    // 2. Feed data
    if (bIsPlaying && AudioPacketQueue.Num() > 0)
    {
        // Feed all available packets
        int32 FedBytes = 0;
        int32 FedPackets = 0;
        int32 LastFeedSeq = -1;

        while (AudioPacketQueue.Num() > 0)
        {
            FAudioPacketBuffer& Pkt = AudioPacketQueue[0];

            if (Pkt.Data.Num() > 0)
            {
                SoundStream->QueueAudio(Pkt.Data.GetData(), Pkt.Data.Num());
                FedBytes += Pkt.Data.Num();
                FedPackets++;
                LastPlayedSeq = Pkt.Seq;
                LastFeedSeq = Pkt.Seq;
            }

            AudioPacketQueue.RemoveAt(0);
        }

        if (FedBytes > 0)
        {
             const int32 BytesPerSample = sizeof(int16);
             const int32 NumSamples = FedBytes / (ActiveWsChannels * BytesPerSample);
             const float Duration = (float)NumSamples / (float)ActiveWsSampleRate;
             TotalAudioFedDuration += Duration;

             FCoreLogHelpers::CoreLog(this, ECoreLogSeverity::Info, TEXT("调试"), TEXT("FedAudio"),
                FString::Printf(TEXT("Fed %d bytes (%d pkts) to SoundStream. LastSeq=%d AddedDuration=%.3f"), FedBytes, FedPackets, LastFeedSeq, Duration));
        }
    }
}



