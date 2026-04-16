#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "CICGazeTrackingSettings.generated.h"

UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "CIC Gaze Tracking"))
class CUSTOMINPUTCONTROLLER_API UCICGazeTrackingSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UCICGazeTrackingSettings();

	/** Port to listen for UDP Gaze Data */
	UPROPERTY(Config, EditAnywhere, Category = "Gaze Tracking", meta = (ClampMin = "1024", ClampMax = "65535"))
	int32 GazeCleanupPort;
};
