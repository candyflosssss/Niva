// Auto-split from NetworkCoreSubsystem.h — MCP tool core (FMCPTool, delegate, library)
#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
// 仅包含声明所需的头；具体类型由前置声明解决，避免循环依赖
class AActor;
class UActorComponent;
#include "MCP/MCPToolProperty.h"
#include "MCPToolCore.generated.h"

USTRUCT(BlueprintType)
struct FMCPTool
{
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite, Category = "NetworkCore|MCP|Tool")
    FString Name;

    UPROPERTY(BlueprintReadWrite, Category = "NetworkCore|MCP|Tool")
    FString Description;

    UPROPERTY(BlueprintReadWrite, Category = "NetworkCore|MCP|Tool")
    TArray<UMCPToolProperty*> Properties;
};

FORCEINLINE bool operator==(const FMCPTool& A, const FMCPTool& B)
{
    return A.Name == B.Name;
}

FORCEINLINE uint32 GetTypeHash(const FMCPTool& Tool)
{
    return GetTypeHash(Tool.Name);
}

class UMCPToolHandle;

DECLARE_DYNAMIC_DELEGATE_ThreeParams(FMCPRouteDelegate, const FString&, Result, UMCPToolHandle*, MCPToolHandle, const FMCPTool&, MCPTool);

UCLASS()
class NETWORKCOREPLUGIN_API UMCPToolBlueprintLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable, Category = "MCP Tool")
    static bool GetIntValue(const FMCPTool& MCPTool, const FString& Name, const FString& InJson,int32& OutValue);

    UFUNCTION(BlueprintCallable, Category = "MCP Tool")
    static bool GetStringValue(const FMCPTool& MCPTool, const FString& Name, const FString& InJson, FString& OutValue);

    UFUNCTION(BlueprintCallable, Category = "MCP Tool")
    static bool GetNumberValue(const FMCPTool& MCPTool, const FString& Name, const FString& InJson, float& OutValue);

    UFUNCTION(BlueprintCallable, Category = "MCP Tool")
    static bool GetActorValue(const FMCPTool& MCPTool, const FString& Name, const FString& InJson, AActor*& OutValue);

    UFUNCTION(BlueprintCallable, Category = "MCP Tool")
    static bool GetComponentValue(const FMCPTool& MCPTool, const FString& Name, const FString& InJson, UActorComponent*& OutValue);

    UFUNCTION(BlueprintCallable, Category = "MCP Tool")
    static void AddProperty(UPARAM(ref) FMCPTool& MCPTool, UMCPToolProperty* Property);

    static UMCPToolProperty* GetProperty(const FMCPTool& MCPTool, const FString& Name);
};
