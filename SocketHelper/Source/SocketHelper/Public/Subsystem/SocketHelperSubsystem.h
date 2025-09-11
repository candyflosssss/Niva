// Copyright RLoris 2024

#pragma once

#include "Subsystems/GameInstanceSubsystem.h"
#include "TCP/Client/TCPClientHandler.h"
#include "TCP/Server/TCPServerHandler.h"
#include "UDP/UDPSocketHandler.h"
#include "UObject/WeakObjectPtrTemplates.h"
#include "SocketHelperSubsystem.generated.h"

UCLASS()
class SOCKETHELPER_API USocketHelperSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

	friend class UTCPClientHandler;
	friend class UTCPServerHandler;
	friend class UUDPSocketHandler;

public:
	USocketHelperSubsystem();

	static USocketHelperSubsystem* Get(const UObject* InOuter= GWorld);

protected:
	//~ UGameInstanceSubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ UGameInstanceSubsystem

	bool RegisterTCPClient(UTCPClientHandler* InClient);
	bool UnregisterTCPClient(UTCPClientHandler* InClient);

	bool RegisterTCPServer(UTCPServerHandler* InServer);
	bool UnregisterTCPServer(UTCPServerHandler* InServer);

	bool RegisterUDPPeer(UUDPSocketHandler* InPeer);
	bool UnregisterUDPPeer(UUDPSocketHandler* InPeer);

	UPROPERTY()
	TSet<TWeakObjectPtr<UTCPClientHandler>> ActiveTCPClients;

	UPROPERTY()
	TSet<TWeakObjectPtr<UTCPServerHandler>> ActiveTCPServers;

	UPROPERTY()
	TSet<TWeakObjectPtr<UUDPSocketHandler>> ActiveUDPPeers;
};