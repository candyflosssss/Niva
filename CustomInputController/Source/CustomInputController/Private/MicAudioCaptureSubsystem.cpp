#include "MicAudioCaptureSubsystem.h"
#include "GameFramework/Actor.h"
#include "Engine/World.h"

void UMicAudioCaptureSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// 初始化捕获组件
	InitializeCaptureComponent();
}

void UMicAudioCaptureSubsystem::Deinitialize()
{
	// 停止捕获和连接
	if (MicCapture)
	{
		MicCapture->StopCapture();
		MicCapture->DisconnectFromServer();
		MicCapture = nullptr;
	}

	Super::Deinitialize();
}

void UMicAudioCaptureSubsystem::InitializeCaptureComponent()
{
	// 创建一个临时的Actor来持有捕获组件
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	if (!MicCapture)
	{
		// 创建一个临时的隐藏Actor
		FActorSpawnParameters SpawnParams;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		SpawnParams.ObjectFlags = RF_Transient;
		SpawnParams.bAllowDuringConstructionScript = true;

		AActor* TempOwner = World->SpawnActor<AActor>(AActor::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
		TempOwner->SetActorHiddenInGame(true);
		TempOwner->SetActorTickEnabled(false);

		// 创建并附加麦克风捕获组件
		MicCapture = NewObject<UMicAudioCaptureComponent>(TempOwner, UMicAudioCaptureComponent::StaticClass(), FName("MicCapture_Subsystem"));
		MicCapture->RegisterComponent();
		MicCapture->SetActive(true);

		// 将事件转发给子系统的代理
		MicCapture->OnMicDevicesUpdated.AddDynamic(this, &UMicAudioCaptureSubsystem::HandleMicDevicesUpdated);
		MicCapture->OnAudioLevelUpdated.AddDynamic(this, &UMicAudioCaptureSubsystem::HandleAudioLevelUpdated);
		MicCapture->OnWebSocketConnected.AddDynamic(this, &UMicAudioCaptureSubsystem::HandleWebSocketConnected);
		MicCapture->OnWebSocketError.AddDynamic(this, &UMicAudioCaptureSubsystem::HandleWebSocketError);

		// 初始化完成后刷新设备列表
		RefreshMicDevices();
	}
}

void UMicAudioCaptureSubsystem::RefreshMicDevices()
{
	if (MicCapture)
	{
		MicCapture->RefreshMicDevices();
		OnMicDevicesUpdated.Broadcast(MicCapture->GetAvailableMicDevices());
	}
	else
	{
		InitializeCaptureComponent();
	}
}

TArray<FString> UMicAudioCaptureSubsystem::GetAvailableMicDevices() const
{
	if (MicCapture)
	{
		return MicCapture->GetAvailableMicDevices();
	}
	return TArray<FString>();
}

bool UMicAudioCaptureSubsystem::StartCapture(int32 DeviceIndex)
{
	if (!MicCapture)
	{
		InitializeCaptureComponent();
	}

	if (MicCapture)
	{
		return MicCapture->StartCapture(DeviceIndex);
	}
	return false;
}

void UMicAudioCaptureSubsystem::StopCapture()
{
	if (MicCapture)
	{
		MicCapture->StopCapture();
	}
}

bool UMicAudioCaptureSubsystem::ConnectToServer(const FString& ServerUrl)
{
	if (!MicCapture)
	{
		InitializeCaptureComponent();
	}

	if (MicCapture)
	{
		return MicCapture->ConnectToServer(ServerUrl);
	}
	return false;
}

void UMicAudioCaptureSubsystem::DisconnectFromServer()
{
	if (MicCapture)
	{
		MicCapture->DisconnectFromServer();
	}
}

bool UMicAudioCaptureSubsystem::IsCapturing() const
{
	if (MicCapture)
	{
		return MicCapture->IsCapturing();
	}
	return false;
}

bool UMicAudioCaptureSubsystem::IsConnectedToServer() const
{
	if (MicCapture)
	{
		return MicCapture->IsConnectedToServer();
	}
	return false;
}

float UMicAudioCaptureSubsystem::GetCurrentAudioLevel() const
{
	if (MicCapture)
	{
		return MicCapture->GetCurrentAudioLevel();
	}
	return 0.0f;
}

void UMicAudioCaptureSubsystem::SetVolumeMultiplier(float InMultiplier)
{
	if (MicCapture)
	{
		MicCapture->VolumeMultiplier = FMath::Max(0.1f, InMultiplier);
	}
}

float UMicAudioCaptureSubsystem::GetVolumeMultiplier() const
{
	if (MicCapture)
	{
		return MicCapture->VolumeMultiplier;
	}
	return 1.0f;
}

void UMicAudioCaptureSubsystem::SetSampleRate(int32 InSampleRate)
{
	if (MicCapture)
	{
		bool bWasCapturing = MicCapture->IsCapturing();
		int32 DeviceIndex = MicCapture->GetCurrentDeviceIndex();

		// 如果正在捕获，需要先停止
		if (bWasCapturing)
		{
			MicCapture->StopCapture();
		}

		MicCapture->SampleRate = FMath::Clamp(InSampleRate, 8000, 48000);

		// 如果之前在捕获，重新开始
		if (bWasCapturing)
		{
			MicCapture->StartCapture(DeviceIndex);
		}
	}
}

int32 UMicAudioCaptureSubsystem::GetSampleRate() const
{
	if (MicCapture)
	{
		return MicCapture->SampleRate;
	}
	return 16000; // 默认值
}

void UMicAudioCaptureSubsystem::SetNumChannels(int32 InNumChannels)
{
	if (MicCapture)
	{
		bool bWasCapturing = MicCapture->IsCapturing();
		int32 DeviceIndex = MicCapture->GetCurrentDeviceIndex();

		// 如果正在捕获，需要先停止
		if (bWasCapturing)
		{
			MicCapture->StopCapture();
		}

		MicCapture->NumChannels = FMath::Clamp(InNumChannels, 1, 2);

		// 如果之前在捕获，重新开始
		if (bWasCapturing)
		{
			MicCapture->StartCapture(DeviceIndex);
		}
	}
}

int32 UMicAudioCaptureSubsystem::GetNumChannels() const
{
	if (MicCapture)
	{
		return MicCapture->NumChannels;
	}
	return 1; // 默认值
}

void UMicAudioCaptureSubsystem::SetBufferSizeMs(int32 InBufferSizeMs)
{
	if (MicCapture)
	{
		MicCapture->BufferSizeMs = FMath::Clamp(InBufferSizeMs, 10, 1000);
	}
}

int32 UMicAudioCaptureSubsystem::GetBufferSizeMs() const
{
	if (MicCapture)
	{
		return MicCapture->BufferSizeMs;
	}
	return 100; // 默认值
}

void UMicAudioCaptureSubsystem::SetEnableDebugLogs(bool bEnable)
{
	if (MicCapture)
	{
		MicCapture->bEnableDebugLogs = bEnable;
	}
}

bool UMicAudioCaptureSubsystem::GetEnableDebugLogs() const
{
	if (MicCapture)
	{
		return MicCapture->bEnableDebugLogs;
	}
	return false;
}


void UMicAudioCaptureSubsystem::HandleMicDevicesUpdated(const TArray<FString>& DeviceNames)
{
	OnMicDevicesUpdated.Broadcast(DeviceNames);
}

void UMicAudioCaptureSubsystem::HandleAudioLevelUpdated(float Level)
{
	OnAudioLevelUpdated.Broadcast(Level);
}

void UMicAudioCaptureSubsystem::HandleWebSocketConnected(const FString& ServerAddress)
{
	OnWebSocketConnected.Broadcast(ServerAddress);
}

void UMicAudioCaptureSubsystem::HandleWebSocketError(const FString& ErrorMsg, int32 ErrorCode)
{
	OnWebSocketError.Broadcast(ErrorMsg, ErrorCode);
}
