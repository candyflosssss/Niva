// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Subsystems/WorldSubsystem.h"
#include "Engine/World.h"
#include "OnlineSubsystem.h"
#include "Interfaces/OnlineIdentityInterface.h"

// 前置声明，避免在头文件中引入沉重依赖，减少循环包含风险
class UMCPToolHandle;
struct FMCPTool;
#include "NivaOnlineSubsystem.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FNivaOnLoginComplete, bool, bWasSuccessful, const FString&, Error);

/**
 * 
 */
UCLASS()
class NETWORKCOREPLUGIN_API UNivaOnlineSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

	// 必须的初始化函数
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	//
	virtual void Deinitialize() override;
	//
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;

public:
	UPROPERTY(BlueprintReadOnly, Category = "NivaOnline|Niva")
	FString DesiredPawn;
 UPROPERTY(BlueprintReadOnly, Category = "NivaOnline|Niva")
 FString Platform;
	UPROPERTY(BlueprintReadOnly, Category = "NivaOnline|Niva")
	bool isServer = false;


	UFUNCTION(BlueprintCallable, Category = "NivaOnline|Niva")
	void SetDesiredPawn(FString InNeedPawn);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "NivaOnline|Niva")
	FString GetDesiredPawn();

	UFUNCTION(BlueprintCallable, Category = "NivaOnline|Niva")
	void SetIsServer(bool InIsServer) { isServer = InIsServer; }
	
	
};


/**
 * World子系统模板 - 在每个World的生命周期内存在
 * 适用于需要与特定World绑定的功能，如关卡管理、游戏逻辑等
 * 用于处理各类 system 请求
 */
UCLASS()
class NETWORKCOREPLUGIN_API UAgentSystemSubsystem : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    // ========== 子系统生命周期 ==========
    
    /** 子系统初始化 - World创建时调用 */
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    
    /** 子系统反初始化 - World销毁时调用 */
    virtual void Deinitialize() override;
    
    /** 决定是否应该创建此子系统实例 */
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override;


public:
    /** 注册简单文本处理MCP工具 */
    UFUNCTION(BlueprintCallable, Category = "MyWorldSubsystem|MCP")
    void RegisterSimpleTextMCPTool();

    /** MCP工具回调函数 */
    UFUNCTION()
    void OnSimpleTextCallback(const FString& JsonRequest, UMCPToolHandle* MCPToolHandle, const FMCPTool& MCPTool);
    
};