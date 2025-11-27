// Fill out your copyright notice in the Description page of Project Settings.


#include "NivaOnlineSubsystem.h" // 添加这个包含
#include "MCP/MCPTransportSubsystem.h" // 新增：分类后的 MCP 传输子系统头，供 UAgentSystemSubsystem 使用
#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "Misc/Paths.h" // 添加这个包含用于路径处理
#include "Misc/Guid.h"
#include "Misc/ConfigCacheIni.h"


void UNivaOnlineSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	// 获取当前平台
	Platform = FPlatformProperties::PlatformName();
}

void UNivaOnlineSubsystem::Deinitialize()
{
	Super::Deinitialize();
}

bool UNivaOnlineSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	return true;
}

void UNivaOnlineSubsystem::SetDesiredPawn(FString InNeedPawn)
{
	DesiredPawn = InNeedPawn;
}

FString UNivaOnlineSubsystem::GetDesiredPawn()
{
	return DesiredPawn;
}

void UAgentSystemSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	// RegisterSimpleTextMCPTool();
}

void UAgentSystemSubsystem::Deinitialize()
{
	Super::Deinitialize();
}

bool UAgentSystemSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	// 基础检查：确保是游戏世界
	UWorld* World = Cast<UWorld>(Outer);
	if (!World || !World->IsGameWorld())
	{
		return false;
	}

	// 1. 检查地图名称是否为 "Rooms"
	FString CurrentMapName = World->GetMapName();
	// 移除可能的路径前缀，只保留地图名称
	FString MapNameOnly = FPaths::GetBaseFilename(CurrentMapName);
	
	if (MapNameOnly != TEXT("UEDPIE_0_Rooms"))
	{
		UE_LOG(LogTemp, Warning, TEXT("MyWorldSubsystem: Map name '%s' does not match required 'Rooms'. Subsystem will not be created."), 
			*MapNameOnly);
		return false;
	}

	// 2. 检查 UNivaOnlineSubsystem 的 isServer 属性
	if (UGameInstance* GameInstance = World->GetGameInstance())
	{
		if (UNivaOnlineSubsystem* OnlineSubsystem = GameInstance->GetSubsystem<UNivaOnlineSubsystem>())
		{
			if (!OnlineSubsystem->isServer)
			{
				UE_LOG(LogTemp, Warning, TEXT("MyWorldSubsystem: NivaOnlineSubsystem.isServer is false. Subsystem will not be created."));
				return false;
			}
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("MyWorldSubsystem: Could not find UNivaOnlineSubsystem. Subsystem will not be created."));
			return false;
		}
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("MyWorldSubsystem: Could not get GameInstance. Subsystem will not be created."));
		return false;
	}

	UE_LOG(LogTemp, Log, TEXT("MyWorldSubsystem: All conditions met. Creating subsystem for map '%s' on server."), *MapNameOnly);
	
	return Super::ShouldCreateSubsystem(Outer);
}

void UAgentSystemSubsystem::RegisterSimpleTextMCPTool()
{
	// 获取MCP传输子系统
	UMCPTransportSubsystem* MCPTransport = GetWorld()->GetGameInstance()->GetSubsystem<UMCPTransportSubsystem>();
	if (!MCPTransport)
	{
		UE_LOG(LogTemp, Error, TEXT("MyWorldSubsystem: Cannot find UMCPTransportSubsystem"));
		return;
	}

	// 创建MCP工具
	FMCPTool SimpleTool;
	SimpleTool.Name = TEXT("generate_suggestions");
	SimpleTool.Description = TEXT("生成4个可能的回答");

	// 创建4个文本输入属性
	TArray<UMCPToolProperty*> Properties;

	UMCPToolProperty* Text1 = UMCPToolPropertyString::CreateStringProperty(
		TEXT("text1"), TEXT("第一个文本输入"));
	Properties.Add(Text1);

	UMCPToolProperty* Text2 = UMCPToolPropertyString::CreateStringProperty(
		TEXT("text2"), TEXT("第二个文本输入"));
	Properties.Add(Text2);

	UMCPToolProperty* Text3 = UMCPToolPropertyString::CreateStringProperty(
		TEXT("text3"), TEXT("第三个文本输入"));
	Properties.Add(Text3);

	UMCPToolProperty* Text4 = UMCPToolPropertyString::CreateStringProperty(
		TEXT("text4"), TEXT("第四个文本输入"));
	Properties.Add(Text4);

	// 设置属性
	SimpleTool.Properties = Properties;

	// 创建委托并绑定
	FMCPRouteDelegate RouteDelegate;
	RouteDelegate.BindUFunction(this, FName("OnSimpleTextCallback"));

	// 注册工具
	MCPTransport->RegisterToolProperties(SimpleTool, RouteDelegate);

	UE_LOG(LogTemp, Log, TEXT("MyWorldSubsystem: Simple text MCP tool registered"));
}

void UAgentSystemSubsystem::OnSimpleTextCallback(const FString& JsonRequest, UMCPToolHandle* MCPToolHandle, const FMCPTool& MCPTool)
{
	UE_LOG(LogTemp, Log, TEXT("MyWorldSubsystem: Simple text callback received"));

	// 解析4个文本输入
	FString Text1, Text2, Text3, Text4;

	if (!UMCPToolBlueprintLibrary::GetStringValue(MCPTool, TEXT("text1"), JsonRequest, Text1) ||
		!UMCPToolBlueprintLibrary::GetStringValue(MCPTool, TEXT("text2"), JsonRequest, Text2) ||
		!UMCPToolBlueprintLibrary::GetStringValue(MCPTool, TEXT("text3"), JsonRequest, Text3) ||
		!UMCPToolBlueprintLibrary::GetStringValue(MCPTool, TEXT("text4"), JsonRequest, Text4))
	{
		MCPToolHandle->ToolCallback(true, TEXT("Failed to extract text parameters"));
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("Parsed texts: 1='%s', 2='%s', 3='%s', 4='%s'"), 
		*Text1, *Text2, *Text3, *Text4);

	// 处理文本并生成3-4个结果
	TArray<FString> Results;
	Results.Add(FString::Printf(TEXT("文本1长度: %d"), Text1.Len()));
	Results.Add(FString::Printf(TEXT("文本2长度: %d"), Text2.Len()));
	Results.Add(FString::Printf(TEXT("文本3长度: %d"), Text3.Len()));
	Results.Add(FString::Printf(TEXT("文本4长度: %d"), Text4.Len()));

	// 构建响应
	FString ResultText = FString::Join(Results, TEXT("\n"));
	MCPToolHandle->ToolCallback(false, ResultText);
}


