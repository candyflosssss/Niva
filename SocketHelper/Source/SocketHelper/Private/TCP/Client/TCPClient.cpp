// Copyright RLoris 2024

#include "TCP/Client/TCPClient.h"

UTCPClient* UTCPClient::AsyncTcpSocket(UObject* InWorldContextObject, const FTcpSocketOptions& InOptions, UTCPClientHandler*& OutHandler)
{
	UTCPClient* Node = NewObject<UTCPClient>(InWorldContextObject);
	Node->Socket = UTCPClientHandler::CreateSocket(InWorldContextObject);
	OutHandler = Node->Socket;
	Node->Options = InOptions;
	Node->WorldContextObject = InWorldContextObject;
	return Node;
}

void UTCPClient::Activate()
{
	if (nullptr == WorldContextObject)
	{
		FFrame::KismetExecutionMessage(TEXT("Invalid WorldContextObject. Cannot execute TCPSocket"), ELogVerbosity::Error);
		OnTCPClientError(0, "Invalid WorldContextObject. Cannot execute TCPSocket", ESocketError::Invalid_Socket);
		return;
	}

	if (Active)
	{
		FFrame::KismetExecutionMessage(TEXT("TCPSocket is already running, close socket and restart to update options"), ELogVerbosity::Warning);
		return;
	}
	// on connected
	Socket->OnConnected.AddUniqueDynamic(this, &UTCPClient::OnTCPClientConnected);

	// on closed
	Socket->OnClosed.AddUniqueDynamic(this, &UTCPClient::OnTCPClientClosed);

	// on error
	Socket->OnError.AddUniqueDynamic(this, &UTCPClient::OnTCPClientError);

	// on text message
	Socket->OnTextMessage.AddUniqueDynamic(this, &UTCPClient::OnTCPClientTextMessage);

	// on text bytes
	Socket->OnByteMessage.AddUniqueDynamic(this, &UTCPClient::OnTCPClientByteMessage);

	// open connection
	Active = Socket->Open(Options);
}

void UTCPClient::OnTCPClientConnected(const FSocketHelperAddress& InAddress)
{
	Active = Socket->IsConnected();
	if (OnConnected.IsBound())
	{
		FTcpSocketResult Result;
		Result.BoundAddress = InAddress;
		OnConnected.Broadcast(Result);
	}
}

void UTCPClient::OnTCPClientClosed(bool ByClient)
{
	Active = Socket->IsConnected();
	if (OnClosed.IsBound())
	{
		FTcpSocketResult Result;
		Result.ClosedByClient = ByClient;
		OnClosed.Broadcast(Result);
	}
}

void UTCPClient::OnTCPClientError(const int32& Code, const FString& Reason, ESocketError Error)
{
	Active = Socket->IsConnected();
	if (OnError.IsBound())
	{
		FTcpSocketResult Result;
		Result.ErrorCode = Code;
		Result.ErrorReason = Error;
		Result.Error = Reason;
		OnError.Broadcast(Result);
	}
}

void UTCPClient::OnTCPClientTextMessage(const FString& Message)
{
	if (OnTextMessage.IsBound())
	{
		FTcpSocketResult Result;
		Result.TextMessage = Message;
		OnTextMessage.Broadcast(Result);
	}
}

void UTCPClient::OnTCPClientByteMessage(const TArray<uint8>& Message)
{
	if (OnBytesMessage.IsBound())
	{
		FTcpSocketResult Result;
		Result.BytesMessage = Message;
		OnBytesMessage.Broadcast(Result);
	}
}
