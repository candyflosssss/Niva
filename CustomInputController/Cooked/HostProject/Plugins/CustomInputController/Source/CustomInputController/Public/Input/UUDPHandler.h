// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Networking.h"
#include "UObject/NoExportTypes.h"
#include "UObject/Object.h"
#include "Containers/Ticker.h"
#include "UUDPHandler.generated.h"
// 声明普通的多播委托（不是动态委托）
DECLARE_MULTICAST_DELEGATE_OneParam(FOnUDPDataReceived, const FString&);

// 新增：二进制数据回调（带远端地址）
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnUDPBinaryReceived, const TArray<uint8>& /*Data*/, const FIPv4Endpoint& /*Remote*/);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnUDPDataReceivedDynamic, const FString&, ReceivedData);

UCLASS(BlueprintType, Blueprintable)
class CUSTOMINPUTCONTROLLER_API UUDPHandler : public UObject
{
	GENERATED_BODY()

public:
	UUDPHandler();
	virtual ~UUDPHandler();

	// 确保GC阶段也能停止线程/Socket
	virtual void BeginDestroy() override;

	UFUNCTION(BlueprintCallable, Category = "UDP")
	bool StartUDPReceiver(int32 Port = 8091);

	UFUNCTION(BlueprintCallable, Category = "UDP")
	void StopUDPReceiver();

	UFUNCTION(BlueprintCallable, Category = "UDP")
	bool IsListening() const { return bIsListening; }

	// 普通委托，用于C++绑定（文本）
	FOnUDPDataReceived OnDataReceived;
	// 新增：二进制数据委托（仅C++）
	FOnUDPBinaryReceived OnBinaryReceived;

	UPROPERTY(BlueprintAssignable, Category = "UDP")
	FOnUDPDataReceivedDynamic OnDataReceivedDynamic;

private:
	FSocket* ListenSocket;
	// 使用 Ticker 在游戏线程轮询，避免接收线程析构时的断言
	FTSTicker::FDelegateHandle TickerHandle;
	bool bIsListening;

	// 引擎退出前回调句柄
	FDelegateHandle PreExitHandle;

	// 轮询接收并广播消息（运行在游戏线程）
	bool PollSocket(float DeltaTime);

	void OnUDPMessageReceived(const FArrayReaderPtr& ArrayReaderPtr, const FIPv4Endpoint& EndPt);
};