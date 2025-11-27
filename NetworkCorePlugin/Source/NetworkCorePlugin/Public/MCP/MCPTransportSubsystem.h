// JasmineLatte

#pragma once

// 说明：
// 本文件为“按功能分类”的重构产物，专门存放 MCP 传输相关的子系统声明。
// 负责基于 CivetWeb 的本地 HTTP/SSE 服务、MCP 工具注册与路由等。

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
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
 *   1) 管理基于 CivetWeb 的 HTTP 服务（包括 SSE）。
 *   2) 负责 MCP 工具的注册、路由与调用。
 *   3) 提供工具数据枚举/探针接口（/tools、/tools/version 等）。
 */
UCLASS()
class NETWORKCOREPLUGIN_API UMCPTransportSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    // 生命周期：初始化/反初始化/是否创建
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;     // 初始化 CivetWeb/路由
    virtual void Deinitialize() override;                                       // 停止服务器并清理资源
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override;          // 目前简单返回 true，可按需限制

    // 事件推送：Server-Sent Events
    // 将事件推送到指定会话（浏览器端通过 EventSource 接收）
    void SendSSE(const FString& SessionId, const FString& Event, const FString& Data);

    // 处理收到的 JSON-RPC POST 请求
    void HandlePostRequest(const FMCPRequest& Request, const FString& SessionId);

    // 启动 MCP 服务器（注册 URI 及处理器）
    UFUNCTION(BlueprintCallable, Category = "NetworkCore|MCP")
    void StartMCPServer();

private:
    // 关闭标记：用于在退出时阻止新请求并安全清理
    bool bIsShuttingDown = false;

    // 会话锁：保护 Sessions 映射的并发访问
    FCriticalSection SessionLock;

    // CivetWeb 服务器上下文
    struct mg_context* ServerContext = nullptr;

    // 会话队列：每个 SessionId 对应一个消息队列，用于 SSE 推送
    TMap<FString, TSharedPtr<TQueue<FString>>> Sessions;

    // HTTP 处理器（CivetWeb 回调）
    static int OnPostMessage(struct mg_connection* Connection, void* UserData);
    static int OnSSE(struct mg_connection* Connection, void* UserData);
    static int OnGetTools(struct mg_connection* Connection, void* UserData);
    static int OnGetToolsUI(struct mg_connection* Connection, void* UserData);
    static int OnGetToolsVersion(struct mg_connection* Connection, void* UserData);

    // 生成唯一的 SessionId
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
