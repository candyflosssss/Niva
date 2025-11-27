// Auto-split — MCP tool storage
#pragma once

#include "CoreMinimal.h"
#include "MCP/MCPToolCore.h"
#include "MCPToolStorage.generated.h"

USTRUCT(BlueprintType)
struct FMCPToolStorage
{
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite, Category = "NetworkCore|MCP|Tool")
    FMCPTool MCPTool;

    UPROPERTY(BlueprintReadWrite, Category = "NetworkCore|MCP|Tool")
    TArray<FMCPTool> MCPToolVariants;

    UPROPERTY(BlueprintReadWrite, Category = "NetworkCore|MCP|Tool")
    int ToolNum = 0;

    UPROPERTY(BlueprintReadWrite, Category = "NetworkCore|MCP|Tool")
    TArray<FMCPRouteDelegate> RouteDelegates;
};
