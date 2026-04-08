// JasmineLatte

#pragma once

// 说明：
// 本文件存放 MCP 传输相关的子系统声明。
// 当前实现基于 HTTP chunked streaming，使用 /stream + /end-stream 端点承载 MCP 请求与流式回包。

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Kismet/BlueprintAsyncActionBase.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Templates/SharedPointer.h"
#include "Containers/Queue.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "civetweb.h"

// MCP 相关类型
#include "MCP/MCPTypes.h"
#include "MCP/MCPToolProperty.h"
#include "MCP/MCPToolCore.h"
#include "MCP/MCPToolStorage.h"
#include "MCP/MCPToolHandle.h"

#include "MCPTransportSubsystem.generated.h"

/**
 * MCP 传输子系统
 * - 职责：
 *   1) 管理基于 HTTP chunked streaming 的本地服务。
 *   2) 负责 MCP 工具的注册、路由与调用。
 *   3) 提供工具数据枚举/探针接口（/tools、/tools/version 等）。
 */
UCLASS()
class MCPFRAMEWORK_API UMCPTransportSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override;

    // 将一帧 JSON payload 放入指定流上下文队列；实际输出格式为 NDJSON。
    void QueueStreamPayload(const FString& StreamId, const FString& Payload);

    // 处理收到的 MCP JSON-RPC 请求，并将结果/进度写入对应流上下文。
    void HandleStreamRequest(const FMCPRequest& Request, const FString& StreamId);

    // 启动 MCP 服务器（注册 URI 及处理器）
    UFUNCTION(BlueprintCallable, Category = "NetworkCore|MCP")
    void StartMCPServer();

private:
    bool bIsShuttingDown = false;

    FCriticalSection SessionLock;

    struct mg_context* ServerContext = nullptr;

    // 流上下文队列：每个 StreamId 对应一个消息队列，输出格式为 NDJSON chunk。
    TMap<FString, TSharedPtr<TQueue<FString>>> StreamQueues;

    // 正在关闭的流，用于让 GET /stream 的长连接优雅结束。
    TSet<FString> ClosingStreams;

    // HTTP 处理器（CivetWeb 回调）
    static int OnStream(struct mg_connection* Connection, void* UserData);
    static int OnEndStream(struct mg_connection* Connection, void* UserData);
    static int OnGetTools(struct mg_connection* Connection, void* UserData);
    static int OnGetToolsUI(struct mg_connection* Connection, void* UserData);
    static int OnGetToolsVersion(struct mg_connection* Connection, void* UserData);

    // 生成唯一的流上下文 ID
    FString GenerateSessionId() const;

public:
    // JSON-RPC 解析辅助
    static void ParseJsonRPC(const FString& JsonString, FString& Method, TSharedPtr<FJsonObject>& Params, int& ID, TSharedPtr<FJsonObject>& JsonObject);

    // 已注册的 MCP 工具存储（支持同名多路由累计）
    UPROPERTY()
    TMap<FString, FMCPToolStorage> MCPTools;

    // 注册工具定义与回调路由
    UFUNCTION(BlueprintCallable, Category = "NetworkCore")
    void RegisterToolProperties(FMCPTool tool, FMCPRouteDelegate MCPRouteDelegate);

    // 工具查询：按目标对象名检索工具数据
    TSharedPtr<FJsonObject> GetToolbyTarget(FString ActorName);

    // 工具路由回调（蓝图适配）
    UFUNCTION(BlueprintCallable, Category = "NetworkCore")
    void OnToolRouteCallback(const FString& Result, UMCPToolHandle* MCPToolHandle, const FMCPTool& MCPTool)
    {
        TSharedPtr<FJsonObject> JsonObject = MakeShareable(new FJsonObject());
        TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Result);
        if (FJsonSerializer::Deserialize(Reader, JsonObject))
        {
            TSharedPtr<FJsonObject> Params = JsonObject->GetObjectField(TEXT("params"));
            TSharedPtr<FJsonObject> Arguments = Params->GetObjectField(TEXT("arguments"));
            const FString TargetName = Arguments->GetStringField(TEXT("ObjectName"));
            MCPToolHandle->ToolCallback(false, GetToolbyTarget(TargetName));
        }
        else
        {
            MCPToolHandle->ToolCallback(true, TEXT("解析json失败"));
        }
    }

    // 工具目标查询：列出工具可作用的目标集合
    TSharedPtr<FJsonObject> GetToolTargets(FString ToolName);

    UFUNCTION(BlueprintCallable, Category = "NetworkCore")
    void OnToolTargetsCallback(const FString& Result, UMCPToolHandle* MCPToolHandle, const FMCPTool& MCPTool)
    {
        TSharedPtr<FJsonObject> JsonObject = MakeShareable(new FJsonObject());
        TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Result);
        if (FJsonSerializer::Deserialize(Reader, JsonObject))
        {
            TSharedPtr<FJsonObject> Params = JsonObject->GetObjectField(TEXT("params"));
            TSharedPtr<FJsonObject> Arguments = Params->GetObjectField(TEXT("arguments"));
            const FString TargetName = Arguments->GetStringField(TEXT("ToolName"));
            MCPToolHandle->ToolCallback(false, GetToolTargets(TargetName));
        }
        else
        {
            MCPToolHandle->ToolCallback(true, TEXT("解析json失败"));
        }
    }

    // 返回所有注册工具的完整 JSON（含参数与可用目标）
    TSharedPtr<FJsonObject> BuildAllRegisteredToolsJsonObject() const;
    UFUNCTION(BlueprintCallable, Category = "NetworkCore|MCP|Introspect")
    FString GetAllRegisteredToolsJson();

    // 线程安全版本（必要时切回游戏线程收集完整 targets）
    UFUNCTION(BlueprintCallable, Category = "NetworkCore|MCP|Introspect")
    FString GetAllRegisteredToolsJson_Safe();
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnRefreshMCPComplete, bool, bSuccess, const FString&, Message);

UCLASS()
class MCPFRAMEWORK_API URefreshMCPClientAsyncAction : public UBlueprintAsyncActionBase
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintCallable, Category = "MCP", meta = (BlueprintInternalUseOnly = "true", HidePin = "WorldContextObject"))
    static URefreshMCPClientAsyncAction* RefreshMCPClient(UObject* WorldContextObject);

    virtual void Activate() override;

    UPROPERTY(BlueprintAssignable)
    FOnRefreshMCPComplete OnSuccess;

    UPROPERTY(BlueprintAssignable)
    FOnRefreshMCPComplete OnFailure;

private:
    void HandleRequestComplete(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bSuccess);
};


