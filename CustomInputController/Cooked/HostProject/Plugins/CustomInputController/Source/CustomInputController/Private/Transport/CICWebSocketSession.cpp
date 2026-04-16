#include "Transport/CICWebSocketSession.h"

#include "Modules/ModuleManager.h"
#include "WebSocketsModule.h"

FCICWebSocketSession::FCICWebSocketSession()
{
}

FCICWebSocketSession::~FCICWebSocketSession()
{
	Close(true);
}

bool FCICWebSocketSession::Connect(const FString& InUrl, const TMap<FString, FString>& InHeaders)
{
	if (InUrl.IsEmpty())
	{
		return false;
	}

	EnsureModuleLoaded();
	Close(false);

	ActiveUrl = InUrl;
	ActiveHeaders = InHeaders;
	PendingBinary.Reset();
	bIsConnecting = true;
	bClosedManually = false;

	Socket = FWebSocketsModule::Get().CreateWebSocket(ActiveUrl, TEXT(""), ActiveHeaders);
	if (!Socket.IsValid())
	{
		bIsConnecting = false;
		return false;
	}

	Socket->OnConnected().AddRaw(this, &FCICWebSocketSession::HandleConnected);
	Socket->OnConnectionError().AddRaw(this, &FCICWebSocketSession::HandleConnectionError);
	Socket->OnClosed().AddRaw(this, &FCICWebSocketSession::HandleClosed);
	Socket->OnMessage().AddRaw(this, &FCICWebSocketSession::HandleMessage);
	Socket->OnRawMessage().AddRaw(this, &FCICWebSocketSession::HandleRawMessage);
	Socket->Connect();
	return true;
}

void FCICWebSocketSession::Close(bool bManualClose)
{
	bClosedManually = bManualClose;
	bIsConnecting = false;
	PendingBinary.Reset();

	if (Socket.IsValid())
	{
		Socket->OnConnected().Clear();
		Socket->OnConnectionError().Clear();
		Socket->OnClosed().Clear();
		Socket->OnMessage().Clear();
		Socket->OnRawMessage().Clear();
		Socket->Close();
		Socket.Reset();
	}
}

bool FCICWebSocketSession::SendText(const FString& Message) const
{
	if (!Socket.IsValid() || !Socket->IsConnected())
	{
		return false;
	}

	Socket->Send(Message);
	return true;
}

bool FCICWebSocketSession::SendBinary(const void* Data, SIZE_T Size, bool bIsBinary) const
{
	if (!Socket.IsValid() || !Socket->IsConnected() || Data == nullptr || Size == 0)
	{
		return false;
	}

	Socket->Send(Data, Size, bIsBinary);
	return true;
}

bool FCICWebSocketSession::SendBinary(const TArray<uint8>& Data, bool bIsBinary) const
{
	return SendBinary(Data.GetData(), Data.Num(), bIsBinary);
}

bool FCICWebSocketSession::IsConnected() const
{
	return Socket.IsValid() && Socket->IsConnected();
}

void FCICWebSocketSession::EnsureModuleLoaded()
{
	if (!FModuleManager::Get().IsModuleLoaded("WebSockets"))
	{
		FModuleManager::Get().LoadModuleChecked<IModuleInterface>("WebSockets");
	}
}

void FCICWebSocketSession::ResetSocketState(bool bResetManualClose)
{
	bIsConnecting = false;
	PendingBinary.Reset();
	Socket.Reset();
	if (bResetManualClose)
	{
		bClosedManually = false;
	}
}

void FCICWebSocketSession::HandleConnected()
{
	bIsConnecting = false;
	if (OnConnected.IsBound())
	{
		OnConnected.Execute();
	}
}

void FCICWebSocketSession::HandleConnectionError(const FString& Error)
{
	ResetSocketState(false);
	if (OnConnectionError.IsBound())
	{
		OnConnectionError.Execute(Error);
	}
}

void FCICWebSocketSession::HandleClosed(int32 StatusCode, const FString& Reason, bool bWasClean)
{
	ResetSocketState(false);
	if (OnClosed.IsBound())
	{
		OnClosed.Execute(StatusCode, Reason, bWasClean);
	}
}

void FCICWebSocketSession::HandleMessage(const FString& Message)
{
	if (OnTextMessage.IsBound())
	{
		OnTextMessage.Execute(Message);
	}
}

void FCICWebSocketSession::HandleRawMessage(const void* Data, SIZE_T Size, SIZE_T BytesRemaining)
{
	if (Data == nullptr || Size == 0)
	{
		return;
	}

	const int32 PreviousNum = PendingBinary.Num();
	PendingBinary.AddUninitialized(static_cast<int32>(Size));
	FMemory::Memcpy(PendingBinary.GetData() + PreviousNum, Data, Size);

	if (BytesRemaining == 0)
	{
		if (OnBinaryMessage.IsBound())
		{
			OnBinaryMessage.Execute(PendingBinary);
		}
		PendingBinary.Reset();
	}
}

