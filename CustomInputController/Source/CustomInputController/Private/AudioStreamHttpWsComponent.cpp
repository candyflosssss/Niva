#include "AudioStreamHttpWsComponent.h"
#include "AudioStreamHttpWsSubsystem.h"
#include "AudioStreamSettings.h"

#include "Engine/GameInstance.h"
#include "GameFramework/Actor.h"
#include "StreamProcSoundWave.h"
#include "Components/AudioComponent.h"
#include "TimerManager.h"
#include "Async/Async.h"
#include "AudioThread.h"

UAudioStreamHttpWsComponent::UAudioStreamHttpWsComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.bStartWithTickEnabled = true;

    if (const UAudioStreamSettings* S = GetDefault<UAudioStreamSettings>())
    {
        this->DefaultSampleRate = S->DefaultSampleRate;
        this->DefaultChannels = S->DefaultChannels;
        this->bPopVisemeByAudioProgress = S->bPopVisemeByAudioProgress;
        this->bAutoPopViseme = S->bAutoPopViseme;
        this->VisemeStepMs = (float)S->VisemeStepMs;
        this->WarmupMs = S->WarmupMs;
        this->bPadSilenceOnUnderflow = S->bPadSilenceOnUnderflow;
        this->UnderflowLowWaterSteps = S->UnderflowLowWaterSteps;
        this->UnderflowPadSteps = S->UnderflowPadSteps;
        this->NeutralVisemeIndex = S->NeutralVisemeIndex;
        this->bDebugLogs = S->bComponentDebugLogsDefault;
        this->VisemeFloatCount = S->VisemeFloatCount;
        this->FormatSwitchLowWaterMs = S->FormatSwitchLowWaterMs;
    }

    SampleRate = DefaultSampleRate;
    NumChannels = DefaultChannels;
    VisemeFloat14.Init(0.f, FMath::Clamp(VisemeFloatCount, 1, 64));
    UpdateTimingParams();
}

void UAudioStreamHttpWsComponent::BeginPlay()
{
    Super::BeginPlay();

    SampleRate = DefaultSampleRate;
    NumChannels = DefaultChannels;
    UpdateTimingParams();

    if (UWorld* W = GetWorld())
    {
        if (UGameInstance* GI = W->GetGameInstance())
        {
            if (UAudioStreamHttpWsSubsystem* Subsys = GI->GetSubsystem<UAudioStreamHttpWsSubsystem>())
            {
                Subsys->AutoRegisterClient();
            }
        }
    }

    RegisterToSubsystem();

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
    const bool bIncomingHasFormat = (InSampleRate > 0 && InChannels > 0);
    const bool bFormatDiffers = bIncomingHasFormat && (InSampleRate != SampleRate || InChannels != NumChannels);

    if (bFormatDiffers)
    {
        const float BufMs = GetBufferedMilliseconds();
        if (bPlayStarted || BufMs > FormatSwitchLowWaterMs)
        {
            bHasPendingFormatChange = true;
            PendingSampleRate = InSampleRate;
            PendingChannels = InChannels;
            PendingPcmBuffer.Append(Data);
            UE_LOG(LogTemp, Log, TEXT("[AudioStream] Pending format SR=%d CH=%d, queued %d bytes (buffer=%.1fms)"), InSampleRate, InChannels, PendingPcmBuffer.Num(), BufMs);
            return;
        }
    }

    bool bParamsChanged = false;
    if (bIncomingHasFormat && InSampleRate != SampleRate) { SampleRate = InSampleRate; bParamsChanged = true; }
    if (bIncomingHasFormat && InChannels   != NumChannels) { NumChannels = InChannels;   bParamsChanged = true; }

    EnsureAudioObjects();

    if (ProcSound && bParamsChanged)
    {
        ProcSound->SetParams(SampleRate, NumChannels);
        UpdateTimingParams();
        LastConsumedBytes = 0;
        AccumConsumedForVisemeBytes = 0;
        TotalQueuedBytes = 0;
        ConsumedBaseBytes = 0;
        LastRawConsumedBytes = 0;
        LastSmoothedConsumedBytes = 0;
        SynthConsumedBytes = 0;
        LastConsumeProgressTimeSec = 0.0;
        ProcSound->ResetCounters();
    }

    const int64 StatBytesPerSec = (int64)FMath::Max(1, SampleRate) * (int64)FMath::Max(1, NumChannels) * 2;

    if (ProcSound && Data.Num() > 0)
    {
        const int32 FrameBytes = FMath::Max(1, NumChannels) * 2;
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
            TArray<uint8> Bytes; Bytes.Append(Data.GetData(), AlignedBytes);
            QueueAudioOnAudioThread(MoveTemp(Bytes));
            UE_LOG(LogTemp, Verbose, TEXT("[AudioStream] Queued PCM bytes=%d (ch=%d sr=%d)"), AlignedBytes, NumChannels, SampleRate);
            TotalQueuedBytes += (int64)AlignedBytes;
            TotalAudioMsReceived += (double)AlignedBytes * 1000.0 / (double)StatBytesPerSec;
        }
    }

    if (AudioComp && !AudioComp->IsPlaying())
    {
        if (TotalQueuedBytes >= WarmupBytes)
        {
            AudioComp->Play();
            bPlayStarted = true;
            if (bAutoPopViseme && GetWorld()) { GetWorld()->GetTimerManager().ClearTimer(VisemePopTimer); }
            LastSmoothedConsumedBytes = GetSmoothedConsumedBytes();
            SynthConsumedBytes = LastSmoothedConsumedBytes;
            LastConsumeProgressTimeSec = FPlatformTime::Seconds();
        }
        else
        {
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

        if (const UAudioStreamSettings* S = GetDefault<UAudioStreamSettings>())
        {
            ProcSound->bEnableUnderRunFade = S->bEnableUnderRunFadeDefault;
            ProcSound->FadeMs = S->FadeMsDefault;
            ProcSound->SetCompactThreshold(S->ProcCompactThresholdBytes);
        }
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
    if (bRegistered) return;
    UGameInstance* GI = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
    if (!GI) return;
    UAudioStreamHttpWsSubsystem* Subsys = GI->GetSubsystem<UAudioStreamHttpWsSubsystem>();
    if (!Subsys) return;

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
    if (!bRegistered) return;
    UGameInstance* GI = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
    if (!GI) return;
    UAudioStreamHttpWsSubsystem* Subsys = GI->GetSubsystem<UAudioStreamHttpWsSubsystem>();
    if (!Subsys) return;

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
    LastVisemeConfidence = InConfidence;
}

void UAudioStreamHttpWsComponent::PushViseme(const TArray<int32>& InViseme)
{
    if (InViseme.Num() <= 0) return;

    VisemeHistory.Append(InViseme);

    int32 CountAdded = 0;
    for (int32 i = 0; i < InViseme.Num(); ++i)
    {
        const int32 idx = FMath::Clamp(InViseme[i], 0, 14);
        float Weight = 1.0f;
        if (LastVisemeConfidence.IsValidIndex(idx)) { Weight = FMath::Clamp(LastVisemeConfidence[idx], 0.0f, 1.0f); }
        VisemePairQueue.Add(TPair<int32, float>(idx, Weight));
        ++CountAdded;
    }

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
    if (N <= 0) return;

    VisemeHistory.Append(InViseme);

    int32 Pushed = 0;
    if (InConfidence.Num() == N)
    {
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
    if (bPlayStarted) return;
    if (VisemeHistory.Num() > 0) { VisemeHistory.RemoveAt(0); }
    if (VisemePairQueue.Num() > 0) { VisemePairQueue.RemoveAt(0); }
    UpdateVisemeFloatFromHead();
}

void UAudioStreamHttpWsComponent::UpdateVisemeFloatFromHead()
{
    const int32 Count = FMath::Clamp(VisemeFloatCount, 1, 64);
    if (VisemeFloat14.Num() != Count) VisemeFloat14.Init(0.f, Count);

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

    const int32 MaxIndex = Count - 1;
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
    const int32 Count = FMath::Clamp(VisemeFloatCount, 1, 64);
    if (VisemeFloat14.Num() != Count) VisemeFloat14.Init(0.f, Count);
    for (int32 i = 0; i < VisemeFloat14.Num(); ++i) VisemeFloat14[i] = 0.f;
    const int32 MaxIndex = Count - 1;
    const int32 idx = FMath::Clamp(NeutralVisemeIndex, 0, MaxIndex);
    VisemeFloat14[idx] = 1.f;
}

void UAudioStreamHttpWsComponent::UpdateTimingParams()
{
    const int64 BytesPerSec = (int64)FMath::Max(1, SampleRate) * (int64)FMath::Max(1, NumChannels) * 2;
    BytesPerStep = (int32)FMath::Max<int64>(1, (int64)((double)BytesPerSec * (VisemeStepMs / 1000.0)));
    WarmupBytes  = (int64)FMath::Max<int64>(0,  (int64)((double)BytesPerSec * (WarmupMs   / 1000.0)));
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

int64 UAudioStreamHttpWsComponent::GetSmoothedConsumedBytes()
{
    if (!ProcSound) return 0;
    const int64 Raw = ProcSound->GetConsumedBytes();
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

    if (!ProcSound) return;

    const int64 Smoothed = GetSmoothedConsumedBytes();
    const double NowSec = FPlatformTime::Seconds();

    if (Smoothed > LastSmoothedConsumedBytes)
    {
        SynthConsumedBytes = Smoothed;
        LastConsumeProgressTimeSec = NowSec;
    }
    else if (bPlayStarted)
    {
        const int64 BytesPerSec = (int64)FMath::Max(1, SampleRate) * (int64)FMath::Max(1, NumChannels) * 2;
        const int64 Add = (int64)((double)BytesPerSec * FMath::Max(0.f, DeltaTime));
        const int64 MaxAllowed = TotalQueuedBytes;
        SynthConsumedBytes = FMath::Min<int64>(SynthConsumedBytes + Add, MaxAllowed);
    }
    LastSmoothedConsumedBytes = Smoothed;

    int64 EstimatedConsumed = FMath::Max<int64>(Smoothed, SynthConsumedBytes);
    EstimatedConsumed = FMath::Min<int64>(EstimatedConsumed, TotalQueuedBytes);

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

    if (bHasPendingFormatChange)
    {
        const float BufMs = GetBufferedMilliseconds();
        if (!bPlayStarted || BufMs <= FormatSwitchLowWaterMs)
        {
            if (AudioComp && AudioComp->IsPlaying()) { AudioComp->Stop(); }
            if (ProcSound) { ProcSound->ResetCounters(); ProcSound->SetParams(PendingSampleRate, PendingChannels); }
            SampleRate = PendingSampleRate;
            NumChannels = PendingChannels;
            UpdateTimingParams();

            TotalQueuedBytes = 0;
            LastConsumedBytes = 0;
            AccumConsumedForVisemeBytes = 0;
            ConsumedBaseBytes = 0;
            LastRawConsumedBytes = 0;
            LastSmoothedConsumedBytes = 0;
            SynthConsumedBytes = 0;
            LastConsumeProgressTimeSec = 0.0;

            if (PendingPcmBuffer.Num() > 0)
            {
                const int32 FrameBytes = FMath::Max(1, NumChannels) * 2;
                int32 AlignedBytes = PendingPcmBuffer.Num() - (PendingPcmBuffer.Num() % FrameBytes);
                if (AlignedBytes > 0)
                {
                    TArray<uint8> Bytes; Bytes.Append(PendingPcmBuffer.GetData(), AlignedBytes);
                    QueueAudioOnAudioThread(MoveTemp(Bytes));
                    TotalQueuedBytes += (int64)AlignedBytes;
                    const int64 StatBytesPerSec = (int64)FMath::Max(1, SampleRate) * (int64)FMath::Max(1, NumChannels) * 2;
                    TotalAudioMsReceived += (double)AlignedBytes * 1000.0 / (double)StatBytesPerSec;
                }
                PendingPcmBuffer.Reset();
            }

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

    if (bPadSilenceOnUnderflow && BytesPerStep > 0)
    {
        const int64 InBuffer = FMath::Max<int64>(0, TotalQueuedBytes - EstimatedConsumed);
        const int32 FrameBytes = FMath::Max(1, NumChannels) * 2;
        const int64 LowWaterBytes = (int64)UnderflowLowWaterSteps * (int64)BytesPerStep;
        if (InBuffer < LowWaterBytes)
        {
            int64 PadBytes = (int64)UnderflowPadSteps * (int64)BytesPerStep;
            PadBytes -= (PadBytes % FrameBytes);
            if (PadBytes < FrameBytes) PadBytes = FrameBytes;
            TArray<uint8> Zeros; Zeros.AddZeroed((int32)PadBytes);
            QueueAudioOnAudioThread(MoveTemp(Zeros));
            TotalQueuedBytes += PadBytes;

            if (UnderflowPadSteps > 0)
            {
                TArray<int32> NeutralBatch; NeutralBatch.Init(FMath::Clamp(NeutralVisemeIndex, 0, 14), UnderflowPadSteps);
                VisemeHistory.Append(NeutralBatch);
                for (int32 i = 0; i < UnderflowPadSteps; ++i)
                {
                    VisemePairQueue.Add(TPair<int32, float>(FMath::Clamp(NeutralVisemeIndex, 0, 14), 1.0f));
                }
                const int64 StatBytesPerSec = (int64)FMath::Max(1, SampleRate) * (int64)FMath::Max(1, NumChannels) * 2;
                TotalAudioMsPadded += (double)PadBytes * 1000.0 / (double)StatBytesPerSec;
                TotalVisemeStepsPadded += UnderflowPadSteps;
                LastVisemeConfidence.Reset();
                if (!bPlayStarted) { SetNeutralVisemeFloat(); }
            }

            if (bDebugLogs)
            {
                UE_LOG(LogTemp, Warning, TEXT("[AudioStream][%s] Underflow pad %lld bytes (buffer=%lld, step=%d, lowSteps=%d, padSteps=%d)"), *RegisteredKey, (long long)PadBytes, (long long)InBuffer, BytesPerStep, UnderflowLowWaterSteps, UnderflowPadSteps);
            }
        }
    }
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
    const double Delta = VisemeMs - AudioMs;
    OutDeltaMs = (float)Delta;
    return FMath::Abs(Delta) <= (double)ToleranceMs;
}

void UAudioStreamHttpWsComponent::QueueAudioOnAudioThread(TArray<uint8>&& Bytes)
{
    UStreamProcSoundWave* LocalProc = ProcSound;
    if (!LocalProc || Bytes.Num() == 0) return;

    TArray<uint8> LocalData = MoveTemp(Bytes);
    FAudioThread::RunCommandOnAudioThread([PS=LocalProc, Data=MoveTemp(LocalData)]() mutable
    {
        if (PS && Data.Num() > 0)
        {
            PS->EnqueuePcm(Data.GetData(), Data.Num());
        }
    });
}
