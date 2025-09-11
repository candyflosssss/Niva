#include "DelayTask.h"
#include "NetworkCoreSubsystem.h"

void UDelayTask::BuildMcpTool(FMCPTool& Tool, UTaskManagerComponent* /*Manager*/) const
{
	// 声明一个 number 类型的参数：Duration（秒）
	UMCPToolProperty* Prop = UMCPToolPropertyNumber::CreateNumberProperty(TEXT("Duration"), TEXT("延时秒数"), 0, 86400);
	if (UMCPToolPropertyNumber* Num = Cast<UMCPToolPropertyNumber>(Prop))
	{
		Num->Default = static_cast<int>(Duration);
	}
	Tool.Properties.Add(Prop);
}

bool UDelayTask::ApplyMcpArguments(const FMCPTool& MCPTool, const FString& Json, UTaskManagerComponent* /*Manager*/)
{
	float Value = Duration;
	if (UMCPToolBlueprintLibrary::GetNumberValue(MCPTool, TEXT("Duration"), Json, Value))
	{
		// 保护：非负
		Duration = FMath::Max(0.f, Value);
	}
	// 未提供时保持类默认值
	return true;
}

void UDelayTask::Start_Implementation(UTaskManagerComponent* Manager)
{
	Super::Start_Implementation(Manager);
	Elapsed = 0.f;
}

void UDelayTask::Update_Implementation(UTaskManagerComponent* /*Manager*/, float DeltaTime)
{
	Elapsed += DeltaTime;
	if (Elapsed >= Duration)
	{
		SetState(ETaskState::Completed);
	}
}

void UDelayTask::Cancel_Implementation(UTaskManagerComponent* Manager)
{
	Super::Cancel_Implementation(Manager);
}
