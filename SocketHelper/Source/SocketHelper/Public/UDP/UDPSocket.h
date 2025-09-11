// Copyright RLoris 2024

#pragma once

#include "Kismet/BlueprintAsyncActionBase.h"
#include "SocketUtility.h"
#include "UDPSocketHandler.h"
#include "UDPSocket.generated.h"

USTRUCT(BlueprintType)
struct FUdpSocketResult
{
	GENERATED_BODY();

	UPROPERTY(BlueprintReadOnly, Category = "SocketHelper|UDP")
	TArray<uint8> BytesMessage;

	UPROPERTY(BlueprintReadOnly, Category = "SocketHelper|UDP")
	FString TextMessage;

	UPROPERTY(BlueprintReadOnly, Category = "SocketHelper|UDP")
	FSocketHelperAddress SenderAddress;

	UPROPERTY(BlueprintReadOnly, Category = "SocketHelper|UDP")
	bool IsBound;

	UPROPERTY(BlueprintReadOnly, Category = "SocketHelper|UDP")
	FSocketHelperAddress BoundAddress;

	UPROPERTY(BlueprintReadOnly, Category = "SocketHelper|UDP")
	int32 ErrorCode;

	UPROPERTY(BlueprintReadOnly, Category = "SocketHelper|UDP")
	ESocketError ErrorReason = ESocketError::None;

	UPROPERTY(BlueprintReadOnly, Category = "SocketHelper|UDP")
	FString Error;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FUdpSocketOutputPin, const FUdpSocketResult&, Result);

UCLASS(Transient)
class SOCKETHELPER_API UUDPSocket : public UBlueprintAsyncActionBase
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintAssignable)
	FUdpSocketOutputPin OnConnected;

	UPROPERTY(BlueprintAssignable)
	FUdpSocketOutputPin OnTextMessage;

	UPROPERTY(BlueprintAssignable)
	FUdpSocketOutputPin OnBytesMessage;

	UPROPERTY(BlueprintAssignable)
	FUdpSocketOutputPin OnClosed;

	UPROPERTY(BlueprintAssignable)
	FUdpSocketOutputPin OnError;

	UFUNCTION(BlueprintCallable, Category = "SocketHelper|UDP", meta = (BlueprintInternalUseOnly = "true", WorldContext = "InWorldContextObject", HidePin = "InWorldContextObject", DefaultToSelf = "InWorldContextObject"))
	static UUDPSocket* AsyncUdpSocket(UObject* InWorldContextObject, const FUdpSocketOptions& InOptions, UUDPSocketHandler*& OutHandler);

protected:
	//~ Begin UBlueprintAsyncActionBase
	virtual void Activate() override;
	//~ End UBlueprintAsyncActionBase

private:
	UFUNCTION()
	void OnUDPSocketConnected(bool IsBound, const FSocketHelperAddress& BoundAddress);

	UFUNCTION()
	void OnUDPSocketClosed();

	UFUNCTION()
	void OnUDPSocketError(const int32& Code, const FString& Reason, ESocketError Error);

	UFUNCTION()
	void OnUDPSocketTextMessage(const FString& Message, const FSocketHelperAddress& SenderAddress);

	UFUNCTION()
	void OnUDPSocketByteMessage(const TArray<uint8>& Message, const FSocketHelperAddress& SenderAddress);

	UPROPERTY()
	TObjectPtr<UUDPSocketHandler> Socket = nullptr;

	UPROPERTY()
	FUdpSocketOptions Options;

	bool Active = false;
};
