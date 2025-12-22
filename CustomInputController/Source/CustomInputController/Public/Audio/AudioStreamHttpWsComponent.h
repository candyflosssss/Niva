#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
// HTTP request typedef (FHttpRequestPtr)
#include "Interfaces/IHttpRequest.h"
#include "AudioStreamHttpWsComponent.generated.h"

class UAudioStreamHttpWsSubsystem;

/**
 * 组件版：每个挂载它的角色都可独立播放自己的流式音频。
 * 默认使用 PCM S16LE，16kHz，单声道；POST可覆盖。
 * POST JSON: { "ws_url": "wss://...", "sample_rate": 16000, "channels": 1 }
 */
UCLASS(ClassGroup=(Audio), meta=(BlueprintSpawnableComponent))
class CUSTOMINPUTCONTROLLER_API UAudioStreamHttpWsComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UAudioStreamHttpWsComponent();

    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

    // 可选：预期注册用Key（为空则由子系统分配或生成）
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AudioStream|Routing")
    FString PreferredKey;

    // 注册后由子系统分配并保持的唯一识别码（UUID）
    UPROPERTY(BlueprintReadOnly, Category="AudioStream|Routing")
    FString RegisteredUuid;

    // 查询此组件的注册 UUID
    UFUNCTION(BlueprintPure, Category="AudioStream|Routing")
    FString GetComponentUuid() const { return RegisteredUuid; }

    // ===== 网络控制（组件拥有） =====
    UFUNCTION(BlueprintCallable, Category="AudioStream")
    void StartRunAndConnect(const FString& ServerHostWithPort = TEXT("127.0.0.1:8001"),
                            const FString& CallbackUrl = TEXT(""),
                            const FString& TargetUuid = TEXT(""),
                            int32 SampleRate = 16000,
                            int32 Channels = 1,
                            bool bUseHttps = false,
                            const FString& HttpRunPath = TEXT("/run"),
                            const FString& WsPathPrefix = TEXT("/ws/"));

    UFUNCTION(BlueprintCallable, Category="AudioStream")
    void PostStreamText(const FString& Text);
    UFUNCTION(BlueprintCallable, Category="AudioStream")
    void PostEndStream();

    // 强制关闭 WebSocket
    UFUNCTION(BlueprintCallable, Category="AudioStream")
    void CloseWebSocket();

private:
    // 注册态
    bool bRegistered = false;

    // 便捷：注册/注销到子系统
    void RegisterToSubsystem();
    void UnregisterFromSubsystem();

    // ===== 组件持有的会话状态 =====
    TSharedPtr<class IWebSocket> WebSocket;
    FString ActiveTaskId;
    FString ActiveHttpHost; // host:port
    bool bActiveUseHttps = false;
    int32 ActiveWsSampleRate = 16000;
    int32 ActiveWsChannels = 1;
    TSharedPtr<IHttpRequest, ESPMode::ThreadSafe> CurrentHttpRequest;

    // 保存当前会话使用的路径（由 StartRunAndConnect 设置），用于重连时重用
    FString ActiveHttpRunPath = TEXT("/run");
    FString ActiveWsPathPrefix = TEXT("/ws/");

    // Internal helpers
    void ConnectWebSocket(const FString& Url);
    // 发起 /run POST 并解析 task_id（从 StartRunAndConnect 中抽取的实现）
    void RequestRunTask(const FString& ServerHostWithPort, const FString& CallbackUrl, const FString& TargetUuid, int32 SampleRate, int32 Channels, bool bUseHttps, const FString& HttpRunPath, const FString& WsPathPrefix);

    // Reconnect logic
    FTimerHandle ReconnectTimerHandle;
    int32 ReconnectAttempts = 0;
    float ReconnectBaseDelaySeconds = 1.0f;
    float ReconnectMaxDelaySeconds = 30.0f;

    // 状态标记：是否曾成功连接（仅在曾经连接后，断开时才触发重连）
    bool bHasEverConnected = false;
    // 手动关闭标记：如果组件主动关闭 WS（例如 EndPlay 或 用户调用 CloseWebSocket），则不要触发重连
    bool bManualClose = false;

    // Schedule cancelable reconnect attempt
    void ScheduleReconnect();
    void CancelReconnect();
};