#include "ASR/FunASRSubsystem.h"
#include "ASR/FunASRSettings.h"
#include "WebSocketsModule.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/Guid.h"
#include "Async/Async.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "Log/CoreLogHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogFunASR, Log, All);

void UFunASRSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	UE_LOG(LogFunASR, Log, TEXT("FunASR Subsystem Initialized"));
	FCoreLogHelpers::CoreLog(this, ECoreLogSeverity::Info, TEXT("CIC"), TEXT("ASR"), TEXT("FunASR Subsystem Initialized"));
}

void UFunASRSubsystem::Deinitialize()
{
	FCoreLogHelpers::CoreLog(this, ECoreLogSeverity::Info, TEXT("CIC"), TEXT("ASR"), TEXT("FunASR Subsystem Deinitialize"));
	ForceDisconnect();
	Super::Deinitialize();
}

void UFunASRSubsystem::ForceDisconnect()
{
	bIsTaskRunning = false;
	bIsWebSocketConnected = false;
	bStartRequested = false;
	CurrentRetryCount = 0; // Reset retry count

	if (GetWorld())
	{
		GetWorld()->GetTimerManager().ClearTimer(TimerHandle_Reconnect);
	}

	if (WebSocket.IsValid())
	{
		WebSocket->OnConnected().Clear();
		WebSocket->OnConnectionError().Clear();
		WebSocket->OnClosed().Clear();
		WebSocket->OnMessage().Clear();
		WebSocket->Close();
		WebSocket.Reset();
	}
}

void UFunASRSubsystem::StartASR()
{
	if (bIsTaskRunning)
	{
		UE_LOG(LogFunASR, Warning, TEXT("ASR Task already running, ignoring StartASR"));
		FCoreLogHelpers::CoreLog(this, ECoreLogSeverity::Warn, TEXT("CIC"), TEXT("ASR"), TEXT("ASR Task already running, ignoring StartASR"));
		return;
	}

	CurrentRetryCount = 0; // Reset retry on fresh start
	FCoreLogHelpers::CoreLog(this, ECoreLogSeverity::Info, TEXT("CIC"), TEXT("ASR"), TEXT("Starting ASR Task (Manual Start)"));

	if (bIsWebSocketConnected && WebSocket.IsValid())
	{
		SendRunTask();
	}
	else
	{
		// Need connect first
		bStartRequested = true;
		ConnectWebSocket();
	}
}

void UFunASRSubsystem::StopASR()
{
	// Cancel any pending reconnects
	if (GetWorld())
	{
		GetWorld()->GetTimerManager().ClearTimer(TimerHandle_Reconnect);
	}

	FCoreLogHelpers::CoreLog(this, ECoreLogSeverity::Info, TEXT("CIC"), TEXT("ASR"), TEXT("Stopping ASR Task (Manual Stop)"));

	if (bIsTaskRunning)
	{
		SendFinishTask();
		// We expect 'task-finished' event to clear bIsTaskRunning
	}
}

void UFunASRSubsystem::SendAudioFrame(const TArray<uint8>& AudioData)
{
	if (bIsTaskRunning && WebSocket.IsValid() && bIsWebSocketConnected)
	{
		// Send binary frame
		WebSocket->Send(AudioData.GetData(), AudioData.Num(), true);
		
		// [Debug] Log periodically to confirm data is hitting the wire
		static double LastSendLogTime = 0.0;
		double Now = FPlatformTime::Seconds();
		if (Now - LastSendLogTime > 2.0)
		{
			FCoreLogHelpers::CoreLog(this, ECoreLogSeverity::Info, TEXT("CIC"), TEXT("ASR"), 
				FString::Printf(TEXT("Payload sent to WebSocket: %d bytes (Stream active)"), AudioData.Num()));
			LastSendLogTime = Now;
		}
	}
	else if (!bIsTaskRunning)
	{
		// This might explain why data is dropped if task-started hasn't arrived yet or failed
		// But usually we sync this.
	}
}

void UFunASRSubsystem::ConnectWebSocket()
{
	if (WebSocket.IsValid())
	{
		// Already connecting or connected
		return;
	}

	const UFunASRSettings* Settings = UFunASRSettings::Get();
	if (!Settings) return;

	FString Url = Settings->WebSocketUrl;
	// No protocol check usually needed if FWebSocketsModule handles it, but verify wss/ws
	
	TMap<FString, FString> Headers;
	if (!Settings->ApiKey.IsEmpty())
	{
		Headers.Add(TEXT("Authorization"), FString::Printf(TEXT("Bearer %s"), *Settings->ApiKey));
	}
	if (!Settings->WorkspaceId.IsEmpty())
	{
		Headers.Add(TEXT("X-DashScope-WorkSpace"), Settings->WorkspaceId);
	}
	if (Settings->bEnableDataInspection)
	{
	    Headers.Add(TEXT("X-DashScope-DataInspection"), TEXT("enable"));
	}

    UE_LOG(LogFunASR, Log, TEXT("Connecting to FunASR: %s (Retry %d)"), *Url, CurrentRetryCount);
    FCoreLogHelpers::CoreLog(this, ECoreLogSeverity::Info, TEXT("CIC"), TEXT("ASR"), FString::Printf(TEXT("Connecting to FunASR: %s (Retry %d)"), *Url, CurrentRetryCount));

	WebSocket = FWebSocketsModule::Get().CreateWebSocket(Url, TEXT(""), Headers);

	WebSocket->OnConnected().AddUObject(this, &UFunASRSubsystem::OnWsConnected);
	WebSocket->OnConnectionError().AddUObject(this, &UFunASRSubsystem::OnWsConnectionError);
	WebSocket->OnClosed().AddUObject(this, &UFunASRSubsystem::OnWsClosed);
	WebSocket->OnMessage().AddUObject(this, &UFunASRSubsystem::OnWsMessage);
	
	WebSocket->Connect();
}

void UFunASRSubsystem::OnWsConnected()
{
	UE_LOG(LogFunASR, Log, TEXT("FunASR WebSocket Connected"));
	FCoreLogHelpers::CoreLog(this, ECoreLogSeverity::Info, TEXT("CIC"), TEXT("ASR"), TEXT("FunASR WebSocket Connected"));
	bIsWebSocketConnected = true;
	CurrentRetryCount = 0; // Connected successfully, reset retries

	if (bStartRequested)
	{
		bStartRequested = false;
		SendRunTask();
	}
}

void UFunASRSubsystem::OnWsConnectionError(const FString& Error)
{
	UE_LOG(LogFunASR, Error, TEXT("FunASR Connection Error: %s"), *Error);
	FCoreLogHelpers::CoreLog(this, ECoreLogSeverity::Error, TEXT("CIC"), TEXT("ASR"), FString::Printf(TEXT("FunASR Connection Error: %s"), *Error));
	bIsWebSocketConnected = false;
	bIsTaskRunning = false;
	WebSocket.Reset();
	
	OnError.Broadcast(TEXT("Connection Error: ") + Error);

	ScheduleReconnect();
}

void UFunASRSubsystem::OnWsClosed(int32 StatusCode, const FString& Reason, bool bWasClean)
{
	UE_LOG(LogFunASR, Log, TEXT("FunASR WebSocket Closed. Code: %d, Reason: %s"), StatusCode, *Reason);
	FCoreLogHelpers::CoreLog(this, ECoreLogSeverity::Info, TEXT("CIC"), TEXT("ASR"), FString::Printf(TEXT("FunASR WebSocket Closed. Code: %d, Reason: %s"), StatusCode, *Reason));
	bIsWebSocketConnected = false;
	bIsTaskRunning = false;
	WebSocket.Reset();

	// If closed unexpectedly while running task
	if (!bWasClean)
	{
		// Maybe broadcast disconnect
		OnError.Broadcast(TEXT("Disconnected unexpectedly"));
		FCoreLogHelpers::CoreLog(this, ECoreLogSeverity::Warn, TEXT("CIC"), TEXT("ASR"), TEXT("WebSocket closed unexpectedly, scheduling reconnect"));
		ScheduleReconnect();
	}
}

void UFunASRSubsystem::ScheduleReconnect()
{
	const UFunASRSettings* Settings = UFunASRSettings::Get();
	if (!Settings || !Settings->bAutoReconnect) return;

	if (Settings->MaxReconnectAttempts > 0 && CurrentRetryCount >= Settings->MaxReconnectAttempts)
	{
		UE_LOG(LogFunASR, Warning, TEXT("Max reconnect attempts reached. Giving up."));
		return;
	}

	if (GetWorld())
	{
		UE_LOG(LogFunASR, Log, TEXT("Scheduling reconnect in %f seconds..."), Settings->ReconnectInterval);
		GetWorld()->GetTimerManager().SetTimer(TimerHandle_Reconnect, this, &UFunASRSubsystem::CheckRetry, Settings->ReconnectInterval, false);
	}
}

void UFunASRSubsystem::CheckRetry()
{
	CurrentRetryCount++;
	FCoreLogHelpers::CoreLog(this, ECoreLogSeverity::Info, TEXT("CIC"), TEXT("ASR"), FString::Printf(TEXT("Retry attempt %d..."), CurrentRetryCount));
	bStartRequested = true;
	ConnectWebSocket();
}

void UFunASRSubsystem::OnWsMessage(const FString& Message)
{
	ProcessJsonMessage(Message);
}

void UFunASRSubsystem::SendRunTask()
{
	const UFunASRSettings* Settings = UFunASRSettings::Get();
	if (!Settings) return;

	CurrentTaskId = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens).ToLower();

	TSharedPtr<FJsonObject> RootObj = MakeShareable(new FJsonObject);
	TSharedPtr<FJsonObject> HeaderObj = MakeShareable(new FJsonObject);
	TSharedPtr<FJsonObject> PayloadObj = MakeShareable(new FJsonObject);

	// Header
	HeaderObj->SetStringField(TEXT("action"), TEXT("run-task"));
	HeaderObj->SetStringField(TEXT("task_id"), CurrentTaskId);
	HeaderObj->SetStringField(TEXT("streaming"), TEXT("duplex"));

	// Reset Full Text Cache on Start
	CurrentTaskFullText = TEXT(""); 

	// Payload
	PayloadObj->SetStringField(TEXT("task_group"), TEXT("audio"));
	PayloadObj->SetStringField(TEXT("task"), TEXT("asr"));
	PayloadObj->SetStringField(TEXT("function"), TEXT("recognition"));
	PayloadObj->SetStringField(TEXT("model"), Settings->Model);

	// Parameters
	TSharedPtr<FJsonObject> ParamsObj = MakeShareable(new FJsonObject);
	ParamsObj->SetStringField(TEXT("format"), Settings->Format);
	ParamsObj->SetNumberField(TEXT("sample_rate"), Settings->SampleRate);
	
	if (Settings->bSemanticPunctuation)
	{
		ParamsObj->SetBoolField(TEXT("semantic_punctuation_enabled"), true);
	}
	else
	{
		ParamsObj->SetBoolField(TEXT("semantic_punctuation_enabled"), false);
		ParamsObj->SetNumberField(TEXT("max_sentence_silence"), Settings->MaxSentenceSilence);
	}
	
	// Language Hints
	if (Settings->LanguageHints.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> LangHintsArray;
		for (const FString& Hint : Settings->LanguageHints)
		{
			LangHintsArray.Add(MakeShareable(new FJsonValueString(Hint)));
		}
		ParamsObj->SetArrayField(TEXT("language_hints"), LangHintsArray);
	}

	PayloadObj->SetObjectField(TEXT("parameters"), ParamsObj);
	PayloadObj->SetObjectField(TEXT("input"), MakeShareable(new FJsonObject));

	RootObj->SetObjectField(TEXT("header"), HeaderObj);
	RootObj->SetObjectField(TEXT("payload"), PayloadObj);

	FString OutputString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
	FJsonSerializer::Serialize(RootObj.ToSharedRef(), Writer);

	if (WebSocket.IsValid() && bIsWebSocketConnected)
	{
		WebSocket->Send(OutputString);
		UE_LOG(LogFunASR, Log, TEXT("Sent run-task: %s"), *CurrentTaskId);
		FCoreLogHelpers::CoreLog(this, ECoreLogSeverity::Info, TEXT("CIC"), TEXT("ASR"), FString::Printf(TEXT("Sent run-task: %s"), *CurrentTaskId));
	}
}

void UFunASRSubsystem::SendFinishTask()
{
	if (CurrentTaskId.IsEmpty()) return;

	TSharedPtr<FJsonObject> RootObj = MakeShareable(new FJsonObject);
	TSharedPtr<FJsonObject> HeaderObj = MakeShareable(new FJsonObject);
	TSharedPtr<FJsonObject> PayloadObj = MakeShareable(new FJsonObject);

	HeaderObj->SetStringField(TEXT("action"), TEXT("finish-task"));
	HeaderObj->SetStringField(TEXT("task_id"), CurrentTaskId);
	HeaderObj->SetStringField(TEXT("streaming"), TEXT("duplex"));

	PayloadObj->SetObjectField(TEXT("input"), MakeShareable(new FJsonObject));

	RootObj->SetObjectField(TEXT("header"), HeaderObj);
	RootObj->SetObjectField(TEXT("payload"), PayloadObj);

	FString OutputString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
	FJsonSerializer::Serialize(RootObj.ToSharedRef(), Writer);

	if (WebSocket.IsValid() && bIsWebSocketConnected)
	{
		WebSocket->Send(OutputString);
		UE_LOG(LogFunASR, Log, TEXT("Sent finish-task: %s"), *CurrentTaskId);
		FCoreLogHelpers::CoreLog(this, ECoreLogSeverity::Info, TEXT("CIC"), TEXT("ASR"), FString::Printf(TEXT("Sent finish-task: %s"), *CurrentTaskId));
	}
}

void UFunASRSubsystem::ProcessJsonMessage(const FString& Message)
{
	TSharedPtr<FJsonObject> RootObj;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Message);

	if (!FJsonSerializer::Deserialize(Reader, RootObj) || !RootObj.IsValid())
	{
		UE_LOG(LogFunASR, Warning, TEXT("Failed to parse JSON message: %s"), *Message);
		return;
	}

	const TSharedPtr<FJsonObject> HeaderObj = RootObj->GetObjectField(TEXT("header"));
	if (!HeaderObj.IsValid()) return;

	FString Event = HeaderObj->GetStringField(TEXT("event"));
	// FString TaskId = HeaderObj->GetStringField(TEXT("task_id")); // Validate if needed

	if (Event == TEXT("task-started"))
	{
		bIsTaskRunning = true;
		OnTaskStarted.Broadcast();
		UE_LOG(LogFunASR, Log, TEXT("Task Started"));
		FCoreLogHelpers::CoreLog(this, ECoreLogSeverity::Info, TEXT("CIC"), TEXT("ASR"), TEXT("Task Started"));
	}
	else if (Event == TEXT("result-generated"))
	{
		// Log raw result for debugging to see full structure
		FCoreLogHelpers::CoreLog(this, ECoreLogSeverity::Debug, TEXT("CIC"), TEXT("ASR"), FString::Printf(TEXT("Raw Result Message: %s"), *Message));

		const TSharedPtr<FJsonObject> PayloadObj = RootObj->GetObjectField(TEXT("payload"));
		if (PayloadObj.IsValid())
		{
			const TSharedPtr<FJsonObject> OutputObj = PayloadObj->GetObjectField(TEXT("output"));
			if (OutputObj.IsValid())
			{
				const TSharedPtr<FJsonObject> SentenceObj = OutputObj->GetObjectField(TEXT("sentence"));
				if (SentenceObj.IsValid())
				{
					FString Text = SentenceObj->GetStringField(TEXT("text"));
					bool bEnd = SentenceObj->GetBoolField(TEXT("sentence_end"));
					
					// Log received text with brackets to see whitespace
					FCoreLogHelpers::CoreLog(this, ECoreLogSeverity::Info, TEXT("CIC"), TEXT("ASR"), FString::Printf(TEXT("Result: [%s] End=%d"), *Text, bEnd));

					OnResultReceived.Broadcast(Text, bEnd);

					// Accumulate full text if sentence ended (FunASR's text field in sentence is usually the full sentence)
					// If streaming results are overlapping, we only append when sentence_end=true
					if (bEnd && !Text.IsEmpty())
					{
						if (!CurrentTaskFullText.IsEmpty())
						{
							CurrentTaskFullText += TEXT(""); // No space needed if punctuation exists, but for safety: handled by ASR usually
						}
						CurrentTaskFullText += Text;
					}
				}
			}
		}
	}
	else if (Event == TEXT("task-finished"))
	{
		bIsTaskRunning = false;
		OnTaskFinished.Broadcast();
		// Broadcast the accumulated full text
		if (!CurrentTaskFullText.IsEmpty())
		{
			OnTaskCompletedWithFullText.Broadcast(CurrentTaskFullText);
			FCoreLogHelpers::CoreLog(this, ECoreLogSeverity::Info, TEXT("CIC"), TEXT("ASR"), FString::Printf(TEXT("任务完成，完整文本: %s"), *CurrentTaskFullText));
		}

		UE_LOG(LogFunASR, Log, TEXT("Task Finished"));
		FCoreLogHelpers::CoreLog(this, ECoreLogSeverity::Info, TEXT("CIC"), TEXT("ASR"), TEXT("Task Finished"));
	}
	else if (Event == TEXT("task-failed"))
	{
		bIsTaskRunning = false;
		FString ErrorCode = HeaderObj->GetStringField(TEXT("error_code"));
		FString ErrorMsg = HeaderObj->GetStringField(TEXT("error_message"));
		UE_LOG(LogFunASR, Error, TEXT("Task Failed: %s - %s"), *ErrorCode, *ErrorMsg);
		FCoreLogHelpers::CoreLog(this, ECoreLogSeverity::Error, TEXT("CIC"), TEXT("ASR"), FString::Printf(TEXT("任务失败: %s - %s"), *ErrorCode, *ErrorMsg));
		OnError.Broadcast(FString::Printf(TEXT("%s: %s"), *ErrorCode, *ErrorMsg));
		// ScheduleReconnect();
	}
}

