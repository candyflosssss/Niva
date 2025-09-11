// PlayMontageTask.h
#pragma once

#include "CoreMinimal.h"
#include "TaskBase.h"
#include "PlayMontageTask.generated.h"

class ACharacter;
class UAnimMontage;
class UAnimInstance;
class UTaskManagerComponent;

/** 播放 AnimMontage；播放结束 → Completed；被打断/取消 → Canceled */
UCLASS(BlueprintType, Blueprintable)
class TASKWEAVER_API UPlayMontageTask : public UTaskBase
{
	GENERATED_BODY()
public:
	/** 播放的角色 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Anim")
	TWeakObjectPtr<ACharacter> Character;

	/** 要播放的 Montage 资产 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Anim")
	TWeakObjectPtr<UAnimMontage> Montage;

	/** 播放速率 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Anim")
	float PlayRate = 1.f;

	/** 跳转到指定 Section（可留空） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Anim")
	FName SectionName = NAME_None;

	/** 是否在 Cancel 时淡出（秒） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Anim")
	float CancelBlendOutTime = 0.2f;

public:
	UPlayMontageTask();

	// MCP：允许作为 MCP 工具注册（示例）
	virtual bool ShouldCreateMcpTool_Implementation(UTaskManagerComponent* Manager) const override;

protected:
	virtual void Start_Implementation(UTaskManagerComponent* Manager) override;
	virtual void Update_Implementation(UTaskManagerComponent* Manager, float DeltaTime) override;
	virtual void Cancel_Implementation(UTaskManagerComponent* Manager) override;

private:
	void OnMontageEnded(UAnimMontage* InMontage, bool bInterrupted);

private:
	TWeakObjectPtr<UAnimInstance> CachedAnimInstance;
	/** 防止多次结束回调 */
	bool bEndedBound = false;
};
