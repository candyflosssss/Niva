// PlayMontageTask.cpp
#include "PlayMontageTask.h"
#include "GameFramework/Character.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"

UPlayMontageTask::UPlayMontageTask()
{
    bIsExclusive = true; // 一般独占
    Priority = 5;
}

// 允许作为 MCP 工具注册（示例）
bool UPlayMontageTask::ShouldCreateMcpTool_Implementation(UTaskManagerComponent* /*Manager*/) const
{
    return true;
}

void UPlayMontageTask::Start_Implementation(UTaskManagerComponent* Manager)
{
    Super::Start_Implementation(Manager);

    if (!Character.IsValid() || !Montage.IsValid())
    {
        UE_LOG(LogTemp, Warning, TEXT("[PlayMontageTask] Invalid Character or Montage."));
        State = ETaskState::Canceled;
        return;
    }

    USkeletalMeshComponent* Mesh = Character.Get() ? Character.Get()->GetMesh() : nullptr;
    if (!Mesh)
    {
        UE_LOG(LogTemp, Warning, TEXT("[PlayMontageTask] Character has no Mesh."));
        State = ETaskState::Canceled;
        return;
    }

    UAnimInstance* Anim = Mesh->GetAnimInstance();
    if (!Anim)
    {
        UE_LOG(LogTemp, Warning, TEXT("[PlayMontageTask] No AnimInstance."));
        State = ETaskState::Canceled;
        return;
    }

    CachedAnimInstance = Anim;

    // 绑定结束回调
    if (!bEndedBound)
    {
        FOnMontageEnded Ended;
        Ended.BindUObject(this, &UPlayMontageTask::OnMontageEnded);
        Anim->Montage_SetEndDelegate(Ended, Montage.Get());
        bEndedBound = true;
    }

    const float Len = Anim->Montage_Play(Montage.Get(), PlayRate);
    if (Len <= 0.f)
    {
        UE_LOG(LogTemp, Warning, TEXT("[PlayMontageTask] Play failed."));
        State = ETaskState::Canceled;
        return;
    }

    if (SectionName != NAME_None)
    {
        Anim->Montage_JumpToSection(SectionName, Montage.Get());
    }
}

void UPlayMontageTask::Update_Implementation(UTaskManagerComponent* /*Manager*/, float /*DeltaTime*/)
{
    // 不需要每帧逻辑；结束由回调驱动
}

void UPlayMontageTask::Cancel_Implementation(UTaskManagerComponent* Manager)
{
    if (CachedAnimInstance.IsValid() && Montage.IsValid())
    {
        CachedAnimInstance->Montage_Stop(CancelBlendOutTime, Montage.Get());
    }
    Super::Cancel_Implementation(Manager);
}

void UPlayMontageTask::OnMontageEnded(UAnimMontage* InMontage, bool bInterrupted)
{
    if (State == ETaskState::Running)
    {
        State = bInterrupted ? ETaskState::Canceled : ETaskState::Completed;
    }
}
