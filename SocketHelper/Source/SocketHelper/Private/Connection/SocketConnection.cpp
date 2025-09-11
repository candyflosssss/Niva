// Copyright RLoris 2024

#include "Connection/SocketConnection.h"

#include "Async/TaskGraphInterfaces.h"

DEFINE_LOG_CATEGORY_STATIC(LogTCPSocketConnection, Log, All);

uint32 FSocketConnection::Run()
{
	while (IsConnected())
	{
		if (!IsUDP() && !IsConnectionAlive())
		{
			bHasLostConnection = true;
			Halt();
			continue;
		}

		if (ReceiveTick())
		{
			Socket->Wait(ESocketWaitConditions::WaitForReadOrWrite, FTimespan::FromMilliseconds(100));
		}
	}

	return 0;
}

void FSocketConnection::Stop()
{
	if (!Socket.IsValid())
	{
		return;
	}

	FSocket* SocketPtr = Socket.Release();

	UE_LOG(LogTCPSocketConnection, Log, TEXT("Stopping Socket Connection Thread : %s Description : %s Address : %s"), *ThreadName, *SocketPtr->GetDescription(), *Address.GetEndpoint());

	const ESocketConnectionState PrevState = SocketPtr->GetConnectionState();

	if (!SocketPtr->Close())
	{
		if (SocketSubsystem
			&& PrevState == ESocketConnectionState::SCS_Connected
			&& OnError.IsBound())
		{
			const FString Error = SocketSubsystem->GetSocketError();
			const int32 Code = SocketSubsystem->GetLastErrorCode();

			OnError.Broadcast(Code, Error, ESocketError::Close_Error);
		}
	}

	if (SocketSubsystem)
	{
		SocketSubsystem->DestroySocket(SocketPtr);
	}

	if (OnClosed.IsBound())
	{
		OnClosed.Broadcast(bHasLostConnection, Address);
	}
}

bool FSocketConnection::Send(const FString& Message, int32& ByteSent) const
{
	return Send(USocketUtility::Encode(Message, TextEncoding), ByteSent);
}

bool FSocketConnection::Send(const TArray<uint8>& Message, int32& ByteSent) const
{
	if (IsConnected())
	{
		ByteSent = 0;
		return Socket->Send(Message.GetData(), Message.Num(), ByteSent);
	}

	return false;
}

bool FSocketConnection::SendTo(const FString& InMessage, int32& OutByteSent, const FSocketHelperAddress& InAddress) const
{
	return SendTo(USocketUtility::Encode(InMessage, TextEncoding), OutByteSent, InAddress);
}

bool FSocketConnection::SendTo(const TArray<uint8>& InMessage, int32& OutByteSent, const FSocketHelperAddress& InAddress) const
{
	if (!InAddress.IsValidAddress())
	{
		UE_LOG(LogTCPSocketConnection, Warning, TEXT("Invalid destination Ip or port provided to send message"));

		if (OnError.IsBound())
		{
			OnError.Broadcast(0, TEXT("Invalid destination Ip or port provided"), ESocketError::Invalid_Address);
		}

		return false;
	}

	if (IsConnected())
	{
		OutByteSent = 0;
		const TSharedPtr<FInternetAddr> Dest = InAddress.GetInternetAddress();
		return Socket->SendTo(InMessage.GetData(), InMessage.Num(), OutByteSent, *Dest);
	}

	return false;
}

const FSocketHelperAddress& FSocketConnection::GetAddress() const
{
	return Address;
}

bool FSocketConnection::IsConnectionAlive() const
{
	if (Socket.IsValid())
	{
		TArray<uint8> Bytes;
		Bytes.Init(0, 1);
		int32 Read = 0;

		return Socket->Recv(Bytes.GetData(), Bytes.Num(), Read, ESocketReceiveFlags::Peek);
	}

	return false;
}

bool FSocketConnection::IsConnected() const
{
	if (!Socket.IsValid())
	{
		return false;
	}

	if (Socket->GetSocketType() == SOCKTYPE_Datagram)
	{
		return true;
	}

	return !bHasLostConnection && Socket->GetConnectionState() == ESocketConnectionState::SCS_Connected;
}

bool FSocketConnection::IsRunning() const
{
	return Socket.IsValid();
}

bool FSocketConnection::Start(FSocket* InSocket, const FSocketHelperAddress& InAddress, const ESocketTextEncoding InTextEncoding)
{
	if (!InSocket)
	{
		UE_LOG(LogTCPSocketConnection, Warning, TEXT("Invalid socket provided to connection thread"));
		return false;
	}

	if (Socket.IsValid() || Thread.IsValid())
	{
		UE_LOG(LogTCPSocketConnection, Warning, TEXT("Existing socket found on connection thread"));
		return false;
	}

	bHasLostConnection = false;
	Socket = TUniquePtr<FSocket>(InSocket);
	Address = InAddress;
	TextEncoding = InTextEncoding;

	SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);

	ThreadName = FGuid::NewGuid().ToString();
	Thread = TUniquePtr<FRunnableThread>(FRunnableThread::Create(this, *ThreadName));

	if (!Thread.IsValid())
	{
		UE_LOG(LogTCPSocketConnection, Warning, TEXT("Could not start thread for connection thread"));
		return false;
	}

	UE_LOG(LogTCPSocketConnection, Log, TEXT("Start Socket Connection Thread : %s Description : %s Address : %s"), *ThreadName, *Socket->GetDescription(), *Address.GetEndpoint());

	return true;
}

void FSocketConnection::Halt()
{
	if (Thread.IsValid())
	{
		FRunnableThread* Runnable = Thread.Release();
		Runnable->Kill(true);
	}
}

void FSocketConnection::Resume() const
{
	if (Thread.IsValid())
	{
		Thread->Suspend(false);
	}
}

void FSocketConnection::Pause() const
{
	if (Thread.IsValid())
	{
		Thread->Suspend(true);
	}
}

bool FSocketConnection::IsUDP() const
{
	return Socket.IsValid() && Socket->GetSocketType() == SOCKTYPE_Datagram;
}

bool FSocketConnection::ReceiveTick()
{
	if (!Socket.IsValid())
	{
		return false;
	}

	uint32 Size = 0;

	if (!Socket->HasPendingData(Size))
	{
		return true;
	}

	int32 Read = 0;
	TArray<uint8> ReceiveBytes;
	ReceiveBytes.Init(0, Size);
	FSocketHelperAddress SenderAddress;

	if (IsUDP())
	{
		const TSharedRef<FInternetAddr> Sender = SocketSubsystem->CreateInternetAddr();
		Socket->RecvFrom(ReceiveBytes.GetData(), ReceiveBytes.Num(), Read, *Sender);
		SenderAddress = FSocketHelperAddress(Sender);
	}
	else
	{
		Socket->Recv(ReceiveBytes.GetData(), ReceiveBytes.Num(), Read, ESocketReceiveFlags::None);
		SenderAddress = GetAddress();
	}

	TWeakPtr<FSocketConnection> ThisWeak(AsWeak());
	FFunctionGraphTask::CreateAndDispatchWhenReady([ThisWeak, ReceiveBytes, SenderAddress]()
	{
		const TSharedPtr<FSocketConnection> This = ThisWeak.Pin();
		if (!This.IsValid())
		{
			return;
		}

		if (This->OnByteMessage.IsBound())
		{
			This->OnByteMessage.Broadcast(ReceiveBytes, SenderAddress);
		}

		if (This->OnTextMessage.IsBound())
		{
			TArray<uint8> CopyBytes(ReceiveBytes);
			CopyBytes.Add(0);
			This->OnTextMessage.Broadcast(USocketUtility::Decode(CopyBytes, This->TextEncoding), SenderAddress);
		}
	}
	, TStatId(), nullptr, ENamedThreads::GameThread);

	return true;
}
