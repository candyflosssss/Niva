// Copyright RLoris 2024

#include "TCP/Server/TCPServer.h"

UTCPServer* UTCPServer::AsyncTcpServer(UObject* InWorldContextObject, const FTcpServerOptions& InOptions, UTCPServerHandler*& OutHandler)
{
	UTCPServer* Node = NewObject<UTCPServer>(InWorldContextObject);
	Node->Socket = UTCPServerHandler::CreateSocket(InWorldContextObject);
	OutHandler = Node->Socket;
	Node->Options = InOptions;
	Node->WorldContextObject = InWorldContextObject;
	return Node;
}

void UTCPServer::Activate()
{
	if (nullptr == WorldContextObject)
	{
		FFrame::KismetExecutionMessage(TEXT("Invalid WorldContextObject. Cannot execute TCPServer"), ELogVerbosity::Error);
		OnTCPServerError(0, "Invalid WorldContextObject. Cannot execute TCPServer", ESocketError::Invalid_Context);
		return;
	}

	if (Active)
	{
		FFrame::KismetExecutionMessage(TEXT("TCPServer is already running, stop server and restart to update options"), ELogVerbosity::Warning);
		return;
	}

	// on start
	Socket->OnStart.AddUniqueDynamic(this, &UTCPServer::OnTCPServerStart);

	// on stop
	Socket->OnStop.AddUniqueDynamic(this, &UTCPServer::OnTCPServerStop);

	// on disconnected
	Socket->OnDisconnected.AddUniqueDynamic(this, &UTCPServer::OnTCPServerDisconnected);

	// on connected
	Socket->OnConnected.AddUniqueDynamic(this, &UTCPServer::OnTCPServerConnected);

	// on error
	Socket->OnError.AddUniqueDynamic(this, &UTCPServer::OnTCPServerError);

	// on text message
	Socket->OnTextMessage.AddUniqueDynamic(this, &UTCPServer::OnTCPServerTextMessage);

	// on byte bytes
	Socket->OnByteMessage.AddUniqueDynamic(this, &UTCPServer::OnTCPServerByteMessage);

	// start server
	Active = Socket->Start(Options);
}

void UTCPServer::OnTCPServerStart(const FSocketHelperAddress& InBoundAddress)
{
	Active = Socket->IsRunning();
	if (OnConnected.IsBound())
	{
		FTcpServerResult Result;
		Result.BoundAddress = InBoundAddress;
		OnConnected.Broadcast(Result);
	}
}

void UTCPServer::OnTCPServerStop()
{
	Active = Socket->IsRunning();
	if (OnStop.IsBound())
	{
		FTcpServerResult Result;
		OnStop.Broadcast(Result);
	}
}

void UTCPServer::OnTCPServerDisconnected(const FSocketHelperAddress& DisconnectedAddress, bool HasLostConnection, const int32& Count)
{
	if (OnDisconnected.IsBound())
	{
		FTcpServerResult Result;
		Result.DiconnectedAddress = DisconnectedAddress;
		Result.HasLostConnection = HasLostConnection;
		Result.Clients = Socket->GetClients();
		Result.BoundAddress = Socket->GetBoundAddress();
		OnDisconnected.Broadcast(Result);
	}
}

void UTCPServer::OnTCPServerConnected(const FSocketHelperAddress& ConnectedAddress, const int32& Count)
{
	if (OnConnected.IsBound())
	{
		FTcpServerResult Result;
		Result.ConnectedAddress = ConnectedAddress;
		Result.Clients = Socket->GetClients();
		Result.BoundAddress = Socket->GetBoundAddress();
		OnConnected.Broadcast(Result);
	}
}

void UTCPServer::OnTCPServerTextMessage(const FString& Message, const FSocketHelperAddress& SenderAddress)
{
	if (OnTextMessage.IsBound())
	{
		FTcpServerResult Result;
		Result.TextMessage = Message;
		Result.SenderAddress = SenderAddress;
		Result.Clients = Socket->GetClients();
		Result.BoundAddress = Socket->GetBoundAddress();
		OnTextMessage.Broadcast(Result);
	}
}

void UTCPServer::OnTCPServerByteMessage(const TArray<uint8>& Message, const FSocketHelperAddress& SenderAddress)
{
	if (OnByteMessage.IsBound())
	{
		FTcpServerResult Result;
		Result.ByteMessage = Message;
		Result.SenderAddress = SenderAddress;
		Result.Clients = Socket->GetClients();
		Result.BoundAddress = Socket->GetBoundAddress();
		OnByteMessage.Broadcast(Result);
	}
}

void UTCPServer::OnTCPServerError(const int32& Code, const FString& Reason, ESocketError EError)
{
	Active = Socket->IsRunning();
	if (OnError.IsBound())
	{
		FTcpServerResult Result;
		Result.EError = EError;
		Result.ErrorReason = Reason;
		Result.ErrorCode = Code;
		Result.Clients = Socket->GetClients();
		Result.BoundAddress = Socket->GetBoundAddress();
		OnError.Broadcast(Result);
	}
}
