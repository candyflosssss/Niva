#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "MCPFrameworkSettings.generated.h"

UCLASS(config = Game, defaultconfig, meta = (DisplayName = "MCP Framework Settings"))
class MCPFRAMEWORK_API UMCPFrameworkSettings : public UDeveloperSettings
{
    GENERATED_BODY()

public:
    UPROPERTY(Config, EditAnywhere, Category = "Transport", meta = (ClampMin = 1024, ClampMax = 65535))
    int32 StreamPort = 8080;

    UPROPERTY(Config, EditAnywhere, Category = "Transport")
    FString StreamBaseURL = TEXT("http://127.0.0.1:8080");

    UPROPERTY(Config, EditAnywhere, Category = "Transport")
    FString StreamPath = TEXT("/stream");

    UPROPERTY(Config, EditAnywhere, Category = "Transport")
    FString EndStreamPath = TEXT("/end-stream");
};


