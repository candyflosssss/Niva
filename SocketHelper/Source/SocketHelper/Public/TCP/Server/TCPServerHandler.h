// Copyright RLoris 2024

#pragma once

#include "UObject/NoExportTypes.h"
#include "Engine/EngineTypes.h"
#include "Engine/World.h"
#include "SocketUtility.h"
#include "Sockets.h"
#include "Connection/SocketConnection.h"
#include "TCPServerHandler.generated.h"

USTRUCT(BlueprintType)
struct FTcpServerOptions
{
	GENERATED_BODY();

	/** Required, local address to listen and send from, 0.0.0.0 is not valid */
	UPROPERTY(BlueprintReadWrite, Category = "SocketHelper|TCP|Server")
	FSocketHelperAddress LocalAddress;

	/** The interval to listen for incoming clients */
	UPROPERTY(BlueprintReadWrite, Category = "SocketHelper|TCP|Server")
	float ListenIntervalRate = 0.2f;

	/** The size of the connection queue for new connection request */
	UPROPERTY(BlueprintReadWrite, Category = "SocketHelper|TCP|Server")
	int32 ConnectionQueueSize = 8;

	/** Maximum number of clients allowed in this server */
	UPROPERTY(BlueprintReadWrite, Category = "SocketHelper|TCP|Server")
	int32 MaxClients = 16;

	/** Debug name of this server */
	UPROPERTY(BlueprintReadWrite, Category = "SocketHelper|TCP|Server")
	FString Name = TEXT("SERVER");

	/** Size of receive buffer */
	UPROPERTY(BlueprintReadWrite, Category = "SocketHelper|TCP|Server")
	int32 ReceiveBufferSize = 1024 * 1024;

	/** Size of send buffer */
	UPROPERTY(BlueprintReadWrite, Category = "SocketHelper|TCP|Server")
	int32 SendBufferSize = 1024 * 1024;

	/** Encoding for text */
	UPROPERTY(BlueprintReadWrite, Category = "SocketHelper|TCP|Server")
	ESocketTextEncoding TextEncoding = ESocketTextEncoding::UTF_8;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnServerStartDelegate, const FSocketHelperAddress&, BoundAddress);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnServerStopDelegate);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnServerDisconnectedDelegate, const FSocketHelperAddress&, DisconnectedAddress, bool, HasLostConnection, const int32&, Count);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnServerConnectedDelegate, const FSocketHelperAddress&, ConnectedAddress, const int32&, Count);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnServerTextMessageDelegate, const FString&, Message, const FSocketHelperAddress&, SenderAddress);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnServerByteMessageDelegate, const TArray<uint8>&, Message, const FSocketHelperAddress&, SenderAddress);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnServerErrorDelegate, const int32&, Code, const FString&, Reason, ESocketError, EError);

UCLASS(BlueprintType, Blueprintable, Transient)
class SOCKETHELPER_API UTCPServerHandler : public UObject, public FTickableGameObject
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintAssignable, Category = "SocketHelper|TCP|Server|Event")
	FOnServerStartDelegate OnStart;

	UPROPERTY(BlueprintAssignable, Category = "SocketHelper|TCP|Server|Event")
	FOnServerStopDelegate OnStop;

	UPROPERTY(BlueprintAssignable, Category = "SocketHelper|TCP|Server|Event")
	FOnServerConnectedDelegate OnConnected;

	UPROPERTY(BlueprintAssignable, Category = "SocketHelper|TCP|Server|Event")
	FOnServerDisconnectedDelegate OnDisconnected;

	UPROPERTY(BlueprintAssignable, Category = "SocketHelper|TCP|Server|Event")
	FOnServerTextMessageDelegate OnTextMessage;

	UPROPERTY(BlueprintAssignable, Category = "SocketHelper|TCP|Server|Event")
	FOnServerByteMessageDelegate OnByteMessage;

	UPROPERTY(BlueprintAssignable, Category = "SocketHelper|TCP|Server|Event")
	FOnServerErrorDelegate OnError;

	UFUNCTION(BlueprintPure, Category = "SocketHelper|TCP|Server", DisplayName = "Create Tcp Socket Server", meta = (WorldContext = "InWorldContext", HidePin = "InWorldContext", DefaultToSelf = "InWorldContext"))
	static UTCPServerHandler* CreateSocket(UObject* InWorldContext);

	UFUNCTION(BlueprintCallable, Category = "SocketHelper|TCP|Server")
	bool Start(const FTcpServerOptions& InOptions);

	UFUNCTION(BlueprintCallable, Category = "SocketHelper|TCP|Server")
	bool Stop();

	UFUNCTION(BlueprintCallable, Category = "SocketHelper|TCP|Server")
	bool Pause();

	UFUNCTION(BlueprintCallable, Category = "SocketHelper|TCP|Server")
	bool Resume();

	UFUNCTION(BlueprintPure, Category = "SocketHelper|TCP|Server")
	bool IsListening() const { return bListening; }

	UFUNCTION(BlueprintPure, Category = "SocketHelper|TCP|Server")
	bool IsRunning() const { return Socket.IsValid(); }

	UFUNCTION(BlueprintPure, Category = "SocketHelper|TCP|Server")
	const FSocketHelperAddress& GetBoundAddress() const;

	UFUNCTION(BlueprintPure, Category = "SocketHelper|TCP|Server")
	int32 GetClientCount() const;

	UFUNCTION(BlueprintPure, Category = "SocketHelper|TCP|Server")
	TArray<FSocketHelperAddress> GetClients() const;

	UFUNCTION(BlueprintCallable, Category = "SocketHelper|TCP|Server")
	bool DisconnectClient(const FSocketHelperAddress& ClientAddress);

	UFUNCTION(BlueprintCallable, Category = "SocketHelper|TCP|Server")
	bool SendText(const FString& Data, int32& ByteSent, ESocketTextEncoding TextEncoding = ESocketTextEncoding::UTF_8);

	UFUNCTION(BlueprintCallable, Category = "SocketHelper|TCP|Server")
	bool SendBytes(const TArray<uint8>& Data, int32& ByteSent);

	UFUNCTION(BlueprintCallable, Category = "SocketHelper|TCP|Server")
	bool SendTextTo(const FString& Data, int32& ByteSent, const FSocketHelperAddress& ClientAddress, ESocketTextEncoding TextEncoding = ESocketTextEncoding::UTF_8);

	UFUNCTION(BlueprintCallable, Category = "SocketHelper|TCP|Server")
	bool SendBytesTo(const TArray<uint8>& Data, int32& ByteSent, const FSocketHelperAddress& ClientAddress);

private:
	// FTickableGameObject
	virtual bool IsTickable() const override { return bListening && Socket; }
	virtual bool IsTickableInEditor() const override { return bListening && Socket; }
	virtual void Tick( float DeltaTime ) override;
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UTCPServerHandler, STATGROUP_Tickables); }
	// ~FTickableGameObject

	void ListenTick();

	void OnTCPServerHandlerClientClosed(bool HasLostConnection, const FSocketHelperAddress& InAddress);

	void OnTCPServerHandlerTextMessage(const FString& Message, const FSocketHelperAddress& InAddress);

	void OnTCPServerHandlerByteMessage(const TArray<uint8>& Message, const FSocketHelperAddress& InAddress);

	void OnTCPServerHandlerError(const int32& Code, const FString& Reason, ESocketError Error);

	/** Main socket of this server running on this machine */
	TUniquePtr<FSocket> Socket = nullptr;

	TMap<FSocketHelperAddress, TSharedPtr<FSocketConnection>> Clients;

	UPROPERTY()
	FTcpServerOptions Options;

	bool bListening = false;

	float ListenDeltaTime = 0.f;

	ISocketSubsystem* SocketSubsystem = nullptr;
};
