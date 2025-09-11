// Copyright RLoris 2024

#include "UDP/UDPSocket.h"

UUDPSocket* UUDPSocket::AsyncUdpSocket(UObject* InWorldContextObject, const FUdpSocketOptions& InOptions, UUDPSocketHandler*& OutHandler)
{
	UUDPSocket* Node = NewObject<UUDPSocket>();
	Node->Socket = UUDPSocketHandler::CreateSocket(InWorldContextObject);
	Node->Options = InOptions;
	OutHandler = Node->Socket;
	return Node;
}

void UUDPSocket::Activate()
{
	if (Active)
	{
		FFrame::KismetExecutionMessage(TEXT("UDPSocket is already running, close socket and restart to update options"), ELogVerbosity::Warning);
		return;
	}

	// on connected
	Socket->OnConnected.AddUniqueDynamic(this, &UUDPSocket::OnUDPSocketConnected);

	// on closed
	Socket->OnClosed.AddUniqueDynamic(this, &UUDPSocket::OnUDPSocketClosed);

	// on error
	Socket->OnError.AddUniqueDynamic(this, &UUDPSocket::OnUDPSocketError);

	// on text message
	Socket->OnTextMessage.AddUniqueDynamic(this, &UUDPSocket::OnUDPSocketTextMessage);

	// on text bytes
	Socket->OnByteMessage.AddUniqueDynamic(this, &UUDPSocket::OnUDPSocketByteMessage);

	// open connection
	Active = Socket->Open(Options);
}

void UUDPSocket::OnUDPSocketConnected(bool IsBound, const FSocketHelperAddress& BoundAddress)
{
	Active = Socket->IsRunning();
	if (OnConnected.IsBound())
	{
		FUdpSocketResult Result;
		Result.IsBound = IsBound;
		Result.BoundAddress = BoundAddress;
		OnConnected.Broadcast(Result);
	}
}

void UUDPSocket::OnUDPSocketClosed()
{
	Active = Socket->IsRunning();
	if (OnClosed.IsBound())
	{
		FUdpSocketResult Result;
		OnClosed.Broadcast(Result);
	}
}

void UUDPSocket::OnUDPSocketError(const int32& Code, const FString& Reason, ESocketError Error)
{
	Active = Socket->IsRunning();
	if (OnError.IsBound())
	{
		FUdpSocketResult Result;
		Result.ErrorCode = Code;
		Result.ErrorReason = Error;
		Result.Error = Reason;
		OnError.Broadcast(Result);
	}
}

void UUDPSocket::OnUDPSocketTextMessage(const FString& Message, const FSocketHelperAddress& SenderAddress)
{
	if (OnTextMessage.IsBound())
	{
		FUdpSocketResult Result;
		Result.SenderAddress = SenderAddress;
		Result.TextMessage = Message;
		OnTextMessage.Broadcast(Result);
	}
}

void UUDPSocket::OnUDPSocketByteMessage(const TArray<uint8>& Message, const FSocketHelperAddress& SenderAddress)
{
	if (OnBytesMessage.IsBound())
	{
		FUdpSocketResult Result;
		Result.SenderAddress = SenderAddress;
		Result.BytesMessage = Message;
		OnBytesMessage.Broadcast(Result);
	}
}
