// Copyright RLoris 2024

#pragma once

#include "UObject/NoExportTypes.h"
#include "Engine/World.h"
#include "Async/Async.h"
#include "SocketUtility.h"
#include "Connection/SocketConnection.h"
#include "TCPClientHandler.generated.h"

USTRUCT(BlueprintType)
struct FTcpSocketOptions
{
	GENERATED_BODY();

	/** Required, address of the remote tcp server we want to connect to */
	UPROPERTY(BlueprintReadWrite, Category = "SocketHelper|TCP|Client")
	FSocketHelperAddress RemoteAddress;

	/** Not required, address used on this machine to listen and send, will be assigned automatically if unset, 0.0.0.0 is not valid */
	UPROPERTY(BlueprintReadWrite, Category = "SocketHelper|TCP|Client")
	FSocketHelperAddress LocalAddress;

	/** Debug name for this client */
	UPROPERTY(BlueprintReadWrite, Category = "SocketHelper|TCP|Client")
	FString Name = TEXT("CLIENT");

	/** Receive buffer size */
	UPROPERTY(BlueprintReadWrite, Category = "SocketHelper|TCP|Client")
	int32 ReceiveBufferSize = 1024 * 1024;

	/** Send buffer size */
	UPROPERTY(BlueprintReadWrite, Category = "SocketHelper|TCP|Client")
	int32 SendBufferSize = 1024 * 1024;

	/** Encoding for text */
	UPROPERTY(BlueprintReadWrite, Category = "SocketHelper|TCP|Client")
	ESocketTextEncoding TextEncoding = ESocketTextEncoding::UTF_8;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnTcpConnectedDelegate, const FSocketHelperAddress&, BoundAddress);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnTcpClosedDelegate, bool, ClosedByClient);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnTcpErrorDelegate, const int32&, Code, const FString&, Reason, ESocketError, Error);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnTcpTextMessageDelegate, const FString&, Message);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnTcpByteMessageDelegate, const TArray<uint8>&, Message);

UCLASS(BlueprintType, Blueprintable, Transient)
class SOCKETHELPER_API UTCPClientHandler : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintAssignable, Category = "SocketHelper|TCP|Client|Event")
	FOnTcpConnectedDelegate OnConnected;

	UPROPERTY(BlueprintAssignable, Category = "SocketHelper|TCP|Client|Event")
	FOnTcpClosedDelegate OnClosed;

	UPROPERTY(BlueprintAssignable, Category = "SocketHelper|TCP|Client|Event")
	FOnTcpTextMessageDelegate OnTextMessage;

	UPROPERTY(BlueprintAssignable, Category = "SocketHelper|TCP|Client|Event")
	FOnTcpByteMessageDelegate OnByteMessage;

	UPROPERTY(BlueprintAssignable, Category = "SocketHelper|TCP|Client|Event")
	FOnTcpErrorDelegate OnError;

	UFUNCTION(BlueprintPure, Category = "SocketHelper|TCP|Client", DisplayName = "Create Tcp Socket Client", meta = (WorldContext = "InWorldContext", HidePin = "InWorldContext", DefaultToSelf = "InWorldContext"))
	static UTCPClientHandler* CreateSocket(UObject* InWorldContext);

	UFUNCTION(BlueprintPure, Category = "SocketHelper|TCP|Client")
	bool IsConnected() const;

	UFUNCTION(BlueprintCallable, Category = "SocketHelper|TCP|Client")
	bool Open(const FTcpSocketOptions& Options);

	UFUNCTION(BlueprintCallable, Category = "SocketHelper|TCP|Client")
	bool Close();

	UFUNCTION(BlueprintCallable, Category = "SocketHelper|TCP|Client")
	bool SendText(const FString& Data, int32& ByteSent, ESocketTextEncoding TextEncoding = ESocketTextEncoding::UTF_8);

	UFUNCTION(BlueprintCallable, Category = "SocketHelper|TCP|Client")
	bool SendBytes(const TArray<uint8>& Data, int32& ByteSent);

	UFUNCTION(BlueprintPure, Category = "SocketHelper|TCP|Client")
	const FSocketHelperAddress& GetLocalAddress() const;

private:
	void OnTCPClientHandlerConnectionClosed(bool HasLostConnection, const FSocketHelperAddress& InAddress);

	void OnTCPClientHandlerTextMessage(const FString& Message, const FSocketHelperAddress& InAddress);

	void OnTCPClientHandlerByteMessage(const TArray<uint8>& Message, const FSocketHelperAddress& InAddress);

	void OnTCPClientHandlerError(const int32& Code, const FString& Reason, ESocketError Error);

	UPROPERTY()
	FTcpSocketOptions Options;

	TSharedPtr<FSocketConnection> RemoteConnection = nullptr;

	ISocketSubsystem* SocketSubsystem = nullptr;
};
