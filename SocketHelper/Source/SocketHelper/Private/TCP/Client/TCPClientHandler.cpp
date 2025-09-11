// Copyright RLoris 2024

#include "TCP/Client/TCPClientHandler.h"
#include "SocketSubsystem.h"
#include "Subsystem/SocketHelperSubsystem.h"

#define LOCTEXT_NAMESPACE "TCPClientHandler"

DEFINE_LOG_CATEGORY_STATIC(LogTCPClientHandler, Log, All);

UTCPClientHandler* UTCPClientHandler::CreateSocket(UObject* WorldContext)
{
	UTCPClientHandler* Node = NewObject<UTCPClientHandler>(WorldContext);
	Node->SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	return Node;
}

bool UTCPClientHandler::IsConnected() const
{
	return RemoteConnection.IsValid() && RemoteConnection->IsConnected();
}

bool UTCPClientHandler::Open(const FTcpSocketOptions& SocketOptions)
{
	USocketHelperSubsystem* SocketHelperSubsystem = USocketHelperSubsystem::Get(this);

	if (!IsValid(SocketHelperSubsystem) || !SocketSubsystem)
	{
		return false;
	}

	// close before opening connection
	if (IsConnected())
	{
		OnTCPClientHandlerError(0, TEXT("A socket connection is already opened, close it first before opening another"), ESocketError::Open_Error);
		return false;
	}

	// invalid remote address provided
	if (!SocketOptions.RemoteAddress.IsValidAddress())
	{
		OnTCPClientHandlerError(0, FText::Format(LOCTEXT("RemoteInvalid", "Remote Ip address provided is invalid {0}"), FText::FromString(Options.RemoteAddress.GetEndpoint())).ToString(), ESocketError::Invalid_Address);
		return false;
	}

	Options = SocketOptions;

	const TSharedPtr<FInternetAddr> RemoteAddr = SocketOptions.RemoteAddress.GetInternetAddress();

	FSocket* Socket = SocketSubsystem->CreateSocket(NAME_Stream, Options.Name, false);

	// invalid socket
	if (!Socket)
	{
		OnTCPClientHandlerError(0, TEXT("Fail to create socket from subsystem"), ESocketError::Invalid_Socket);
		return false;
	}

	Socket->SetLinger(false, 0);
	// buffers size
	Socket->SetSendBufferSize(Options.SendBufferSize, Options.SendBufferSize);
	Socket->SetReceiveBufferSize(Options.ReceiveBufferSize, Options.ReceiveBufferSize);
	// for compatibility
	Socket->SetNoDelay(true);

	// bind socket to local endpoint
	if (Options.LocalAddress.IsValidAddress())
	{
		const TSharedPtr<FInternetAddr> BindAddr = Options.LocalAddress.GetInternetAddress();

		if (!Socket->Bind(*BindAddr))
		{
			SocketSubsystem->DestroySocket(Socket);
			Socket = nullptr;

			OnTCPClientHandlerError(0, TEXT("Fail to bind socket to specified local address or port"), ESocketError::Bind_Error);

			return false;
		}
	}

	RemoteConnection = MakeShareable(new FSocketConnection());

	// bind events
	RemoteConnection->OnClosed.AddUObject(this, &UTCPClientHandler::OnTCPClientHandlerConnectionClosed);
	RemoteConnection->OnError.AddUObject(this, &UTCPClientHandler::OnTCPClientHandlerError);
	RemoteConnection->OnTextMessage.AddUObject(this, &UTCPClientHandler::OnTCPClientHandlerTextMessage);
	RemoteConnection->OnByteMessage.AddUObject(this, &UTCPClientHandler::OnTCPClientHandlerByteMessage);

	// thread
	TWeakObjectPtr<UTCPClientHandler> WeakThis(this);
	Async(EAsyncExecution::Thread, [WeakThis, RemoteAddr, Socket, SocketHelperSubsystem]()
	{
		UTCPClientHandler* This = WeakThis.Get();
		if (!IsValid(This) || !This->SocketSubsystem)
		{
			return;
		}

		// fail to connect to remote
		if (!Socket->Connect(*RemoteAddr))
		{
			This->SocketSubsystem->DestroySocket(Socket);

			This->RemoteConnection = nullptr;

			This->OnTCPClientHandlerError(
				0,
				FText::Format(LOCTEXT("ConnectionFail", "Fail to connect to address or port {0}"), FText::FromString(This->Options.RemoteAddress.GetEndpoint())).ToString(),
				ESocketError::Connect_Error);

			return;
		}

		const TSharedRef<FInternetAddr> Local = This->SocketSubsystem->CreateInternetAddr();
		Socket->GetAddress(*Local);

		This->Options.LocalAddress = FSocketHelperAddress(Local);

		// connection thread fail to launch
		if (!This->RemoteConnection->Start(Socket, This->Options.RemoteAddress, This->Options.TextEncoding))
		{
			// close server connection
			This->RemoteConnection->Halt();

			This->SocketSubsystem->DestroySocket(Socket);

			This->RemoteConnection = nullptr;

			This->OnTCPClientHandlerError(0, TEXT("Server connection thread failed to launch"), ESocketError::Invalid_Thread);

			return;
		}

		if (This->OnConnected.IsBound())
		{
			This->OnConnected.Broadcast(This->Options.LocalAddress);
		}

		SocketHelperSubsystem->RegisterTCPClient(This);
	});

	return true;
}

bool UTCPClientHandler::Close()
{
	if (!RemoteConnection.IsValid())
	{
		return false;
	}

	RemoteConnection->Halt();

	if (USocketHelperSubsystem* Subsystem = USocketHelperSubsystem::Get(this))
	{
		Subsystem->UnregisterTCPClient(this);
	}

	Options = FTcpSocketOptions();

	RemoteConnection = nullptr;

	return true;
}

bool UTCPClientHandler::SendText(const FString& Data, int32& ByteSent, ESocketTextEncoding TextEncoding)
{
	return SendBytes(USocketUtility::Encode(Data, TextEncoding), ByteSent);
}

bool UTCPClientHandler::SendBytes(const TArray<uint8>& Data, int32& ByteSent)
{
	if (!RemoteConnection.IsValid())
	{
		return false;
	}

	return RemoteConnection->Send(Data, ByteSent);
}

const FSocketHelperAddress& UTCPClientHandler::GetLocalAddress() const
{
	static const FSocketHelperAddress InvalidAddress;
	return IsConnected() ? Options.LocalAddress : InvalidAddress;
}

void UTCPClientHandler::OnTCPClientHandlerConnectionClosed(bool HasLostConnection, const FSocketHelperAddress& InAddress)
{
	if (OnClosed.IsBound())
	{
		OnClosed.Broadcast(!HasLostConnection);
	}

	if (USocketHelperSubsystem* Subsystem = USocketHelperSubsystem::Get(this))
	{
		Subsystem->UnregisterTCPClient(this);
	}
}

void UTCPClientHandler::OnTCPClientHandlerTextMessage(const FString& Message, const FSocketHelperAddress& InAddress)
{
	if (OnTextMessage.IsBound())
	{
		OnTextMessage.Broadcast(Message);
	}
}

void UTCPClientHandler::OnTCPClientHandlerByteMessage(const TArray<uint8>& Message, const FSocketHelperAddress& InAddress)
{
	if (OnByteMessage.IsBound())
	{
		OnByteMessage.Broadcast(Message);
	}
}

void UTCPClientHandler::OnTCPClientHandlerError(const int32& Code, const FString& Reason, ESocketError Error)
{
	const FString Display = UEnum::GetValueAsString(Error);

	if (OnError.IsBound())
	{
		OnError.Broadcast(Code, Reason, Error);
	}

	UE_LOG(LogTCPClientHandler, Warning, TEXT("TCPClient [%i] [%s] : %s"), Code, *Display, *Reason);
}

#undef LOCTEXT_NAMESPACE