#include "TaskManagerComponent.h"
#include "UObject/UObjectIterator.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "GameFramework/Actor.h"
#include "NetworkCoreSubsystem.h"

UTaskManagerComponent::UTaskManagerComponent(){ PrimaryComponentTick.bCanEverTick = true; }
void UTaskManagerComponent::BeginPlay(){ Super::BeginPlay(); RegisterMcpTools(); }
void UTaskManagerComponent::EndPlay(const EEndPlayReason::Type Reason){ ClearTasks(); bMcpToolsRegistered = false; RegisteredToolNames.Empty(); McpTaskTools.Empty(); Super::EndPlay(Reason); }

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
	// 发起对当前任务的取消，但等待其在 Tick 中完成善后后再切换
	if (CurrentTask && !CurrentTask->IsFinished())
	{
		CurrentTask->Cancel(this);
	}

	// 取消并清空队列中未开始的任务
	for (TObjectPtr<UTaskBase>& T : TaskQueue)
	{
		if (T && !T->IsFinished())
		{
			T->Cancel(this);
		}
	}
	TaskQueue.Empty();
}

void UTaskManagerComponent::CancelCurrentTask()
{
	if (CurrentTask && !CurrentTask->IsFinished())
	{
		CurrentTask->Cancel(this);
	}
}

// 本地辅助：将键值对序列化为 JSON 风格对象字符串（值做转义）
static FString TW_KvToJsonObject(const TMap<FString, FString>& KVs)
{
	FString Out(TEXT("{")); bool bFirst = true;
	for (const auto& It : KVs)
	{
		if (!bFirst) Out += TEXT(", "); bFirst = false;
		const FString KeyEsc = It.Key.ReplaceCharWithEscapedChar();
		const FString ValEsc = It.Value.ReplaceCharWithEscapedChar();
		Out += FString::Printf(TEXT("\"%s\":\"%s\""), *KeyEsc, *ValEsc);
	}
	Out += TEXT("}");
	return Out;
}

// 将任务转为 JSON 风格文本（调用任务的可视化导出钩子）
static FString TW_TaskToText(const UTaskBase* Task)
{
	if (!Task) return TEXT("{}");
	TMap<FString, FString> KVs;
	const_cast<UTaskBase*>(Task)->BuildVisualizationPairs(KVs);
	if (!KVs.Contains(TEXT("name")))
	{
		KVs.Add(TEXT("name"), Task->GetClass()->GetName());
	}
	return TW_KvToJsonObject(KVs);
}

void UTaskManagerComponent::AddTaskImmediate(UTaskBase* Task, bool bHardAbort /*= false*/)
{
	if (!Task) return;

	const bool bHasRunning = (CurrentTask && !CurrentTask->IsFinished());
	if (bHasRunning)
	{
		// 默认仅发起取消，等待优雅结束
		CurrentTask->Cancel(this);
	}

	// 插到队列最前
	TaskQueue.Insert(Task, 0);

	if (!CurrentTask)
	{
		// 空闲则立即启动
		PopAndStartNext();
		return;
	}

	if (bHardAbort)
	{
		// 警告：硬中断会违背“善后”约束，仅在确需时使用
		UE_LOG(LogTemp, Warning, TEXT("[TaskWeaver] HARD ABORT current task to start immediate task: %s"), *Task->GetName());
		CurrentTask = nullptr;
		PopAndStartNext();
	}
}

FString UTaskManagerComponent::GetQueueText() const
{
	FString Out(TEXT("[")); bool bFirst = true;
	for (const TObjectPtr<UTaskBase>& T : TaskQueue)
	{
		if (!bFirst) Out += TEXT(", "); bFirst = false;
		Out += TW_TaskToText(T.Get());
	}
	Out += TEXT("]");
	return Out;
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
		// if (RegisteredToolNames.Contains(ToolName))
		// {
		// 	continue; // 本组件已注册过同名工具，跳过
		// }

		FMCPTool Tool;
		Tool.Name = ToolName;
		Tool.Description = FString::Printf(TEXT("TaskWeaver task: %s"), *Tool.Name);

		// 添加 Owner 参数（Actor 指针类型），用于调用时指定本组件的拥有者
		UClass* OwnerClass = GetOwner() ? GetOwner()->GetClass() : AActor::StaticClass();
		UE_LOG(LogTemp, Verbose, TEXT("[TaskWeaver] Register tool '%s' with OwnerClass=%s"), *ToolName, *GetNameSafe(OwnerClass));
		UMCPToolProperty* OwnerProp = UMCPToolPropertyActorPtr::CreateActorPtrProperty(
			TEXT("Owner"), TEXT("UTaskManagerComponent Owner Actor"), OwnerClass
		);
		// // Restrict Owner selection to this exact owner to prevent mismatches when multiple actors share the same class
		// if (AActor* OwnerActor = GetOwner())
		// {
		// 	if (UMCPToolPropertyActorPtr* OwnerPropC = Cast<UMCPToolPropertyActorPtr>(OwnerProp))
		// 	{
		// 		OwnerPropC->ActorClass = OwnerActor->GetClass();
		// 		OwnerPropC->ActorMap.Empty();
		// 		OwnerPropC->ActorMap.Add(OwnerActor->GetName(), OwnerActor);
		// 	}
		// }
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
	// 标记本组件已完成一次注册尝试，避免重复注册
	bMcpToolsRegistered = true;
}

void UTaskManagerComponent::OnMcpToolCalled(const FString& Result, UMCPToolHandle* MCPToolHandle, const FMCPTool& MCPTool)
{
	// 先验证 Owner 参数是否匹配本组件拥有者
	AActor* ProvidedOwner = nullptr;
	if (!UMCPToolBlueprintLibrary::GetActorValue(MCPTool, TEXT("Owner"), Result, ProvidedOwner) || ProvidedOwner == nullptr)
	{
		// 静默忽略：不是本组件的调用，不做回调
		UE_LOG(LogTemp, Verbose, TEXT("[TaskWeaver] Ignore MCP call: missing/invalid Owner (tool=%s, comp=%s)"), *MCPTool.Name, *GetName());
		return;
	}
	UE_LOG(LogTemp, Verbose, TEXT("[TaskWeaver] OnMcpToolCalled Owner check: ProvidedOwner=%s (%s), ThisOwner=%s (%s)"),
		*GetNameSafe(ProvidedOwner),
		ProvidedOwner ? *ProvidedOwner->GetClass()->GetName() : TEXT("<null>"),
		*GetNameSafe(GetOwner()),
		GetOwner() ? *GetOwner()->GetClass()->GetName() : TEXT("<null>"));
	if (ProvidedOwner != GetOwner())
	{
		// 仅接受指针严格相等的 Owner，避免多组件/重名误命中
		UE_LOG(LogTemp, Verbose, TEXT("[TaskWeaver] Ignore MCP call: owner mismatch (Provided=%s, Expected=%s, tool=%s)"),
			*GetNameSafe(ProvidedOwner), *GetNameSafe(GetOwner()), *MCPTool.Name);
		return; // 静默忽略
	}

	// 根据工具名称找到 Task 子类，并创建实例入队
	TSubclassOf<UTaskBase>* Found = McpTaskTools.Find(MCPTool.Name);
	if (!Found || !(*Found))
	{
		// 静默忽略：该组件未注册此工具名
		UE_LOG(LogTemp, Verbose, TEXT("[TaskWeaver] Ignore MCP call: unknown tool on this component. Tool=%s This=%s"), *MCPTool.Name, *GetName());
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
