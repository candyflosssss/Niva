#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "SpeakerDialogueComponent.generated.h"

UENUM(BlueprintType)
enum class ESpeechEndReason : uint8
{
    Completed   UMETA(DisplayName="Completed"),
    Cancelled   UMETA(DisplayName="Cancelled"),
    Interrupted UMETA(DisplayName="Interrupted"),
    OutOfRange  UMETA(DisplayName="OutOfRange")
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_FiveParams(
    FOnSpeechStart, int32, StreamId, AActor*, Initiator, AActor*, Target, AActor*, Anchor, float, RangeMeters);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_FiveParams(
    FOnSpeechChunk, int32, StreamId, AActor*, Initiator, AActor*, Target, int32, Seq, const FString&, DeltaText);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_SixParams(
    FOnSpeechEnd, int32, StreamId, AActor*, Initiator, AActor*, Target, ESpeechEndReason, Reason, const TArray<FString>&, AllChunks, const FString&, FullText);

/**
 * 单组件对话分发（挂在 Pawn 上）：
 * - 内置 StreamId 自增 + 自动 Seq
 * - 服务器：拥有端校验 → 收集“服务器听众”(范围内所有挂组件 Pawn，含 Target，不含 Initiator) → 收集客户端接收者(附近玩家PC)
 * - 客户端：各自组件收末端事件（OnSpeech*）做 UI（Panel/Bubble）
 * - 服务器听众：K2_OnHear*(Start/Chunk/End)，End 携带 AllChunks
 * - 超时：最后一个 Chunk 起 5s 无新 Chunk → End(Interrupted) + 清理
 */
UCLASS(ClassGroup=(Dialogue), BlueprintType, Blueprintable, meta=(BlueprintSpawnableComponent))
class SPEAKERDIALOGUE_API UDialogueTalkComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UDialogueTalkComponent();

    /** 角色持久 UUID（服务器生成并复制） */
    UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category="Dialogue")
    FGuid CharacterUuid;

    /** 默认广播半径（米），StartSpeech RangeMeters<=0 时使用 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Dialogue")
    float DefaultRangeMeters = 10.f;

    /** 超时：最后一个 Chunk 起超过该秒数仍未收到新 Chunk → 自动 End(Interrupted)。<=0 关闭 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Dialogue|Timeout")
    float ChunkTimeoutSeconds = 5.f;

    /* ================= 统一入口（内置 StreamId / 自动 Seq） ================= */

    /** 开始一段话（返回生成的 StreamId）。Target 可为玩家/NPC。 */
    UFUNCTION(BlueprintCallable, Category="Dialogue|Emit")
    int32 StartSpeech(AActor* Target, float RangeMeters = -1.f);

    /** 追加文本片段（自动递增 Seq）。 */
    UFUNCTION(BlueprintCallable, Category="Dialogue|Emit")
    void PushChunk(int32 StreamId, const FString& DeltaText);

    /** 结束本段话。 */
    UFUNCTION(BlueprintCallable, Category="Dialogue|Emit")
    void EndSpeech(int32 StreamId, ESpeechEndReason Reason = ESpeechEndReason::Completed);

    /** 便捷：一次性整句（非流式）。返回 StreamId。 */
    UFUNCTION(BlueprintCallable, Category="Dialogue|Emit")
    int32 SpeakOnce(AActor* Target, const FString& FullText, float RangeMeters = -1.f);

    /* ================= 客户端蓝图钩子/事件（UI 在这边接） ================= */

    UFUNCTION(BlueprintNativeEvent, Category="Dialogue|ClientHooks")
    void K2_OnSpeechStart(int32 StreamId, AActor* Initiator, AActor* Target, AActor* Anchor, float RangeMeters);
    virtual void K2_OnSpeechStart_Implementation(int32, AActor*, AActor*, AActor*, float) {}

    UFUNCTION(BlueprintNativeEvent, Category="Dialogue|ClientHooks")
    void K2_OnSpeechChunk(int32 StreamId, AActor* Initiator, AActor* Target, int32 Seq, const FString& DeltaText);
    virtual void K2_OnSpeechChunk_Implementation(int32, AActor*, AActor*, int32, const FString&) {}

    UFUNCTION(BlueprintNativeEvent, Category="Dialogue|ClientHooks")
    void K2_OnSpeechEnd(int32 StreamId, AActor* Initiator, AActor* Target, ESpeechEndReason Reason);
    virtual void K2_OnSpeechEnd_Implementation(int32, AActor*, AActor*, ESpeechEndReason) {}
    
    // —— 服务器“听到”的事件（NPC/服务器蓝图里直接可见并可绑定）——
    DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(FOnHearStartServer, int32, StreamId, AActor*, Initiator, AActor*, Target, float, RangeMeters);
    DECLARE_DYNAMIC_MULTICAST_DELEGATE_FiveParams(FOnHearChunkServer, int32, StreamId, AActor*, Initiator, AActor*, Target, int32, Seq, const FString&, DeltaText);
    DECLARE_DYNAMIC_MULTICAST_DELEGATE_SixParams(FOnHearEndServer,   int32, StreamId, AActor*, Initiator, AActor*, Target, ESpeechEndReason, Reason, const TArray<FString>&, AllChunks, const FString&, FullText);

    UPROPERTY(BlueprintAssignable, Category="Dialogue|ServerHooks")
    FOnHearStartServer OnHearStartServer;

    UPROPERTY(BlueprintAssignable, Category="Dialogue|ServerHooks")
    FOnHearChunkServer OnHearChunkServer;

    UPROPERTY(BlueprintAssignable, Category="Dialogue|ServerHooks")
    FOnHearEndServer OnHearEndServer;
    
    /** 客户端事件（蓝图绑定） */
    UPROPERTY(BlueprintAssignable, Category="Dialogue|Events")
    FOnSpeechStart OnSpeechStart;
    UPROPERTY(BlueprintAssignable, Category="Dialogue|Events")
    FOnSpeechChunk OnSpeechChunk;
    UPROPERTY(BlueprintAssignable, Category="Dialogue|Events")
    FOnSpeechEnd   OnSpeechEnd;

protected:
    virtual void BeginPlay() override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

private:
    /* ================= Client → Server ================= */
    UFUNCTION(Server, Reliable)
    void Server_StartSpeech(int32 StreamId, AActor* Initiator, AActor* Target, FVector Origin, float RangeMeters);

    UFUNCTION(Server, Unreliable)
    void Server_PushChunk(int32 StreamId, int32 Seq, const FString& DeltaText);

    UFUNCTION(Server, Reliable)
    void Server_EndSpeech(int32 StreamId, ESpeechEndReason Reason);

    /* ================= Server → Client（发到接收者各自组件实例） ================= */
    UFUNCTION(Client, Reliable)
    void Client_OnSpeechStart(int32 StreamId, AActor* Initiator, AActor* Target, AActor* Anchor, float RangeMeters);

    UFUNCTION(Client, Unreliable)
    void Client_OnSpeechChunk(int32 StreamId, int32 Seq, const FString& DeltaText);

    UFUNCTION(Client, Reliable)
    void Client_OnSpeechEnd(int32 StreamId, ESpeechEndReason Reason);

    /* ================= 服务器辅助 ================= */
    static void CollectClientReceiversByRange(UWorld* World, const FVector& Origin, float RangeMeters, TArray<TWeakObjectPtr<class APlayerController>>& OutPCs);
    static void CollectServerHearersByRange(UWorld* World, const FVector& Origin, float RangeMeters, TArray<TWeakObjectPtr<UDialogueTalkComponent>>& OutComps, UDialogueTalkComponent* InitiatorComp);
    static class UDialogueTalkComponent* GetFromPawn(class APawn* Pawn);

    /** 客户端接收者：发起者组件 → (StreamId → 接收者PC列表) */
    static TMap<TWeakObjectPtr<UDialogueTalkComponent>, TMap<int32, TArray<TWeakObjectPtr<class APlayerController>>>> G_ReceiversByComp;

    /** 服务器“听众”：发起者组件 → (StreamId → 听众组件列表) */
    static TMap<TWeakObjectPtr<UDialogueTalkComponent>, TMap<int32, TArray<TWeakObjectPtr<UDialogueTalkComponent>>>> G_ServerHearersByComp;

    /* ================= 本地状态（内置 StreamId / 自动 Seq） ================= */
    UPROPERTY(Transient) int32 NextStreamId = 0;                 // 本组件内自增
    UPROPERTY(Transient) TMap<int32, int32> NextSeqByStream;     // 每个 StreamId 的下一个 Seq（初始=1）

    /* ================= 每流信息缓存（服务器） ================= */
    TMap<int32, TWeakObjectPtr<AActor>> StreamInitiators_Server; // StreamId -> Initiator
    TMap<int32, TWeakObjectPtr<AActor>> StreamTargets_Server;    // StreamId -> Target
    TMap<int32, TArray<FString>>        StreamAllChunks_Server;  // StreamId -> 全部文本分片

    /* ================= 超时清理（服务器） ================= */
    TMap<int32, FTimerHandle> StreamTimeoutHandles;              // 每流一个计时器
    void Server_ResetStreamTimeout(int32 StreamId);
    void Server_CancelStreamTimeout(int32 StreamId);
    void Server_OnStreamTimeout(int32 StreamId);
};
