// Copyright RLoris 2024

#include "UDP/UDPSocketHandler.h"
#include "Common/UdpSocketBuilder.h"
#include "Subsystem/SocketHelperSubsystem.h"

DEFINE_LOG_CATEGORY_STATIC(LogUDPSocketHandler, Log, All);

UUDPSocketHandler* UUDPSocketHandler::CreateSocket(const UObject* WorldContext)
{
	UUDPSocketHandler* Node = NewObject<UUDPSocketHandler>(WorldContext->GetWorld());
	Node->SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	return Node;
}

bool UUDPSocketHandler::IsListening() const
{
	return IsRunning() && Options.ListenAddress.IsValidAddress();
}

bool UUDPSocketHandler::IsRunning() const
{
	return RemoteConnection.IsValid() && RemoteConnection->IsRunning();
}

bool UUDPSocketHandler::Open(const FUdpSocketOptions& SocketOptions)
{
	USocketHelperSubsystem* SocketHelperSubsystem = USocketHelperSubsystem::Get(this);

	if (!IsValid(SocketHelperSubsystem) || !SocketSubsystem)
	{
		return false;
	}

	if (RemoteConnection.IsValid())
	{
		OnUDPPeerHandlerError(0, TEXT("A socket connection is already opened, close it first before opening another"), ESocketError::Open_Error);
		return false;
	}

	Options = SocketOptions;

	FSocket* Socket = FUdpSocketBuilder(Options.Name)
		.AsReusable()
		.WithBroadcast()
		.AsNonBlocking()
		.Build();

	if (!Socket)
	{
		OnUDPPeerHandlerError(0, TEXT("Fail to create socket from subsystem"), ESocketError::Invalid_Socket);
		return false;
	}

	Socket->SetSendBufferSize(Options.SendBufferSize, Options.SendBufferSize);
	Socket->SetReceiveBufferSize(Options.ReceiveBufferSize, Options.ReceiveBufferSize);

	bool bBound = false;
	if (!Options.ListenAddress.IsEmptyAddress())
	{
		if (!Options.ListenAddress.IsValidAddress())
		{
			SocketSubsystem->DestroySocket(Socket);
			Socket = nullptr;

			OnUDPPeerHandlerError(0, TEXT("Invalid listen Ip or port provided"), ESocketError::Invalid_Address);

			return false;
		}

		const TSharedPtr<FInternetAddr> BindAddr = Options.ListenAddress.GetInternetAddress();

		if (!Socket->Bind(*BindAddr))
		{
			SocketSubsystem->DestroySocket(Socket);
			Socket = nullptr;

			OnUDPPeerHandlerError(0, TEXT("Fail to bind socket to specified local address or port"), ESocketError::Bind_Error);

			return false;
		}

		bBound = true;
	}

	RemoteConnection = MakeShareable(new FSocketConnection());

	// bind events
	RemoteConnection->OnClosed.AddUObject(this, &UUDPSocketHandler::OnUDPPeerHandlerConnectionClosed);
	RemoteConnection->OnError.AddUObject(this, &UUDPSocketHandler::OnUDPPeerHandlerError);
	RemoteConnection->OnTextMessage.AddUObject(this, &UUDPSocketHandler::OnUDPPeerHandlerTextMessage);
	RemoteConnection->OnByteMessage.AddUObject(this, &UUDPSocketHandler::OnUDPPeerHandlerByteMessage);

	TWeakObjectPtr<UUDPSocketHandler> WeakThis(this);
	Async(EAsyncExecution::Thread, [WeakThis, Socket, SocketHelperSubsystem, bBound]()
	{
		UUDPSocketHandler* This = WeakThis.Get();
		if (!IsValid(This) || !This->SocketSubsystem)
		{
			return;
		}

		// connection thread fail to launch
		if (!This->RemoteConnection->Start(Socket, This->Options.ListenAddress, This->Options.TextEncoding))
		{
			// close server connection
			This->RemoteConnection->Halt();

			This->SocketSubsystem->DestroySocket(Socket);

			This->RemoteConnection = nullptr;

			This->OnUDPPeerHandlerError(0, TEXT("Peer connection thread failed to launch"), ESocketError::Invalid_Thread);

			return;
		}

		if (This->OnConnected.IsBound())
		{
			This->OnConnected.Broadcast(bBound, This->Options.ListenAddress);
		}

		SocketHelperSubsystem->RegisterUDPPeer(This);
	});

	return true;
}

bool UUDPSocketHandler::Close()
{
	if (!RemoteConnection.IsValid())
	{
		return false;
	}

	RemoteConnection->Halt();

	if (USocketHelperSubsystem* Subsystem = USocketHelperSubsystem::Get(this))
	{
		Subsystem->UnregisterUDPPeer(this);
	}

	Options = FUdpSocketOptions();

	RemoteConnection = nullptr;

	return true;
}

bool UUDPSocketHandler::SendText(const FString& Data, int32& ByteSent, const FSocketHelperAddress& InRemoteAddress, ESocketTextEncoding Encoding)
{
	return SendBytes(USocketUtility::Encode(Data, Encoding), ByteSent, InRemoteAddress);
}

bool UUDPSocketHandler::SendBytes(const TArray<uint8>& Data, int32& ByteSent, const FSocketHelperAddress& InRemoteAddress)
{
	if (!RemoteConnection.IsValid())
	{
		return false;
	}

	ByteSent = 0;
	return RemoteConnection->SendTo(Data, ByteSent, InRemoteAddress);
}

void UUDPSocketHandler::OnUDPPeerHandlerConnectionClosed(bool bInHasLostConnection, const FSocketHelperAddress& InAddress)
{
	if (OnClosed.IsBound())
	{
		OnClosed.Broadcast();
	}
}

void UUDPSocketHandler::OnUDPPeerHandlerTextMessage(const FString& InMessage, const FSocketHelperAddress& InAddress)
{
	if (IsListening() && OnTextMessage.IsBound())
	{
		OnTextMessage.Broadcast(InMessage, InAddress);
	}
}

void UUDPSocketHandler::OnUDPPeerHandlerByteMessage(const TArray<uint8>& InMessage, const FSocketHelperAddress& InAddress)
{
	if (IsListening() && OnByteMessage.IsBound())
	{
		OnByteMessage.Broadcast(InMessage, InAddress);
	}
}

void UUDPSocketHandler::OnUDPPeerHandlerError(const int32& InCode, const FString& InReason, ESocketError InError)
{
	const FString Display = UEnum::GetValueAsString(InError);

	if (OnError.IsBound())
	{
		OnError.Broadcast(InCode, InReason, InError);
	}

	UE_LOG(LogUDPSocketHandler, Warning, TEXT("UDPPeer [%i] [%s] : %s"), InCode, *Display, *InReason);
}
