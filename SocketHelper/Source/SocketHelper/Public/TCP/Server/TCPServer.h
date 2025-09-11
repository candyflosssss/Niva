// Copyright RLoris 2024

#pragma once

#include "Kismet/BlueprintAsyncActionBase.h"
#include "SocketUtility.h"
#include "TCPServerHandler.h"
#include "TCPServer.generated.h"

USTRUCT(BlueprintType)
struct FTcpServerResult
{
	GENERATED_BODY();

	UPROPERTY(BlueprintReadOnly, Category = "SocketHelper|TCP|Server")
	FSocketHelperAddress BoundAddress;

	UPROPERTY(BlueprintReadOnly, Category = "SocketHelper|TCP|Server")
	TArray<FSocketHelperAddress> Clients;

	UPROPERTY(BlueprintReadOnly, Category = "SocketHelper|TCP|Server")
	TArray<uint8> ByteMessage;

	UPROPERTY(BlueprintReadOnly, Category = "SocketHelper|TCP|Server")
	FString TextMessage;

	UPROPERTY(BlueprintReadOnly, Category = "SocketHelper|TCP|Server")
	FSocketHelperAddress SenderAddress;

	UPROPERTY(BlueprintReadOnly, Category = "SocketHelper|TCP|Server")
	FSocketHelperAddress ConnectedAddress;

	UPROPERTY(BlueprintReadOnly, Category = "SocketHelper|TCP|Server")
	FSocketHelperAddress DiconnectedAddress;

	UPROPERTY(BlueprintReadOnly, Category = "SocketHelper|TCP|Server")
	bool HasLostConnection;

	UPROPERTY(BlueprintReadOnly, Category = "SocketHelper|TCP|Server")
	int32 ErrorCode;

	UPROPERTY(BlueprintReadOnly, Category = "SocketHelper|TCP|Server")
	FString ErrorReason;

	UPROPERTY(BlueprintReadOnly, Category = "SocketHelper|TCP|Server")
	ESocketError EError;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FTcpServerOutputPin, const FTcpServerResult&, Result);

UCLASS(Transient)
class SOCKETHELPER_API UTCPServer : public UBlueprintAsyncActionBase
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintAssignable)
	FTcpServerOutputPin OnStart;

	UPROPERTY(BlueprintAssignable)
	FTcpServerOutputPin OnConnected;

	UPROPERTY(BlueprintAssignable)
	FTcpServerOutputPin OnTextMessage;

	UPROPERTY(BlueprintAssignable)
	FTcpServerOutputPin OnByteMessage;

	UPROPERTY(BlueprintAssignable)
	FTcpServerOutputPin OnDisconnected;

	UPROPERTY(BlueprintAssignable)
	FTcpServerOutputPin OnStop;

	UPROPERTY(BlueprintAssignable)
	FTcpServerOutputPin OnError;

	UFUNCTION(BlueprintCallable, Category = "SocketHelper|TCP|Server", meta = (BlueprintInternalUseOnly = "true", WorldContext = "InWorldContextObject", HidePin = "InWorldContextObject", DefaultToSelf = "InWorldContextObject"))
	static UTCPServer* AsyncTcpServer(UObject* InWorldContextObject, const FTcpServerOptions& InOptions, UTCPServerHandler*& OutHandler);

protected:
	//~ UBlueprintAsyncActionBase
	virtual void Activate() override;
	//~ UBlueprintAsyncActionBase

private:
	UFUNCTION()
	void OnTCPServerStart(const FSocketHelperAddress& InBoundAddress);

	UFUNCTION()
	void OnTCPServerStop();

	UFUNCTION()
	void OnTCPServerDisconnected(const FSocketHelperAddress& DisconnectedAddress, bool HasLostConnection, const int32& Count);

	UFUNCTION()
	void OnTCPServerConnected(const FSocketHelperAddress& ConnectedAddress, const int32& Count);

	UFUNCTION()
	void OnTCPServerTextMessage(const FString& Message, const FSocketHelperAddress& SenderAddress);

	UFUNCTION()
	void OnTCPServerByteMessage(const TArray<uint8>& Message, const FSocketHelperAddress& SenderAddress);

	UFUNCTION()
	void OnTCPServerError(const int32& Code, const FString& Reason, ESocketError EError);

	UPROPERTY()
	TObjectPtr<UObject> WorldContextObject = nullptr;

	UPROPERTY()
	TObjectPtr<UTCPServerHandler> Socket = nullptr;

	UPROPERTY()
	FTcpServerOptions Options;

	bool Active = false;
};
