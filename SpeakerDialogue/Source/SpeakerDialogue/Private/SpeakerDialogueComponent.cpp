#include "SpeakerDialogueComponent.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"
#include "EngineUtils.h"

TMap<TWeakObjectPtr<UDialogueTalkComponent>, TMap<int32, TArray<TWeakObjectPtr<APlayerController>>>> UDialogueTalkComponent::G_ReceiversByComp;
TMap<TWeakObjectPtr<UDialogueTalkComponent>, TMap<int32, TArray<TWeakObjectPtr<UDialogueTalkComponent>>>> UDialogueTalkComponent::G_ServerHearersByComp;

UDialogueTalkComponent::UDialogueTalkComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
    SetIsReplicatedByDefault(true);
}

void UDialogueTalkComponent::BeginPlay()
{
    Super::BeginPlay();
    if (!CharacterUuid.IsValid())
    {
        CharacterUuid = FGuid::NewGuid();
    }
}

void UDialogueTalkComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(UDialogueTalkComponent, CharacterUuid);
}

/* ================= 统一入口（内置 StreamId / 自动 Seq） ================= */

int32 UDialogueTalkComponent::StartSpeech(AActor* Target, float RangeMeters)
{
    AActor* Initiator = GetOwner();
    if (!Initiator) return INDEX_NONE;

    const int32 StreamId = ++NextStreamId;
    NextSeqByStream.FindOrAdd(StreamId) = 1;

    if (RangeMeters <= 0.f) RangeMeters = DefaultRangeMeters;
    const FVector Origin = Target ? Target->GetActorLocation() : Initiator->GetActorLocation();

    Server_StartSpeech(StreamId, Initiator, Target, Origin, RangeMeters);
    return StreamId;
}

void UDialogueTalkComponent::PushChunk(int32 StreamId, const FString& DeltaText)
{
    int32* NextSeqPtr = NextSeqByStream.Find(StreamId);
    if (!NextSeqPtr) { NextSeqPtr = &NextSeqByStream.Add(StreamId, 1); }
    const int32 Seq = (*NextSeqPtr)++;

    Server_PushChunk(StreamId, Seq, DeltaText);
}

void UDialogueTalkComponent::EndSpeech(int32 StreamId, ESpeechEndReason Reason)
{
    Server_EndSpeech(StreamId, Reason);
    NextSeqByStream.Remove(StreamId);
}

int32 UDialogueTalkComponent::SpeakOnce(AActor* Target, const FString& FullText, float RangeMeters)
{
    const int32 StreamId = StartSpeech(Target, RangeMeters);
    if (StreamId != INDEX_NONE)
    {
        PushChunk(StreamId, FullText);
        EndSpeech(StreamId, ESpeechEndReason::Completed);
    }
    return StreamId;
}

/* ================= Client → Server RPC（服务器统一处理 + 安全校验） ================= */

void UDialogueTalkComponent::Server_StartSpeech_Implementation(int32 StreamId, AActor* Initiator, AActor* Target, FVector Origin, float RangeMeters)
{
    UWorld* World = GetWorld();
    if (!World) return;
    if (RangeMeters <= 0.f) RangeMeters = DefaultRangeMeters;

    // --- 所有权校验：客户端只能让自己拥有的 Pawn 发言 ---
    APlayerController* CallerPC = nullptr;
    if (APawn* OwnerPawn = Cast<APawn>(GetOwner()))
    {
        CallerPC = Cast<APlayerController>(OwnerPawn->GetController());
    }
    if (CallerPC) // 来自客户端的 RPC
    {
        APawn* CallerPawn = CallerPC->GetPawn();
        if (!Initiator || CallerPawn != Initiator)
        {
            return; // 非法：冒充他人发言
        }
    }

    // 记录本流的 initiator/target
    StreamInitiators_Server.FindOrAdd(StreamId) = Initiator;
    StreamTargets_Server.FindOrAdd(StreamId)    = Target;
    StreamAllChunks_Server.FindOrAdd(StreamId).Reset();

    // （1）收集服务器“听众”（范围内所有挂组件 Pawn，不含 Initiator，含 Target）
    TArray<TWeakObjectPtr<UDialogueTalkComponent>> Hearers;
    CollectServerHearersByRange(World, Origin, RangeMeters, Hearers, this);

    // 缓存听众列表 + 广播 Start 给听众
    {
        TMap<int32, TArray<TWeakObjectPtr<UDialogueTalkComponent>>>& Bucket =
            G_ServerHearersByComp.FindOrAdd(this);
        Bucket.FindOrAdd(StreamId) = Hearers;

        for (const auto& CompWeak : Hearers)
            if (UDialogueTalkComponent* C = CompWeak.Get())
                C->OnHearStartServer.Broadcast(StreamId, Initiator, Target, RangeMeters); 
    }

    // （2）收集客户端接收者（附近玩家 PC）
    TArray<TWeakObjectPtr<APlayerController>> PCs;
    CollectClientReceiversByRange(World, Origin, RangeMeters, PCs);

    // 缓存客户端接收者列表 + 给客户端广播 Start
    {
        TMap<int32, TArray<TWeakObjectPtr<APlayerController>>>& Bucket =
            G_ReceiversByComp.FindOrAdd(this);
        Bucket.FindOrAdd(StreamId) = PCs;

        for (const auto& PCWeak : PCs)
        {
            if (APlayerController* PC = PCWeak.Get())
                if (APawn* P = PC->GetPawn())
                    if (UDialogueTalkComponent* Comp = GetFromPawn(P))
                        Comp->Client_OnSpeechStart(StreamId, Initiator, Target, /*Anchor=*/Initiator, RangeMeters);
        }
    }

    // （3）启动超时
    Server_ResetStreamTimeout(StreamId);
}

void UDialogueTalkComponent::Server_PushChunk_Implementation(int32 StreamId, int32 Seq, const FString& DeltaText)
{
    UWorld* World = GetWorld();
    if (!World) return;

    // 记录文本
    StreamAllChunks_Server.FindOrAdd(StreamId).Add(DeltaText);
    AActor* Initiator = StreamInitiators_Server.FindRef(StreamId).Get();
    AActor* Target    = StreamTargets_Server.FindRef(StreamId).Get();

    // 客户端接收者
    if (TMap<int32, TArray<TWeakObjectPtr<APlayerController>>>* RecvBucket = G_ReceiversByComp.Find(this))
    {
        if (TArray<TWeakObjectPtr<APlayerController>>* PCs = RecvBucket->Find(StreamId))
        {
            for (const auto& PCWeak : *PCs)
            {
                if (APlayerController* PC = PCWeak.Get())
                    if (APawn* P = PC->GetPawn())
                        if (UDialogueTalkComponent* Comp = GetFromPawn(P))
                            Comp->Client_OnSpeechChunk(StreamId, Seq, DeltaText);
            }
        }
    }

    // 服务器听众
    if (TMap<int32, TArray<TWeakObjectPtr<UDialogueTalkComponent>>>* HearBucket = G_ServerHearersByComp.Find(this))
    {
        if (TArray<TWeakObjectPtr<UDialogueTalkComponent>>* Hearers = HearBucket->Find(StreamId))
        {
            for (const auto& CompWeak : *Hearers)
                if (UDialogueTalkComponent* C = CompWeak.Get())
                    C->OnHearChunkServer.Broadcast(StreamId, Initiator, Target, Seq, DeltaText);
        }
    }

    // 重置超时
    Server_ResetStreamTimeout(StreamId);
}

void UDialogueTalkComponent::Server_EndSpeech_Implementation(int32 StreamId, ESpeechEndReason Reason)
{
    UWorld* World = GetWorld();
    if (!World) return;

    const TArray<FString> Empty;
    const TArray<FString>& AllChunks = StreamAllChunks_Server.FindRef(StreamId);

    AActor* Initiator = StreamInitiators_Server.FindRef(StreamId).Get();
    AActor* Target    = StreamTargets_Server.FindRef(StreamId).Get();

    // 客户端接收者
    if (TMap<int32, TArray<TWeakObjectPtr<APlayerController>>>* RecvBucket = G_ReceiversByComp.Find(this))
    {
        if (TArray<TWeakObjectPtr<APlayerController>>* PCs = RecvBucket->Find(StreamId))
        {
            for (const auto& PCWeak : *PCs)
            {
                if (APlayerController* PC = PCWeak.Get())
                    if (APawn* P = PC->GetPawn())
                        if (UDialogueTalkComponent* Comp = GetFromPawn(P))
                            Comp->Client_OnSpeechEnd(StreamId, Reason);
            }
            RecvBucket->Remove(StreamId);
            if (RecvBucket->Num() == 0)
                G_ReceiversByComp.Remove(this);
        }
    }

    // 服务器听众（End 携带 AllChunks）
    if (TMap<int32, TArray<TWeakObjectPtr<UDialogueTalkComponent>>>* HearBucket = G_ServerHearersByComp.Find(this))
    {
        if (TArray<TWeakObjectPtr<UDialogueTalkComponent>>* Hearers = HearBucket->Find(StreamId))
        {
            FString FullText = FString::Join(AllChunks, TEXT(""));

            for (const auto& CompWeak : *Hearers)
                if (UDialogueTalkComponent* C = CompWeak.Get())
                    C->OnHearEndServer.Broadcast(StreamId, Initiator, Target, Reason, AllChunks, FullText);

            HearBucket->Remove(StreamId);
            if (HearBucket->Num() == 0)
                G_ServerHearersByComp.Remove(this);
        }
    }

    // 清理本流缓存
    StreamInitiators_Server.Remove(StreamId);
    StreamTargets_Server.Remove(StreamId);
    StreamAllChunks_Server.Remove(StreamId);

    // 取消超时
    Server_CancelStreamTimeout(StreamId);
}

/* ================= Server → Client RPC（客户端本地触发蓝图） ================= */

void UDialogueTalkComponent::Client_OnSpeechStart_Implementation(int32 StreamId, AActor* Initiator, AActor* Target, AActor* Anchor, float RangeMeters)
{
    // 客户端同步缓存发起者和目标
    StreamInitiators_Server.FindOrAdd(StreamId) = Initiator;
    StreamTargets_Server.FindOrAdd(StreamId) = Target;
    K2_OnSpeechStart(StreamId, Initiator, Target, Anchor, RangeMeters);
    OnSpeechStart.Broadcast(StreamId, Initiator, Target, Anchor, RangeMeters);
}

void UDialogueTalkComponent::Client_OnSpeechChunk_Implementation(int32 StreamId, int32 Seq, const FString& DeltaText)
{
    // 获取 Initiator/Target
    AActor* Initiator = StreamInitiators_Server.FindRef(StreamId).Get();
    AActor* Target    = StreamTargets_Server.FindRef(StreamId).Get();
    K2_OnSpeechChunk(StreamId, Initiator, Target, Seq, DeltaText);
    OnSpeechChunk.Broadcast(StreamId, Initiator, Target, Seq, DeltaText);
}

void UDialogueTalkComponent::Client_OnSpeechEnd_Implementation(int32 StreamId, ESpeechEndReason Reason)
{
    AActor* Initiator = StreamInitiators_Server.FindRef(StreamId).Get();
    AActor* Target    = StreamTargets_Server.FindRef(StreamId).Get();
    const TArray<FString>& AllChunks = StreamAllChunks_Server.FindRef(StreamId);
    FString FullText = FString::Join(AllChunks, TEXT(""));
    K2_OnSpeechEnd(StreamId, Initiator, Target, Reason);
    OnSpeechEnd.Broadcast(StreamId, Initiator, Target, Reason, AllChunks, FullText);
    // 结束后清理客户端缓存
    StreamInitiators_Server.Remove(StreamId);
    StreamTargets_Server.Remove(StreamId);
    StreamAllChunks_Server.Remove(StreamId);
}

/* ================= 服务器工具：收集接收者/听众 ================= */

void UDialogueTalkComponent::CollectClientReceiversByRange(UWorld* World, const FVector& Origin, float RangeMeters, TArray<TWeakObjectPtr<APlayerController>>& OutPCs)
{
    if (!World) return;
    const float R2 = FMath::Square(RangeMeters * 100.f);

    for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
    {
        APlayerController* PC = It->Get();
        if (!PC) continue;
        APawn* Viewer = PC->GetPawn();
        if (!Viewer) continue;

        if (FVector::DistSquared(Viewer->GetActorLocation(), Origin) <= R2)
        {
            OutPCs.Add(PC);
        }
    }
}

void UDialogueTalkComponent::CollectServerHearersByRange(
    UWorld* World, const FVector& Origin, float RangeMeters,
    TArray<TWeakObjectPtr<UDialogueTalkComponent>>& OutComps,
    UDialogueTalkComponent* InitiatorComp)
{
    if (!World) return;

    const float R2 = FMath::Square(RangeMeters * 100.f);

    for (TActorIterator<APawn> It(World); It; ++It)
    {
        APawn* P = *It;
        if (!P) continue;
        if (InitiatorComp && P == InitiatorComp->GetOwner()) continue; // 排除发起者
        if (FVector::DistSquared(P->GetActorLocation(), Origin) > R2) continue;

        if (UDialogueTalkComponent* C = P->FindComponentByClass<UDialogueTalkComponent>())
        {
            OutComps.Add(C);
        }
    }
}

UDialogueTalkComponent* UDialogueTalkComponent::GetFromPawn(APawn* Pawn)
{
    return Pawn ? Pawn->FindComponentByClass<UDialogueTalkComponent>() : nullptr;
}

/* ================= 超时：最后一个 Chunk 起 5s 无新 Chunk → 自动 End(Interrupted) ================= */

void UDialogueTalkComponent::Server_ResetStreamTimeout(int32 StreamId)
{
    UWorld* World = GetWorld();
    if (!World || !World->GetAuthGameMode()) return;
    if (ChunkTimeoutSeconds <= 0.f) return;

    FTimerHandle& Handle = StreamTimeoutHandles.FindOrAdd(StreamId);
    World->GetTimerManager().ClearTimer(Handle);

    FTimerDelegate D;
    D.BindUObject(this, &UDialogueTalkComponent::Server_OnStreamTimeout, StreamId);
    World->GetTimerManager().SetTimer(Handle, D, ChunkTimeoutSeconds, /*bLoop=*/false);
}

void UDialogueTalkComponent::Server_CancelStreamTimeout(int32 StreamId)
{
    UWorld* World = GetWorld();
    if (!World || !World->GetAuthGameMode()) return;

    if (FTimerHandle* Handle = StreamTimeoutHandles.Find(StreamId))
    {
        World->GetTimerManager().ClearTimer(*Handle);
        StreamTimeoutHandles.Remove(StreamId);
    }
}

void UDialogueTalkComponent::Server_OnStreamTimeout(int32 StreamId)
{
    // 超时 → 走正常 End 流程（Reason = Interrupted）
    Server_EndSpeech(StreamId, ESpeechEndReason::Interrupted);
}
