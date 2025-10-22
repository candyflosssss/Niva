#pragma once
#include "CoreMinimal.h"
#include "TaskBase.h"
#include "HeartbeatTestTask.generated.h"

class UTaskManagerComponent;

// 简单心跳/进度测试任务：运行给定时长，按百分比上报进度，便于观察 MCP 心跳
UCLASS(BlueprintType)
class TASKWEAVER_API UHeartbeatTestTask : public UTaskBase
{
	GENERATED_BODY()
public:
	// 运行总时长（秒）。默认 10 秒以保证多次心跳/进度可见
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="TaskWeaver|Test")
	float DurationSeconds = 10.0f;

	// 是否附带阶段标签
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="TaskWeaver|Test")
	bool bEmitStageMessages = true;

	virtual void Start_Implementation(UTaskManagerComponent* Manager) override;
	virtual void Update_Implementation(UTaskManagerComponent* Manager, float DeltaTime) override;
	virtual void BuildVisualizationPairs_Implementation(TMap<FString, FString>& OutKVs) const override;

	// 作为 MCP 工具导出，便于从 MCP 客户端直接发起测试
	virtual bool ShouldCreateMcpTool_Implementation(UTaskManagerComponent* /*Manager*/) const override { return true; }

private:
	float Elapsed = 0.f;
};
