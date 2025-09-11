// Copyright RLoris 2024

#pragma once

#include "Kismet/BlueprintAsyncActionBase.h"
#include "SocketUtility.h"
#include "TCPClientHandler.h"
#include "TCPClient.generated.h"

USTRUCT(BlueprintType)
struct FTcpSocketResult
{
	GENERATED_BODY();

	UPROPERTY(BlueprintReadOnly, Category = "SocketHelper|TCP|Client")
	TArray<uint8> BytesMessage;

	UPROPERTY(BlueprintReadOnly, Category = "SocketHelper|TCP|Client")
	FString TextMessage;

	UPROPERTY(BlueprintReadOnly, Category = "SocketHelper|TCP|Client")
	FSocketHelperAddress BoundAddress;

	UPROPERTY(BlueprintReadOnly, Category = "SocketHelper|TCP|Client")
	bool ClosedByClient = false;

	UPROPERTY(BlueprintReadOnly, Category = "SocketHelper|TCP|Client")
	int32 ErrorCode;

	UPROPERTY(BlueprintReadOnly, Category = "SocketHelper|TCP|Client")
	ESocketError ErrorReason = ESocketError::None;

	UPROPERTY(BlueprintReadOnly, Category = "SocketHelper|TCP|Client")
	FString Error;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FTcpSocketOutputPin, const FTcpSocketResult&, Result);

UCLASS(Transient)
class SOCKETHELPER_API UTCPClient : public UBlueprintAsyncActionBase
{
	GENERATED_BODY()
public:
	UPROPERTY(BlueprintAssignable)
	FTcpSocketOutputPin OnConnected;

	UPROPERTY(BlueprintAssignable)
	FTcpSocketOutputPin OnTextMessage;

	UPROPERTY(BlueprintAssignable)
	FTcpSocketOutputPin OnBytesMessage;

	UPROPERTY(BlueprintAssignable)
	FTcpSocketOutputPin OnClosed;

	UPROPERTY(BlueprintAssignable)
	FTcpSocketOutputPin OnError;

	UFUNCTION(BlueprintCallable, Category = "SocketHelper|TCP|Client", meta = (BlueprintInternalUseOnly = "true", WorldContext = "InWorldContextObject", HidePin = "InWorldContextObject", DefaultToSelf = "InWorldContextObject"))
	static UTCPClient* AsyncTcpSocket(UObject* InWorldContextObject, const FTcpSocketOptions& InOptions, UTCPClientHandler*& OutHandler);

protected:
	//~ UBlueprintAsyncActionBase
	virtual void Activate() override;
	//~ UBlueprintAsyncActionBase

private:
	UFUNCTION()
	void OnTCPClientConnected(const FSocketHelperAddress& InAddress);

	UFUNCTION()
	void OnTCPClientClosed(bool ByServer);

	UFUNCTION()
	void OnTCPClientError(const int32& Code, const FString& Reason, ESocketError Error);

	UFUNCTION()
	void OnTCPClientTextMessage(const FString& Message);

	UFUNCTION()
	void OnTCPClientByteMessage(const TArray<uint8>& Message);

	UPROPERTY()
	TObjectPtr<UObject> WorldContextObject = nullptr;

	UPROPERTY()
	TObjectPtr<UTCPClientHandler> Socket = nullptr;

	UPROPERTY()
	FTcpSocketOptions Options;

	bool Active = false;
};
