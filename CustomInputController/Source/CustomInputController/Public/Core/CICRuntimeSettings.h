#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "CICRuntimeSettings.generated.h"

/**
 * Unified runtime settings for CIC listener ports and auto-start behavior.
 */
UCLASS(Config=Game, DefaultConfig, meta=(DisplayName="CIC Runtime Settings"))
class CUSTOMINPUTCONTROLLER_API UCICRuntimeSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Config, Category="Input Device", meta=(ToolTip="UDP port used by the custom input device when auto-start is enabled."))
	int32 InputDeviceUdpPort = 8091;

	UPROPERTY(EditAnywhere, Config, Category="Input Device", meta=(ToolTip="Whether the custom UDP input device should start listening immediately when created."))
	bool bAutoStartInputDeviceUdp = true;

	UPROPERTY(EditAnywhere, Config, Category="Hand Tracking", meta=(ToolTip="UDP port used by the hand landmark subsystem."))
	int32 HandTrackingUdpPort = 8092;

	UPROPERTY(EditAnywhere, Config, Category="Hand Tracking", meta=(ToolTip="Whether the hand landmark subsystem should automatically start its UDP listener during Initialize."))
	bool bAutoStartHandTrackingUdp = true;

	UPROPERTY(EditAnywhere, Config, Category="Gaze Tracking", meta=(ToolTip="UDP port used by the gaze tracking subsystem."))
	int32 GazeUdpPort = 8888;

	UPROPERTY(EditAnywhere, Config, Category="Gaze Tracking", meta=(ToolTip="Whether the gaze tracking subsystem should automatically start its listener during Initialize."))
	bool bAutoStartGazeListener = true;

	UPROPERTY(EditAnywhere, Config, Category="Audio Stream", meta=(ToolTip="Minimum UDP port used when the audio relay subsystem auto-selects a bind port."))
	int32 AudioSocketServerPortMin = 19001;

	UPROPERTY(EditAnywhere, Config, Category="Audio Stream", meta=(ToolTip="Maximum UDP port used when the audio relay subsystem auto-selects a bind port."))
	int32 AudioSocketServerPortMax = 19010;

	static const UCICRuntimeSettings* Get();
};

