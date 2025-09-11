// Copyright RLoris 2024

#pragma once

#include "Sockets.h"
#include "SocketUtility.h"
#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"

class FSocketConnection final : public FRunnable, public TSharedFromThis<FSocketConnection>
{
public:
	DECLARE_MULTICAST_DELEGATE_TwoParams(FOnClientClosedDelegate, bool /* ConnectionLost */, const FSocketHelperAddress& /* Address */);
	FOnClientClosedDelegate OnClosed;

	DECLARE_MULTICAST_DELEGATE_TwoParams(FOnClientTextMessageDelegate, const FString& /* Message */, const FSocketHelperAddress& /* Address */);
	FOnClientTextMessageDelegate OnTextMessage;

	DECLARE_MULTICAST_DELEGATE_TwoParams(FOnClientByteMessageDelegate, const TArray<uint8>& /* Message */, const FSocketHelperAddress& /* Address */);
	FOnClientByteMessageDelegate OnByteMessage;

	DECLARE_MULTICAST_DELEGATE_ThreeParams(FOnClientErrorDelegate, const int32& /* Code */, const FString& /* Reason */, ESocketError /* EError */);
	FOnClientErrorDelegate OnError;

	bool Send(const FString& Message, int32& ByteSent) const;
	bool Send(const TArray<uint8>& Message, int32& ByteSent) const;

	bool SendTo(const FString& InMessage, int32& OutByteSent, const FSocketHelperAddress& InAddress) const;
	bool SendTo(const TArray<uint8>& InMessage, int32& OutByteSent, const FSocketHelperAddress& InAddress) const;

	const FSocketHelperAddress& GetAddress() const;
	bool IsConnected() const;
	bool IsRunning() const;

	bool Start(FSocket* InSocket, const FSocketHelperAddress& InAddress, const ESocketTextEncoding InTextEncoding);
	void Halt();

	void Resume() const;
	void Pause() const;

private:
	//~ Begin FRunnable
	virtual bool Init() override { return true; }
	virtual uint32 Run() override;
	virtual void Stop() override;
	virtual void Exit() override {}
	//~ End FRunnable

	bool IsUDP() const;
	bool IsConnectionAlive() const;
	bool ReceiveTick();

	/** The thread GUID name */
	FString ThreadName = TEXT("");

	/** The thread for this runnable */
	TUniquePtr<FRunnableThread> Thread = nullptr;

	/** The socket for this ongoing connection */
	TUniquePtr<FSocket> Socket = nullptr;

	/** The address this socket represents */
	FSocketHelperAddress Address;

	/** Text encoding for text exchanges and conversion */
	ESocketTextEncoding TextEncoding = ESocketTextEncoding::UTF_8;

	/** Whether the connection was lost */
	bool bHasLostConnection = false;

	/** Socket subsystem for current platform */
	ISocketSubsystem* SocketSubsystem = nullptr;
};
