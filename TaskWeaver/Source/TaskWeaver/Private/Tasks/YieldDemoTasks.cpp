#include "Tasks/YieldDemoTasks.h"
#include "TaskManagerComponent.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"

// ================= UYieldDemoEnsureFolderTask =================
void UYieldDemoEnsureFolderTask::Start_Implementation(UTaskManagerComponent* Manager)
{
	State = ETaskState::Running;
	bool bOk = true;
	FString UsePath = FolderPath;
	if (UsePath.IsEmpty())
	{
		// 默认使用项目 Saved/YieldDemo 目录，便于在任何环境下创建
		UsePath = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("YieldDemo"));
	}

	IFileManager& FM = IFileManager::Get();
	if (!FM.DirectoryExists(*UsePath))
	{
		bOk = FM.MakeDirectory(*UsePath, /*Tree*/ true);
	}
	// 再次确认
	bOk = bOk && FM.DirectoryExists(*UsePath);

	UE_LOG(LogTemp, Log, TEXT("[YieldDemo] EnsureFolder '%s' => %s"), *UsePath, bOk ? TEXT("OK") : TEXT("FAIL"));
	State = bOk ? ETaskState::Completed : ETaskState::Canceled;
}

void UYieldDemoEnsureFolderTask::BuildVisualizationPairs_Implementation(TMap<FString, FString>& OutKVs) const
{
	Super::BuildVisualizationPairs_Implementation(OutKVs);
	OutKVs.Add(TEXT("subtask"), TEXT("EnsureFolder"));
	OutKVs.Add(TEXT("path"), FolderPath.IsEmpty() ? TEXT("<Saved>/YieldDemo") : FolderPath);
}

// ================= UYieldDemoMainTask =================
void UYieldDemoMainTask::Start_Implementation(UTaskManagerComponent* Manager)
{
	const bool bWasDeferred = (State == ETaskState::Deferred);
	State = ETaskState::Running;

	if (bWasDeferred)
	{
		OnResume(Manager);
		// 恢复：可以开始/继续主体工作
		if (!bSimulateWorkAfterResume)
		{
			// 直接完成
			if (Manager)
			{
				TMap<FString,FString> Extra; Extra.Add(TEXT("stage"), TEXT("resume"));
				Manager->ReportProgress(this, 100.f, TEXT("resumed and completed"), Extra);
			}
			State = ETaskState::Completed;
			return;
		}
		// 需要模拟工作：在 Update 中推进
		SimWorkElapsed = 0.f;
		if (Manager)
		{
			TMap<FString,FString> Extra; Extra.Add(TEXT("stage"), TEXT("resume"));
			Manager->ReportProgress(this, -1.f, TEXT("resumed, working..."), Extra);
		}
		return;
	}

	// 首次启动：先计算路径
	FString UsePath = TargetFolder;
	if (UsePath.IsEmpty())
	{
		UsePath = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("YieldDemo"));
	}

	// （测试专用）强制前置检查失败：必定让出并插入子任务（仅首次生效，避免死循环）
	if (!bWasDeferred && bForcePrecheckFailForTest)
	{
		bForcePrecheckFailForTest = false; // 仅第一次强制失败
		if (Manager)
		{
			TMap<FString,FString> Extra; Extra.Add(TEXT("stage"), TEXT("precheck")); Extra.Add(TEXT("result"), TEXT("fail"));
			Manager->ReportProgress(this, -1.f, TEXT("precheck failed, yielding to subtask"), Extra);
		}
		UYieldDemoEnsureFolderTask* Sub = NewObject<UYieldDemoEnsureFolderTask>(Manager, UYieldDemoEnsureFolderTask::StaticClass());
		if (Sub)
		{
			Sub->FolderPath = UsePath;
			if (Manager && Manager->InsertImmediateAndYield(Sub, /*after inserted*/ true))
			{
				return; // 已让出
			}
		}
	}

	// 正常路径：检查目录是否存在，不存在则让出插入创建目录的子任务
	const bool bExists = IFileManager::Get().DirectoryExists(*UsePath);
	if (!bExists)
	{
		// 汇报让出阶段（如果是 MCP 任务会被看到；本地任务则被忽略）
		if (Manager)
		{
			TMap<FString,FString> Extra; Extra.Add(TEXT("stage"), TEXT("yield")); Extra.Add(TEXT("reason"), TEXT("ensure folder"));
			Manager->ReportProgress(this, -1.f, FString::Printf(TEXT("yield to subtask for '%s'"), *UsePath), Extra);
		}

		// 创建静默子任务并让出
		UYieldDemoEnsureFolderTask* Sub = NewObject<UYieldDemoEnsureFolderTask>(Manager, UYieldDemoEnsureFolderTask::StaticClass());
		if (Sub)
		{
			Sub->FolderPath = UsePath;
			if (Manager && Manager->InsertImmediateAndYield(Sub, /*after inserted*/ true))
			{
				return; // 已让出
			}
		}
	}

	// 条件已满足：按配置决定是否直接完成或模拟工作，以产生足够心跳
	if (bSimulateWorkAfterResume)
	{
		SimWorkElapsed = 0.f;
		if (Manager)
		{
			TMap<FString,FString> Extra; Extra.Add(TEXT("stage"), TEXT("start"));
			Manager->ReportProgress(this, -1.f, TEXT("conditions ready, working..."), Extra);
		}
		return; // 后续由 Update 推进，持续多帧以产生心跳
	}
	else
	{
		if (Manager)
		{
			TMap<FString,FString> Extra; Extra.Add(TEXT("stage"), TEXT("start"));
			Manager->ReportProgress(this, -1.f, TEXT("conditions ready, completing"), Extra);
		}
		State = ETaskState::Completed;
	}
}

void UYieldDemoMainTask::Update_Implementation(UTaskManagerComponent* Manager, float DeltaTime)
{
	if (State != ETaskState::Running) return;
	if (!bSimulateWorkAfterResume) return;

	// 简单模拟若干秒工作（SimulatedWorkSeconds），期间按节流报告进度
	SimWorkElapsed += DeltaTime;
	const float Total = FMath::Max(0.1f, SimulatedWorkSeconds);
	const float Pct = FMath::Clamp(SimWorkElapsed / Total * 100.f, 0.f, 100.f);
	if (Manager)
	{
		TMap<FString,FString> Extra; Extra.Add(TEXT("stage"), TEXT("work"));
		Manager->ReportProgress(this, Pct, TEXT("working after resume"), Extra);
	}
	if (SimWorkElapsed >= Total)
	{
		State = ETaskState::Completed;
	}
}

void UYieldDemoMainTask::BuildVisualizationPairs_Implementation(TMap<FString, FString>& OutKVs) const
{
	Super::BuildVisualizationPairs_Implementation(OutKVs);
	OutKVs.Add(TEXT("demo"), TEXT("YieldMain"));
	OutKVs.Add(TEXT("target"), TargetFolder.IsEmpty() ? TEXT("<Saved>/YieldDemo") : TargetFolder);
}
