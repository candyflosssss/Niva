#include "MicAudioCaptureSettings.h"

UMicAudioCaptureSettings::UMicAudioCaptureSettings()
{
	// 设置默认值
	DefaultServerUrl = TEXT("ws://10.1.20.57:8765");
	DefaultSampleRate = 16000;
	DefaultNumChannels = 1;
	DefaultBufferSizeMs = 100;
	DefaultVolumeMultiplier = 1.0f;
	bEnableVolumeLeveling = false;
	VolumeLevelingSpeed = 0.1f;
	bAutoConnectOnGameStart = false;
	bAutoStartCaptureOnGameStart = false;
	ReconnectAttempts = 3;
	ReconnectInterval = 2.0f;
	ChunkSizeBytes = 4096;
	bEnableDebugLogs = false;
	bShowAudioLevelInEditor = true;
}

const UMicAudioCaptureSettings* UMicAudioCaptureSettings::Get()
{
	return GetDefault<UMicAudioCaptureSettings>();
}

FName UMicAudioCaptureSettings::GetCategoryName() const
{
	return TEXT("Plugins");
}

#if WITH_EDITOR
FText UMicAudioCaptureSettings::GetSectionText() const
{
	return NSLOCTEXT("MicAudioCapture", "MicAudioCaptureSectionText", "麦克风音频捕获");
}

FText UMicAudioCaptureSettings::GetSectionDescription() const
{
	return NSLOCTEXT("MicAudioCapture", "MicAudioCaptureSectionDescription", 
		"麦克风音频捕获插件的全局设置。配置麦克风捕获和WebSocket推流参数。");
}

#endif

#if WITH_EDITOR
void UMicAudioCaptureSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	// 可以在这里添加对特定属性更改的响应
	const FName PropertyName = (PropertyChangedEvent.Property != nullptr) 
		? PropertyChangedEvent.Property->GetFName() 
		: NAME_None;

	// 验证数值范围
	if (PropertyName == GET_MEMBER_NAME_CHECKED(UMicAudioCaptureSettings, DefaultSampleRate))
	{
		DefaultSampleRate = FMath::Clamp(DefaultSampleRate, 8000, 48000);
	}
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(UMicAudioCaptureSettings, DefaultNumChannels))
	{
		DefaultNumChannels = FMath::Clamp(DefaultNumChannels, 1, 2);
	}
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(UMicAudioCaptureSettings, DefaultBufferSizeMs))
	{
		DefaultBufferSizeMs = FMath::Clamp(DefaultBufferSizeMs, 10, 1000);
	}
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(UMicAudioCaptureSettings, DefaultVolumeMultiplier))
	{
		DefaultVolumeMultiplier = FMath::Clamp(DefaultVolumeMultiplier, 0.1f, 10.0f);
	}
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(UMicAudioCaptureSettings, VolumeLevelingSpeed))
	{
		VolumeLevelingSpeed = FMath::Clamp(VolumeLevelingSpeed, 0.01f, 1.0f);
	}
}
#endif
