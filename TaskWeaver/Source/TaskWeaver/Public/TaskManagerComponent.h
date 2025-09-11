#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "TaskBase.h"
#include "Templates/SubclassOf.h"
#include "TaskManagerComponent.generated.h"

// 前置声明，避免在头文件引入外部插件头
class UMCPToolHandle;
struct FMCPTool;

UCLASS(ClassGroup=(TaskWeaver), meta=(BlueprintSpawnableComponent))
class TASKWEAVER_API UTaskManagerComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UTaskManagerComponent();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	UFUNCTION(BlueprintCallable, Category="TaskManager")
	void AddTask(UTaskBase* Task);

	UFUNCTION(BlueprintCallable, Category="TaskManager")
	void ClearTasks();

	UFUNCTION(BlueprintPure, Category="TaskManager")
	bool IsIdle() const { return !CurrentTask && TaskQueue.Num()==0; }

private:
	void PopAndStartNext();

	// MCP: 在 BeginPlay 收集并注册工具
	void RegisterMcpTools();

	// MCP: 工具被调用时的回调，将对应 Task 入队
	UFUNCTION()
	void OnMcpToolCalled(const FString& Result, UMCPToolHandle* MCPToolHandle, const FMCPTool& MCPTool);

	// MCP: 记录 ToolName -> Task 子类映射；用于避免本组件重复注册同名工具
	UPROPERTY(Transient)
	TMap<FString, TSubclassOf<UTaskBase>> McpTaskTools;

	// MCP: 已注册的工具名集合（本组件范围内去重）
	UPROPERTY(Transient)
	TSet<FString> RegisteredToolNames;

	UPROPERTY(Transient) TObjectPtr<UTaskBase> CurrentTask;
	UPROPERTY(Transient) TArray<TObjectPtr<UTaskBase>> TaskQueue;
};
