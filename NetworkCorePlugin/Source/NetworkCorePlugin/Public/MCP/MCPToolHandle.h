// Auto-split — MCP tool handle
#pragma once

#include "CoreMinimal.h"
#include "MCP/MCPTypes.h"
#include "Dom/JsonObject.h"
#include "MCP/MCPToolCore.h"
#include "MCPToolHandle.generated.h"

class UMCPTransportSubsystem;

UCLASS(BlueprintType)
class NETWORKCOREPLUGIN_API UMCPToolHandle : public UObject
{
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadOnly, Category = "NetworkCore|MCP|Tool")
    int MCPid = -1;

    UPROPERTY(BlueprintReadOnly, Category = "NetworkCore|MCP|Tool")
    FString SessionId = "none";

    UPROPERTY(BlueprintReadOnly, Category = "NetworkCore|MCP|Tool")
    UMCPTransportSubsystem* MCPTransportSubsystem = nullptr;

    UPROPERTY(BlueprintReadOnly, Category = "NetworkCore|MCP|Tool")
    FString ProgressToken;

    UMCPToolHandle() : MCPid(-1), SessionId("none"), MCPTransportSubsystem(nullptr), ProgressToken("") {}

    // 与原始接口保持一致：提供一个静态工厂方法
    UFUNCTION(BlueprintCallable, Category = "NetworkCore|MCP")
    static UMCPToolHandle* initToolHandle(int id, const FString& _SessionID, UMCPTransportSubsystem* _subsystem, const FString& InProgressToken = TEXT(""));

    UFUNCTION(BlueprintCallable, Category = "NetworkCore|MCP", meta = (HidePin = "json"))
    void ToolCallbackRaw(bool isError, const FString& text, bool bFinal, int32 Completed=-1, int32 Total=-1);

    UFUNCTION(BlueprintCallable, Category = "NetworkCore|MCP", meta = (HidePin = "json"))
    inline void ToolCallback(bool isError, FString text) {
        ToolCallbackRaw(isError, text, /*bFinal=*/true);
    }

    void ToolCallback(bool isError, TSharedPtr<FJsonObject> json);
};
