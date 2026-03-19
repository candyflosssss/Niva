#include "Audio/AudioStreamHttpWsComponent.h"
#include "Audio/AudioStreamHttpWsSubsystem.h"
#include "Audio/AudioStreamSettings.h" // ensure settings declared
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"

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
#include "Serialization/MemoryReader.h" // Added for Viseme payload parsing

#if defined(CUSTOMINPUT_USE_OPUS)
#include "opus.h"
#endif

// Helper: sanitize text for logs while keeping control characters visible instead of silently converting them.
static FString SanitizeForLogPreview(const FString& In, int32 MaxLen = 1024)
{
    if (In.IsEmpty()) return FString();
    FString Out = In;
    Out.ReplaceInline(TEXT("\\"), TEXT("\\\\"));
    Out.ReplaceInline(TEXT("\r"), TEXT("\\r"));
    Out.ReplaceInline(TEXT("\n"), TEXT("\\n"));
    Out.ReplaceInline(TEXT("\t"), TEXT("\\t"));
    if (MaxLen > 0 && Out.Len() > MaxLen) { Out = Out.Left(MaxLen); }
    return Out;
}

static bool IsWhitespaceToNormalize(TCHAR Ch)
{
    return Ch == TEXT(' ') || Ch == TEXT('\t') || Ch == TEXT('\r') || Ch == TEXT('\n');
}

static bool IsCjkIdeograph(TCHAR Ch)
{
    return (Ch >= 0x3400 && Ch <= 0x4DBF)
        || (Ch >= 0x4E00 && Ch <= 0x9FFF)
        || (Ch >= 0xF900 && Ch <= 0xFAFF);
}

static bool IsCjkPunctuation(TCHAR Ch)
{
    return (Ch >= 0x3000 && Ch <= 0x303F)
        || (Ch >= 0xFF00 && Ch <= 0xFFEF);
}

static bool IsCjkContextChar(TCHAR Ch)
{
    return IsCjkIdeograph(Ch) || IsCjkPunctuation(Ch);
}

static FString NormalizeStreamTextForTts(const FString& In)
{
    if (In.IsEmpty())
    {
        return FString();
    }

    FString Out;
    Out.Reserve(In.Len());

    for (int32 Index = 0; Index < In.Len(); ++Index)
    {
        const TCHAR Ch = In[Index];
        if (!IsWhitespaceToNormalize(Ch))
        {
            Out.AppendChar(Ch);
            continue;
        }

        int32 NextIndex = Index + 1;
        while (NextIndex < In.Len() && IsWhitespaceToNormalize(In[NextIndex]))
        {
            ++NextIndex;
        }

        if (Out.IsEmpty() || NextIndex >= In.Len())
        {
            Index = NextIndex - 1;
            continue;
        }

        const TCHAR PrevCh = Out[Out.Len() - 1];
        const TCHAR NextCh = In[NextIndex];
        const bool bTouchesCjk = IsCjkContextChar(PrevCh) || IsCjkContextChar(NextCh);
        if (!bTouchesCjk && PrevCh != TEXT(' '))
        {
            Out.AppendChar(TEXT(' '));
        }

        Index = NextIndex - 1;
    }

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

static FString NormalizeWsPath(const FString& InPath, bool bAppendTrailingSlash)
{
    FString Path = InPath.IsEmpty() ? TEXT("/ws") : InPath;
    if (!Path.StartsWith(TEXT("/")))
    {
        Path = TEXT("/") + Path;
    }

    while (Path.Len() > 1 && Path.EndsWith(TEXT("/")) && !bAppendTrailingSlash)
    {
        Path.LeftChopInline(1, EAllowShrinking::No);
    }

    if (bAppendTrailingSlash && !Path.EndsWith(TEXT("/")))
    {
        Path += TEXT("/");
    }

    return Path;
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
FString UAudioStreamHttpWsComponent::BuildActiveWebSocketUrl() const
{
    const FString WsScheme = bActiveUseHttps ? TEXT("wss") : TEXT("ws");
    if (ProtocolMode == EAudioStreamProtocolMode::PureWebSocket)
    {
        const FString Path = NormalizeWsPath(ActiveWsPathPrefix, false);
        return FString::Printf(TEXT("%s://%s%s"), *WsScheme, *ActiveHttpHost, *Path);
    }

    const FString Path = NormalizeWsPath(ActiveWsPathPrefix, true);
    return FString::Printf(TEXT("%s://%s%s%s"), *WsScheme, *ActiveHttpHost, *Path, *ActiveTaskId);
}

bool UAudioStreamHttpWsComponent::SendPureWsMessage(const FString& Action, const TSharedPtr<FJsonObject>& Payload, const FString& TaskIdOverride)
{
    if (!WebSocket.IsValid() || !WebSocket->IsConnected())
    {
        return false;
    }

    const FString TaskId = TaskIdOverride.IsEmpty() ? ActiveTaskId : TaskIdOverride;
    if (TaskId.IsEmpty())
    {
        return false;
    }

    TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
    TSharedRef<FJsonObject> Header = MakeShared<FJsonObject>();
    Header->SetStringField(TEXT("action"), Action);
    Header->SetStringField(TEXT("task_id"), TaskId);
    Root->SetObjectField(TEXT("header"), Header);
    Root->SetObjectField(TEXT("payload"), Payload.IsValid() ? Payload.ToSharedRef() : MakeShared<FJsonObject>());

    FString Message;
    const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Message);
    if (!FJsonSerializer::Serialize(Root, Writer))
    {
        return false;
    }

    WebSocket->Send(Message);
    return true;
}

void UAudioStreamHttpWsComponent::BeginPureWsTask(bool bGenerateNewTaskId)
{
    if (ProtocolMode != EAudioStreamProtocolMode::PureWebSocket)
    {
        return;
    }

    if (!WebSocket.IsValid() || !WebSocket->IsConnected())
    {
        return;
    }

    if (bGenerateNewTaskId || ActiveTaskId.IsEmpty())
    {
        ActiveTaskId = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens);
    }

    TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
    Payload->SetObjectField(TEXT("parameters"), MakeShared<FJsonObject>());

    bPureWsTaskStarted = false;
    bPureWsAwaitingTaskStart = SendPureWsMessage(TEXT("run-task"), Payload, ActiveTaskId);
}

void UAudioStreamHttpWsComponent::HandleIncomingAudioChunk(const FString& Base64Audio, int32 SampleRate, int32 Channels)
{
    if (Base64Audio.IsEmpty())
    {
        return;
    }

    LogIncomingAudioReturn(Base64Audio.Len());

    TArray<uint8> Decoded;
    if (!FBase64::Decode(Base64Audio, Decoded))
    {
        FCoreLogHelpers::CoreLog(this, ECoreLogSeverity::Warn, TEXT("流数据处理"), TEXT("接收处理"), TEXT("WS audio base64 decode failed (component)"));
        return;
    }

    TArray<uint8> Pcm;
    int32 UseSR = SampleRate;
    int32 UseCH = Channels;
    const bool bWav = ExtractPcmFromMaybeWav_Local(Decoded, Pcm, UseSR, UseCH);
    const TArray<uint8>& FinalPayload = bWav ? Pcm : Decoded;

    FGuid Guid;
    FGuid::Parse(RegisteredUuid, Guid);

    if (UGameInstance* GI = GetWorld() ? GetWorld()->GetGameInstance() : nullptr)
    {
        if (UAudioStreamHttpWsSubsystem* SS = GI->GetSubsystem<UAudioStreamHttpWsSubsystem>())
        {
            SS->SendPacket(AudioStreamPacket::Audio, FinalPayload, Guid);
            SS->UpdateStats(bWav ? Pcm.Num() : Decoded.Num(), UseSR, UseCH);

            AudioStreamPacket::FHeader Header;
            Header.Type = AudioStreamPacket::Audio;
            Header.Flags = 0x01 | LocalDecodedPcmFlag;
            Header.Uuid = Guid;
            Header.Seq = ++LocalAudioSeq;
            ReceiveSocketMessage(Header, FinalPayload);
        }
    }
}

void UAudioStreamHttpWsComponent::HandleIncomingVisemeChunk(const TArray<int32>& Visemes, const TArray<float>& Confidence)
{
    FGuid Guid;
    FGuid::Parse(RegisteredUuid, Guid);

    if (UGameInstance* GI = GetWorld() ? GetWorld()->GetGameInstance() : nullptr)
    {
        if (UAudioStreamHttpWsSubsystem* SS = GI->GetSubsystem<UAudioStreamHttpWsSubsystem>())
        {
            TArray<uint8> Payload;
            FMemoryWriter Writer(Payload);
            TArray<int32> VisCopy = Visemes;
            TArray<float> ConfCopy = Confidence;
            Writer << VisCopy;
            Writer << ConfCopy;

            SS->SendPacket(AudioStreamPacket::Viseme, Payload, Guid);
            SS->UpdateVisemeStats(Visemes.Num());

            AudioStreamPacket::FHeader Header;
            Header.Type = AudioStreamPacket::Viseme;
            Header.Flags = 0x01;
            Header.Uuid = Guid;
            ReceiveSocketMessage(Header, Payload);
        }
    }
}

void UAudioStreamHttpWsComponent::HandleIncomingTextChunk(const FString& Text)
{
    FGuid Guid;
    FGuid::Parse(RegisteredUuid, Guid);

    if (UGameInstance* GI = GetWorld() ? GetWorld()->GetGameInstance() : nullptr)
    {
        if (UAudioStreamHttpWsSubsystem* SS = GI->GetSubsystem<UAudioStreamHttpWsSubsystem>())
        {
            FTCHARToUTF8 Utf8(*Text);
            TArray<uint8> Payload;
            Payload.Append((const uint8*)Utf8.Get(), Utf8.Length());

            SS->SendPacket(AudioStreamPacket::Text, Payload, Guid);

            AudioStreamPacket::FHeader Header;
            Header.Type = AudioStreamPacket::Text;
            Header.Flags = 0x01;
            Header.Uuid = Guid;
            ReceiveSocketMessage(Header, Payload);
        }
    }
}

void UAudioStreamHttpWsComponent::ApplySettingsDefaultsFromProject()
{
    if (HasAnyFlags(RF_ClassDefaultObject))
    {
        return;
    }

    const UAudioStreamSettings* Settings = UAudioStreamSettings::Get();
    const UAudioStreamHttpWsComponent* NativeDefaults = UAudioStreamHttpWsComponent::StaticClass()->GetDefaultObject<UAudioStreamHttpWsComponent>();
    if (!Settings || !NativeDefaults)
    {
        return;
    }

    if (ProtocolMode == NativeDefaults->ProtocolMode)
    {
        ProtocolMode = Settings->DefaultProtocolMode;
    }

    if (JitterBufferThreshold == NativeDefaults->JitterBufferThreshold)
    {
        JitterBufferThreshold = Settings->DefaultJitterBufferThreshold;
    }

    if (FMath::IsNearlyEqual(TargetBufferedTime, NativeDefaults->TargetBufferedTime))
    {
        TargetBufferedTime = Settings->DefaultTargetBufferedTime;
    }

    if (FMath::IsNearlyEqual(MinStartDuration, NativeDefaults->MinStartDuration))
    {
        MinStartDuration = Settings->DefaultMinStartDuration;
    }

    if (FMath::IsNearlyEqual(MinStreamRequestIntervalSeconds, NativeDefaults->MinStreamRequestIntervalSeconds))
    {
        MinStreamRequestIntervalSeconds = Settings->DefaultMinStreamRequestIntervalSeconds;
    }

    if (FMath::IsNearlyEqual(StreamTextCoalesceWindowSeconds, NativeDefaults->StreamTextCoalesceWindowSeconds))
    {
        StreamTextCoalesceWindowSeconds = Settings->DefaultStreamTextCoalesceWindowSeconds;
    }

    if (MaxPendingStreamTextItems == NativeDefaults->MaxPendingStreamTextItems)
    {
        MaxPendingStreamTextItems = Settings->DefaultMaxPendingStreamTextItems;
    }

    if (FMath::IsNearlyEqual(StreamTextFlushIntervalSeconds, NativeDefaults->StreamTextFlushIntervalSeconds))
    {
        StreamTextFlushIntervalSeconds = Settings->DefaultStreamTextFlushIntervalSeconds;
    }

    if (StreamTextMaxBatchChars == NativeDefaults->StreamTextMaxBatchChars)
    {
        StreamTextMaxBatchChars = Settings->DefaultStreamTextMaxBatchChars;
    }

    if (FMath::IsNearlyEqual(StreamFailureCooldownBaseSeconds, NativeDefaults->StreamFailureCooldownBaseSeconds))
    {
        StreamFailureCooldownBaseSeconds = Settings->DefaultStreamFailureCooldownBaseSeconds;
    }

    if (FMath::IsNearlyEqual(StreamFailureCooldownMaxSeconds, NativeDefaults->StreamFailureCooldownMaxSeconds))
    {
        StreamFailureCooldownMaxSeconds = Settings->DefaultStreamFailureCooldownMaxSeconds;
    }

    if (VisemeStepMs == NativeDefaults->VisemeStepMs)
    {
        VisemeStepMs = Settings->DefaultVisemeStepMs;
    }

    if (VisemeKeyframeIntervalMs == NativeDefaults->VisemeKeyframeIntervalMs)
    {
        VisemeKeyframeIntervalMs = Settings->DefaultVisemeKeyframeIntervalMs;
    }
}

void UAudioStreamHttpWsComponent::EnqueuePendingWebSocketMessage(const FString& Message)
{
    FScopeLock Lock(&PendingWebSocketMessagesCS);
    PendingWebSocketMessages.Add(Message);
}

void UAudioStreamHttpWsComponent::ProcessPendingWebSocketMessages(int32 MaxMessagesToProcess)
{
    if (MaxMessagesToProcess <= 0)
    {
        return;
    }

    TArray<FString> MessagesToProcess;
    {
        FScopeLock Lock(&PendingWebSocketMessagesCS);
        const int32 NumToProcess = FMath::Min(MaxMessagesToProcess, PendingWebSocketMessages.Num());
        if (NumToProcess <= 0)
        {
            return;
        }

        MessagesToProcess.Reserve(NumToProcess);
        for (int32 Index = 0; Index < NumToProcess; ++Index)
        {
            MessagesToProcess.Add(MoveTemp(PendingWebSocketMessages[Index]));
        }
        PendingWebSocketMessages.RemoveAt(0, NumToProcess, EAllowShrinking::No);
    }

    for (const FString& PendingMessage : MessagesToProcess)
    {
        ProcessWebSocketMessage(PendingMessage);
    }
}

void UAudioStreamHttpWsComponent::ResetPendingWebSocketMessages()
{
    FScopeLock Lock(&PendingWebSocketMessagesCS);
    PendingWebSocketMessages.Reset();
}

void UAudioStreamHttpWsComponent::ResetAudioPacketQueue()
{
    AudioPacketQueue.Reset();
    QueuedAudioBytes = 0;
}

float UAudioStreamHttpWsComponent::GetBufferedAudioDurationSeconds() const
{
    if (ActiveWsSampleRate <= 0 || ActiveWsChannels <= 0)
    {
        return 0.0f;
    }

    const int32 BytesPerSecond = ActiveWsSampleRate * ActiveWsChannels * static_cast<int32>(sizeof(int16));
    if (BytesPerSecond <= 0)
    {
        return 0.0f;
    }

    return static_cast<float>(QueuedAudioBytes) / static_cast<float>(BytesPerSecond);
}

void UAudioStreamHttpWsComponent::LogStreamPushDispatch(const FString& ProtocolLabel, const FString& Text)
{
    LastStreamPushRequestTimeSeconds = FPlatformTime::Seconds();
    ++StreamPushRequestSequence;

    TMap<FString, FString> Data;
    Data.Add(TEXT("protocol"), ProtocolLabel);
    Data.Add(TEXT("request_seq"), FString::FromInt(static_cast<int32>(StreamPushRequestSequence)));
    Data.Add(TEXT("task_id"), ActiveTaskId);
    Data.Add(TEXT("text_len"), FString::FromInt(Text.Len()));
    Data.Add(TEXT("text_preview"), SanitizeForLogPreview(Text, 64));
    FCoreLogHelpers::CoreLog(this, ECoreLogSeverity::Fatal, TEXT("音频流组件"), TEXT("流时序"), TEXT("发送了流音频请求"), Data);
}

void UAudioStreamHttpWsComponent::LogIncomingAudioReturn(int32 Base64AudioLen)
{
    ++StreamAudioReturnSequence;

    const double NowSeconds = FPlatformTime::Seconds();
    const double DeltaMs = LastStreamPushRequestTimeSeconds > 0.0
        ? (NowSeconds - LastStreamPushRequestTimeSeconds) * 1000.0
        : -1.0;

    TMap<FString, FString> Data;
    Data.Add(TEXT("task_id"), ActiveTaskId);
    Data.Add(TEXT("request_seq"), FString::FromInt(static_cast<int32>(StreamPushRequestSequence)));
    Data.Add(TEXT("audio_return_seq"), FString::FromInt(static_cast<int32>(StreamAudioReturnSequence)));
    Data.Add(TEXT("base64_len"), FString::FromInt(Base64AudioLen));
    Data.Add(TEXT("since_last_request_ms"), DeltaMs >= 0.0 ? FString::Printf(TEXT("%.2f"), DeltaMs) : TEXT("N/A"));
    FCoreLogHelpers::CoreLog(this, ECoreLogSeverity::Fatal, TEXT("音频流组件"), TEXT("流时序"), TEXT("接收到返回的流音频数据"), Data);
}

void UAudioStreamHttpWsComponent::ProcessWebSocketMessage(const FString& Message)
{
    TSharedPtr<FJsonObject> RootObj;
    const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Message);
    if (!FJsonSerializer::Deserialize(Reader, RootObj) || !RootObj.IsValid())
    {
        FCoreLogHelpers::CoreLog(this, ECoreLogSeverity::Warn, TEXT("流数据处理"), TEXT("首次接收处理"), TEXT("WS parse failed (component)"));
        return;
    }

    const TSharedPtr<FJsonObject>* HeaderObj = nullptr;
    const TSharedPtr<FJsonObject>* PayloadObj = nullptr;
    if (RootObj->TryGetObjectField(TEXT("header"), HeaderObj) && HeaderObj && HeaderObj->IsValid())
    {
        const TSharedPtr<FJsonObject> Header = *HeaderObj;
        const TSharedPtr<FJsonObject> Payload = (RootObj->TryGetObjectField(TEXT("payload"), PayloadObj) && PayloadObj && PayloadObj->IsValid()) ? *PayloadObj : MakeShared<FJsonObject>();

        FString Event;
        FString TaskId;
        Header->TryGetStringField(TEXT("event"), Event);
        Header->TryGetStringField(TEXT("task_id"), TaskId);

        const bool bHasTaskId = !TaskId.IsEmpty();
        const bool bHasActiveTaskId = !ActiveTaskId.IsEmpty();
        const bool bTaskMatchesActive = !bHasTaskId || !bHasActiveTaskId || TaskId == ActiveTaskId;

        auto ParseIntArray = [](const TSharedPtr<FJsonObject>& Obj, const FString& FieldName, TArray<int32>& Out)
        {
            const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
            if (!Obj.IsValid() || !Obj->TryGetArrayField(FieldName, Arr) || !Arr)
            {
                return;
            }

            Out.Reserve(Arr->Num());
            for (const TSharedPtr<FJsonValue>& Value : *Arr)
            {
                int32 Parsed = 0;
                if (Value.IsValid() && Value->TryGetNumber(Parsed))
                {
                    Out.Add(Parsed);
                }
            }
        };

        auto ParseFloatArray = [](const TSharedPtr<FJsonObject>& Obj, const FString& FieldName, TArray<float>& Out)
        {
            const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
            if (!Obj.IsValid() || !Obj->TryGetArrayField(FieldName, Arr) || !Arr)
            {
                return;
            }

            Out.Reserve(Arr->Num());
            for (const TSharedPtr<FJsonValue>& Value : *Arr)
            {
                double Parsed = 0.0;
                if (Value.IsValid() && Value->TryGetNumber(Parsed))
                {
                    Out.Add((float)Parsed);
                }
            }
        };

        if (Event.Equals(TEXT("task-started"), ESearchCase::IgnoreCase))
        {
            if (!bTaskMatchesActive)
            {
                return;
            }
            if (!TaskId.IsEmpty())
            {
                ActiveTaskId = TaskId;
            }
            bPureWsAwaitingTaskStart = false;
            bPureWsTaskStarted = true;
            ScheduleProcessNextStreamQueueItem();
            return;
        }

        if (Event.Equals(TEXT("audio-viseme-data"), ESearchCase::IgnoreCase))
        {
            FString AudioBase64;
            Payload->TryGetStringField(TEXT("audio"), AudioBase64);
            if (!AudioBase64.IsEmpty())
            {
                HandleIncomingAudioChunk(AudioBase64, ActiveWsSampleRate, ActiveWsChannels);
            }

            TArray<int32> Visemes;
            TArray<float> Confidence;
            ParseIntArray(Payload, TEXT("visemes"), Visemes);
            ParseFloatArray(Payload, TEXT("confidence"), Confidence);
            if (Visemes.Num() > 0 || Confidence.Num() > 0)
            {
                HandleIncomingVisemeChunk(Visemes, Confidence);
            }
            return;
        }

        if (Event.Equals(TEXT("task-finished"), ESearchCase::IgnoreCase))
        {
            if (!bTaskMatchesActive)
            {
                return;
            }
            bPureWsTaskStarted = false;
            bPureWsAwaitingTaskStart = false;
            if (ProtocolMode == EAudioStreamProtocolMode::PureWebSocket && WebSocket.IsValid() && WebSocket->IsConnected() && !bManualClose)
            {
                BeginPureWsTask(true);
            }
            return;
        }

        if (Event.Equals(TEXT("task-failed"), ESearchCase::IgnoreCase))
        {
            if (!bTaskMatchesActive)
            {
                return;
            }
            FString ErrorCode;
            FString ErrorMessage;
            Header->TryGetStringField(TEXT("error_code"), ErrorCode);
            Header->TryGetStringField(TEXT("error_message"), ErrorMessage);
            bPureWsTaskStarted = false;
            bPureWsAwaitingTaskStart = false;

            TMap<FString, FString> Data;
            Data.Add(TEXT("task_id"), TaskId);
            Data.Add(TEXT("error_code"), ErrorCode);
            Data.Add(TEXT("error_message"), ErrorMessage);
            FCoreLogHelpers::CoreLog(this, ECoreLogSeverity::Warn, TEXT("流数据处理"), TEXT("流程更新"), TEXT("Pure WS task failed"), Data);

            ScheduleProcessNextStreamQueueItem(0.1f);
            return;
        }
    }

    FString StatusStr;
    if (RootObj->TryGetStringField(TEXT("status"), StatusStr) && StatusStr.Equals(TEXT("completed"), ESearchCase::IgnoreCase))
    {
        FCoreLogHelpers::CoreLog(this, ECoreLogSeverity::Info, TEXT("流数据处理"), TEXT("流程更新"), TEXT("WS status=completed (component)"));
        CloseWebSocket(true);

        if (!ActiveHttpHost.IsEmpty() && !ActiveHttpRunPath.IsEmpty() && !ActiveWsPathPrefix.IsEmpty())
        {
            StartRunAndConnect(ActiveHttpHost,
                               FString(),
                               RegisteredUuid,
                               ActiveWsSampleRate,
                               ActiveWsChannels,
                               bActiveUseHttps,
                               ActiveHttpRunPath,
                               ActiveWsPathPrefix,
                               true);
        }
        return;
    }

    FString Type;
    RootObj->TryGetStringField(TEXT("type"), Type);
    if (Type.Equals(TEXT("audio"), ESearchCase::IgnoreCase))
    {
        int32 SR = ActiveWsSampleRate;
        int32 CH = ActiveWsChannels;
        int32 Tmp = 0;
        if (RootObj->TryGetNumberField(TEXT("sample_rate"), Tmp)) SR = Tmp;
        if (RootObj->TryGetNumberField(TEXT("channels"), Tmp)) CH = FMath::Clamp(Tmp, 1, 8);

        FString Base64;
        RootObj->TryGetStringField(TEXT("data"), Base64);
        HandleIncomingAudioChunk(Base64, SR, CH);
    }
    else if (Type.Equals(TEXT("text"), ESearchCase::IgnoreCase))
    {
        FString Text;
        RootObj->TryGetStringField(TEXT("data"), Text);
        HandleIncomingTextChunk(Text);
    }
    else if (Type.Equals(TEXT("viseme"), ESearchCase::IgnoreCase))
    {
        TArray<int32> Vis;
        TArray<float> Confidence;
        const TArray<TSharedPtr<FJsonValue>>* ArrPtr = nullptr;
        if (!RootObj->TryGetArrayField(TEXT("data"), ArrPtr) || !ArrPtr)
        {
            FCoreLogHelpers::CoreLog(this, ECoreLogSeverity::Warn, TEXT("流数据处理"), TEXT("Audio处理"), TEXT("WS viseme dropped: no array"));
            return;
        }

        Vis.Reserve(ArrPtr->Num());
        for (const auto& V : *ArrPtr)
        {
            int32 Val = 0;
            if (V.IsValid() && V->TryGetNumber(Val))
            {
                Vis.Add(Val);
            }
        }

        const TArray<TSharedPtr<FJsonValue>>* ConfPtr = nullptr;
        if (RootObj->TryGetArrayField(TEXT("confidence"), ConfPtr) && ConfPtr)
        {
            Confidence.Reserve(ConfPtr->Num());
            for (const auto& C : *ConfPtr)
            {
                double D = 0.0;
                if (C.IsValid() && C->TryGetNumber(D))
                {
                    Confidence.Add((float)D);
                }
            }
        }

        HandleIncomingVisemeChunk(Vis, Confidence);
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
    // 软重连时保留队列的标志位初始化
    bPreserveQueuesNextClose = false;
}

void UAudioStreamHttpWsComponent::BeginPlay()
{
    Super::BeginPlay();

    ApplySettingsDefaultsFromProject();
    
    EnsureAndZeroVisemeArray();

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

    // 按协议模式选择对应的服务配置
    const UAudioStreamSettings* S = GetDefault<UAudioStreamSettings>();
    const EAudioStreamProtocolMode EffectiveProtocolMode = ProtocolMode;
    FString Host = S ? S->GetEffectiveWsHost(EffectiveProtocolMode) : TEXT("127.0.0.1:8001");
    const FString WsScheme = S ? S->GetEffectiveWsScheme(EffectiveProtocolMode) : TEXT("ws");
    const bool bHttps = WsScheme.Equals(TEXT("wss"), ESearchCase::IgnoreCase);
    const int32 SR = S ? S->DefaultSampleRate : ActiveWsSampleRate;
    const int32 CH = S ? S->DefaultChannels : ActiveWsChannels;
    const FString RunPath = S ? S->DefaultHttpRunPath : TEXT("/run");
    const FString WsPrefix = S ? S->GetEffectiveWsPath(EffectiveProtocolMode) : TEXT("/ws/");

    // 自动在 BeginPlay 尝试发起 /run 并连接 WebSocket（使用 PreferredKey 作为 key，如果为空则让子系统分配）
    StartRunAndConnect(Host, FString(), PreferredKey, SR, CH, bHttps, RunPath, WsPrefix);
}

void UAudioStreamHttpWsComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    // 尝试优雅结束（这些调用在客户端上会早退，无副作用）
    PostEndStream();
    CloseWebSocket();
    // 清零当前口型数组
    EnsureAndZeroVisemeArray();
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
        // Server side registration done. Replication handles client side via OnRep.
    }
    else
    {
        FCoreLogHelpers::CoreLog(this, ECoreLogSeverity::Error, TEXT("音频流组件"), TEXT("服务器注册"), FString::Printf(TEXT("RegisterToSubsystem FAILED (%s)"), Role));
    }
}

// ===== 网络逻辑：StartRunAndConnect / POST / WS =====

void UAudioStreamHttpWsComponent::StartRunAndConnect(const FString& ServerHostWithPort, const FString& CallbackUrl, const FString& TargetUuid, int32 SampleRate, int32 Channels, bool bUseHttps, const FString& HttpRunPath, const FString& WsPathPrefix, bool bSoftReconnect)
{
    // Server-authority gating
    if (!ComponentHasServerAuthority(this))
    {
        FCoreLogHelpers::CoreLog(this, ECoreLogSeverity::Debug, TEXT("音频流组件"), TEXT("权限"), TEXT("Skip StartRunAndConnect on client (server-only)"));
        return;
    }

    // 检查是否可以使用软重连（采样率必须一致）
    // Check if configuration matches for soft reconnect
    bool bCanSoftReconnect = bSoftReconnect && 
                             (ActiveWsSampleRate == ((SampleRate > 0) ? SampleRate : 16000)) &&
                             (ActiveWsChannels == ((Channels > 0) ? Channels : 1));

    if (bCanSoftReconnect)
    {
         FCoreLogHelpers::CoreLog(this, ECoreLogSeverity::Info, TEXT("音频流组件"), TEXT("播放控制"), TEXT("Soft reconnect: retaining audio stream state for seamless transition"));
         // 标记下一次关闭时保留队列，避免软重连过程中丢失未消费的数据
         bPreserveQueuesNextClose = true;
    }

    // 记住会话信息
    ActiveHttpHost = ServerHostWithPort;
    bActiveUseHttps = bUseHttps;
    ActiveWsSampleRate = (SampleRate > 0) ? SampleRate : 16000;
    ActiveWsChannels = (Channels > 0) ? Channels : 1;

    if (!bCanSoftReconnect)
    {
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
        SoundStream->Duration =  INDEFINITELY_LOOPING_DURATION; // Use indefinite duration to avoid frame count assertions
        SoundStream->bLooping = false;
        SoundStream->bProcedural = true;

        // 更新 AudioPlayer
        if (AudioPlayer)
        {
            AudioPlayer->SetSound(SoundStream);
        }

        // 清理 Viseme 状态
        VisemeQueue.Empty();
        VisemeConfQueue.Empty();
        VisemeStepsEmitted = 0;
        EnsureAndZeroVisemeArray();
        ResetPendingWebSocketMessages();
        StreamPushRequestSequence = 0;
        StreamAudioReturnSequence = 0;
        LastStreamPushRequestTimeSeconds = 0.0;
    }

    // 保存当前会话的路径，以便重连时复用
    ActiveHttpRunPath = HttpRunPath;
    ActiveWsPathPrefix = WsPathPrefix;
    bPureWsTaskStarted = false;
    bPureWsAwaitingTaskStart = false;

    bForceNextFadeIn = true; // Force fade-in for the new run/stream

    // 新的会话显式开始：这不是手动关闭，允许后续重连逻辑如有需要
    bManualClose = false;
    // 从新启动时重置尝试计数
    ReconnectAttempts = 0;

    if (!bCanSoftReconnect)
    {
        // 重置播放状态
        bIsPlaying = false;
        LastPlayedSeq = 0;
        LocalAudioSeq = 0;
        ResetAudioPacketQueue();
        TotalAudioFedDuration = 0.0;
    }
    // else: Keep audio state (packet queue, sequences, playing status) intact.

    if (ProtocolMode == EAudioStreamProtocolMode::PureWebSocket)
    {
        if (ActiveTaskId.IsEmpty() || !bCanSoftReconnect)
        {
            ActiveTaskId = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens);
        }
        ConnectWebSocket(BuildActiveWebSocketUrl());
    }
    else
    {
        RequestRunTask(ServerHostWithPort, CallbackUrl, TargetUuid, SampleRate, Channels, bUseHttps, HttpRunPath, WsPathPrefix);
    }
    
    // 确保播放被正确触发
    if (AudioPlayer)
    {
        if (SoundStream && !bCanSoftReconnect)
        {
            AudioPlayer->Play();
            FCoreLogHelpers::CoreLog(this, ECoreLogSeverity::Info, TEXT("音频流组件"), TEXT("播放控制"), TEXT("Playback restarted after new session initialization"));
        }
        else if (bCanSoftReconnect && !AudioPlayer->IsPlaying())
        {
            // 如果软重连期间播放停止了（例如刚好耗尽缓冲），则恢复播放
             AudioPlayer->Play();
             FCoreLogHelpers::CoreLog(this, ECoreLogSeverity::Info, TEXT("音频流组件"), TEXT("播放控制"), TEXT("Playback resumed during soft reconnect"));
        }
    }
    else
    {
        FCoreLogHelpers::CoreLog(this, ECoreLogSeverity::Error, TEXT("音频流组件"), TEXT("播放控制"), TEXT("Playback failed to start: AudioPlayer or SoundStream is null"));
    }
}

// 新方法：执行 /run POST 并解析 task_id，成功后发起 WebSocket 连接
void UAudioStreamHttpWsComponent::RequestRunTask(const FString& ServerHostWithPort, const FString& CallbackUrl, const FString& TargetUuid, int32 SampleRate, int32 Channels, bool bUseHttps, const FString& HttpRunPath, const FString& WsPathPrefix)
{
    if (ProtocolMode == EAudioStreamProtocolMode::PureWebSocket)
    {
        ConnectWebSocket(BuildActiveWebSocketUrl());
        return;
    }

    // Ensure GameThread execution
    if (!IsInGameThread())
    {
        TWeakObjectPtr<UAudioStreamHttpWsComponent> Self = this;
        AsyncTask(ENamedThreads::GameThread, [Self, ServerHostWithPort, CallbackUrl, TargetUuid, SampleRate, Channels, bUseHttps, HttpRunPath, WsPathPrefix]()
        {
            if (UAudioStreamHttpWsComponent* P = Self.Get())
            {
                P->RequestRunTask(ServerHostWithPort, CallbackUrl, TargetUuid, SampleRate, Channels, bUseHttps, HttpRunPath, WsPathPrefix);
            }
        });
        return;
    }

    // MANDATORY 2.0s DELAY as requested (for initialization/stabilization)
    // We schedule a timer to execute the actual request logic.
    TWeakObjectPtr<UAudioStreamHttpWsComponent> Self = this;
    if (UWorld* World = GetWorld())
    {
        FCoreLogHelpers::CoreLog(this, ECoreLogSeverity::Debug, TEXT("调试"), TEXT("RequestRunTask"), TEXT("Scheduling RequestRunTask with 2.0s delay"));
        FTimerHandle Handle;
        World->GetTimerManager().SetTimer(Handle, [Self, ServerHostWithPort, CallbackUrl, SampleRate, Channels, bUseHttps, HttpRunPath, WsPathPrefix]()
        {
            if (!Self.IsValid()) return;
            UAudioStreamHttpWsComponent* P = Self.Get();

            // --- ACTUAL REQUEST LOGIC START (Delayed) ---
            
            FCoreLogHelpers::CoreLog(P, ECoreLogSeverity::Debug, TEXT("调试"), TEXT("RequestRunTask"), TEXT("Executing RequestRunTask Logic after delay"));

            // Server-authority gating
            if (!ComponentHasServerAuthority(P))
            {
                FCoreLogHelpers::CoreLog(P, ECoreLogSeverity::Trace, TEXT("音频流组件"), TEXT("权限"), TEXT("Skip RequestRunTask on client (server-only)"));
                return;
            }

            const FString Scheme = bUseHttps ? TEXT("https") : TEXT("http");
            const FString RunUrl = FString::Printf(TEXT("%s://%s%s"), *Scheme, *ServerHostWithPort, *HttpRunPath);

            FCoreLogHelpers::CoreLog(P, ECoreLogSeverity::Debug, TEXT("调试"), TEXT("RequestRunTask"), FString::Printf(TEXT("Creating HTTP Request to %s"), *RunUrl));

            TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Req = FHttpModule::Get().CreateRequest();
            Req->SetURL(RunUrl);
            Req->SetVerb(TEXT("POST"));
            Req->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
            Req->SetTimeout(10.0f); // 显式设置超时 Explicit timeout

            FCoreLogHelpers::CoreLog(P, ECoreLogSeverity::Debug, TEXT("调试"), TEXT("RequestRunTask"), TEXT("HTTP Request Object Created"));

            TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
            if (!CallbackUrl.IsEmpty()) Body->SetStringField(TEXT("callback"), CallbackUrl);
            Body->SetStringField(TEXT("key"), P->RegisteredUuid);
            Body->SetNumberField(TEXT("sample_rate"), SampleRate);
            Body->SetNumberField(TEXT("channels"), Channels);
            FString BodyStr; const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&BodyStr);
            FJsonSerializer::Serialize(Body, Writer);
            Req->SetContentAsString(BodyStr);

                FCoreLogHelpers::CoreLog(P, ECoreLogSeverity::Debug, TEXT("调试"), TEXT("RequestRunTask"), FString::Printf(TEXT("HTTP Body Set: %s"), *SanitizeForLogPreview(BodyStr)));

            // Log the request URL and sanitized body
            {
                TMap<FString, FString> Data;
                Data.Add(TEXT("url"), SanitizeForLogPreview(RunUrl));
                Data.Add(TEXT("body"), SanitizeForLogPreview(BodyStr));
                // 把data写进日志的message
                FCoreLogHelpers::CoreLog(P, ECoreLogSeverity::Debug, TEXT("音频流组件"), TEXT("连接握手"), TEXT("RequestRunTask /run request, url = ") + SanitizeForLogPreview(RunUrl), Data);
            }

            Req->OnProcessRequestComplete().BindLambda([Self, ServerHostWithPort, WsPathPrefix, bUseHttps](FHttpRequestPtr ReqPtr, FHttpResponsePtr Resp, bool bOK)
            {
                // Must ensure callback logic (especially next steps) happens on GameThread or handles object safety.
                // FHttpModule callbacks are generally onGameThread, but let's be safe if we touch UObjects or timers.
                
                // Detailed logging for callback
                FString RespCodeStr = Resp.IsValid() ? FString::FromInt(Resp->GetResponseCode()) : TEXT("Invalid");
                UE_LOG(LogTemp, Log, TEXT("[Debugging] RequestRunTask Callback: bOK=%d, ResponseCode=%s, SelfValid=%d"), bOK, *RespCodeStr, Self.IsValid());

                if (!Self.IsValid()) return;
                UAudioStreamHttpWsComponent* P = Self.Get();
                
                FCoreLogHelpers::CoreLog(P, ECoreLogSeverity::Trace, TEXT("调试"), TEXT("RequestRunTask"), 
                    FString::Printf(TEXT("RequestRunTask Completed. Success=%d ResponseValid=%d Code=%s"), bOK, Resp.IsValid(), *RespCodeStr));

                if (!bOK || !Resp.IsValid())
                {
                    TMap<FString,FString> Data;
                    Data.Add(TEXT("host"), ServerHostWithPort);
                    Data.Add(TEXT("response"), Resp.IsValid() ? Resp->GetContentAsString().Left(256) : TEXT("Invalid Response"));
                    FCoreLogHelpers::CoreLog(P, ECoreLogSeverity::Warn, TEXT("音频流组件"), TEXT("连接握手"), TEXT("/run request failed"), Data);
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
                    FCoreLogHelpers::CoreLog(P, ECoreLogSeverity::Info, TEXT("音频流组件"), TEXT("连接握手"), TEXT("/run response received"), {
                        { TEXT("task_id"), TaskId },
                        { TEXT("host"), ServerHostWithPort }
                    });
                    const FString WsScheme = bUseHttps ? TEXT("wss") : TEXT("ws");
                    const FString WsUrl = FString::Printf(TEXT("%s://%s%s%s"), *WsScheme, *ServerHostWithPort, *WsPathPrefix, *TaskId);
                    // 软重连路径：确保下一次 Close 保留队列
                    P->bPreserveQueuesNextClose = true;
                    AsyncTask(ENamedThreads::GameThread, [Self, WsUrl]() 
                    { 
                        if (UAudioStreamHttpWsComponent* SafeP = Self.Get())
                        {
                            SafeP->ConnectWebSocket(WsUrl);
                        }
                    });
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

            // --- ACTUAL REQUEST LOGIC END ---

        }, 2.0f, false);
    }
    else
    {
         FCoreLogHelpers::CoreLog(this, ECoreLogSeverity::Error, TEXT("调试"), TEXT("RequestRunTask"), TEXT("Invalid World, cannot schedule delay."));
    }
}

void UAudioStreamHttpWsComponent::CancelActiveStreamRequest(const TCHAR* Reason)
{
    if (!IsInGameThread())
    {
        TWeakObjectPtr<UAudioStreamHttpWsComponent> Self = this;
        const FString ReasonCopy = Reason ? Reason : TEXT("Unknown");
        AsyncTask(ENamedThreads::GameThread, [Self, ReasonCopy]()
        {
            if (UAudioStreamHttpWsComponent* P = Self.Get())
            {
                P->CancelActiveStreamRequest(*ReasonCopy);
            }
        });
        return;
    }

    if (ActiveStreamRequest.IsValid())
    {
        ActiveStreamRequest->OnProcessRequestComplete().Unbind();
        ActiveStreamRequest->CancelRequest();
        ActiveStreamRequest.Reset();
        ActiveStreamRequestId = 0;
        FCoreLogHelpers::CoreLog(this, ECoreLogSeverity::Debug, TEXT("音频流组件"), TEXT("信息流"), FString::Printf(TEXT("Canceled active stream request (%s)"), Reason ? Reason : TEXT("Unknown")));
    }
}

void UAudioStreamHttpWsComponent::ScheduleProcessNextStreamQueueItem(float DelaySeconds)
{
    if (!IsInGameThread())
    {
        TWeakObjectPtr<UAudioStreamHttpWsComponent> Self = this;
        const float DelayCopy = DelaySeconds;
        AsyncTask(ENamedThreads::GameThread, [Self, DelayCopy]()
        {
            if (UAudioStreamHttpWsComponent* P = Self.Get())
            {
                P->ScheduleProcessNextStreamQueueItem(DelayCopy);
            }
        });
        return;
    }

    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(StreamQueueProcessTimerHandle);

        TWeakObjectPtr<UAudioStreamHttpWsComponent> Self = this;
        const float SafeDelay = FMath::Max(0.0f, DelaySeconds);
        if (SafeDelay <= KINDA_SMALL_NUMBER)
        {
            World->GetTimerManager().SetTimerForNextTick([Self]()
            {
                if (UAudioStreamHttpWsComponent* P = Self.Get())
                {
                    P->ProcessNextStreamQueueItem();
                }
            });
            return;
        }

        World->GetTimerManager().SetTimer(StreamQueueProcessTimerHandle, [Self]()
        {
            if (UAudioStreamHttpWsComponent* P = Self.Get())
            {
                P->ProcessNextStreamQueueItem();
            }
        }, SafeDelay, false);
    }
}

void UAudioStreamHttpWsComponent::SchedulePendingStreamTextFlush(float DelaySeconds)
{
    if (!IsInGameThread())
    {
        TWeakObjectPtr<UAudioStreamHttpWsComponent> Self = this;
        const float DelayCopy = DelaySeconds;
        AsyncTask(ENamedThreads::GameThread, [Self, DelayCopy]()
        {
            if (UAudioStreamHttpWsComponent* P = Self.Get())
            {
                P->SchedulePendingStreamTextFlush(DelayCopy);
            }
        });
        return;
    }

    if (UWorld* World = GetWorld())
    {
        const float SafeDelay = FMath::Max(0.0f, DelaySeconds);
        TWeakObjectPtr<UAudioStreamHttpWsComponent> Self = this;
        World->GetTimerManager().ClearTimer(PendingStreamTextFlushTimerHandle);
        World->GetTimerManager().SetTimer(PendingStreamTextFlushTimerHandle, [Self]()
        {
            if (UAudioStreamHttpWsComponent* P = Self.Get())
            {
                P->FlushPendingStreamText(false);
            }
        }, SafeDelay, false);
    }
}

void UAudioStreamHttpWsComponent::FlushPendingStreamText(bool bForce)
{
    if (!IsInGameThread())
    {
        TWeakObjectPtr<UAudioStreamHttpWsComponent> Self = this;
        AsyncTask(ENamedThreads::GameThread, [Self, bForce]()
        {
            if (UAudioStreamHttpWsComponent* P = Self.Get())
            {
                P->FlushPendingStreamText(bForce);
            }
        });
        return;
    }

    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(PendingStreamTextFlushTimerHandle);
    }

    if (PendingStreamTextBuffer.IsEmpty())
    {
        return;
    }

    // 非强制 flush 由定时器触发；一旦定时器到了，就应该把当前已积累文本发出去，
    // 否则短文本会一直达不到字符阈值，导致请求长期不发出。

    const FString TextToQueue = NormalizeStreamTextForTts(PendingStreamTextBuffer);
    PendingStreamTextBuffer.Empty();
    if (TextToQueue.IsEmpty())
    {
        return;
    }

    const double NowSeconds = FPlatformTime::Seconds();
    if (!TryCoalesceQueuedText(TextToQueue, NowSeconds))
    {
        FStreamQueueItem Item;
        Item.Type = FStreamQueueItem::EType::Text;
        Item.TextContent = TextToQueue;
        Item.EnqueueTimeSeconds = NowSeconds;
        StreamQueue.Add(Item);
    }

    TMap<FString, FString> Data;
    Data.Add(TEXT("len"), FString::FromInt(TextToQueue.Len()));
    Data.Add(TEXT("queue"), FString::FromInt(StreamQueue.Num()));
    Data.Add(TEXT("force"), bForce ? TEXT("1") : TEXT("0"));
    Data.Add(TEXT("preview"), SanitizeForLogPreview(TextToQueue, 64));
    FCoreLogHelpers::CoreLog(this, ECoreLogSeverity::Fatal, TEXT("音频流组件"), TEXT("节流"), TEXT("FlushPendingStreamText"), Data);

    ScheduleProcessNextStreamQueueItem();
}

bool UAudioStreamHttpWsComponent::TryCoalesceQueuedText(const FString& Text, double NowSeconds)
{
    const int32 QueueNum = StreamQueue.Num();
    if (QueueNum <= 0)
    {
        return false;
    }

    const float CoalesceWindow = FMath::Max(0.0f, StreamTextCoalesceWindowSeconds);
    const bool bForceMerge = QueueNum >= FMath::Max(1, MaxPendingStreamTextItems);

    for (int32 Index = QueueNum - 1; Index >= 0; --Index)
    {
        FStreamQueueItem& Candidate = StreamQueue[Index];
        if (Candidate.Type == FStreamQueueItem::EType::EndStream)
        {
            break;
        }

        if (Candidate.Type != FStreamQueueItem::EType::Text)
        {
            continue;
        }

        const double Age = NowSeconds - Candidate.EnqueueTimeSeconds;
        if (!bForceMerge && (CoalesceWindow <= KINDA_SMALL_NUMBER || Age > CoalesceWindow))
        {
            return false;
        }

        Candidate.TextContent.Append(Text);
        Candidate.EnqueueTimeSeconds = NowSeconds;
        return true;
    }

    return false;
}

void UAudioStreamHttpWsComponent::PostStreamText(const FString& Text)
{
    if (!IsInGameThread())
    {
        TWeakObjectPtr<UAudioStreamHttpWsComponent> Self = this;
        const FString TextCopy = Text;
        AsyncTask(ENamedThreads::GameThread, [Self, TextCopy]()
        {
            if (UAudioStreamHttpWsComponent* P = Self.Get())
            {
                P->PostStreamText(TextCopy);
            }
        });
        return;
    }

    // Server-authority gating
    if (!ComponentHasServerAuthority(this))
    {
        FCoreLogHelpers::CoreLog(this, ECoreLogSeverity::Trace, TEXT("音频流组件"), TEXT("权限"), TEXT("Skip PostStreamText on client (server-only)"));
        return;
    }

    if (ActiveTaskId.IsEmpty() || ActiveHttpHost.IsEmpty())
    {
        return;
    }

    if (Text.IsEmpty())
    {
        return;
    }

    PendingStreamTextBuffer.Append(Text);

    const int32 FlushChars = FMath::Max(1, StreamTextMaxBatchChars);
    if (PendingStreamTextBuffer.Len() >= FlushChars)
    {
        FlushPendingStreamText(true);
        return;
    }

    SchedulePendingStreamTextFlush(FMath::Max(0.02f, StreamTextFlushIntervalSeconds));
}

void UAudioStreamHttpWsComponent::ProcessNextStreamQueueItem()
{
    if (!IsInGameThread())
    {
        ScheduleProcessNextStreamQueueItem();
        return;
    }

    if (bIsProcessingStreamQueue || ActiveStreamRequest.IsValid() || StreamQueue.Num() == 0)
    {
        return;
    }

    if (ActiveTaskId.IsEmpty() || ActiveHttpHost.IsEmpty())
    {
        // Connection lost? clear queue
        StreamQueue.Empty();
        return;
    }

    const float MinInterval = FMath::Max(0.0f, MinStreamRequestIntervalSeconds);
    if (MinInterval > KINDA_SMALL_NUMBER && LastStreamRequestDispatchTime > 0.0)
    {
        const double Elapsed = FPlatformTime::Seconds() - LastStreamRequestDispatchTime;
        if (Elapsed < MinInterval)
        {
            ScheduleProcessNextStreamQueueItem(static_cast<float>(MinInterval - Elapsed));
            return;
        }
    }

    const double NowSeconds = FPlatformTime::Seconds();
    if (NextStreamRequestAllowedTime > NowSeconds)
    {
        ScheduleProcessNextStreamQueueItem(static_cast<float>(NextStreamRequestAllowedTime - NowSeconds));
        return;
    }

    bIsProcessingStreamQueue = true;

    const FString TaskId = ActiveTaskId;
    const FString Host = ActiveHttpHost;
    const bool bHttps = bActiveUseHttps;
    TWeakObjectPtr<UAudioStreamHttpWsComponent> Self = this;

    if (ProtocolMode == EAudioStreamProtocolMode::PureWebSocket)
    {
        if (!WebSocket.IsValid() || !WebSocket->IsConnected())
        {
            bIsProcessingStreamQueue = false;
            ScheduleProcessNextStreamQueueItem(0.1f);
            return;
        }

        if (!bPureWsTaskStarted)
        {
            bIsProcessingStreamQueue = false;
            if (!bPureWsAwaitingTaskStart)
            {
                BeginPureWsTask(ActiveTaskId.IsEmpty());
            }
            ScheduleProcessNextStreamQueueItem(0.05f);
            return;
        }

        const FStreamQueueItem Item = StreamQueue[0];
        StreamQueue.RemoveAt(0);

        const uint32 RequestId = ++StreamRequestIdCounter;
        ActiveStreamRequestId = RequestId;
        LastStreamRequestDispatchTime = FPlatformTime::Seconds();

        bool bSent = false;
        if (Item.Type == FStreamQueueItem::EType::Text)
        {
            const FString NormalizedText = NormalizeStreamTextForTts(Item.TextContent);
            if (NormalizedText.IsEmpty())
            {
                ActiveStreamRequestId = 0;
                bIsProcessingStreamQueue = false;
                ScheduleProcessNextStreamQueueItem();
                return;
            }

            TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
            TSharedRef<FJsonObject> Input = MakeShared<FJsonObject>();
            Input->SetStringField(TEXT("text"), NormalizedText);
            Payload->SetObjectField(TEXT("input"), Input);
            LogStreamPushDispatch(TEXT("PureWebSocket"), NormalizedText);
            bSent = SendPureWsMessage(TEXT("continue-task"), Payload);
        }
        else
        {
            TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
            Payload->SetObjectField(TEXT("input"), MakeShared<FJsonObject>());
            bSent = SendPureWsMessage(TEXT("finish-task"), Payload);
            if (bSent)
            {
                bPureWsTaskStarted = false;
                bPureWsAwaitingTaskStart = true;
            }
        }

        if (!bSent)
        {
            ConsecutiveStreamFailures += 1;
            const float Base = FMath::Max(0.0f, StreamFailureCooldownBaseSeconds);
            const float MaxCooldown = FMath::Max(Base, StreamFailureCooldownMaxSeconds);
            const float Cooldown = (Base <= KINDA_SMALL_NUMBER)
                ? 0.0f
                : FMath::Min(MaxCooldown, Base * FMath::Pow(2.0f, FMath::Max(0, ConsecutiveStreamFailures - 1)));
            NextStreamRequestAllowedTime = FPlatformTime::Seconds() + Cooldown;
        }
        else
        {
            ConsecutiveStreamFailures = 0;
            NextStreamRequestAllowedTime = 0.0;
        }

        ActiveStreamRequestId = 0;
        bIsProcessingStreamQueue = false;
        ScheduleProcessNextStreamQueueItem();
        return;
    }

    const FStreamQueueItem Item = StreamQueue[0];
    StreamQueue.RemoveAt(0);

    if (Item.Type == FStreamQueueItem::EType::Text)
    {
        const FString NormalizedText = NormalizeStreamTextForTts(Item.TextContent);
        if (NormalizedText.IsEmpty())
        {
            ActiveStreamRequestId = 0;
            bIsProcessingStreamQueue = false;
            ScheduleProcessNextStreamQueueItem();
            return;
        }

        const FString Scheme = bHttps ? TEXT("https") : TEXT("http");
        const UAudioStreamSettings* S = GetDefault<UAudioStreamSettings>();
        const FString StreamPath = S ? S->DefaultHttpStreamPath : TEXT("/stream");
        const FString Url = FString::Printf(TEXT("%s://%s%s/%s"), *Scheme, *Host, *StreamPath, *TaskId);

        TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Req = FHttpModule::Get().CreateRequest();
        Req->SetURL(Url);
        Req->SetVerb(TEXT("POST"));
        Req->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
        Req->SetTimeout(5.0f);

        TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
        Obj->SetStringField(TEXT("text"), NormalizedText);
        FString BodyStr; const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&BodyStr);
        FJsonSerializer::Serialize(Obj, Writer);
        Req->SetContentAsString(BodyStr);

        LogStreamPushDispatch(TEXT("LegacyHttpWs"), NormalizedText);

        const uint32 RequestId = ++StreamRequestIdCounter;
        ActiveStreamRequest = Req;
        ActiveStreamRequestId = RequestId;
        LastStreamRequestDispatchTime = FPlatformTime::Seconds();

        Req->OnProcessRequestComplete().BindLambda([Self, RequestId](FHttpRequestPtr, FHttpResponsePtr Resp, bool bOK)
        {
            if (!Self.IsValid()) return;
            UAudioStreamHttpWsComponent* P = Self.Get();
            if (!IsValid(P)) return;

            if (P->ActiveStreamRequestId != RequestId)
            {
                return;
            }

            P->ActiveStreamRequest.Reset();
            P->ActiveStreamRequestId = 0;

            if (!bOK || !Resp.IsValid())
            {
                TMap<FString,FString> Data; Data.Add(TEXT("action"), TEXT("PostStreamText"));
                FCoreLogHelpers::CoreLog(P, ECoreLogSeverity::Warn, TEXT("音频流组件"), TEXT("信息流"), TEXT("PostStreamText failed"), Data);

                P->ConsecutiveStreamFailures += 1;
                const float Base = FMath::Max(0.0f, P->StreamFailureCooldownBaseSeconds);
                const float MaxCooldown = FMath::Max(Base, P->StreamFailureCooldownMaxSeconds);
                const float Cooldown = (Base <= KINDA_SMALL_NUMBER)
                    ? 0.0f
                    : FMath::Min(MaxCooldown, Base * FMath::Pow(2.0f, FMath::Max(0, P->ConsecutiveStreamFailures - 1)));
                P->NextStreamRequestAllowedTime = FPlatformTime::Seconds() + Cooldown;
            }
            else
            {
                P->ConsecutiveStreamFailures = 0;
                P->NextStreamRequestAllowedTime = 0.0;
            }

            P->bIsProcessingStreamQueue = false;
            P->ScheduleProcessNextStreamQueueItem();
        });

        if (!Req->ProcessRequest())
        {
            ActiveStreamRequest.Reset();
            ActiveStreamRequestId = 0;
            bIsProcessingStreamQueue = false;
            FCoreLogHelpers::CoreLog(this, ECoreLogSeverity::Error, TEXT("音频流组件"), TEXT("信息流"), TEXT("PostStreamText ProcessRequest failed immediately"));

            ConsecutiveStreamFailures += 1;
            const float Base = FMath::Max(0.0f, StreamFailureCooldownBaseSeconds);
            const float MaxCooldown = FMath::Max(Base, StreamFailureCooldownMaxSeconds);
            const float Cooldown = (Base <= KINDA_SMALL_NUMBER)
                ? 0.0f
                : FMath::Min(MaxCooldown, Base * FMath::Pow(2.0f, FMath::Max(0, ConsecutiveStreamFailures - 1)));
            NextStreamRequestAllowedTime = FPlatformTime::Seconds() + Cooldown;

            ScheduleProcessNextStreamQueueItem();
        }
    }
    else if (Item.Type == FStreamQueueItem::EType::EndStream)
    {
        const FString Scheme = bHttps ? TEXT("https") : TEXT("http");
        const UAudioStreamSettings* S = GetDefault<UAudioStreamSettings>();
        const FString EndPath = S ? S->DefaultHttpEndStreamPath : TEXT("/end-stream");
        const FString Url = FString::Printf(TEXT("%s://%s%s/%s"), *Scheme, *Host, *EndPath, *TaskId);

        TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Req = FHttpModule::Get().CreateRequest();
        Req->SetURL(Url);
        Req->SetVerb(TEXT("POST"));
        Req->SetTimeout(5.0f);

        {
            TMap<FString, FString> Data;
            Data.Add(TEXT("url"), SanitizeForLogPreview(Url));
            Data.Add(TEXT("body"), TEXT(""));
            FCoreLogHelpers::CoreLog(this, ECoreLogSeverity::Debug, TEXT("音频流组件"), TEXT("信息流"), TEXT("PostEndStream"), Data);
        }

        const uint32 RequestId = ++StreamRequestIdCounter;
        ActiveStreamRequest = Req;
        ActiveStreamRequestId = RequestId;
        LastStreamRequestDispatchTime = FPlatformTime::Seconds();

        Req->OnProcessRequestComplete().BindLambda([Self, RequestId](FHttpRequestPtr, FHttpResponsePtr, bool bOK)
        {
            if (!Self.IsValid()) return;
            UAudioStreamHttpWsComponent* P = Self.Get();
            if (!IsValid(P)) return;

            if (P->ActiveStreamRequestId != RequestId)
            {
                return;
            }

            P->ActiveStreamRequest.Reset();
            P->ActiveStreamRequestId = 0;

            if (bOK)
            {
                TMap<FString,FString> Data; Data.Add(TEXT("action"), TEXT("PostEndStream"));
                FCoreLogHelpers::CoreLog(P, ECoreLogSeverity::Info, TEXT("音频流组件"), TEXT("信息流"), TEXT("EndStream posted"), Data);
                P->ConsecutiveStreamFailures = 0;
                P->NextStreamRequestAllowedTime = 0.0;
            }
            else
            {
                TMap<FString,FString> Data; Data.Add(TEXT("action"), TEXT("PostEndStream"));
                FCoreLogHelpers::CoreLog(P, ECoreLogSeverity::Warn, TEXT("音频流组件"), TEXT("信息流"), TEXT("EndStream post failed"), Data);
                P->ConsecutiveStreamFailures += 1;
                const float Base = FMath::Max(0.0f, P->StreamFailureCooldownBaseSeconds);
                const float MaxCooldown = FMath::Max(Base, P->StreamFailureCooldownMaxSeconds);
                const float Cooldown = (Base <= KINDA_SMALL_NUMBER)
                    ? 0.0f
                    : FMath::Min(MaxCooldown, Base * FMath::Pow(2.0f, FMath::Max(0, P->ConsecutiveStreamFailures - 1)));
                P->NextStreamRequestAllowedTime = FPlatformTime::Seconds() + Cooldown;
            }

            P->bIsProcessingStreamQueue = false;
            P->ScheduleProcessNextStreamQueueItem();
        });

        if (!Req->ProcessRequest())
        {
            ActiveStreamRequest.Reset();
            ActiveStreamRequestId = 0;
            bIsProcessingStreamQueue = false;
            FCoreLogHelpers::CoreLog(this, ECoreLogSeverity::Error, TEXT("音频流组件"), TEXT("信息流"), TEXT("PostEndStream ProcessRequest failed immediately"));

            ConsecutiveStreamFailures += 1;
            const float Base = FMath::Max(0.0f, StreamFailureCooldownBaseSeconds);
            const float MaxCooldown = FMath::Max(Base, StreamFailureCooldownMaxSeconds);
            const float Cooldown = (Base <= KINDA_SMALL_NUMBER)
                ? 0.0f
                : FMath::Min(MaxCooldown, Base * FMath::Pow(2.0f, FMath::Max(0, ConsecutiveStreamFailures - 1)));
            NextStreamRequestAllowedTime = FPlatformTime::Seconds() + Cooldown;

            ScheduleProcessNextStreamQueueItem();
        }
    }
}

void UAudioStreamHttpWsComponent::PostEndStream()
{
    if (!IsInGameThread())
    {
        TWeakObjectPtr<UAudioStreamHttpWsComponent> Self = this;
        AsyncTask(ENamedThreads::GameThread, [Self]()
        {
            if (UAudioStreamHttpWsComponent* P = Self.Get())
            {
                P->PostEndStream();
            }
        });
        return;
    }

    // Server-authority gating
    if (!ComponentHasServerAuthority(this))
    {
        FCoreLogHelpers::CoreLog(this, ECoreLogSeverity::Trace, TEXT("音频流组件"), TEXT("权限"), TEXT("Skip PostEndStream on client (server-only)"));
        return;
    }

    if (ActiveTaskId.IsEmpty() || ActiveHttpHost.IsEmpty()) return;

    FlushPendingStreamText(true);

    FStreamQueueItem Item;
    Item.Type = FStreamQueueItem::EType::EndStream;
    Item.EnqueueTimeSeconds = FPlatformTime::Seconds();
    StreamQueue.Add(Item);
    ScheduleProcessNextStreamQueueItem();
}

void UAudioStreamHttpWsComponent::ConnectWebSocket(const FString& Url)
{
    // Server-authority gating
    if (!ComponentHasServerAuthority(this))
    {
        FCoreLogHelpers::CoreLog(this, ECoreLogSeverity::Trace, TEXT("音频流组件"), TEXT("权限"), TEXT("Skip ConnectWebSocket on client (server-only)"));
        return;
    }

    // 根据标志决定是否保留队列
    const bool bKeepQueue = bPreserveQueuesNextClose;
    // 关闭旧连接（软重连时保留队列）
    CloseWebSocket(bKeepQueue);
    // 重置标志，避免后续误用
    bPreserveQueuesNextClose = false;

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
        if (P->ProtocolMode == EAudioStreamProtocolMode::PureWebSocket)
        {
            P->BeginPureWsTask(true);
        }
    });

    WebSocket->OnConnectionError().AddLambda([Self](const FString& Error)
    {
        if (!Self.IsValid()) return;
        UAudioStreamHttpWsComponent* P = Self.Get();
        TMap<FString,FString> Data; Data.Add(TEXT("error"), Error);
        FCoreLogHelpers::CoreLog(P, ECoreLogSeverity::Warn, TEXT("音频流组件"), TEXT("连接恢复"), FString::Printf(TEXT("WS connection error: %s"), *Error), Data);
        // 仅在之前已经成功过连接且不是手动关闭的情况下才安排重试
        if (P->bHasEverConnected && !P->bManualClose)
        {
            AsyncTask(ENamedThreads::GameThread, [Self]() 
            { 
                if (UAudioStreamHttpWsComponent* SafeP = Self.Get())
                {
                    SafeP->ScheduleReconnect();
                }
            });
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
            AsyncTask(ENamedThreads::GameThread, [Self]() 
            { 
                if (UAudioStreamHttpWsComponent* SafeP = Self.Get())
                {
                    SafeP->ScheduleReconnect();
                }
            });
        }
    });

    WebSocket->OnMessage().AddLambda([Self](const FString& Message)
    {
        if (UAudioStreamHttpWsComponent* P = Self.Get())
        {
            P->EnqueuePendingWebSocketMessage(Message);
        }
    });

    WebSocket->Connect();
}

void UAudioStreamHttpWsComponent::CloseWebSocket(bool bKeepQueue)
{
    // 标记为手动关闭，避免自动重连被触发
    bManualClose = true;
    // 取消任何计划的重连
    CancelReconnect();
    CancelActiveStreamRequest(TEXT("CloseWebSocket"));
    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(StreamQueueProcessTimerHandle);
        World->GetTimerManager().ClearTimer(PendingStreamTextFlushTimerHandle);
    }
    bIsProcessingStreamQueue = false;
    bPureWsTaskStarted = false;
    bPureWsAwaitingTaskStart = false;
    if (WebSocket.IsValid())
    {
        WebSocket->OnConnected().Clear();
        WebSocket->OnConnectionError().Clear();
        WebSocket->OnClosed().Clear();
        WebSocket->OnMessage().Clear();
        WebSocket->Close();
        WebSocket.Reset();
    }

    ResetPendingWebSocketMessages();

    // 清空音频队列，避免旧数据干扰
    if (!bKeepQueue)
    {
        ResetAudioPacketQueue();
        VisemeQueue.Empty();
        VisemeConfQueue.Empty();
        VisemeStepsEmitted = 0;
        EnsureAndZeroVisemeArray();
        PendingStreamTextBuffer.Empty();
        ConsecutiveStreamFailures = 0;
        NextStreamRequestAllowedTime = 0.0;
        StreamPushRequestSequence = 0;
        StreamAudioReturnSequence = 0;
        LastStreamPushRequestTimeSeconds = 0.0;
        FCoreLogHelpers::CoreLog(this, ECoreLogSeverity::Debug, TEXT("音频流组件"), TEXT("关闭"), TEXT("WebSocket 已关闭，音频+Viseme 队列已清空"));
    }
    else
    {
         FCoreLogHelpers::CoreLog(this, ECoreLogSeverity::Info, TEXT("音频流组件"), TEXT("关闭"), TEXT("WebSocket 已关闭，音频队列保留(seamless)"));
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

         FCoreLogHelpers::CoreLog(this, ECoreLogSeverity::Debug, TEXT("音频流组件"), TEXT("连接状态"), FString::Printf(TEXT("Schedule reconnect attempt %d in %.1f sec"), ReconnectAttempts, Delay));

    if (UWorld* W2 = GetWorld())
    {
        TWeakObjectPtr<UAudioStreamHttpWsComponent> WeakThis(this);
        W2->GetTimerManager().SetTimer(ReconnectTimerHandle, [WeakThis]() {
            if (UAudioStreamHttpWsComponent* P = WeakThis.Get())
            {
                // 使用上次的 host 和注册 uuid 发起 /run 并重连（仅服务器侧会执行 RequestRunTask 自身的权限检查）
                if (P->ProtocolMode == EAudioStreamProtocolMode::PureWebSocket)
                {
                    P->ConnectWebSocket(P->BuildActiveWebSocketUrl());
                }
                else
                {
                    P->RequestRunTask(P->ActiveHttpHost, FString(), P->RegisteredUuid, P->ActiveWsSampleRate, P->ActiveWsChannels, P->bActiveUseHttps, P->ActiveHttpRunPath, P->ActiveWsPathPrefix);
                }
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
        const UAudioStreamSettings* S = GetDefault<UAudioStreamSettings>();
        const bool bPayloadAlreadyDecodedPcm = (Header.Flags & LocalDecodedPcmFlag) != 0;
#if defined(CUSTOMINPUT_USE_OPUS)
        const bool bDecodeOpus = S && S->bEnableOpus && !bPayloadAlreadyDecodedPcm;
#else
        const bool bDecodeOpus = false;
#endif

        TArray<uint8> DecodedPcm;
        const TArray<uint8>* DataToQueue = &Payload;

#if defined(CUSTOMINPUT_USE_OPUS)
        if (bDecodeOpus)
        {
            if (!OpusDecoderHandle)
            {
                InitOpusDecoder(ActiveWsSampleRate, ActiveWsChannels);
            }
            OpusDecoder* Dec = (OpusDecoder*)OpusDecoderHandle;
            if (Dec)
            {
                // 估算每帧样本数（按发送端帧长设置）；为稳妥用最大 120ms 框架
                const int32 MaxSamplesPerChannel = FMath::Clamp(S ? S->FrameDurationMs : 20, 10, 120) * ActiveWsSampleRate / 1000;
                const int32 MaxOutSamples = MaxSamplesPerChannel * ActiveWsChannels;
                DecodedPcm.SetNumUninitialized(MaxOutSamples * sizeof(int16));
                int16* OutSamples = reinterpret_cast<int16*>(DecodedPcm.GetData());

                const int32 OutCount = opus_decode(Dec, Payload.GetData(), Payload.Num(), OutSamples, MaxOutSamples / ActiveWsChannels, 0);
                if (OutCount > 0)
                {
                    const int32 OutBytes = OutCount * ActiveWsChannels * sizeof(int16);
                    DecodedPcm.SetNum(OutBytes, EAllowShrinking::Yes);
                    DataToQueue = &DecodedPcm;
                    
                    FCoreLogHelpers::CoreLog(this, ECoreLogSeverity::Trace, TEXT("调试"), TEXT("Opus"), 
                        FString::Printf(TEXT("Opus Decoded: In=%d -> OutBytes=%d (Samples=%d)"), Payload.Num(), OutBytes, OutCount));
                }
                else
                {
                    FCoreLogHelpers::CoreLog(this, ECoreLogSeverity::Error, TEXT("音频解码"), TEXT("Opus"), FString::Printf(TEXT("opus_decode failed (%d), fallback to raw"), OutCount));
                }
            }
        }
#endif

        // 自动重置逻辑更新：
        bool bIsRestart = (Header.Seq == 0) || (Header.Seq == 1 && LastPlayedSeq > 10);
        if (bIsPlaying && !bPendingStreamReset && bIsRestart)
        {
            bPendingStreamReset = true;
            ResetAudioPacketQueue();
            FCoreLogHelpers::CoreLog(this, ECoreLogSeverity::Info, TEXT("流数据处理"), TEXT("Socket接收"), 
                FString::Printf(TEXT("Stream reset detected (Seq=%d), scheduled reset. Previous LastSeq=%d"), Header.Seq, LastPlayedSeq));
        }
        if (bIsPlaying && !bPendingStreamReset && Header.Seq <= LastPlayedSeq)
        {
            FCoreLogHelpers::CoreLog(this, ECoreLogSeverity::Trace, TEXT("调试"), TEXT("Discard"),
                FString::Printf(TEXT("Discarding Seq=%d because <= LastPlayedSeq=%d"), Header.Seq, LastPlayedSeq));
            return;
        }

        FAudioPacketBuffer NewPacket;
        NewPacket.Seq = Header.Seq;
        NewPacket.Data = *DataToQueue;

        bool bInserted = false;
        if (AudioPacketQueue.Num() == 0 || AudioPacketQueue.Last().Seq < NewPacket.Seq)
        {
            AudioPacketQueue.Add(NewPacket);
            bInserted = true;
        }

        for (int32 i = 0; !bInserted && i < AudioPacketQueue.Num(); ++i)
        {
            if (AudioPacketQueue[i].Seq == NewPacket.Seq)
            {
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
        QueuedAudioBytes += NewPacket.Data.Num();
    }
    else if (Header.Type == AudioStreamPacket::Viseme)
    {
        // Parse payload: Writer << Vis; Writer << Confidence;
        TArray<int32> InVis;
        TArray<float> InConf;
        FMemoryReader Reader(Payload);
        Reader << InVis;
        Reader << InConf;

        // Append to queues
        if (InVis.Num() > 0)
        {
            VisemeQueue.Append(InVis);
            if (InConf.Num() == InVis.Num())
            {
                VisemeConfQueue.Append(InConf);
            }
            else
            {
                // pad confidences
                const int32 OldNum = VisemeConfQueue.Num();
                VisemeConfQueue.SetNum(VisemeQueue.Num());
                for (int32 i = OldNum; i < VisemeQueue.Num(); ++i) VisemeConfQueue[i] = 1.0f;
            }
            FCoreLogHelpers::CoreLog(this, ECoreLogSeverity::Trace, TEXT("Viseme"), TEXT("入队"), FString::Printf(TEXT("+%d -> total=%d"), InVis.Num(), VisemeQueue.Num()));
        }
    }
}

#if defined(CUSTOMINPUT_USE_OPUS)
void UAudioStreamHttpWsComponent::InitOpusDecoder(int32 SampleRate, int32 Channels)
{
    if (OpusDecoderHandle)
    {
        DestroyOpusDecoder();
    }
    int Err = 0;
    OpusDecoder* Dec = opus_decoder_create(SampleRate, Channels, &Err);
    if (!Dec || Err != OPUS_OK)
    {
        OpusDecoderHandle = nullptr;
        UE_LOG(LogTemp, Warning, TEXT("[Opus] decoder init failed (sr=%d ch=%d err=%d)"), SampleRate, Channels, Err);
        return;
    }
    OpusDecoderHandle = Dec;
    UE_LOG(LogTemp, Log, TEXT("[Opus] decoder initialized (sr=%d ch=%d)"), SampleRate, Channels);
}

void UAudioStreamHttpWsComponent::DestroyOpusDecoder()
{
    if (OpusDecoderHandle)
    {
        opus_decoder_destroy((OpusDecoder*)OpusDecoderHandle);
        OpusDecoderHandle = nullptr;
    }
}
#endif

void UAudioStreamHttpWsComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(UAudioStreamHttpWsComponent, RegisteredUuid);
}

void UAudioStreamHttpWsComponent::OnRep_RegisteredUuid()
{
    const TCHAR* Role = GetRoleLabel(this);
    if (RegisteredUuid.IsEmpty()) return;

    // 若本端已注册则跳过（避免重复注册）
    if (bRegistered) return;

    UGameInstance* GI = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
    if (!GI) return;
    if (UAudioStreamHttpWsSubsystem* Subsys = GI->GetSubsystem<UAudioStreamHttpWsSubsystem>())
    {
        FString Dummy;
        if (Subsys->RegisterComponentWithUuid(this, RegisteredUuid, Dummy))
        {
            bRegistered = true;
            FCoreLogHelpers::CoreLog(this, ECoreLogSeverity::Info, TEXT("音频流组件"), TEXT("客户端注册"), FString::Printf(TEXT("Client register via RepNotify (%s) uuid=%s"), Role, *RegisteredUuid));
        }
        else
        {
            FCoreLogHelpers::CoreLog(this, ECoreLogSeverity::Error, TEXT("音频流组件"), TEXT("客户端注册"), FString::Printf(TEXT("Client register via RepNotify FAILED (%s) uuid=%s"), Role, *RegisteredUuid));
        }
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
        VisemeStepsEmitted = 0;
        ResetAudioPacketQueue();
        StreamAudioReturnSequence = 0;
        VisemeQueue.Empty();
        VisemeConfQueue.Empty();
        VisemeStepsEmitted = 0;
        EnsureAndZeroVisemeArray();
        // 重置播放头
        VisemePlayheadSec = 0.0;

        // 安全地重建 SoundStream
        if (AudioPlayer)
        {
            AudioPlayer->Stop();
            AudioPlayer->SetSound(nullptr);
        }

        SoundStream = NewObject<USoundWaveProcedural>(this);
        SoundStream->SetSampleRate(ActiveWsSampleRate);
        SoundStream->NumChannels = ActiveWsChannels;
        SoundStream->Duration =  INDEFINITELY_LOOPING_DURATION; // Use indefinite duration to avoid frame count assertions
        SoundStream->bLooping = false;
        SoundStream->bProcedural = true;

        if (AudioPlayer)
        {
            AudioPlayer->SetSound(SoundStream);
        }

        FCoreLogHelpers::CoreLog(this, ECoreLogSeverity::Info, TEXT("流数据处理"), TEXT("播放控制"), TEXT("Stream reset executed in Tick"));
    }

    ProcessPendingWebSocketMessages(MaxPendingWebSocketMessagesPerTick);

    // 当处于播放状态时，按 DeltaTime 推进 viseme 播放头
    LastDeltaTime = DeltaTime;
    if (bIsPlaying && AudioPlayer && AudioPlayer->IsPlaying())
    {
        VisemePlayheadSec += FMath::Max(0.f, DeltaTime);
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
        SoundStream->Duration =  INDEFINITELY_LOOPING_DURATION; // Use indefinite duration to avoid frame count assertions
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

        const float Duration = GetBufferedAudioDurationSeconds();

        // Condition: Packet count threshold OR Duration threshold
        // If packets are large, Duration threshold will trigger first.
        // If packets are small, Packet count threshold might trigger first.
        // We use a small minimum duration to avoid starting with just a tiny blip.
        const float MinStartDurationLocal = FMath::Max(0.0f, MinStartDuration);

        bool bCanStart = (AudioPacketQueue.Num() >= JitterBufferThreshold) || (Duration >= TargetBufferedTime) || (Duration >= MinStartDurationLocal && AudioPacketQueue.Num() >= 1);

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

            // Delayed Play() after feeding data to avoid assertion failure (NumTotalFrames > 0)
            FCoreLogHelpers::CoreLog(this, ECoreLogSeverity::Info, TEXT("音频流组件"), TEXT("播放控制"), FString::Printf(TEXT("Buffering complete: buffered %d pkts, %.2fs. Will Play after feed."), AudioPacketQueue.Num(), Duration));
        }
        else
        {
            // Buffering...
            return;
        }
    }

    // 2. Feed data
    bool bDataFed = false;
    int32 FedBytes = 0; // move out for viseme timing
    if (bIsPlaying && AudioPacketQueue.Num() > 0)
    {
        int32 FedPackets = 0;
        int32 PacketsToRemove = 0;
        int32 RemovedBytes = 0;

        while (PacketsToRemove < AudioPacketQueue.Num() && FedPackets < MaxAudioPacketsToFeedPerTick)
        {
            FAudioPacketBuffer& Pkt = AudioPacketQueue[PacketsToRemove];
            if (FedPackets > 0 && (FedBytes + Pkt.Data.Num()) > MaxAudioBytesToFeedPerTick)
            {
                break;
            }

            if (Pkt.Data.Num() > 0)
            {
                // 动态淡入逻辑
                const int32 BufferedBytes = SoundStream->GetAvailableAudioByteCount();
                if (BufferedBytes <= 0 || bForceNextFadeIn) 
                {
                    FCoreLogHelpers::CoreLog(this, ECoreLogSeverity::Trace, TEXT("音频流组件"), TEXT("Buf"), FString::Printf(TEXT("Applying Fade-In (Buf=%d Force=%d)"), BufferedBytes, bForceNextFadeIn?1:0));
                    
                    bForceNextFadeIn = false; // Reset force flag

                    int16* Samples = reinterpret_cast<int16*>(Pkt.Data.GetData());
                    const int32 SampleCount = Pkt.Data.Num() / sizeof(int16);
                    const int32 FadeSamples = FMath::Min(SampleCount, (int32)(ActiveWsSampleRate * 0.1f)); 
                    
                    for (int32 i = 0; i < FadeSamples; ++i)
                    {
                        float FadeAlpha = (float)i / (float)FadeSamples;
                        FadeAlpha = FadeAlpha * FadeAlpha * (3.0f - 2.0f * FadeAlpha); 
                        Samples[i] = (int16)(Samples[i] * FadeAlpha);
                    }
                }

                SoundStream->QueueAudio(Pkt.Data.GetData(), Pkt.Data.Num());
                FedBytes += Pkt.Data.Num();
                FedPackets++;
                LastPlayedSeq = Pkt.Seq;
            }

            RemovedBytes += Pkt.Data.Num();
            ++PacketsToRemove;
        }

        if (PacketsToRemove > 0)
        {
            AudioPacketQueue.RemoveAt(0, PacketsToRemove, EAllowShrinking::No);
            QueuedAudioBytes = FMath::Max(0, QueuedAudioBytes - RemovedBytes);
        }

        if (FedBytes > 0)
        {
             const int32 BytesPerSample = sizeof(int16);
             const int32 NumSamples = FedBytes / (ActiveWsChannels * BytesPerSample);
             const float AddedDuration = (float)NumSamples / (float)ActiveWsSampleRate;
             TotalAudioFedDuration += AddedDuration;
             bDataFed = true;
        }
    }

    // 2.5 Consume Visemes according to TotalAudioFedDuration and VisemeStepMs
    if (bIsPlaying && (VisemeQueue.Num() > 0))
    {
        const float StepSec = FMath::Max(1, VisemeStepMs) / 1000.f;
        // 使用播放头时间作为更稳定的对齐基准
        const float PlayedSec = (float)VisemePlayheadSec;

        const int32 ExpectedSteps = (int32)FMath::FloorToFloat(PlayedSec / StepSec);
        const int32 Remaining = FMath::Max(0, VisemeQueue.Num() - VisemeStepsEmitted);
        const int32 ToEmit = FMath::Clamp(ExpectedSteps - VisemeStepsEmitted, 0, Remaining);
        if (ToEmit > 0)
        {
            FCoreLogHelpers::CoreLog(this, ECoreLogSeverity::Trace, TEXT("Viseme"), TEXT("Emit"), FString::Printf(TEXT("PlayedSec=%.3f StepSec=%.3f Expect=%d Emitting=%d Queue=%d Remaining=%d Emitted=%d"), PlayedSec, StepSec, ExpectedSteps, ToEmit, VisemeQueue.Num(), Remaining, VisemeStepsEmitted));
        }
        for (int32 i = 0; i < ToEmit; ++i)
        {
            const int32 Index = VisemeStepsEmitted + i;
            const int32 V = VisemeQueue[Index];
            const float C = (Index < VisemeConfQueue.Num()) ? VisemeConfQueue[Index] : 1.f;
            EnsureAndZeroVisemeArray();
            const int32 ClampedIndex = FMath::Clamp(V, 0, VisemeArraySize - 1);
            CurrentVisemeArray[ClampedIndex] = C;
            bVisemeArrayDirty = true;
            OnViseme(V, C);
        }
        if (bVisemeArrayDirty)
        {
            bVisemeArrayDirty = false;
            OnVisemeArrayUpdated.Broadcast();
        }
        VisemeStepsEmitted += ToEmit;
        if (VisemeStepsEmitted >= VisemeQueue.Num())
        {
            VisemeQueue.Empty();
            VisemeConfQueue.Empty();
            VisemeStepsEmitted = 0;
            EnsureAndZeroVisemeArray();
            OnVisemeArrayUpdated.Broadcast();
            // 口型队列消费完后重置播放头，避免下次从旧时间继续累加
            VisemePlayheadSec = 0.0;
        }
        else if (VisemeStepsEmitted > 0 && VisemeStepsEmitted <= VisemeQueue.Num())
        {
            const int32 CleanupThreshold = 256; // arbitrary chunk cleanup
            if (VisemeStepsEmitted >= CleanupThreshold)
            {
                VisemeQueue.RemoveAt(0, VisemeStepsEmitted);
                if (VisemeConfQueue.Num() >= VisemeStepsEmitted)
                {
                    VisemeConfQueue.RemoveAt(0, VisemeStepsEmitted);
                }
                VisemeStepsEmitted = 0; // reset counter after compaction
                // 压缩后从0重新计时，避免累计误差
                VisemePlayheadSec = 0.0;
            }
        }
    }

    // 3. Start Playback if needed
    if (bIsPlaying && !AudioPlayer->IsPlaying())
    {
        if (bDataFed)
        {
            AudioPlayer->Play();
            FCoreLogHelpers::CoreLog(this, ECoreLogSeverity::Info, TEXT("音频流组件"), TEXT("播放控制"), TEXT("AudioPlayer::Play() called after feeding data"));
        }
    }
}

const TArray<float>& UAudioStreamHttpWsComponent::GetCurrentVisemeArray()
{
    if (CurrentVisemeArray.Num() != VisemeArraySize)
    {
        CurrentVisemeArray.SetNum(VisemeArraySize);
        for (int32 i=0;i<VisemeArraySize;++i) CurrentVisemeArray[i] = 0.f;
    }
    return CurrentVisemeArray;
}

void UAudioStreamHttpWsComponent::EnsureAndZeroVisemeArray(int32 Size)
{
    if (CurrentVisemeArray.Num() != Size)
    {
        CurrentVisemeArray.SetNum(Size);
    }
    for (int32 i=0;i<CurrentVisemeArray.Num();++i) CurrentVisemeArray[i] = 0.f;
}
