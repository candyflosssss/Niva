#include "AudioStreamHttpWsComponent.h"
#include "AudioStreamHttpWsSubsystem.h"

#include "Engine/GameInstance.h"
#include "GameFramework/Actor.h"
#include "StreamProcSoundWave.h" // 改为包含自定义派生类
#include "Components/AudioComponent.h"
#include "TimerManager.h"
#include "Async/Async.h"
#include "AudioThread.h"

UAudioStreamHttpWsComponent::UAudioStreamHttpWsComponent()
{
    PrimaryComponentTick.bCanEverTick = true; // 启用Tick用于按播放进度出队
    PrimaryComponentTick.bStartWithTickEnabled = true;
    SampleRate = DefaultSampleRate;
    NumChannels = DefaultChannels;
    // 固定长度为15
    VisemeFloat14.Init(0.f, 15);
    UpdateTimingParams();
}

void UAudioStreamHttpWsComponent::BeginPlay()
{
    Super::BeginPlay();
    SampleRate = DefaultSampleRate;
    NumChannels = DefaultChannels;
    UpdateTimingParams();
    RegisterToSubsystem();

    // Start the viseme auto-pop timer if enabled
    if (bAutoPopViseme)
    {
        StartVisemeAutoPop();
    }
}

void UAudioStreamHttpWsComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    StopVisemeAutoPop();
    StopStreaming();
    UnregisterFromSubsystem();
    Super::EndPlay(EndPlayReason);
}

void UAudioStreamHttpWsComponent::StopStreaming()
{
    ResetAudio();
    TotalQueuedBytes = 0;
    LastConsumedBytes = 0;
    AccumConsumedForVisemeBytes = 0;
    ConsumedBaseBytes = 0;
    LastRawConsumedBytes = 0;
    LastSmoothedConsumedBytes = 0;
    SynthConsumedBytes = 0;
    LastConsumeProgressTimeSec = 0.0;
    bPlayStarted = false;
    SetNeutralVisemeFloat();
    if (ProcSound)
    {
        ProcSound->ResetCounters();
    }
}

void UAudioStreamHttpWsComponent::PushPcmData(const TArray<uint8>& Data, int32 InSampleRate, int32 InChannels)
{
    // 若收到与现有不同的格式，根据策略决定挂起或立即切换
    const bool bIncomingHasFormat = (InSampleRate > 0 && InChannels > 0);
    const bool bFormatDiffers = bIncomingHasFormat && (InSampleRate != SampleRate || InChannels != NumChannels);
    
    if (bFormatDiffers)
    {
        // 若已开播或缓冲高于格式切换低水位，则挂起新格式并延迟切换
        const float BufMs = GetBufferedMilliseconds();
        if (bPlayStarted || BufMs > FormatSwitchLowWaterMs)
        {
            bHasPendingFormatChange = true;
            PendingSampleRate = InSampleRate;
            PendingChannels = InChannels;
            // 直接累加挂起字节，实际对齐在切换时执行
            PendingPcmBuffer.Append(Data);
            UE_LOG(LogTemp, Log, TEXT("[AudioStream] Pending format SR=%d CH=%d, queued %d bytes (buffer=%.1fms)"), InSampleRate, InChannels, PendingPcmBuffer.Num(), BufMs);
            return;
        }
        // 否则允许立即切换
    }
    
    bool bParamsChanged = false;
    if (bIncomingHasFormat && InSampleRate != SampleRate)
    {
        SampleRate = InSampleRate;
        bParamsChanged = true;
    }
    if (bIncomingHasFormat && InChannels != NumChannels)
    {
        NumChannels = InChannels;
        bParamsChanged = true;
    }
    
    EnsureAudioObjects();
    
    if (ProcSound && bParamsChanged)
    {
        ProcSound->SetParams(SampleRate, NumChannels);
        UpdateTimingParams();
        // 采样参数变化，重置消费估计以避免跳变
        LastConsumedBytes = 0;
        AccumConsumedForVisemeBytes = 0;
        TotalQueuedBytes = 0; // 重新累计，避免估计偏差
        ConsumedBaseBytes = 0;
        LastRawConsumedBytes = 0;
        LastSmoothedConsumedBytes = 0;
        SynthConsumedBytes = 0;
        LastConsumeProgressTimeSec = 0.0;
        ProcSound->ResetCounters();
    }
    
    // 计算当前BytesPerSec（用于统计毫秒）
    const int64 StatBytesPerSec = (int64)FMath::Max(1, SampleRate) * (int64)FMath::Max(1, NumChannels) * 2; // S16LE

    if (ProcSound && Data.Num() > 0)
    {
        const int32 FrameBytes = FMath::Max(1, NumChannels) * 2; // S16LE 每样本2字节
        int32 AlignedBytes = Data.Num() - (Data.Num() % FrameBytes);
        if (AlignedBytes <= 0)
        {
            UE_LOG(LogTemp, Warning, TEXT("[AudioStream] Drop unaligned PCM chunk bytes=%d (ch=%d)"), Data.Num(), NumChannels);
        }
        else
        {
            if (AlignedBytes != Data.Num())
            {
                UE_LOG(LogTemp, Verbose, TEXT("[AudioStream] Trim PCM %d->%d to align frames"), Data.Num(), AlignedBytes);
            }
            // 将要入队的数据拷入临时数组并投递到音频线程，避免与渲染线程锁竞争
            TArray<uint8> Bytes; Bytes.Append(Data.GetData(), AlignedBytes);
            QueueAudioOnAudioThread(MoveTemp(Bytes));
            UE_LOG(LogTemp, Verbose, TEXT("[AudioStream] Queued PCM bytes=%d (ch=%d sr=%d)"), AlignedBytes, NumChannels, SampleRate);
            TotalQueuedBytes += (int64)AlignedBytes;
            // 统计：接收音频毫秒（不含补零）
            TotalAudioMsReceived += (double)AlignedBytes * 1000.0 / (double)StatBytesPerSec;
        }
    }

    // 仅当累计达到预热阈值才开始播放，降低起始噪音
    if (AudioComp && !AudioComp->IsPlaying())
    {
        if (TotalQueuedBytes >= WarmupBytes)
        {
            AudioComp->Play();
            bPlayStarted = true;
            // 若启用了定时出队，开始播放后就停用定时器，避免与按进度出队冲突
            if (bAutoPopViseme && GetWorld())
            {
                GetWorld()->GetTimerManager().ClearTimer(VisemePopTimer);
            }
            // 初始化合成消费基线
            LastSmoothedConsumedBytes = GetSmoothedConsumedBytes();
            SynthConsumedBytes = LastSmoothedConsumedBytes;
            LastConsumeProgressTimeSec = FPlatformTime::Seconds();
        }
        else
        {
            // 未开播保持中性嘴型
            SetNeutralVisemeFloat();
        }
    }
}

void UAudioStreamHttpWsComponent::EnsureAudioObjects()
{
    if (!ProcSound)
    {
        ProcSound = NewObject<UStreamProcSoundWave>(this);
        ProcSound->bLooping = false;
        ProcSound->bProcedural = true;
        ProcSound->SetParams(SampleRate, NumChannels);
        ProcSound->SoundGroup = SOUNDGROUP_Voice;
    }

    if (!AudioComp)
    {
        AActor* Owner = GetOwner();
        UObject* Outer = Owner ? (UObject*)Owner : (UObject*)this;
        AudioComp = NewObject<UAudioComponent>(Outer);
        AudioComp->bAutoActivate = false;
        if (Owner && Owner->GetRootComponent())
        {
            AudioComp->AttachToComponent(Owner->GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
        }
        AudioComp->RegisterComponent();
    }

    ProcSound->SetParams(SampleRate, NumChannels);
    if (AudioComp->Sound != ProcSound)
    {
        AudioComp->SetSound(ProcSound);
    }
}

void UAudioStreamHttpWsComponent::ResetAudio()
{
    if (AudioComp)
    {
        AudioComp->Stop();
        AudioComp->DestroyComponent();
        AudioComp = nullptr;
    }
    ProcSound = nullptr;
}

void UAudioStreamHttpWsComponent::RegisterToSubsystem()
{
    if (bRegistered)
    {
        return;
    }
    UGameInstance* GI = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
    if (!GI)
    {
        return;
    }
    UAudioStreamHttpWsSubsystem* Subsys = GI->GetSubsystem<UAudioStreamHttpWsSubsystem>();
    if (!Subsys)
    {
        return;
    }

    FString OutKey;
    if (Subsys->RegisterComponent(this, OutKey, PreferredKey))
    {
        RegisteredKey = OutKey;
        bRegistered = true;
        UE_LOG(LogTemp, Log, TEXT("AudioStream component registered key: %s"), *RegisteredKey);
    }
}

void UAudioStreamHttpWsComponent::UnregisterFromSubsystem()
{
    if (!bRegistered)
    {
        return;
    }
    UGameInstance* GI = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
    if (!GI)
    {
        return;
    }
    UAudioStreamHttpWsSubsystem* Subsys = GI->GetSubsystem<UAudioStreamHttpWsSubsystem>();
    if (!Subsys)
    {
        return;
    }

    Subsys->UnregisterComponent(this);
    bRegistered = false;
    RegisteredKey.Reset();
}

void UAudioStreamHttpWsComponent::PushText(const FString& InText)
{
    TextHistory.Add(InText);
    UE_LOG(LogTemp, Log, TEXT("[AudioStream] Text[%s] len=%d -> %s"), *RegisteredKey, InText.Len(), *InText.Left(64));
    OnTextReceived.Broadcast(InText);
}

void UAudioStreamHttpWsComponent::PushVisemeConfidence(const TArray<float>& InConfidence)
{
    // 兼容旧流程：仅缓存；真正入队请使用 PushVisemeEx
    LastVisemeConfidence = InConfidence;
}

void UAudioStreamHttpWsComponent::PushViseme(const TArray<int32>& InViseme)
{
    if (InViseme.Num() <= 0) return;

    // 兼容：原有仅索引的接口。将索引入旧历史用于外部调试；同时将 (idx, weight) 入配对队列
    VisemeHistory.Append(InViseme);

    int32 CountAdded = 0;
    for (int32 i = 0; i < InViseme.Num(); ++i)
    {
        const int32 idx = FMath::Clamp(InViseme[i], 0, 14);
        float Weight = 1.0f;
        if (LastVisemeConfidence.IsValidIndex(idx))
        {
            Weight = FMath::Clamp(LastVisemeConfidence[idx], 0.0f, 1.0f);
        }
        VisemePairQueue.Add(TPair<int32, float>(idx, Weight));
        ++CountAdded;
    }

    // 统计：接收的viseme步数
    TotalVisemeStepsReceived += CountAdded;
    UpdateVisemeFloatFromHead();

    if (bDebugLogs)
    {
        FString Debug;
        for (int32 i = 0; i < InViseme.Num() && i < 16; ++i) { Debug += FString::Printf(TEXT("%d,"), InViseme[i]); }
        UE_LOG(LogTemp, Log, TEXT("[AudioStream] Viseme[%s] appended %d (pairQ=%d) -> [%s...]"), *RegisteredKey, CountAdded, VisemePairQueue.Num(), *Debug);
    }

    OnVisemeReceived.Broadcast(InViseme);
}

void UAudioStreamHttpWsComponent::PushVisemeEx(const TArray<int32>& InViseme, const TArray<float>& InConfidence)
{
    const int32 N = InViseme.Num();
    if (N <= 0)
    {
        return;
    }

    // 对外保留旧历史索引
    VisemeHistory.Append(InViseme);

    int32 Pushed = 0;
    if (InConfidence.Num() == N)
    {
        // 情况A：逐步权重一一对应
        for (int32 i = 0; i < N; ++i)
        {
            const int32 idx = FMath::Clamp(InViseme[i], 0, 14);
            const float w = FMath::Clamp(InConfidence[i], 0.0f, 1.0f);
            VisemePairQueue.Add(TPair<int32, float>(idx, w));
            ++Pushed;
        }
    }
    else if (InConfidence.Num() == VisemeFloatCount)
    {
        // 情况B：confidence 为按标签的权重表（长度=15），每步取对应索引权重
        for (int32 i = 0; i < N; ++i)
        {
            const int32 idx = FMath::Clamp(InViseme[i], 0, VisemeFloatCount - 1);
            const float w = FMath::Clamp(InConfidence[idx], 0.0f, 1.0f);
            VisemePairQueue.Add(TPair<int32, float>(idx, w));
            ++Pushed;
        }
    }
    else
    {
        // 情况C：无权重或长度不匹配，回退为1.0
        for (int32 i = 0; i < N; ++i)
        {
            const int32 idx = FMath::Clamp(InViseme[i], 0, 14);
            VisemePairQueue.Add(TPair<int32, float>(idx, 1.0f));
            ++Pushed;
        }
    }

    TotalVisemeStepsReceived += Pushed;
    UpdateVisemeFloatFromHead();

    if (bDebugLogs)
    {
        UE_LOG(LogTemp, Log, TEXT("[AudioStream] VisemeEx[%s] appended %d (pairQ=%d, confN=%d)"), *RegisteredKey, Pushed, VisemePairQueue.Num(), InConfidence.Num());
    }

    OnVisemeReceived.Broadcast(InViseme);
}

void UAudioStreamHttpWsComponent::StartVisemeAutoPop()
{
    if (!GetWorld()) return;
    const float Secs = FMath::Max(0.001f, VisemeStepMs / 1000.0f);
    GetWorld()->GetTimerManager().SetTimer(VisemePopTimer, this, &UAudioStreamHttpWsComponent::VisemePopTick, Secs, true);
}

void UAudioStreamHttpWsComponent::StopVisemeAutoPop()
{
    if (!GetWorld()) return;
    GetWorld()->GetTimerManager().ClearTimer(VisemePopTimer);
}

void UAudioStreamHttpWsComponent::VisemePopTick()
{
    // 若已开始播放，改由按音频进度出队，这里不处理
    if (bPlayStarted)
    {
        return;
    }
    if (VisemeHistory.Num() > 0)
    {
        VisemeHistory.RemoveAt(0);
    }
    if (VisemePairQueue.Num() > 0)
    {
        VisemePairQueue.RemoveAt(0);
    }
    UpdateVisemeFloatFromHead();
}

void UAudioStreamHttpWsComponent::UpdateVisemeFloatFromHead()
{
    // 固定长度为15
    const int32 Count = 15;
    if (VisemeFloat14.Num() != Count) VisemeFloat14.Init(0.f, Count);
    
    // 清零所有位
    for (int32 i = 0; i < VisemeFloat14.Num(); ++i) VisemeFloat14[i] = 0.f;
    
    if (!bPlayStarted || VisemePairQueue.Num() == 0)
    {
        SetNeutralVisemeFloat();
        if (bDebugLogs)
        {
            UE_LOG(LogTemp, Log, TEXT("[AudioStream][%s] Viseme neutral (playStarted=%d, pairQ=%d)"), *RegisteredKey, bPlayStarted, VisemePairQueue.Num());
        }
        return;
    }
    
    // 读取配对队首 (idx, weight)
    const int32 MaxIndex = Count - 1; // 0..14
    const int32 idx = FMath::Clamp(VisemePairQueue[0].Key, 0, MaxIndex);
    const float Weight = FMath::Clamp(VisemePairQueue[0].Value, 0.0f, 1.0f);
    VisemeFloat14[idx] = Weight;
    
    if (bDebugLogs)
    {
        UE_LOG(LogTemp, Verbose, TEXT("[AudioStream][%s] Viseme active: idx=%d weight=%.3f (pairQ=%d)"), *RegisteredKey, idx, Weight, VisemePairQueue.Num());
    }
}

void UAudioStreamHttpWsComponent::SetNeutralVisemeFloat()
{
    // 固定长度为15
    const int32 Count = 15;
    if (VisemeFloat14.Num() != Count) VisemeFloat14.Init(0.f, Count);
    for (int32 i = 0; i < VisemeFloat14.Num(); ++i) VisemeFloat14[i] = 0.f;
    const int32 MaxIndex = Count - 1; // 0..14
    const int32 idx = FMath::Clamp(NeutralVisemeIndex, 0, MaxIndex);
    VisemeFloat14[idx] = 1.f;
}

void UAudioStreamHttpWsComponent::UpdateTimingParams()
{
    const int64 BytesPerSec = (int64)FMath::Max(1, SampleRate) * (int64)FMath::Max(1, NumChannels) * 2; // S16
    BytesPerStep = (int32)FMath::Max<int64>(1, (int64)((double)BytesPerSec * (VisemeStepMs / 1000.0)));
    WarmupBytes   = (int64)FMath::Max<int64>(0,   (int64)((double)BytesPerSec * (WarmupMs   / 1000.0)));
    UE_LOG(LogTemp, Log, TEXT("[AudioStream] Timing updated SR=%d Ch=%d -> BytesPerStep=%d WarmupBytes=%lld (stepMs=%.2f, warmMs=%.2f)"), SampleRate, NumChannels, BytesPerStep, (long long)WarmupBytes, VisemeStepMs, WarmupMs);
}

float UAudioStreamHttpWsComponent::GetBufferedMilliseconds()
{
    const int64 BytesPerSec = (int64)FMath::Max(1, SampleRate) * (int64)FMath::Max(1, NumChannels) * 2;
    const int64 Consumed = GetSmoothedConsumedBytes();
    const int64 InBuffer = FMath::Max<int64>(0, TotalQueuedBytes - Consumed);
    const double Ms = (BytesPerSec > 0) ? (double)InBuffer * 1000.0 / (double)BytesPerSec : 0.0;
    return (float)Ms;
}

// 平滑累计：将渲染层读取到的原始 ConsumedBytes 聚合成单调不减的总量
int64 UAudioStreamHttpWsComponent::GetSmoothedConsumedBytes()
{
    if (!ProcSound) return 0;
    const int64 Raw = ProcSound->GetConsumedBytes();
    // 若原始值较上次变小，认为底层做了复位：把上次的原始累计并入基线
    if (Raw < LastRawConsumedBytes)
    {
        ConsumedBaseBytes += LastRawConsumedBytes;
    }
    LastRawConsumedBytes = Raw;
    return ConsumedBaseBytes + Raw;
}

void UAudioStreamHttpWsComponent::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    if (!ProcSound)
    {
        // 打印中性或空状态日志（可选）
        return;
    }

    // 读取平滑后的已消费字节
    const int64 Smoothed = GetSmoothedConsumedBytes();
    const double NowSec = FPlatformTime::Seconds();

    // 若真实进度前进，刷新基线与时间戳；否则用 DeltaTime 合成推进
    if (Smoothed > LastSmoothedConsumedBytes)
    {
        SynthConsumedBytes = Smoothed;
        LastConsumeProgressTimeSec = NowSec;
    }
    else if (bPlayStarted)
    {
        const int64 BytesPerSec = (int64)FMath::Max(1, SampleRate) * (int64)FMath::Max(1, NumChannels) * 2; // S16
        const int64 Add = (int64)((double)BytesPerSec * FMath::Max(0.f, DeltaTime));
        const int64 MaxAllowed = TotalQueuedBytes; // 不超过已入队
        SynthConsumedBytes = FMath::Min<int64>(SynthConsumedBytes + Add, MaxAllowed);
    }
    LastSmoothedConsumedBytes = Smoothed;

    // 估算消费采用 max(真实平滑, 合成)
    int64 EstimatedConsumed = FMath::Max<int64>(Smoothed, SynthConsumedBytes);
    EstimatedConsumed = FMath::Min<int64>(EstimatedConsumed, TotalQueuedBytes);

    // 缓冲状态调试（每2秒打印一次）
    if (bDebugLogs)
    {
        const double CurrentTime = NowSec;
        if (CurrentTime - LastBufferedLogTimeSec > 2.0)
        {
            const int64 BytesPerSec = (int64)FMath::Max(1, SampleRate) * (int64)FMath::Max(1, NumChannels) * 2;
            const int64 InBuffer = FMath::Max<int64>(0, TotalQueuedBytes - EstimatedConsumed);
            const double BufferMs = (BytesPerSec > 0) ? (double)InBuffer * 1000.0 / (double)BytesPerSec : 0.0;
            UE_LOG(LogTemp, Log, TEXT("[AudioStream][%s] Status: buffer=%.1fms, queued=%lld, consumed=%lld, playing=%d, visemeQueue=%d"),
                *RegisteredKey, BufferMs, (long long)TotalQueuedBytes, (long long)EstimatedConsumed, bPlayStarted, VisemeHistory.Num());
            LastBufferedLogTimeSec = CurrentTime;
        }
    }

    // 按音频消费出队
    if (bPopVisemeByAudioProgress && BytesPerStep > 0)
    {
        int64 Delta = EstimatedConsumed - LastConsumedBytes;
        if (Delta > 0)
        {
            AccumConsumedForVisemeBytes += Delta;
            int32 Steps = (int32)(AccumConsumedForVisemeBytes / BytesPerStep);
            if (Steps > 0)
            {
                for (int32 i = 0; i < Steps; ++i)
                {
                    if (VisemeHistory.Num() > 0) { VisemeHistory.RemoveAt(0); }
                    if (VisemePairQueue.Num() > 0) { VisemePairQueue.RemoveAt(0); }
                }
                AccumConsumedForVisemeBytes %= BytesPerStep;
                UpdateVisemeFloatFromHead();
            }
        }
        LastConsumedBytes = EstimatedConsumed;
    }

    // 若存在待切换的新格式，且缓冲低于低水位或尚未开播，则执行切换
    if (bHasPendingFormatChange)
    {
        const float BufMs = GetBufferedMilliseconds();
        if (!bPlayStarted || BufMs <= FormatSwitchLowWaterMs)
        {
            // 停止播放，切新参数，接管挂起缓冲
            if (AudioComp && AudioComp->IsPlaying())
            {
                AudioComp->Stop();
            }
            if (ProcSound)
            {
                ProcSound->ResetCounters();
                ProcSound->SetParams(PendingSampleRate, PendingChannels);
            }
            SampleRate = PendingSampleRate;
            NumChannels = PendingChannels;
            UpdateTimingParams();

            // 清空统计与已入队字节，重新以新格式累计
            TotalQueuedBytes = 0;
            LastConsumedBytes = 0;
            AccumConsumedForVisemeBytes = 0;
            ConsumedBaseBytes = 0;
            LastRawConsumedBytes = 0;
            LastSmoothedConsumedBytes = 0;
            SynthConsumedBytes = 0;
            LastConsumeProgressTimeSec = 0.0;

            // 对齐并入队挂起的PCM
            if (PendingPcmBuffer.Num() > 0)
            {
                const int32 FrameBytes = FMath::Max(1, NumChannels) * 2;
                int32 AlignedBytes = PendingPcmBuffer.Num() - (PendingPcmBuffer.Num() % FrameBytes);
                if (AlignedBytes > 0)
                {
                    TArray<uint8> Bytes; Bytes.Append(PendingPcmBuffer.GetData(), AlignedBytes);
                    QueueAudioOnAudioThread(MoveTemp(Bytes));
                    TotalQueuedBytes += (int64)AlignedBytes;
                    // 统计：将挂起PCM视为接收音频
                    const int64 StatBytesPerSec = (int64)FMath::Max(1, SampleRate) * (int64)FMath::Max(1, NumChannels) * 2;
                    TotalAudioMsReceived += (double)AlignedBytes * 1000.0 / (double)StatBytesPerSec;
                }
                PendingPcmBuffer.Reset();
            }

            // 重新判断播放/预热
            if (AudioComp)
            {
                if (TotalQueuedBytes >= WarmupBytes)
                {
                    AudioComp->Play();
                    bPlayStarted = true;
                    LastSmoothedConsumedBytes = GetSmoothedConsumedBytes();
                    SynthConsumedBytes = LastSmoothedConsumedBytes;
                    LastConsumeProgressTimeSec = FPlatformTime::Seconds();
                }
                else
                {
                    bPlayStarted = false;
                    SetNeutralVisemeFloat();
                }
            }

            bHasPendingFormatChange = false;
            if (bDebugLogs)
            {
                UE_LOG(LogTemp, Log, TEXT("[AudioStream][%s] Switched format to SR=%d CH=%d"), *RegisteredKey, SampleRate, NumChannels);
            }
        }
    }

    // 欠载保护：���缓冲过低，自动补零静音，避免 under-run 爆音
    if (bPadSilenceOnUnderflow && BytesPerStep > 0)
    {
        const int64 InBuffer = FMath::Max<int64>(0, TotalQueuedBytes - EstimatedConsumed);
        const int32 FrameBytes = FMath::Max(1, NumChannels) * 2;
        const int64 LowWaterBytes = (int64)UnderflowLowWaterSteps * (int64)BytesPerStep;
        if (InBuffer < LowWaterBytes)
        {
            int64 PadBytes = (int64)UnderflowPadSteps * (int64)BytesPerStep;
            // 对齐到样本帧，至少补一帧
            PadBytes -= (PadBytes % FrameBytes);
            if (PadBytes < FrameBytes) PadBytes = FrameBytes;
            TArray<uint8> Zeros; Zeros.AddZeroed((int32)PadBytes);
            QueueAudioOnAudioThread(MoveTemp(Zeros));
            TotalQueuedBytes += PadBytes;
            // 为保持A/V步数一致，补充等量中性viseme
            if (UnderflowPadSteps > 0)
            {
                // 固定索引范围为 0..14
                TArray<int32> NeutralBatch; NeutralBatch.Init(FMath::Clamp(NeutralVisemeIndex, 0, 14), UnderflowPadSteps);
                VisemeHistory.Append(NeutralBatch);
                for (int32 i = 0; i < UnderflowPadSteps; ++i)
                {
                    VisemePairQueue.Add(TPair<int32, float>(FMath::Clamp(NeutralVisemeIndex, 0, 14), 1.0f));
                }
                // 统计：补零与补viseme步数
                const int64 StatBytesPerSec = (int64)FMath::Max(1, SampleRate) * (int64)FMath::Max(1, NumChannels) * 2;
                TotalAudioMsPadded += (double)PadBytes * 1000.0 / (double)StatBytesPerSec;
                TotalVisemeStepsPadded += UnderflowPadSteps;
                // 清空置信度缓存（兼容旧流程）
                LastVisemeConfidence.Reset();
                if (!bPlayStarted) { SetNeutralVisemeFloat(); }
            }
            if (bDebugLogs)
            {
                UE_LOG(LogTemp, Warning, TEXT("[AudioStream][%s] Underflow pad %lld bytes (buffer=%lld, step=%d, lowSteps=%d, padSteps=%d)"), *RegisteredKey, (long long)PadBytes, (long long)InBuffer, BytesPerStep, UnderflowLowWaterSteps, UnderflowPadSteps);
            }
        }
    }

    // Viseme pop is handled by auto timer or audio progress
}

void UAudioStreamHttpWsComponent::ResetAudioVisemeStats()
{
    TotalAudioMsReceived = 0.0;
    TotalAudioMsPadded = 0.0;
    TotalVisemeStepsReceived = 0;
    TotalVisemeStepsPadded = 0;
}

FAudioVisemeStats UAudioStreamHttpWsComponent::GetAudioVisemeStats() const
{
    FAudioVisemeStats S;
    S.AudioMsReceived = TotalAudioMsReceived;
    S.AudioMsPadded = TotalAudioMsPadded;
    S.AudioMsTotal = TotalAudioMsReceived + TotalAudioMsPadded;
    S.VisemeStepsReceived = TotalVisemeStepsReceived;
    S.VisemeStepsPadded = TotalVisemeStepsPadded;
    S.VisemeStepsTotal = TotalVisemeStepsReceived + TotalVisemeStepsPadded;

    const double StepMs = (double)VisemeStepMs;
    S.VisemeMsReceived = (double)TotalVisemeStepsReceived * StepMs;
    S.VisemeMsPadded   = (double)TotalVisemeStepsPadded   * StepMs;
    S.VisemeMsTotal    = (double)S.VisemeStepsTotal       * StepMs;

    S.DeltaMsReceived = S.VisemeMsReceived - S.AudioMsReceived;
    S.DeltaMsTotal    = S.VisemeMsTotal    - S.AudioMsTotal;
    return S;
}

bool UAudioStreamHttpWsComponent::IsDurationConsistent(bool bUseTotal, float ToleranceMs, float& OutDeltaMs) const
{
    const double StepMs = (double)VisemeStepMs;
    const int32 Steps = bUseTotal ? (TotalVisemeStepsReceived + TotalVisemeStepsPadded) : TotalVisemeStepsReceived;
    const double VisemeMs = (double)Steps * StepMs;
    const double AudioMs  = bUseTotal ? (TotalAudioMsReceived + TotalAudioMsPadded) : TotalAudioMsReceived;
    const double Delta = VisemeMs - AudioMs; // >0 表示viseme更长
    OutDeltaMs = (float)Delta;
    return FMath::Abs(Delta) <= (double)ToleranceMs;
}

void UAudioStreamHttpWsComponent::QueueAudioOnAudioThread(TArray<uint8>&& Bytes)
{
    UStreamProcSoundWave* LocalProc = ProcSound;
    if (!LocalProc || Bytes.Num() == 0)
    {
        return;
    }
    
    // 投递到音频线程执行，避免与渲染回调在不同线程上竞争自管队列的锁
    TArray<uint8> LocalData = MoveTemp(Bytes);
    FAudioThread::RunCommandOnAudioThread([PS=LocalProc, Data=MoveTemp(LocalData)]() mutable
    {
        if (PS && Data.Num() > 0)
        {
            PS->EnqueuePcm(Data.GetData(), Data.Num());
        }
    });
}
