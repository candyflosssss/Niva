// Copyright RLoris 2024

#include "TCP/Server/TCPServerHandler.h"
#include "Subsystem/SocketHelperSubsystem.h"

DEFINE_LOG_CATEGORY_STATIC(LogTCPServerHandler, Log, All);

void UTCPServerHandler::Tick(float DeltaTime)
{
	if (!bListening)
	{
		return;
	}

	ListenDeltaTime += DeltaTime;

	if (ListenDeltaTime < Options.ListenIntervalRate)
	{
		return;
	}

	ListenDeltaTime = 0.f;
	ListenTick();
}

UTCPServerHandler* UTCPServerHandler::CreateSocket(UObject* WorldContext)
{
	UTCPServerHandler* Node = NewObject<UTCPServerHandler>(WorldContext->GetWorld());
	Node->SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	return Node;
}

bool UTCPServerHandler::Start(const FTcpServerOptions& SocketOptions)
{
	USocketHelperSubsystem* SocketHelperSubsystem = USocketHelperSubsystem::Get(this);

	if (!IsValid(SocketHelperSubsystem) || !SocketSubsystem)
	{
		return false;
	}

	if (Socket != nullptr)
	{
		return false;
	}

	Options = SocketOptions;

	if (!Options.LocalAddress.IsValidAddress())
	{
		OnTCPServerHandlerError(0, "Error invalid ip or port provided for the bind endpoint", ESocketError::Invalid_Address);
		// error ip
		return false;
	}

	Socket = TUniquePtr<FSocket>(SocketSubsystem->CreateSocket(NAME_Stream, Options.Name, false));

	if (Socket == nullptr)
	{
		OnTCPServerHandlerError(0, "Error while creating the socket", ESocketError::Invalid_Socket);
		// error socket init
		return false;
	}

	Socket->SetNonBlocking(false);
	Socket->SetReuseAddr(true);
	Socket->SetLinger(false, 0);
	// buffers size
	Socket->SetSendBufferSize(Options.SendBufferSize, Options.SendBufferSize);
	Socket->SetReceiveBufferSize(Options.ReceiveBufferSize, Options.ReceiveBufferSize);
	// for compatibility
	Socket->SetNoDelay(true);

	const TSharedPtr<FInternetAddr> LocalAddress = Options.LocalAddress.GetInternetAddress();

	if (!Socket->Bind(*LocalAddress))
	{
		SocketSubsystem->DestroySocket(Socket.Release());

		Socket = nullptr;

		OnTCPServerHandlerError(0, "Error while binding the socket to the local endpoint", ESocketError::Bind_Error);

		// error binding
		return false;
	}

	if (Options.ConnectionQueueSize <= 0 || !Socket->Listen(Options.ConnectionQueueSize))
	{
		Socket->Close();

		SocketSubsystem->DestroySocket(Socket.Release());

		Socket = nullptr;

		OnTCPServerHandlerError(0, "Error starting socket in listening mode", ESocketError::Listen_Error);

		// error listening
		return false;
	}

	SocketHelperSubsystem->RegisterTCPServer(this);

	Clients.Empty();
	Resume();

	if (OnStart.IsBound())
	{
		OnStart.Broadcast(Options.LocalAddress);
	}

	return true;
}

bool UTCPServerHandler::Stop()
{
	if (!Socket.IsValid())
	{
		return false;
	}

	Pause();

	FSocket* SocketPtr = Socket.Release();

	if (USocketHelperSubsystem* Subsystem = USocketHelperSubsystem::Get(this))
	{
		Subsystem->UnregisterTCPServer(this);
	}

	TArray<TSharedPtr<FSocketConnection>> ValueClients;
	Clients.GenerateValueArray(ValueClients);

	for (TSharedPtr<FSocketConnection>& Client : ValueClients)
	{
		if (Client.IsValid())
		{
			Client->Halt();
		}
	}

	Clients.Empty();

	if (!SocketPtr->Close())
	{
		if (SocketSubsystem)
		{
			OnTCPServerHandlerError(0, "Error while closing the server socket " + FString(SocketSubsystem->GetSocketError()), ESocketError::Close_Error);
		}
	}

	if (SocketSubsystem)
	{
		SocketSubsystem->DestroySocket(SocketPtr);
	}

	Socket = nullptr;

	Options = FTcpServerOptions();

	if (OnStop.IsBound())
	{
		OnStop.Broadcast();
	}

	return true;
}

bool UTCPServerHandler::Pause()
{
	if (!bListening)
	{
		return false;
	}

	bListening = false;
	ListenDeltaTime = 0.f;

	if (Socket.IsValid())
	{
		Socket->Listen(0);
	}

	return true;
}

bool UTCPServerHandler::Resume()
{
	if (bListening)
	{
		return false;
	}

	ListenDeltaTime = 0.f;
	bListening = true;

	if (Socket.IsValid())
	{
		Socket->Listen(Options.ConnectionQueueSize);
	}

	return true;
}

const FSocketHelperAddress& UTCPServerHandler::GetBoundAddress() const
{
	static const FSocketHelperAddress InvalidAddress;
	return Socket ? Options.LocalAddress : InvalidAddress;
}

int32 UTCPServerHandler::GetClientCount() const
{
	return Clients.Num();
}

TArray<FSocketHelperAddress> UTCPServerHandler::GetClients() const
{
	TArray<FSocketHelperAddress> OutClients;
	Clients.GetKeys(OutClients);
	return OutClients;
}

bool UTCPServerHandler::DisconnectClient(const FSocketHelperAddress& ClientAddress)
{
	if (!Socket.IsValid())
	{
		return false;
	}

	const TSharedPtr<FSocketConnection>* Client = Clients.Find(ClientAddress);
	if (Client == nullptr)
	{
		return false;
	}

	if (!Client->IsValid())
	{
		OnTCPServerHandlerError(0, "Error client is null", ESocketError::Invalid_Socket);
		return false;
	}

	(*Client)->Halt();

	return true;
}

bool UTCPServerHandler::SendText(const FString& Data, int32& ByteSent, ESocketTextEncoding TextEncoding)
{
	return SendBytes(USocketUtility::Encode(Data, TextEncoding), ByteSent);
}

bool UTCPServerHandler::SendBytes(const TArray<uint8>& Data, int32& ByteSent)
{
	if (!Socket.IsValid())
	{
		return false;
	}

	ByteSent = 0;
	for (const TPair<FSocketHelperAddress, TSharedPtr<FSocketConnection>>& Client : Clients)
	{
		if (!Client.Value.IsValid())
		{
			OnTCPServerHandlerError(0, "Error client connection is invalid", ESocketError::Invalid_Socket);
			continue;
		}

		int32 ClientByteSent = 0;
		if (!Client.Value->Send(Data, ClientByteSent))
		{
			// error sending to this client
			OnTCPServerHandlerError(0, "Error while sending to client " + Client.Value->GetAddress().GetEndpoint(), ESocketError::Send_Error);
		}

		ByteSent += ClientByteSent;
	}

	return true;
}

bool UTCPServerHandler::SendTextTo(const FString& Data, int32& ByteSent, const FSocketHelperAddress& ClientAddress, ESocketTextEncoding TextEncoding)
{
	return SendBytesTo(USocketUtility::Encode(Data, TextEncoding), ByteSent, ClientAddress);
}

bool UTCPServerHandler::SendBytesTo(const TArray<uint8>& Data, int32& ByteSent, const FSocketHelperAddress& ClientAddress)
{
	if (!Socket.IsValid())
	{
		return false;
	}

	const TSharedPtr<FSocketConnection>* Client = Clients.Find(ClientAddress);
	if (Client == nullptr)
	{
		return false;
	}

	if (!Client->IsValid())
	{
		OnTCPServerHandlerError(0, "Error client is null", ESocketError::Send_Error);
		return false;
	}

	return (*Client)->Send(Data, ByteSent);
}

void UTCPServerHandler::ListenTick()
{
	if (!Socket.IsValid() || !SocketSubsystem)
	{
		return;
	}

	bool IsPending = false;
	if (!(Socket->HasPendingConnection(IsPending) && IsPending))
	{
		return;
	}

	if (Clients.Num() >= Options.MaxClients)
	{
		UE_LOG(LogTCPServerHandler, Warning, TEXT("Maximum client connections reached, refusing pending connections : %i/%i"), Clients.Num(), Options.MaxClients);
		// max clients reached
		return;
	}

	const TSharedRef<FInternetAddr> ClientAddr = SocketSubsystem->CreateInternetAddr();
	FSocket* ClientSocket = Socket->Accept(*ClientAddr, "ClientSocket" + FString::FromInt(GetClientCount()) + "_" + (FDateTime::UtcNow()).ToString());
	if (ClientSocket == nullptr)
	{
		OnTCPServerHandlerError(SocketSubsystem->GetLastErrorCode(), "Client socket is invalid", ESocketError::Invalid_Socket);
		// invalid socket
		return;
	}

	const FSocketHelperAddress NewClientAddress(ClientAddr);
	const TSharedPtr<FSocketConnection>* ExistingClient = Clients.Find(NewClientAddress);
	if (ExistingClient && ExistingClient->IsValid())
	{
		(*ExistingClient)->Halt();

		// already connected
		UE_LOG(LogTCPServerHandler, Warning, TEXT("Client %s already connected to server, replacing it by new client"), *NewClientAddress.GetEndpoint());
	}

	const TSharedPtr<FSocketConnection> NewClient = MakeShareable(new FSocketConnection());

	// bind events
	NewClient->OnClosed.AddUObject(this, &UTCPServerHandler::OnTCPServerHandlerClientClosed);
	NewClient->OnError.AddUObject(this, &UTCPServerHandler::OnTCPServerHandlerError);
	NewClient->OnTextMessage.AddUObject(this, &UTCPServerHandler::OnTCPServerHandlerTextMessage);
	NewClient->OnByteMessage.AddUObject(this, &UTCPServerHandler::OnTCPServerHandlerByteMessage);

	// start thread
	if (!NewClient->Start(ClientSocket, NewClientAddress, Options.TextEncoding))
	{
		// thread fail to launch
		OnTCPServerHandlerError(0, "Client connection thread failed to launch", ESocketError::Invalid_Thread);

		// close client connection
		NewClient->Halt();

		return;
	}

	// add new client
	Clients.Add(NewClientAddress, NewClient);

	if (OnConnected.IsBound())
	{
		OnConnected.Broadcast(NewClientAddress, GetClientCount());
	}
}

void UTCPServerHandler::OnTCPServerHandlerClientClosed(bool HasLostConnection, const FSocketHelperAddress& InAddress)
{
	const FSocketHelperAddress ClientAddress = InAddress;

	if (Clients.Remove(ClientAddress) > 0)
	{
		if (OnDisconnected.IsBound())
		{
			OnDisconnected.Broadcast(ClientAddress, HasLostConnection, GetClientCount());
		}
	}
}

void UTCPServerHandler::OnTCPServerHandlerTextMessage(const FString& Message, const FSocketHelperAddress& InAddress)
{
	if (OnTextMessage.IsBound())
	{
		OnTextMessage.Broadcast(Message, InAddress);
	}
}

void UTCPServerHandler::OnTCPServerHandlerByteMessage(const TArray<uint8>& Message, const FSocketHelperAddress& InAddress)
{
	if (OnByteMessage.IsBound())
	{
		OnByteMessage.Broadcast(Message, InAddress);
	}
}

void UTCPServerHandler::OnTCPServerHandlerError(const int32& Code, const FString& Reason, ESocketError Error)
{
	const FString Display = UEnum::GetValueAsString(Error);

	if (OnError.IsBound())
	{
		OnError.Broadcast(Code, Reason, Error);
	}

	UE_LOG(LogTCPServerHandler, Warning, TEXT("TCPServer [%i] [%s] : %s"), Code, *Display, *Reason);
}
