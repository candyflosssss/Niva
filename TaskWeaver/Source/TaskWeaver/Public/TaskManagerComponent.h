#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "TaskBase.h"
#include "Templates/SubclassOf.h"
#include "TaskManagerComponent.generated.h"

// 前置声明，避免在头文件引入外部插件头
class UMCPToolHandle;
struct FMCPTool;

USTRUCT()
struct FTaskCallbackContext
{
	GENERATED_BODY()

	// 唯一标识一次任务执行
	FGuid TaskId;
	// 归因信息
	FString ToolName;
	FString OwnerDisplay;
	// 回调通道
	TWeakObjectPtr<UMCPToolHandle> Handle;
	// 进度节流/心跳
	float LastProgressTime = 0.f;
	float LastHeartbeatTime = 0.f;
	float LastPercent = -1.f;
	float MinProgressInterval = 0.5f; // s
	float HeartbeatInterval = 2.0f;   // s
	// 完结控制
	bool bFinalSent = false;
	// 统计
	double StartedAt = 0.0;
};

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

	// 取消当前正在执行的任务（优雅取消）：仅发起取消，等待任务在 Tick 中完成善后后再切换
	UFUNCTION(BlueprintCallable, Category="TaskManager")
	void CancelCurrentTask();

	// 立马执行型任务：插入队列最前；默认仅优雅取消当前任务等待其结束。bHardAbort=true 将立即打断并同帧启动（谨慎使用）。
	UFUNCTION(BlueprintCallable, Category="TaskManager")
	void AddTaskImmediate(UTaskBase* Task, bool bHardAbort /*= false*/);

	// 轻量插入：由当前任务在 Start/Update 中调用。将 NewTask 插入到队首，并把当前任务挂起(Deferred)后放在其后，随后立刻切换执行 NewTask。
	// 若当前没有运行中的任务，则等价于 AddTaskImmediate(NewTask, false)。
	UFUNCTION(BlueprintCallable, Category="TaskManager")
	bool InsertImmediateAndYield(UTaskBase* NewTask, bool bRequeueCurrentAfterInserted /*= true*/);

	// 导出“队列”的文本（严格 JSON 风格字符串），不包含当前任务，仅用于可视化
	UFUNCTION(BlueprintPure, Category="TaskManager|Viz")
	FString GetQueueText() const;

	UFUNCTION(BlueprintPure, Category="TaskManager")
	bool IsIdle() const { return !CurrentTask && TaskQueue.Num()==0; }

	// === 配置：心跳与进度节流（可在编辑器或蓝图中设置） ===
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="TaskManager|MCP|Config")
	float HeartbeatIntervalSeconds = 3.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="TaskManager|MCP|Config")
	float MinProgressIntervalSeconds = 0.5f;

	// 任务侧上报接口（由任务在关键节点/定时调用）
	// ReportProgress：向 MCP/上层报告当前任务的进度与状态信息。
	// 参数说明：
	// - Task：发起上报的任务实例指针。仅当该任务来源于 MCP 调用且仍绑定着回调通道时才会被转发；否则调用将被忽略。
	// - Percent：进度百分比，建议范围 [0,100]。传入 <0 表示“未知/不更新”，仅发送文本消息与扩展键值。
	// - Message：人类可读的阶段性描述，例如“下载中(3/10)”或“正在解析配置”。
	// - ExtraKVs：额外的键值对，会并入 JSON 负载中，便于上层做结构化消费（例如 stage=download, speed=1.2MBps）。
	// 行为说明：
	// - 函数内部会进行“节流”，默认最小间隔 MinProgressIntervalSeconds（可在组件上配置），过快调用将被丢弃以避免刷屏。
	// - 若 Percent>=0，会被同时转换为整数 Completed/Total（0..100/100）以兼容通用进度条；否则不附带 Completed/Total。
	// - 该函数不会结束任务，仅作为中间态通知；上层收到的 JSON 负载包含：type=progress, taskId, tool, owner, 以及可选的 percent 与 message 等字段。
	UFUNCTION(BlueprintCallable, Category="TaskManager|MCP")
	void ReportProgress(UTaskBase* Task, float Percent, const FString& Message, const TMap<FString,FString>& ExtraKVs);
	UFUNCTION(BlueprintCallable, Category="TaskManager|MCP")
	// ReportResult：向 MCP/上层报告任务的最终结果（成功/失败），并宣告回调通道完成。
	// 参数说明：
	// - Task：完成的任务实例指针。
	// - bSuccess：是否成功。true 表示成功，false 表示失败；将会影响底层 ToolCallbackRaw 的 error 标记（取反传递）。
	// - Message：最终的人类可读总结/错误信息。
	// - PayloadKVs：最终的结构化结果负载，会与内置字段合并成 JSON 返回上层（如输出路径、统计数据等）。
	// 行为说明：
	// - 每个任务仅允许上报一次最终结果，重复上报将被忽略（内部用 bFinalSent 防抖）。
	// - 上报的 JSON 负载包含：type=result, taskId, tool, owner, success, message 以及自定义负载字段。
	// - 调用后底层会以 bFinal=true 结束回调流，之后不应再调用 ReportProgress。
	void ReportResult(UTaskBase* Task, bool bSuccess, const FString& Message, const TMap<FString,FString>& PayloadKVs);

private:
	void PopAndStartNext();

	// MCP: 在 BeginPlay 收集并注册工具
	void RegisterMcpTools();

	// 防止重复注册（例如关卡切换或重复 BeginPlay）
	UPROPERTY(Transient)
	bool bMcpToolsRegistered = false;

	// MCP: 工具被调用时的回调，将对应 Task 入队
	UFUNCTION()
	void OnMcpToolCalled(const FString& Result, UMCPToolHandle* MCPToolHandle, const FMCPTool& MCPTool);

	// MCP: 记录 ToolName -> Task 子类映射；用于避免本组件重复注册同名工具
	UPROPERTY(Transient)
	TMap<FString, TSubclassOf<UTaskBase>> McpTaskTools;

	// MCP: 已注册的工具名集合（本组件范围内去重）
	UPROPERTY(Transient)
	TSet<FString> RegisteredToolNames;

	// 回调上下文（仅对来自 MCP 的任务建立）
	UPROPERTY(Transient)
	TMap<TWeakObjectPtr<UTaskBase>, FTaskCallbackContext> CallbackContexts;

	UPROPERTY(Transient) TObjectPtr<UTaskBase> CurrentTask;
	UPROPERTY(Transient) TArray<TObjectPtr<UTaskBase>> TaskQueue;
};
