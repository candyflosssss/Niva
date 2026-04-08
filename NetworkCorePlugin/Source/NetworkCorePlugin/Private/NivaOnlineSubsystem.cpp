// Fill out your copyright notice in the Description page of Project Settings.


#include "NivaOnlineSubsystem.h" // 添加这个包含
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
	CurrentMapName.RemoveFromStart(World->StreamingLevelsPrefix);
	// 移除可能的路径前缀，只保留地图名称
	FString MapNameOnly = FPaths::GetBaseFilename(CurrentMapName);
	
	if (MapNameOnly != TEXT("Rooms"))
	{
		UE_LOG(LogTemp, Warning, TEXT("AgentSystemSubsystem: Map name '%s' does not match required 'Rooms'. Subsystem will not be created."), 
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
				UE_LOG(LogTemp, Warning, TEXT("AgentSystemSubsystem: NivaOnlineSubsystem.isServer is false. Subsystem will not be created."));
				return false;
			}
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("AgentSystemSubsystem: Could not find UNivaOnlineSubsystem. Subsystem will not be created."));
			return false;
		}
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("AgentSystemSubsystem: Could not get GameInstance. Subsystem will not be created."));
		return false;
	}

	UE_LOG(LogTemp, Log, TEXT("AgentSystemSubsystem: All conditions met. Creating subsystem for map '%s' on server."), *MapNameOnly);
	
	return Super::ShouldCreateSubsystem(Outer);
}



