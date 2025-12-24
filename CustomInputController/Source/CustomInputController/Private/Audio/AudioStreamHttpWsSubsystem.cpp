#include "Audio/AudioStreamHttpWsSubsystem.h"
#include "Audio/AudioStreamHttpWsComponent.h"
// #include "Input/UUDPHandler.h" // UDP removed for now
// #include "Audio/MediaStreamPacket.h"

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

#include "Engine/NetConnection.h"
#include "Engine/NetDriver.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"

#include "Sockets.h"
#include "SocketSubsystem.h"

// CoreManager logging
#include "Log/CoreLogSubsystem.h"
#include "Log/CoreLogTypes.h"
#include "Log/CoreLogHelpers.h"

#include "HttpServerModule.h"
#include "HttpServerResponse.h"
#include "HttpServerRequest.h"
#include "IHttpRouter.h"
#include "HttpPath.h"

// 进程级：最近一次成功HELLO的服务器IP（用于换房间/跨服务器时触发重新注册）
static FString GLastHelloServerIp;


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
    
}

void UAudioStreamHttpWsSubsystem::Deinitialize()
{
    UE_LOG(LogTemp, Log, TEXT("[AudioStream] Deinitialize begin"));
    FCoreLogHelpers::CoreLog(this, ECoreLogSeverity::Warn, TEXT("音频流子系统"), TEXT("开始销毁"), TEXT("Deinitialize begin"));

    UuidComponentMap.Empty();
    Super::Deinitialize();
    UE_LOG(LogTemp, Log, TEXT("[AudioStream] Deinitialize end"));
    FCoreLogHelpers::CoreLog(this, ECoreLogSeverity::Warn, TEXT("音频流子系统"), TEXT("销毁成功"), TEXT("Deinitialize end"));
}



bool UAudioStreamHttpWsSubsystem::RegisterComponent(UAudioStreamHttpWsComponent* Comp, FString& OutUuid)
{
    if (!IsValid(Comp)) return false;

    // 生成 UUID（使用 FGuid）
    FGuid G = FGuid::NewGuid();
    FString Uuid = G.ToString(EGuidFormats::DigitsWithHyphens);
    bool bFirst = false;
    {
        FScopeLock Lock(&UuidMapCS);
        // 确保 UUID 唯一（理论上 NewGuid 已足够，但额外检查以防)
        while (UuidComponentMap.Contains(Uuid))
        {
            FGuid NG = FGuid::NewGuid();
            Uuid = NG.ToString(EGuidFormats::DigitsWithHyphens);
        }
        UuidComponentMap.Add(Uuid, Comp);
        bFirst = (UuidComponentMap.Num() == 1);
    }

    OutUuid = Uuid;
    FCoreLogHelpers::CoreLog(this, ECoreLogSeverity::Info, TEXT("流组件注册"), TEXT("组件注册"), FString::Printf(TEXT("Component registered uuid=%s total=%d"), *Uuid, UuidComponentMap.Num()));

    // If this is the first component, log auto-start suggestion (networking now handled per-component)
    if (bFirst)
    {
        const UAudioStreamSettings* S = GetDefault<UAudioStreamSettings>();
        const FString Host = S ? S->DefaultWsHost : TEXT("127.0.0.1:8001");
        const bool bHttps = (S && S->DefaultWsScheme.Equals(TEXT("wss"), ESearchCase::IgnoreCase));
        const int32 SR = S ? S->DefaultSampleRate : 16000;
        const int32 CH = S ? S->DefaultChannels : 1;
        const FString RunPath = TEXT("/run");
        const FString WsPrefix = S ? S->DefaultWsPathPrefix : TEXT("/ws/");
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

static FNivaHttpResponse MakePlainResponse(int32 StatusCode, const FString& Body)
{
    FNivaHttpResponse R;
    // Fill response code
    R.HttpServerResponse.Code = static_cast<EHttpServerResponseCodes>(StatusCode);
    // Convert Body (FString) to UTF-8 bytes for Body array
    FTCHARToUTF8 Utf8(*Body);
    const uint8* Bytes = reinterpret_cast<const uint8*>(Utf8.Get());
    R.HttpServerResponse.Body.Empty();
    if (Utf8.Length() > 0)
    {
        R.HttpServerResponse.Body.Append(Bytes, Utf8.Length());
    }
    // Headers: FHttpServerResponse expects TMap<FString,TArray<FString>>
    R.HttpServerResponse.Headers.Add(TEXT("Content-Type"), TArray<FString>{ TEXT("text/plain; charset=utf-8") });
    return R;
}




// ======= Client register / auto =======

void UAudioStreamHttpWsSubsystem::ClientRegisterToServer(const FString& ServerIp)
{
    // 记忆最后成功的服务器IP
    GLastHelloServerIp = ServerIp;
    LastHelloServerIp = ServerIp;
    FCoreLogHelpers::CoreLog(this, ECoreLogSeverity::Info, TEXT("流组件注册"), TEXT("组件成功注册"), FString::Printf(TEXT("Registered to server %s"), *ServerIp));
}

void UAudioStreamHttpWsSubsystem::AutoRegisterClient()
{
    if (IsServer())
    {
        FCoreLogHelpers::CoreLog(this, ECoreLogSeverity::Warn, TEXT("子系统注册"), TEXT("自动注册跳过"), TEXT("AutoRegisterClient called on server; skipping"));
        return;
    }

    if (!GLastHelloServerIp.IsEmpty())
    {
        ClientRegisterToServer(GLastHelloServerIp);
        return;
    }

    // 简化：尝试从NetDriver获取服务器地址
    UWorld* W = GetWorld(); if (!W) return;
    if (UNetDriver* ND = W->GetNetDriver())
    {
        if (ND->ServerConnection && ND->ServerConnection->LowLevelGetRemoteAddress(true) != TEXT(""))
        {
            FString Remote = ND->ServerConnection->LowLevelGetRemoteAddress(true);
            // 解析出 IP
            FString Ip, Port;
            if (Remote.Split(TEXT(":"), &Ip, &Port))
            {
                ClientRegisterToServer(Ip);
                return;
            }
        }
    }

    FCoreLogHelpers::CoreLog(this, ECoreLogSeverity::Warn, TEXT("子系统注册"), TEXT("自动注册失败"), TEXT("AutoRegisterClient could not determine server IP"));
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
    MediaUdpPort = S->MediaUdpPort;
    FrameDurationMs = S->FrameDurationMs;
    TargetPreRollMs = S->TargetPreRollMs;
    TargetJitterMs = S->TargetJitterMs;
    VisemeStepMs = S->VisemeStepMs;
    VisemeKeyframeIntervalMs = S->VisemeKeyframeIntervalMs;
    HeartbeatIntervalMs = S->HeartbeatIntervalMs;
    OffsetLerpAlpha = S->OffsetLerpAlpha;
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

