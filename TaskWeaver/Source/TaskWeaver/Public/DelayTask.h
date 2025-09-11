#pragma once
#include "CoreMinimal.h"
#include "TaskBase.h"
#include "DelayTask.generated.h"

class UTaskManagerComponent;
struct FMCPTool;

UCLASS(BlueprintType, Blueprintable)
class TASKWEAVER_API UDelayTask : public UTaskBase
{
	GENERATED_BODY()
public:
	// 延时秒数（由 MCP 参数提供，默认 1s）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Delay")
	float Duration = 1.0f;

	// 已经过的时间
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Delay")
	float Elapsed = 0.0f;

public:
	// 允许作为 MCP 工具注册
	virtual bool ShouldCreateMcpTool_Implementation(UTaskManagerComponent* Manager) const override { return true; }

	// 注册时仅声明该任务需要的 MCP 参数
	virtual void BuildMcpTool(FMCPTool& Tool, UTaskManagerComponent* Manager) const override;

	// 工具被调用时解析参数并应用
	virtual bool ApplyMcpArguments(const FMCPTool& MCPTool, const FString& Json, UTaskManagerComponent* Manager) override;

protected:
	virtual void Start_Implementation(UTaskManagerComponent* Manager) override;
	virtual void Update_Implementation(UTaskManagerComponent* Manager, float DeltaTime) override;
	virtual void Cancel_Implementation(UTaskManagerComponent* Manager) override;
};
