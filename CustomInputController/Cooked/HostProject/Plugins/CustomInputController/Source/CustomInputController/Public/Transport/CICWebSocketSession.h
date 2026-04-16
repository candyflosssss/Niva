#pragma once

#include "CoreMinimal.h"
#include "IWebSocket.h"
#include "Templates/SharedPointer.h"

DECLARE_DELEGATE(FCICWebSocketConnected);
DECLARE_DELEGATE_OneParam(FCICWebSocketConnectionError, const FString& /*Error*/);
DECLARE_DELEGATE_ThreeParams(FCICWebSocketClosed, int32 /*StatusCode*/, const FString& /*Reason*/, bool /*bWasClean*/);
DECLARE_DELEGATE_OneParam(FCICWebSocketTextMessage, const FString& /*Message*/);
DECLARE_DELEGATE_OneParam(FCICWebSocketBinaryMessage, const TArray<uint8>& /*Data*/);

/**
 * Shared transport-layer WebSocket session.
 * Owns IWebSocket, assembles binary fragments into complete frames,
 * and exposes transport-level delegates without business/session logic.
 */
class CUSTOMINPUTCONTROLLER_API FCICWebSocketSession : public TSharedFromThis<FCICWebSocketSession>
{
public:
	FCICWebSocketSession();
	~FCICWebSocketSession();

	bool Connect(const FString& InUrl, const TMap<FString, FString>& InHeaders = TMap<FString, FString>());
	void Close(bool bManualClose = true);

	bool SendText(const FString& Message) const;
	bool SendBinary(const void* Data, SIZE_T Size, bool bIsBinary = true) const;
	bool SendBinary(const TArray<uint8>& Data, bool bIsBinary = true) const;

	bool IsConnected() const;
	bool IsConnecting() const { return bIsConnecting; }
	bool WasClosedManually() const { return bClosedManually; }
	const FString& GetActiveUrl() const { return ActiveUrl; }

	FCICWebSocketConnected OnConnected;
	FCICWebSocketConnectionError OnConnectionError;
	FCICWebSocketClosed OnClosed;
	FCICWebSocketTextMessage OnTextMessage;
	FCICWebSocketBinaryMessage OnBinaryMessage;

private:
	void EnsureModuleLoaded();
	void ResetSocketState(bool bResetManualClose);

	void HandleConnected();
	void HandleConnectionError(const FString& Error);
	void HandleClosed(int32 StatusCode, const FString& Reason, bool bWasClean);
	void HandleMessage(const FString& Message);
	void HandleRawMessage(const void* Data, SIZE_T Size, SIZE_T BytesRemaining);

private:
	TSharedPtr<IWebSocket> Socket;
	FString ActiveUrl;
	TMap<FString, FString> ActiveHeaders;
	TArray<uint8> PendingBinary;
	bool bIsConnecting = false;
	bool bClosedManually = false;
};

