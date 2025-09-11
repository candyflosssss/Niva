// Copyright RLoris 2024

#pragma once

#include "UObject/NoExportTypes.h"
#include "SocketUtility.h"
#include "Common/UdpSocketReceiver.h"
#include "Engine/World.h"
#include "Sockets.h"
#include "Connection/SocketConnection.h"
#include "UDPSocketHandler.generated.h"

USTRUCT(BlueprintType)
struct FUdpSocketOptions
{
	GENERATED_BODY();

	/** Not required, if no listen address is provided then we only send */
	UPROPERTY(BlueprintReadWrite, Category = "SocketHelper|UDP")
	FSocketHelperAddress ListenAddress;

	/** Debug name for this peer */
	UPROPERTY(BlueprintReadWrite, Category = "SocketHelper|UDP")
	FString Name = "PEER";

	UPROPERTY(BlueprintReadWrite, Category = "SocketHelper|UDP")
	int32 ReceiveBufferSize = 1024 * 1024;

	UPROPERTY(BlueprintReadWrite, Category = "SocketHelper|UDP")
	int32 SendBufferSize = 1024 * 1024;

	UPROPERTY(BlueprintReadWrite, Category = "SocketHelper|UDP")
	ESocketTextEncoding TextEncoding = ESocketTextEncoding::UTF_8;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnUdpConnectedDelegate, bool, IsBound, const FSocketHelperAddress&, BoundAddress);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnUdpClosedDelegate);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnUdpErrorDelegate, const int32&, Code, const FString&, Reason, ESocketError, Error);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnUdpTextMessageDelegate, const FString&, Message, const FSocketHelperAddress&, SenderAddress);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnUdpByteMessageDelegate, const TArray<uint8>&, Message, const FSocketHelperAddress&, SenderAddress);

UCLASS(BlueprintType, Blueprintable, Transient)
class SOCKETHELPER_API UUDPSocketHandler : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintAssignable, Category = "SocketHelper|UDP|Event")
	FOnUdpConnectedDelegate OnConnected;

	UPROPERTY(BlueprintAssignable, Category = "SocketHelper|UDP|Event")
	FOnUdpClosedDelegate OnClosed;

	UPROPERTY(BlueprintAssignable, Category = "SocketHelper|UDP|Event")
	FOnUdpTextMessageDelegate OnTextMessage;

	UPROPERTY(BlueprintAssignable, Category = "SocketHelper|UDP|Event")
	FOnUdpByteMessageDelegate OnByteMessage;

	UPROPERTY(BlueprintAssignable, Category = "SocketHelper|UDP|Event")
	FOnUdpErrorDelegate OnError;

	UFUNCTION(BlueprintPure, Category = "SocketHelper|UDP", DisplayName = "Create Udp Socket", meta = (WorldContext = "WorldContext", HidePin = "WorldContext", DefaultToSelf = "WorldContext"))
	static UUDPSocketHandler* CreateSocket(const UObject* WorldContext);

	UFUNCTION(BlueprintPure, Category = "SocketHelper|UDP")
	bool IsListening() const;

	UFUNCTION(BlueprintPure, Category = "SocketHelper|UDP")
	bool IsRunning() const;

	UFUNCTION(BlueprintCallable, Category = "SocketHelper|UDP")
	bool Open(const FUdpSocketOptions& Options);

	UFUNCTION(BlueprintCallable, Category = "SocketHelper|UDP")
	bool Close();

	UFUNCTION(BlueprintCallable, Category = "SocketHelper|UDP")
	bool SendText(const FString& Data, int32& ByteSent, const FSocketHelperAddress& InRemoteAddress, ESocketTextEncoding Encoding = ESocketTextEncoding::UTF_8);

	UFUNCTION(BlueprintCallable, Category = "SocketHelper|UDP")
	bool SendBytes(const TArray<uint8>& Data, int32& ByteSent, const FSocketHelperAddress& InRemoteAddress);

private:
	void OnUDPPeerHandlerConnectionClosed(bool bInHasLostConnection, const FSocketHelperAddress& InAddress);

	void OnUDPPeerHandlerTextMessage(const FString& InMessage, const FSocketHelperAddress& InAddress);

	void OnUDPPeerHandlerByteMessage(const TArray<uint8>& InMessage, const FSocketHelperAddress& InAddress);

	void OnUDPPeerHandlerError(const int32& InCode, const FString& InReason, ESocketError InError);

	UPROPERTY()
	FUdpSocketOptions Options;

	TSharedPtr<FSocketConnection> RemoteConnection = nullptr;

	ISocketSubsystem* SocketSubsystem = nullptr;
};
