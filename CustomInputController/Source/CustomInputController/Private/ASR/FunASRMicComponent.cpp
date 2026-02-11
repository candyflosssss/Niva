#include "ASR/FunASRMicComponent.h"
#include "ASR/FunASRSubsystem.h"
#include "ASR/FunASRSettings.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"
#include "Log/CoreLogHelpers.h"

// Define static member
UFunASRMicComponent* UFunASRMicComponent::GlobalActiveMic = nullptr;

UFunASRMicComponent::UFunASRMicComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void UFunASRMicComponent::BeginPlay()
{
	Super::BeginPlay();
	// Manual start required now.
}

void UFunASRMicComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (GlobalActiveMic == this)
	{
		GlobalActiveMic = nullptr;
	}

	StopAudioCapture();
	Super::EndPlay(EndPlayReason);
}

void UFunASRMicComponent::Start()
{
	if (UGameInstance* GI = UGameplayStatics::GetGameInstance(this))
	{
		if (UFunASRSubsystem* ASR = GI->GetSubsystem<UFunASRSubsystem>())
		{
			// If not running, start it
			if (!ASR->IsTaskRunning())
			{
			    FCoreLogHelpers::CoreLog(this, ECoreLogSeverity::Info, TEXT("CIC"), TEXT("MicComp"), TEXT("Start调用。触发ASR启动。"));
				ASR->StartASR(); // This connects and sends run-task
			}
			else 
			{
			    FCoreLogHelpers::CoreLog(this, ECoreLogSeverity::Info, TEXT("CIC"), TEXT("MicComp"), TEXT("Start调用。ASR已在运行。"));
			}
			
			// We always start capture if component is active, assuming we want to feed the ASR
			bool bStarted = StartAudioCapture();
			if (bStarted)
			{
			    FCoreLogHelpers::CoreLog(this, ECoreLogSeverity::Info, TEXT("CIC"), TEXT("MicComp"), TEXT("手动启动：音频采集已开始。"));
			}
		}
	}
}

void UFunASRMicComponent::Stop()
{
    FCoreLogHelpers::CoreLog(this, ECoreLogSeverity::Info, TEXT("CIC"), TEXT("MicComp"), TEXT("手动Stop调用。"));
	
	// 1. Flush remaining audio buffer
	{
		FScopeLock Lock(&AudioBufferMutex);
		if (PendingAudioData.Num() > 0)
		{
			if (UGameInstance* GI = UGameplayStatics::GetGameInstance(this))
			{
				if (UFunASRSubsystem* ASR = GI->GetSubsystem<UFunASRSubsystem>())
				{
					ASR->SendAudioFrame(PendingAudioData);
					FCoreLogHelpers::CoreLog(this, ECoreLogSeverity::Info, TEXT("CIC"), TEXT("MicComp"), 
						FString::Printf(TEXT("停止时刷新剩余音频：%d 字节"), PendingAudioData.Num()));
				}
			}
			PendingAudioData.Reset();
		}
	}

	// 2. Stop Capture
	StopAudioCapture();

	// 3. Signal ASR Task Finish to ensure independence
	if (UGameInstance* GI = UGameplayStatics::GetGameInstance(this))
	{
		if (UFunASRSubsystem* ASR = GI->GetSubsystem<UFunASRSubsystem>())
		{
			ASR->StopASR(); // Sends finish-task
		}
	}
}

void UFunASRMicComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// ASR Optimization: Send audio in ~100ms chunks (16000Hz * 2 bytes * 0.1s = 3200 bytes)
	// Sending too frequently (e.g. 10ms) causes network congestion and high latency.
	const int32 ChunkThreshold = 3200; 

	TArray<uint8> ChunkToSend;
	{
		FScopeLock Lock(&AudioBufferMutex);
		
		// Only take data if we have enough for a chunk, OR if it's been a while (flush)
		if (PendingAudioData.Num() >= ChunkThreshold)
		{
			// Extract fixed chunk size to keep flow steady, or everything?
			// Let's send everything currently buffered to clear latency, but only if it's substantial.
			ChunkToSend = MoveTemp(PendingAudioData);
			PendingAudioData.Reset();
		}
	}

	if (ChunkToSend.Num() > 0)
	{
		if (UGameInstance* GI = UGameplayStatics::GetGameInstance(this))
		{
			if (UFunASRSubsystem* ASR = GI->GetSubsystem<UFunASRSubsystem>())
			{
				ASR->SendAudioFrame(ChunkToSend);
			}
		}
		
		// [Debug] Log periodically to confirm data is hitting the wire
		static double LastSendLogTime = 0.0;
		double Now = FPlatformTime::Seconds();
		if (Now - LastSendLogTime > 2.0)
		{
			FCoreLogHelpers::CoreLog(this, ECoreLogSeverity::Info, TEXT("CIC"), TEXT("MicComp"), 
				FString::Printf(TEXT("发送音频块：%d 字节（优化：>100ms缓冲）"), ChunkToSend.Num()));
			LastSendLogTime = Now;
		}
	}
}

bool UFunASRMicComponent::StartAudioCapture()
{
	if (GlobalActiveMic != nullptr && GlobalActiveMic != this)
	{
		UE_LOG(LogTemp, Error, TEXT("UFunASRMicComponent: Another Mic Component is already active (%s). Only one allowed per client."), *GlobalActiveMic->GetName());
		FCoreLogHelpers::CoreLog(this, ECoreLogSeverity::Error, TEXT("CIC"), TEXT("MicComp"), FString::Printf(TEXT("另一个Mic组件已激活 (%s)。每个客户端仅允许一个。"), *GlobalActiveMic->GetName()));
		return false;
	}

	if (!AudioCapture)
	{
		AudioCapture = MakeUnique<Audio::FAudioCapture>();
	}

	if (AudioCapture->IsStreamOpen())
	{
		return true;
	}

	// Use Subsystem settings for SampleRate
	int32 SampleRate = 16000;
	if (const UFunASRSettings* Settings = UFunASRSettings::Get())
	{
		SampleRate = Settings->SampleRate;
	}
	
	int32 NumChannels = 1; // ASR usually needs Mono

	// List devices for debugging
	TArray<Audio::FCaptureDeviceInfo> DeviceInfos;
	AudioCapture->GetCaptureDevicesAvailable(DeviceInfos);
	FCoreLogHelpers::CoreLog(this, ECoreLogSeverity::Info, TEXT("CIC"), TEXT("MicComp"), FString::Printf(TEXT("发现 %d 个采集设备"), DeviceInfos.Num()));
	for (int32 i = 0; i < DeviceInfos.Num(); ++i)
	{
		FCoreLogHelpers::CoreLog(this, ECoreLogSeverity::Info, TEXT("CIC"), TEXT("MicComp"), FString::Printf(TEXT("设备 %d: %s (通道: %d, 频率: %d)"), i, *DeviceInfos[i].DeviceName, DeviceInfos[i].InputChannels, DeviceInfos[i].PreferredSampleRate));
	}

	Audio::FOnAudioCaptureFunction InCallback = [this](const void* InAudioData, int32 InNumFrames, int32 InNumChannels, int32 InSampleRate, double InStreamTime, bool bOverflow)
	{
		this->OnAudioCaptureData(InAudioData, InNumFrames, InNumChannels, InSampleRate, InStreamTime, bOverflow);
	};

	// Gather Fallback Configurations
	struct FCaptureConfig
	{
		int32 Rate;
		int32 Channels;
		FString Desc;
	};

	TArray<FCaptureConfig> ConfigsToCheck;
	
	// 1. Target (16k)
	ConfigsToCheck.Add({SampleRate, 1, TEXT("Target Mono")});
	ConfigsToCheck.Add({SampleRate, 2, TEXT("Target Stereo")});

	// 2. Device Preferred (if available and different)
	if (DeviceInfos.Num() > 0)
	{
		// Try to find default device info, or just use the first valid one as a hint
		int32 NativeRate = DeviceInfos[0].PreferredSampleRate;
		if (NativeRate > 0 && NativeRate != SampleRate)
		{
			ConfigsToCheck.Add({NativeRate, 1, FString::Printf(TEXT("Native %d Mono"), NativeRate)});
			ConfigsToCheck.Add({NativeRate, 2, FString::Printf(TEXT("Native %d Stereo"), NativeRate)});
		}
	}

	// 3. Common Standards (48k, 44.1k)
	int32 CommonRates[] = {48000, 44100};
	for (int32 Rate : CommonRates)
	{
		if (Rate != SampleRate) // Don't duplicate if native was one of these
		{
			ConfigsToCheck.Add({Rate, 1, FString::Printf(TEXT("%d Mono"), Rate)});
			ConfigsToCheck.Add({Rate, 2, FString::Printf(TEXT("%d Stereo"), Rate)});
		}
	}

	// Try Configs
	Audio::FAudioCaptureDeviceParams Params;
	Params.DeviceIndex = INDEX_NONE; 

	for (const auto& Cfg : ConfigsToCheck)
	{
		Params.SampleRate = Cfg.Rate;
		Params.NumInputChannels = Cfg.Channels;

		if (AudioCapture->OpenAudioCaptureStream(Params, InCallback, 1024))
		{
			AudioCapture->StartStream();
			GlobalActiveMic = this;
			FCoreLogHelpers::CoreLog(this, ECoreLogSeverity::Info, TEXT("CIC"), TEXT("MicComp"), 
				FString::Printf(TEXT("麦克风采集已启动 [%s]: %dHz %dCh"), *Cfg.Desc, Cfg.Rate, Cfg.Channels));
			return true;
		}
	}

	FCoreLogHelpers::CoreLog(this, ECoreLogSeverity::Error, TEXT("CIC"), TEXT("MicComp"), TEXT("打开采集流失败（所有尝试均失败）"));
	return false;
}

void UFunASRMicComponent::StopAudioCapture()
{
	if (AudioCapture && AudioCapture->IsStreamOpen())
	{
		AudioCapture->StopStream();
		AudioCapture->CloseStream();
		FCoreLogHelpers::CoreLog(this, ECoreLogSeverity::Info, TEXT("CIC"), TEXT("MicComp"), TEXT("麦克风采集已停止"));
	}
	// Release the capture helper to free resources
	AudioCapture.Reset();
	
	if (GlobalActiveMic == this)
	{
		GlobalActiveMic = nullptr;
	}
}

void UFunASRMicComponent::OnAudioCaptureData(const void* InAudioData, int32 InNumFrames, int32 InNumChannels, int32 InSampleRate, double InStreamTime, bool bOverflow)
{
	if (InNumFrames <= 0) return;

	// UE5 Audio Capture typically delivers Float (32-bit) data, not Int16.
	// We need to convert Float array to Int16 array.
	const float* FloatData = static_cast<const float*>(InAudioData);
	
	// Step 1: Extract Mono (taking first channel) & Convert to Int16
	TArray<int16> MonoSamples;
	MonoSamples.SetNumUninitialized(InNumFrames);

	if (InNumChannels == 1)
	{
		for (int32 i = 0; i < InNumFrames; ++i)
		{
			MonoSamples[i] = (int16)FMath::Clamp(FloatData[i] * 32767.0f, -32768.0f, 32767.0f);
		}
	}
	else
	{
		// Interleaved data: [L, R, L, R...]
		// Take first channel
		for (int32 i = 0; i < InNumFrames; ++i)
		{
			MonoSamples[i] = (int16)FMath::Clamp(FloatData[i * InNumChannels] * 32767.0f, -32768.0f, 32767.0f);
		}
	}

	// Step 2: Resample to Target Rate (16000) if needed
	const int32 TargetRate = 16000; 
	// (Should fetch from settings conceptually, but hardcoded to 16k per requirement usually)

	TArray<int16> FinalSamples;

	if (InSampleRate == TargetRate)
	{
		FinalSamples = MoveTemp(MonoSamples);
	}
	else
	{
		// Simple Linear Interpolation Resampler
		double Ratio = (double)InSampleRate / (double)TargetRate;
		int32 OutNumFrames = (int32)(InNumFrames / Ratio);
		FinalSamples.SetNumUninitialized(OutNumFrames);

		for (int32 OutIdx = 0; OutIdx < OutNumFrames; ++OutIdx)
		{
			double InPos = OutIdx * Ratio; 
			int32 Index0 = (int32)InPos;
			int32 Index1 = FMath::Min(Index0 + 1, InNumFrames - 1);
			float Alpha = (float)(InPos - Index0);

			int16 Val0 = MonoSamples[Index0];
			int16 Val1 = MonoSamples[Index1];

			FinalSamples[OutIdx] = (int16)FMath::Lerp((float)Val0, (float)Val1, Alpha);
		}
	}

	// [Debug] Calculate RMS to check if we are capturing silence
	static double AccumEnergy = 0;
	static int32 AccumCount = 0;
	
	if (FinalSamples.Num() > 0)
	{
		double FrameEnergy = 0;
		for (int16 Sample : FinalSamples)
		{
			FrameEnergy += (double)Sample * (double)Sample;
		}
		AccumEnergy += FrameEnergy;
		AccumCount += FinalSamples.Num();

		// Log every ~16000 samples (approx 1 sec)
		if (AccumCount >= 16000)
		{
			double RMS = FMath::Sqrt(AccumEnergy / AccumCount);
			// RMS for 16bit quiet room is usually < 100, voice > 500-1000. Silence is 0.
			FCoreLogHelpers::CoreLog(this, ECoreLogSeverity::Info, TEXT("CIC"), TEXT("MicComp"), 
				FString::Printf(TEXT("音频信号RMS: %.2f (采集中...)"), RMS));

			AccumEnergy = 0;
			AccumCount = 0;
		}
	}

	const int32 DataSize = FinalSamples.Num() * sizeof(int16);
	if (DataSize > 0)
	{
		FScopeLock Lock(&AudioBufferMutex);
		int32 CurrentSize = PendingAudioData.Num();
		PendingAudioData.AddUninitialized(DataSize);
		FMemory::Memcpy(PendingAudioData.GetData() + CurrentSize, FinalSamples.GetData(), DataSize);
	}
}
