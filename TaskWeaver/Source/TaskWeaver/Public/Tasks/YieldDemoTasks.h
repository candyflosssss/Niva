#pragma once
#include "CoreMinimal.h"
#include "TaskBase.h"
#include "YieldDemoTasks.generated.h"

class UTaskManagerComponent;

// 静默子任务：确保目录存在（不导出为 MCP 工具，不发送最终结果）
UCLASS(BlueprintType)
class TASKWEAVER_API UYieldDemoEnsureFolderTask : public UTaskBase
{
	GENERATED_BODY()
public:
	// 目标目录（绝对或相对工程目录的路径）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="YieldDemo|EnsureFolder")
	FString FolderPath;

	virtual void Start_Implementation(UTaskManagerComponent* Manager) override;
	virtual void BuildVisualizationPairs_Implementation(TMap<FString, FString>& OutKVs) const override;

	// 显式声明不作为 MCP 工具导出
	virtual bool ShouldCreateMcpTool_Implementation(UTaskManagerComponent* /*Manager*/) const override { return false; }
};

// 主任务：首次启动若发现目录不存在，则插入 UYieldDemoEnsureFolderTask 并让出；
// 恢复后继续并完成。该任务支持注册为 MCP 工具，用于验证 MCP 下的进度/心跳/结果收口。
UCLASS(BlueprintType)
class TASKWEAVER_API UYieldDemoMainTask : public UTaskBase
{
	GENERATED_BODY()
public:
	// 需要确保存在的目录
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="YieldDemo|Main")
	FString TargetFolder;

	// 是否在恢复后模拟一点工作（便于观察心跳与进度）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="YieldDemo|Main")
	bool bSimulateWorkAfterResume = true;

	// 模拟工作的总时长（秒），用于确保有足够心跳/进度可观察
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="YieldDemo|Main", meta=(ClampMin="0.1", UIMin="0.1"))
	float SimulatedWorkSeconds = 6.0f;

	// 测试用：强制首帧前置检查失败，从而必定触发让出并插入子任务（仅首次生效，恢复后或再次启动不再生效）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="YieldDemo|Main")
	bool bForcePrecheckFailForTest = true;

	virtual void Start_Implementation(UTaskManagerComponent* Manager) override;
	virtual void Update_Implementation(UTaskManagerComponent* Manager, float DeltaTime) override;
	virtual void BuildVisualizationPairs_Implementation(TMap<FString, FString>& OutKVs) const override;

	// 作为 MCP 工具导出（仅包含 Owner 参数；最小实现用于验证通道复用）
	virtual bool ShouldCreateMcpTool_Implementation(UTaskManagerComponent* /*Manager*/) const override { return false; }

private:
	// 内部用于模拟恢复后的耗时工作
	float SimWorkElapsed = 0.f;
};
