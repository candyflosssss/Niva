// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Networking.h"
#include "UObject/NoExportTypes.h"
#include "UObject/Object.h"
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
	FUdpSocketReceiver* UDPReceiver;
	bool bIsListening;

	void OnUDPMessageReceived(const FArrayReaderPtr& ArrayReaderPtr, const FIPv4Endpoint& EndPt);
};