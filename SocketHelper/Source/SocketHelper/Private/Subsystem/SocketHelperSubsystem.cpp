// Copyright RLoris 2024

#include "Subsystem/SocketHelperSubsystem.h"

#include "Engine/GameInstance.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/Engine.h"

DEFINE_LOG_CATEGORY_STATIC(LogSocketHelperSubsystem, Log, All);

USocketHelperSubsystem::USocketHelperSubsystem()
	: UGameInstanceSubsystem()
{
}

USocketHelperSubsystem* USocketHelperSubsystem::Get(const UObject* InOuter)
{
	const UWorld* World = nullptr;

	if (InOuter && InOuter->GetWorld())
	{
		World = InOuter->GetWorld();
	}
	else if (GEngine)
	{
		World = GEngine->GetCurrentPlayWorld();
	}

	if (!World)
	{
		World = GWorld;
	}

	if (const UGameInstance* GameInstance = UGameplayStatics::GetGameInstance(World))
	{
		return GameInstance->GetSubsystem<USocketHelperSubsystem>();
	}

	return nullptr;
}

void USocketHelperSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	UE_LOG(LogSocketHelperSubsystem, Log, TEXT("SocketHelper Initialize Subsystem"));

	ActiveTCPServers.Empty();
	ActiveTCPClients.Empty();
	ActiveUDPPeers.Empty();
}

void USocketHelperSubsystem::Deinitialize()
{
	Super::Deinitialize();

	UE_LOG(LogSocketHelperSubsystem, Log, TEXT("SocketHelper Uninitialize Subsystem"));

	for (const TWeakObjectPtr<UTCPServerHandler>& TCPServerWeak : ActiveTCPServers.Array())
	{
		if (UTCPServerHandler* TCPServer = TCPServerWeak.Get())
		{
			TCPServer->Stop();
			ActiveTCPServers.Remove(TCPServer);
		}
	}

	for (const TWeakObjectPtr<UTCPClientHandler>& TCPClientWeak : ActiveTCPClients.Array())
	{
		if (UTCPClientHandler* TCPClient = TCPClientWeak.Get())
		{
			TCPClient->Close();
			ActiveTCPClients.Remove(TCPClient);
		}
	}

	for (const TWeakObjectPtr<UUDPSocketHandler>& UDPPeerWeak : ActiveUDPPeers.Array())
	{
		if (UUDPSocketHandler* UDPPeer = UDPPeerWeak.Get())
		{
			UDPPeer->Close();
			ActiveUDPPeers.Remove(UDPPeer);
		}
	}
}

bool USocketHelperSubsystem::RegisterTCPClient(UTCPClientHandler* InClient)
{
	if (IsValid(InClient) && !ActiveTCPClients.Contains(InClient))
	{
		ActiveTCPClients.Add(InClient);
		return true;
	}
	return false;
}

bool USocketHelperSubsystem::UnregisterTCPClient(UTCPClientHandler* InClient)
{
	return ActiveTCPClients.Remove(InClient) != 0;
}

bool USocketHelperSubsystem::RegisterTCPServer(UTCPServerHandler* InServer)
{
	if (IsValid(InServer) && !ActiveTCPServers.Contains(InServer))
	{
		ActiveTCPServers.Add(InServer);
		return true;
	}
	return false;
}

bool USocketHelperSubsystem::UnregisterTCPServer(UTCPServerHandler* InServer)
{
	return ActiveTCPServers.Remove(InServer) != 0;
}

bool USocketHelperSubsystem::RegisterUDPPeer(UUDPSocketHandler* InPeer)
{
	if (IsValid(InPeer) && !ActiveUDPPeers.Contains(InPeer))
	{
		ActiveUDPPeers.Add(InPeer);
		return true;
	}
	return false;
}

bool USocketHelperSubsystem::UnregisterUDPPeer(UUDPSocketHandler* InPeer)
{
	return ActiveUDPPeers.Remove(InPeer) != 0;
}
