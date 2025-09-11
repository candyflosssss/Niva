// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Networking.h"
#include "UObject/NoExportTypes.h"
#include "UObject/Object.h"
#include "UUDPHandler.generated.h"
// 声明普通的多播委托（不是动态委托）
DECLARE_MULTICAST_DELEGATE_OneParam(FOnUDPDataReceived, const FString&);

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
	
	// 普通委托，用于C++绑定
	FOnUDPDataReceived OnDataReceived;

	UPROPERTY(BlueprintAssignable, Category = "UDP")
	FOnUDPDataReceivedDynamic OnDataReceivedDynamic;

private:
	FSocket* ListenSocket;
	FUdpSocketReceiver* UDPReceiver;
	bool bIsListening;

	void OnUDPMessageReceived(const FArrayReaderPtr& ArrayReaderPtr, const FIPv4Endpoint& EndPt);
};
