#pragma once
#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "TaskBase.generated.h"

class UTaskManagerComponent;
struct FMCPTool;

UENUM(BlueprintType)
enum class ETaskState : uint8 { None, Running, Completed, Canceled };

UCLASS(BlueprintType, Blueprintable, Abstract)
class TASKWEAVER_API UTaskBase : public UObject
{
	GENERATED_BODY()
public:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Task")	
	ETaskState State = ETaskState::None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Task") int32 Priority = 0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Task") bool bIsExclusive = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Task") bool bDiscardIfBusy = false;

	UFUNCTION(BlueprintNativeEvent, Category="Task|Lifecycle") void Start(UTaskManagerComponent* Manager);
	virtual void Start_Implementation(UTaskManagerComponent* /*Manager*/){ State = ETaskState::Running; }
	UFUNCTION(BlueprintNativeEvent, Category="Task|Lifecycle") void Update(UTaskManagerComponent* Manager, float DeltaTime);
	virtual void Update_Implementation(UTaskManagerComponent* /*Manager*/, float /*DeltaTime*/){ }
	UFUNCTION(BlueprintNativeEvent, Category="Task|Lifecycle") void Cancel(UTaskManagerComponent* Manager);
	virtual void Cancel_Implementation(UTaskManagerComponent* /*Manager*/){ State = ETaskState::Canceled; }

	UFUNCTION(BlueprintPure, Category="Task|Lifecycle")
	bool IsFinished() const { return State==ETaskState::Completed || State==ETaskState::Canceled; }

	UFUNCTION(BlueprintCallable, Category="Task|Lifecycle")
	void SetState(ETaskState NewState)
	{
		State = NewState;
	}

	// === MCP 集成：子类可覆盖以决定是否导出为 MCP 工具（基于组件） ===
	UFUNCTION(BlueprintNativeEvent, Category="Task|MCP")
	bool ShouldCreateMcpTool(UTaskManagerComponent* Manager) const;
	virtual bool ShouldCreateMcpTool_Implementation(UTaskManagerComponent* /*Manager*/) const { return false; }

	// C++：构建该任务的 MCP 工具参数（仅声明所需参数）
	virtual void BuildMcpTool(FMCPTool& Tool, UTaskManagerComponent* /*Manager*/) const {}
	// Blueprint：构建 MCP 参数（蓝图可实现）。默认调用 C++ 版本
	UFUNCTION(BlueprintNativeEvent, Category="Task|MCP")
	void BuildMcpToolBP(UPARAM(ref) FMCPTool& Tool, UTaskManagerComponent* Manager) const;
	virtual void BuildMcpToolBP_Implementation(FMCPTool& Tool, UTaskManagerComponent* Manager) const { BuildMcpTool(Tool, Manager); }

	// C++：从 MCP 调用参数中解析并应用到任务实例；缺省使用对象/CDO 默认值
	virtual bool ApplyMcpArguments(const FMCPTool& /*MCPTool*/, const FString& /*Json*/, UTaskManagerComponent* /*Manager*/) { return true; }
	// Blueprint：解析 MCP 参数（蓝图可实现）。默认调用 C++ 版本
	UFUNCTION(BlueprintNativeEvent, Category="Task|MCP")
	bool ApplyMcpArgumentsBP(const FMCPTool& MCPTool, const FString& Json, UTaskManagerComponent* Manager);
	virtual bool ApplyMcpArgumentsBP_Implementation(const FMCPTool& MCPTool, const FString& Json, UTaskManagerComponent* Manager) { return ApplyMcpArguments(MCPTool, Json, Manager); }
};
