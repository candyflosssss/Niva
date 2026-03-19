#include "Audio/NetMicWsComponent.h"
#include "WebSocketsModule.h"
#include "Modules/ModuleManager.h"
#include "TimerManager.h"
#include "Engine/World.h"
#include "ASR/FunASRSubsystem.h"
#include "Engine/GameInstance.h"
#include "Log/CoreLogHelpers.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Audio/AudioStreamSettings.h"

UNetMicWsComponent::UNetMicWsComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UNetMicWsComponent::BeginPlay()
{
	Super::BeginPlay();
	// 自动连接：优先 AutoConnectWsUrl，其次从设置读取 DefaultNetMicWsUrl
	FString Url = AutoConnectWsUrl;
	if (Url.IsEmpty())
	{
		if (const UAudioStreamSettings* S = UAudioStreamSettings::Get())
		{
			Url = S->DefaultNetMicWsUrl;
		}
	}
	if (!Url.IsEmpty())
	{
		FCoreLogHelpers::CoreLog(this, ECoreLogSeverity::Debug, TEXT("CIC"), TEXT("NetMic"), FString::Printf(TEXT("AutoConnect: %s"), *Url));
		Connect(Url);
	}
}

void UNetMicWsComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	FCoreLogHelpers::CoreLog(this, ECoreLogSeverity::Info, TEXT("CIC"), TEXT("NetMic"), TEXT("EndPlay: closing WS and canceling reconnect"));
	CancelReconnect();
	CloseWebSocket(true);
	Super::EndPlay(EndPlayReason);
}

void UNetMicWsComponent::ConfigureReconnect(bool bEnable, float InBaseDelaySeconds, float InMaxDelaySeconds)
{
	bAutoReconnect = bEnable;
	ReconnectBaseDelaySeconds = FMath::Max(0.1f, InBaseDelaySeconds);
	ReconnectMaxDelaySeconds = FMath::Max(ReconnectBaseDelaySeconds, InMaxDelaySeconds);
	FCoreLogHelpers::CoreLog(this, ECoreLogSeverity::Debug, TEXT("CIC"), TEXT("NetMic"), FString::Printf(TEXT("ConfigureReconnect: enable=%d base=%.2f max=%.2f"), bAutoReconnect ? 1 : 0, ReconnectBaseDelaySeconds, ReconnectMaxDelaySeconds));
}

void UNetMicWsComponent::Connect(const FString& InWsUrl)
{
	LastUrl = InWsUrl;
	// 防止并发连接
	if (bIsConnecting || bIsConnected)
	{
		FCoreLogHelpers::CoreLog(this, ECoreLogSeverity::Warn, TEXT("CIC"), TEXT("NetMic"), TEXT("Connect ignored: already connecting or connected"));
		return;
	}
	ReconnectAttempts = 0;
	FCoreLogHelpers::CoreLog(this, ECoreLogSeverity::Debug, TEXT("CIC"), TEXT("NetMic"), FString::Printf(TEXT("Connect: %s"), *LastUrl));
	OpenWebSocket(LastUrl);
}

void UNetMicWsComponent::Disconnect()
{
	FCoreLogHelpers::CoreLog(this, ECoreLogSeverity::Debug, TEXT("CIC"), TEXT("NetMic"), TEXT("Disconnect: manual close requested"));
	CancelReconnect();
	CloseWebSocket(true);
}

void UNetMicWsComponent::StartRecording()
{
	bShouldBeRecording = true;
	FCoreLogHelpers::CoreLog(this, ECoreLogSeverity::Debug, TEXT("CIC"), TEXT("NetMic"), TEXT("StartRecording: sending <start> and requesting ASR task start"));
	SendCtrl(TEXT("<start>"));
	// 自动触发 ASR 任务开始（由子系统负责连接与协议）
	if (UWorld* World = GetWorld())
	{
		if (UGameInstance* GI = World->GetGameInstance())
		{
			if (auto* ASR = GI->GetSubsystem<UFunASRSubsystem>())
			{
				ASR->StartASR();
			}
		}
	}
}

void UNetMicWsComponent::StopRecording()
{
	bShouldBeRecording = false;
	FCoreLogHelpers::CoreLog(this, ECoreLogSeverity::Debug, TEXT("CIC"), TEXT("NetMic"), TEXT("StopRecording: sending <end> and requesting ASR task stop"));
	SendCtrl(TEXT("<end>"));
	// 自动触发 ASR 任务结束
	if (UWorld* World = GetWorld())
	{
		if (UGameInstance* GI = World->GetGameInstance())
		{
			if (auto* ASR = GI->GetSubsystem<UFunASRSubsystem>())
			{
				ASR->StopASR();
			}
		}
	}
}

void UNetMicWsComponent::RequestDeviceList()
{
	FCoreLogHelpers::CoreLog(this, ECoreLogSeverity::Debug, TEXT("CIC"), TEXT("NetMic"), TEXT("RequestDeviceList: sending <list_devices>"));
	SendCtrl(TEXT("<list_devices>"));
}

void UNetMicWsComponent::SetDeviceIndex(int32 Index)
{
	DesiredDeviceIndex = Index;
	FCoreLogHelpers::CoreLog(this, ECoreLogSeverity::Debug, TEXT("CIC"), TEXT("NetMic"), FString::Printf(TEXT("SetDeviceIndex: %d"), Index));
	SendCtrl(FString::Printf(TEXT("<set_device:%d>"), Index));
}

void UNetMicWsComponent::OpenWebSocket(const FString& Url)
{
	if (!FModuleManager::Get().IsModuleLoaded("WebSockets"))
	{
		FModuleManager::Get().LoadModuleChecked<IModuleInterface>("WebSockets");
	}

	CloseWebSocket(false);

	Socket = FWebSocketsModule::Get().CreateWebSocket(Url);
	Socket->OnConnected().AddUObject(this, &UNetMicWsComponent::OnWsConnected);
	Socket->OnConnectionError().AddUObject(this, &UNetMicWsComponent::OnWsConnectionError);
	Socket->OnClosed().AddUObject(this, &UNetMicWsComponent::OnWsClosed);
	Socket->OnMessage().AddUObject(this, &UNetMicWsComponent::OnWsMessage);
	Socket->OnRawMessage().AddUObject(this, &UNetMicWsComponent::OnWsRawMessage);

	bManualClose = false;
	bIsConnecting = true;
	FCoreLogHelpers::CoreLog(this, ECoreLogSeverity::Debug, TEXT("CIC"), TEXT("NetMic"), FString::Printf(TEXT("WebSocket connecting: %s"), *Url));
	Socket->Connect();
}

void UNetMicWsComponent::CloseWebSocket(bool bIsManual)
{
	if (Socket.IsValid())
	{
		bManualClose = bIsManual;
		FCoreLogHelpers::CoreLog(this, ECoreLogSeverity::Debug, TEXT("CIC"), TEXT("NetMic"), TEXT("CloseWebSocket"));
		Socket->Close();
		Socket.Reset();
	}
	PendingBinary.Reset();
	if (bIsConnected)
	{
		bIsConnected = false;
		OnDisconnected.Broadcast();
	}
}

void UNetMicWsComponent::OnWsConnected()
{
	bIsConnected = true;
	bIsConnecting = false;
	ReconnectAttempts = 0;
	FCoreLogHelpers::CoreLog(this, ECoreLogSeverity::Info, TEXT("CIC"), TEXT("NetMic"), TEXT("WebSocket connected"));
	OnConnected.Broadcast();
	TryRestoreDesiredStateAfterConnect();
}

void UNetMicWsComponent::OnWsConnectionError(const FString& Error)
{
	FCoreLogHelpers::CoreLog(this, ECoreLogSeverity::Warn, TEXT("CIC"), TEXT("NetMic"), FString::Printf(TEXT("Connection error: %s"), *Error));
	OnError.Broadcast(Error);
	bIsConnected = false;
	bIsConnecting = false;
	if (!bManualClose && bAutoReconnect && !LastUrl.IsEmpty())
	{
		ScheduleReconnect();
	}
}

void UNetMicWsComponent::OnWsClosed(int32 StatusCode, const FString& Reason, bool bWasClean)
{
	bIsConnected = false;
	bIsConnecting = false;
	FCoreLogHelpers::CoreLog(this, ECoreLogSeverity::Warn, TEXT("CIC"), TEXT("NetMic"), FString::Printf(TEXT("WebSocket closed: code=%d reason=%s clean=%d"), StatusCode, *Reason, bWasClean ? 1 : 0));
	// 服务端主动正常关闭（1000/clean=1），不重连，避免死循环；其他情况按策略重连
	const bool bServerCleanClose = (bWasClean && StatusCode == 1000);
	if (!bManualClose && bAutoReconnect && !LastUrl.IsEmpty() && !bServerCleanClose)
	{
		ScheduleReconnect();
	}
}

void UNetMicWsComponent::OnWsMessage(const FString& Message)
{
	FCoreLogHelpers::CoreLog(this, ECoreLogSeverity::Trace, TEXT("CIC"), TEXT("NetMic"), FString::Printf(TEXT("WS text: %s"), *Message));
	OnServerMessage.Broadcast(Message);
}

void UNetMicWsComponent::OnWsRawMessage(const void* Data, SIZE_T Size, SIZE_T BytesRemaining)
{
	if (Size == 0) return;

	// 累积分片
	int32 Prev = PendingBinary.Num();
	PendingBinary.AddUninitialized(Size);
	FMemory::Memcpy(PendingBinary.GetData() + Prev, Data, Size);

	if (BytesRemaining == 0)
	{
		// 一个完整帧到达
		FCoreLogHelpers::CoreLog(this, ECoreLogSeverity::Trace, TEXT("CIC"), TEXT("NetMic"), FString::Printf(TEXT("WS audio frame: %d bytes"), PendingBinary.Num()));
		OnAudioFrame.Broadcast(PendingBinary);
		if (bForwardToASR)
		{
			ForwardAudioToASR(PendingBinary);
		}
		PendingBinary.Reset();
	}
}

void UNetMicWsComponent::TryRestoreDesiredStateAfterConnect()
{
	if (DesiredDeviceIndex >= 0)
	{
		FCoreLogHelpers::CoreLog(this, ECoreLogSeverity::Debug, TEXT("CIC"), TEXT("NetMic"), FString::Printf(TEXT("Restore device index: %d"), DesiredDeviceIndex));
		SendCtrl(FString::Printf(TEXT("<set_device:%d>"), DesiredDeviceIndex));
	}
	if (bShouldBeRecording)
	{
		FCoreLogHelpers::CoreLog(this, ECoreLogSeverity::Debug, TEXT("CIC"), TEXT("NetMic"), TEXT("Restore: start recording"));
		SendCtrl(TEXT("<start>"));
	}
}

void UNetMicWsComponent::ScheduleReconnect()
{
	// 如果正在连接，跳过重连计划
	if (bIsConnecting)
	{
		FCoreLogHelpers::CoreLog(this, ECoreLogSeverity::Warn, TEXT("CIC"), TEXT("NetMic"), TEXT("Reconnect skipped: already connecting"));
		return;
	}
	CancelReconnect();
	ReconnectAttempts++;
	const float Delay = GetNextBackoffSeconds();
	FCoreLogHelpers::CoreLog(this, ECoreLogSeverity::Debug, TEXT("CIC"), TEXT("NetMic"), FString::Printf(TEXT("Schedule reconnect in %.2fs (attempt %d)"), Delay, ReconnectAttempts));
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(ReconnectTimerHandle, [this]()
		{
			if (!LastUrl.IsEmpty() && !bIsConnecting)
			{
				OpenWebSocket(LastUrl);
			}
		}, Delay, false);
	}
}

void UNetMicWsComponent::CancelReconnect()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ReconnectTimerHandle);
	}
}

float UNetMicWsComponent::GetNextBackoffSeconds() const
{
	// Exponential backoff with cap
	const double Pow = FMath::Pow(2.0, static_cast<double>(FMath::Clamp(ReconnectAttempts - 1, 0, 30)));
	const double Delay = static_cast<double>(ReconnectBaseDelaySeconds) * Pow;
	return static_cast<float>(FMath::Clamp(Delay, static_cast<double>(ReconnectBaseDelaySeconds), static_cast<double>(ReconnectMaxDelaySeconds)));
}

void UNetMicWsComponent::SendCtrl(const FString& Ctrl)
{
	if (Socket.IsValid() && Socket->IsConnected())
	{
		Socket->Send(Ctrl);
	}
}

void UNetMicWsComponent::ForwardAudioToASR(const TArray<uint8>& Bytes)
{
	if (!bForwardToASR) return;
	if (UWorld* World = GetWorld())
	{
		if (UGameInstance* GI = World->GetGameInstance())
		{
			if (auto* ASR = GI->GetSubsystem<UFunASRSubsystem>())
			{
				ASR->SendAudioFrame(Bytes);
				FCoreLogHelpers::CoreLog(this, ECoreLogSeverity::Trace, TEXT("CIC"), TEXT("NetMic"), FString::Printf(TEXT("Forwarded audio to ASR: %d bytes"), Bytes.Num()));
			}
		}
	}
}
