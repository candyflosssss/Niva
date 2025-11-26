// Fill out your copyright notice in the Description page of Project Settings.


#include "NivaOnlineSubsystem.h" // 添加这个包含
#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "Misc/Paths.h" // 添加这个包含用于路径处理
#include "NivaOnlineSubsystem.h"
#include "OnlineSubsystem.h"
#include "OnlineSubsystemUtils.h"
#include "Misc/Guid.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/SaveGame.h"
#include "Misc/ConfigCacheIni.h"


void UNivaOnlineSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	// 获取当前平台
	Platform = FPlatformProperties::PlatformName();

	IOnlineSubsystem* OSS = IOnlineSubsystem::Get();
	if (OSS)
	{
		IdentityInterface = OSS->GetIdentityInterface();
		if (IdentityInterface.IsValid())
		{
			// 绑定登录回调（保留 handle）
			OnLoginCompleteDelegateHandle = IdentityInterface->AddOnLoginCompleteDelegate_Handle(LocalUserIndex,
				FOnLoginCompleteDelegate::CreateUObject(this, &UNivaOnlineSubsystem::HandleOnLoginComplete));

			OnLogoutCompleteDelegateHandle = IdentityInterface->AddOnLogoutCompleteDelegate_Handle(LocalUserIndex,
				FOnLogoutCompleteDelegate::CreateUObject(this, &UNivaOnlineSubsystem::HandleOnLogoutComplete));
		}
	}
}

void UNivaOnlineSubsystem::Deinitialize()
{
	if (IdentityInterface.IsValid())
	{
		if (OnLoginCompleteDelegateHandle.IsValid())
		{
			IdentityInterface->ClearOnLoginCompleteDelegate_Handle(LocalUserIndex, OnLoginCompleteDelegateHandle);
		}
		if (OnLogoutCompleteDelegateHandle.IsValid())
		{
			IdentityInterface->ClearOnLogoutCompleteDelegate_Handle(LocalUserIndex, OnLogoutCompleteDelegateHandle);
		}
	}

	IdentityInterface = nullptr;
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


bool UNivaOnlineSubsystem::LoginWithDeviceID()
{
    if (!IdentityInterface.IsValid())
    {
        UE_LOG(LogTemp, Warning, TEXT("[NivaOnline] IdentityInterface invalid"));
        OnLoginComplete.Broadcast(false, TEXT("IdentityInterface invalid"));
    	return false;
    }

    // 1) 首先尝试 AutoLogin（很多平台或插件会把 DeviceID 自动作为匿名登录）
    UE_LOG(LogTemp, Log, TEXT("[NivaOnline] Attempting AutoLogin..."));
    //IdentityInterface->AutoLogin(LocalUserIndex);

    // AutoLogin 会触发 OnLoginComplete 的回调
    // 但有些平台/插件不支持 AutoLogin 做匿名 DeviceID 登录 —— 做回退策略：延迟后检查是否登录成功
    // 如果你想立即强制显式凭证登录，请取消下面注释以直接使用 GetOrCreateDeviceId() 的手动登录方式：
    //
    FString DeviceId = GetOrCreateDeviceId();
    FOnlineAccountCredentials Credentials;
    Credentials.Id = DeviceId;
    Credentials.Token = TEXT(""); // 有的实现需要空 token
    Credentials.Type = TEXT("DeviceId"); // 注意：根据 OSS/插件不同，这个字符串可能不同（例如 "device" / "DEV" / "epic"），参照你的插件文档
    IdentityInterface->Login(LocalUserIndex, Credentials);
	return true;
}

void UNivaOnlineSubsystem::Logout()
{
    if (!IdentityInterface.IsValid())
    {
        OnLoginComplete.Broadcast(false, TEXT("IdentityInterface invalid"));
        return;
    }

    IdentityInterface->Logout(LocalUserIndex);
}

void UNivaOnlineSubsystem::HandleOnLoginComplete(int32 LocalUserNum, bool bWasSuccessful, const FUniqueNetId& UserId, const FString& Error)
{
    if (bWasSuccessful)
    {
        FString IdStr = UserId.IsValid() ? UserId.ToString() : TEXT("InvalidId");
        UE_LOG(LogTemp, Log, TEXT("[NivaOnline] Login successful. LocalUser=%d UserId=%s"), LocalUserNum, *IdStr);
        OnLoginComplete.Broadcast(true, TEXT(""));
        return;
    }

    UE_LOG(LogTemp, Warning, TEXT("[NivaOnline] Login failed: %s"), *Error);

    // AutoLogin 可能不支持匿名登录（或失败），回退到显式 DeviceID 登录
    FString DeviceId = GetOrCreateDeviceId();
    if (!DeviceId.IsEmpty() && IdentityInterface.IsValid())
    {
        UE_LOG(LogTemp, Log, TEXT("[NivaOnline] AutoLogin failed, attempting explicit DeviceID login with id: %s"), *DeviceId);

        FOnlineAccountCredentials Credentials;
        Credentials.Id = DeviceId;
        Credentials.Token = TEXT(""); // 常见做法：空 token（不同实现差异）
        // NOTE: 下面 Type 的值可能需要根据你的 OnlineSubsystem 插件来调整。
        // OnlineSubsystemEOS 常见是让 SDK 自动处理 device 登录；如果需要手动指定，可尝试 "deviceauth" / "deviceid" / "epic" 等（参见插件文档）。
        Credentials.Type = TEXT("DeviceId");

        IdentityInterface->Login(LocalUserIndex, Credentials);
    }
    else
    {
        OnLoginComplete.Broadcast(false, Error);
    }
}

void UNivaOnlineSubsystem::HandleOnLogoutComplete(int32 LocalUserNum, bool bWasSuccessful)
{
    UE_LOG(LogTemp, Log, TEXT("[NivaOnline] Logout complete. LocalUser=%d Success=%d"), LocalUserNum, bWasSuccessful);
}

FString UNivaOnlineSubsystem::GetOrCreateDeviceId()
{
    // 方法 A：从 GConfig (DefaultGame.ini) 读取（轻量，调试友好）
    FString DeviceId;
    if (GConfig->GetString(TEXT("/Script/Engine.GameUserSettings"), *DeviceIdKey, DeviceId, GGameIni) && !DeviceId.IsEmpty())
    {
        return DeviceId;
    }

    // 方法 B：如果没有，生成一个 GUID 并保存
    FGuid NewGuid = FGuid::NewGuid();
    DeviceId = NewGuid.ToString(EGuidFormats::Digits);
    SaveDeviceId(DeviceId);
    return DeviceId;
}

void UNivaOnlineSubsystem::SaveDeviceId(const FString& DeviceId)
{
    // 保存到 GConfig（DefaultGame.ini），开发/测试时足够用
    GConfig->SetString(TEXT("/Script/Engine.GameUserSettings"), *DeviceIdKey, *DeviceId, GGameIni);
    GConfig->Flush(false, GGameIni);

    // 如果你想用 SaveGame 存档或平台专用存储，可在这里改成 SaveGame 实现
}