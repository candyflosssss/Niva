#include "TaskManagerComponent.h"
#include "UObject/UObjectIterator.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "GameFramework/Actor.h"
#include "NetworkCoreSubsystem.h"

UTaskManagerComponent::UTaskManagerComponent(){ PrimaryComponentTick.bCanEverTick = true; }
void UTaskManagerComponent::BeginPlay(){ Super::BeginPlay(); RegisterMcpTools(); }
void UTaskManagerComponent::EndPlay(const EEndPlayReason::Type Reason){ ClearTasks(); Super::EndPlay(Reason); }

void UTaskManagerComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (CurrentTask){ CurrentTask->Update(this, DeltaTime); if (CurrentTask->IsFinished()) CurrentTask=nullptr; }
	if (!CurrentTask && TaskQueue.Num()>0) PopAndStartNext(); 
}

void UTaskManagerComponent::AddTask(UTaskBase* Task)
{
	if (!Task) return;
	if (CurrentTask && !CurrentTask->IsFinished() && Task->bDiscardIfBusy)
	{
		UE_LOG(LogTemp, Warning, TEXT("[TaskWeaver] Discarded %s (busy)."), *Task->GetName());
		return;
	}
	TaskQueue.Add(Task);
	if (!CurrentTask) PopAndStartNext();
}

void UTaskManagerComponent::ClearTasks()
{
	if (CurrentTask && !CurrentTask->IsFinished())
	{
		CurrentTask->Cancel(this);
		CurrentTask = nullptr;
	}

	for (TObjectPtr<UTaskBase>& T : TaskQueue)
	{
		if (T && !T->IsFinished())
		{
			T->Cancel(this);
		}
	}

	TaskQueue.Empty();
}
void UTaskManagerComponent::PopAndStartNext()
{
	if (TaskQueue.Num()==0) return;
	CurrentTask = TaskQueue[0];
	TaskQueue.RemoveAt(0);
	if (CurrentTask)
	{
		if (!CurrentTask->GetOuter()) CurrentTask->Rename(nullptr, this);
		CurrentTask->Start(this);
	}
}

void UTaskManagerComponent::RegisterMcpTools()
{
	UWorld* World = GetWorld();
	if (!World) return;
	UGameInstance* GI = World->GetGameInstance();
	if (!GI) return;
	UMCPTransportSubsystem* MCP = GI->GetSubsystem<UMCPTransportSubsystem>();
	if (!MCP)
	{
		UE_LOG(LogTemp, Warning, TEXT("[TaskWeaver] UMCPTransportSubsystem not found; skip MCP tool registration."));
		return;
	}

	int32 Registered = 0;
	for (TObjectIterator<UClass> It; It; ++It)
	{
		UClass* Cls = *It;
		if (!Cls || !Cls->IsChildOf(UTaskBase::StaticClass())) continue;
		if (Cls->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists)) continue;

		const UTaskBase* CDO = Cast<UTaskBase>(Cls->GetDefaultObject());
		if (!CDO) continue;
		if (!CDO->ShouldCreateMcpTool(this)) continue;

		const FString ToolName = Cls->GetName();
		if (RegisteredToolNames.Contains(ToolName))
		{
			continue; // 本组件已注册过同名工具，跳过
		}

		FMCPTool Tool;
		Tool.Name = ToolName;
		Tool.Description = FString::Printf(TEXT("TaskWeaver task: %s"), *Tool.Name);

		// 添加 Owner 参数（Actor 指针类型），用于调用时指定本组件的拥有者
		UMCPToolProperty* OwnerProp = UMCPToolPropertyActorPtr::CreateActorPtrProperty(
			TEXT("Owner"), TEXT("UTaskManagerComponent Owner Actor"), AActor::StaticClass()
		);
		Tool.Properties.Add(OwnerProp);

		// 由任务声明自身所需的 MCP 参数（只添加必要的参数）
		CDO->BuildMcpToolBP(Tool, this);

		FMCPRouteDelegate Route;
		Route.BindDynamic(this, &UTaskManagerComponent::OnMcpToolCalled);

		MCP->RegisterToolProperties(Tool, Route);
		McpTaskTools.Add(Tool.Name, Cls);
		RegisteredToolNames.Add(Tool.Name);
		Registered++;
	}


	UE_LOG(LogTemp, Log, TEXT("[TaskWeaver] Registered %d MCP tools from TaskWeaver tasks (component-level)."), Registered);
	if (Registered > 0)
	{
		URefreshMCPClientAsyncAction::RefreshMCPClient(this);
	}
}

void UTaskManagerComponent::OnMcpToolCalled(const FString& Result, UMCPToolHandle* MCPToolHandle, const FMCPTool& MCPTool)
{
	// 先验证 Owner 参数是否匹配本组件拥有者
	AActor* ProvidedOwner = nullptr;
	if (!UMCPToolBlueprintLibrary::GetActorValue(MCPTool, TEXT("Owner"), Result, ProvidedOwner) || ProvidedOwner == nullptr)
	{
		if (MCPToolHandle) MCPToolHandle->ToolCallback(true, TEXT("Missing or invalid Owner parameter"));
		return;
	}
	if (ProvidedOwner != GetOwner())
	{
		if (MCPToolHandle) MCPToolHandle->ToolCallback(true, TEXT("Owner mismatch for this TaskManagerComponent"));
		return;
	}

	// 根据工具名称找到 Task 子类，并创建实例入队
	TSubclassOf<UTaskBase>* Found = McpTaskTools.Find(MCPTool.Name);
	if (!Found || !(*Found))
	{
		if (MCPToolHandle) MCPToolHandle->ToolCallback(true, TEXT("Unknown TaskWeaver tool"));
		return;
	}

	UClass* TaskClass = *Found;
	UTaskBase* NewTask = NewObject<UTaskBase>(this, TaskClass);
	if (!NewTask)
	{
		if (MCPToolHandle) MCPToolHandle->ToolCallback(true, TEXT("Failed to create task instance"));
		return;
	}

	// 让任务解析并应用自身所需的参数，失���则返回错误
	if (!NewTask->ApplyMcpArgumentsBP(MCPTool, Result, this))
	{
		if (MCPToolHandle) MCPToolHandle->ToolCallback(true, TEXT("Failed to apply MCP arguments"));
		return;
	}

	AddTask(NewTask);
	if (MCPToolHandle)
	{
		const FString Msg = FString::Printf(TEXT("Queued task: %s (Owner: %s)"), *MCPTool.Name, *GetNameSafe(GetOwner()));
		MCPToolHandle->ToolCallback(false, Msg);
	}
}
