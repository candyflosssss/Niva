#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "IWebSocket.h"
#include "FunASRSubsystem.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FFunASRVoidDelegate);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FFunASRStringDelegate, const FString&, Message);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FFunASRResultDelegate, const FString&, Text, bool, bIsSentenceEnd);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FFunASRTaskStateDelegate);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FFunASRFullResultDelegate, const FString&, FullText);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FFunASRErrorDelegate, const FString&, ErrorMessage);

/**
 * Subsystem to handle FunASR (Aliyun) Real-time Speech Recognition
 */
UCLASS()
class CUSTOMINPUTCONTROLLER_API UFunASRSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	// Subsystem Lifecycle
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** Start ASR Task - Connects if not connected, sends run-task */
	UFUNCTION(BlueprintCallable, Category="FunASR")
	void StartASR();

	/** Stop ASR Task - Sends finish-task, waits for server confirmation */
	UFUNCTION(BlueprintCallable, Category="FunASR")
	void StopASR();

	/** Send audio frame (should match SampleRate & Format, usually PCM) */
	UFUNCTION(BlueprintCallable, Category="FunASR")
	void SendAudioFrame(const TArray<uint8>& AudioData);

	/** Force Close Connection */
	UFUNCTION(BlueprintCallable, Category="FunASR")
	void ForceDisconnect();

	// Event Delegates
	UPROPERTY(BlueprintAssignable, Category="FunASR")
	FFunASRResultDelegate OnResultReceived;

	UPROPERTY(BlueprintAssignable, Category="FunASR")
	FFunASRTaskStateDelegate OnTaskStarted;

	UPROPERTY(BlueprintAssignable, Category="FunASR")
	FFunASRTaskStateDelegate OnTaskFinished;

	/** 新增：任务彻底结束时，返回本次会话积累的所有文本 */
	UPROPERTY(BlueprintAssignable, Category="FunASR")
	FFunASRFullResultDelegate OnTaskCompletedWithFullText;

	UPROPERTY(BlueprintAssignable, Category="FunASR")
	FFunASRErrorDelegate OnError;

	UFUNCTION(BlueprintPure, Category="FunASR")
	bool IsTaskRunning() const { return bIsTaskRunning; }

private:
	void ConnectWebSocket();
	
	// WebSocket Callbacks
	void OnWsConnected();
	void OnWsConnectionError(const FString& Error);
	void OnWsClosed(int32 StatusCode, const FString& Reason, bool bWasClean);
	void OnWsMessage(const FString& Message);

	// Protocol methods
	void SendRunTask();
	void SendFinishTask();
	
	void ProcessJsonMessage(const FString& Message);

    // Reconnection
    void CheckRetry();
    void ScheduleReconnect();

	TSharedPtr<IWebSocket> WebSocket;
	FString CurrentTaskId;
	
	// Cache for the full text of the current session
	FString CurrentTaskFullText;

	// State
	bool bIsWebSocketConnected = false;
	bool bIsTaskRunning = false;
    bool bStartRequested = false; // Flag to send run-task once connected

    // Retry State
    FTimerHandle TimerHandle_Reconnect;
    int32 CurrentRetryCount = 0;
};
