#include "Tasks/HeartbeatTestTask.h"
#include "TaskManagerComponent.h"

void UHeartbeatTestTask::Start_Implementation(UTaskManagerComponent* Manager)
{
	State = ETaskState::Running;
	Elapsed = 0.f;
	if (Manager && bEmitStageMessages)
	{
		TMap<FString,FString> Extra; Extra.Add(TEXT("stage"), TEXT("start"));
		Manager->ReportProgress(this, 0.f, TEXT("heartbeat test started"), Extra);
	}
}

void UHeartbeatTestTask::Update_Implementation(UTaskManagerComponent* Manager, float DeltaTime)
{
	if (State != ETaskState::Running) return;
	Elapsed += DeltaTime;
	const float Total = FMath::Max(0.1f, DurationSeconds);
	const float Pct = FMath::Clamp(Elapsed / Total * 100.f, 0.f, 100.f);
	if (Manager)
	{
		TMap<FString,FString> Extra; if (bEmitStageMessages) Extra.Add(TEXT("stage"), TEXT("work"));
		Manager->ReportProgress(this, Pct, TEXT("heartbeat test running"), Extra);
	}
	if (Elapsed >= Total)
	{
		State = ETaskState::Completed;
		if (Manager && bEmitStageMessages)
		{
			TMap<FString,FString> Extra; Extra.Add(TEXT("stage"), TEXT("done"));
			Manager->ReportProgress(this, 100.f, TEXT("heartbeat test completed"), Extra);
		}
	}
}

void UHeartbeatTestTask::BuildVisualizationPairs_Implementation(TMap<FString, FString>& OutKVs) const
{
	Super::BuildVisualizationPairs_Implementation(OutKVs);
	OutKVs.Add(TEXT("test"), TEXT("Heartbeat"));
	OutKVs.Add(TEXT("duration"), FString::SanitizeFloat(DurationSeconds));
}
