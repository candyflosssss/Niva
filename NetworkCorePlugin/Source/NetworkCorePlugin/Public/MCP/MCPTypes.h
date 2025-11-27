// Auto-split from NetworkCoreSubsystem.h — MCP basic types
#pragma once

#include "CoreMinimal.h"
#include "MCPTypes.generated.h"

// FMCP 基础请求与枚举类型

USTRUCT(BlueprintType)
struct FMCPRequest
{
    GENERATED_BODY()
public:
    UPROPERTY()
    FString Json;
};

UENUM(BlueprintType)
enum class EMCPJsonType : uint8
{
    None = 0,
    String = 1,
    Number = 2,
    Integer = 3,
    Boolean = 4,
    Object = 5,
    Array = 6
};
