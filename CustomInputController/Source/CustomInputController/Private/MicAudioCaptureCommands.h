#pragma once

#if WITH_EDITOR
#include "CoreMinimal.h"
#include "Framework/Commands/Commands.h"
#include "Styling/AppStyle.h"

/**
 * 麦克风音频捕获编辑器命令
 */
class FMicAudioCaptureCommands : public TCommands<FMicAudioCaptureCommands>
{
public:
	FMicAudioCaptureCommands()
		: TCommands<FMicAudioCaptureCommands>(
			FName(TEXT("MicAudioCapture")),
			FText::FromString(TEXT("麦克风音频捕获")),
			FName(TEXT("MicAudioCaptureCategory")),
			FAppStyle::GetAppStyleSetName())
	{
	}

	// 注册命令
	virtual void RegisterCommands() override;

	// 定义命令
	TSharedPtr<FUICommandInfo> OpenMicAudioCaptureSettings;
	TSharedPtr<FUICommandInfo> StartMicCapture;
	TSharedPtr<FUICommandInfo> StopMicCapture;
	TSharedPtr<FUICommandInfo> ConnectToServer;
	TSharedPtr<FUICommandInfo> DisconnectFromServer;
	TSharedPtr<FUICommandInfo> RefreshMicDevices;
};
#endif // WITH_EDITOR
