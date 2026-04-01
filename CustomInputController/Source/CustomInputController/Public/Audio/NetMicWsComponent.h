#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "NetMicWsComponent.generated.h"

class FCICWebSocketSession;
class UNetMicWsSubsystem;

DECLARE_DYNAMIC_MULTICAST_DELEGATE( FNetMicSimpleEvent );
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam( FNetMicErrorEvent, const FString&, Error );
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam( FNetMicTextEvent, const FString&, Message );
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam( FNetMicAudioEvent, const TArray<uint8>&, AudioBytes );

/**
 * 网络麦克风组件：
 * - 连接到文档所述的 WebSocket 服务器（<start>/<end>/<list_devices>/<set_device:N> 文本控制）
 * - 接收的二进制音频（PCM S16LE, 16kHz, mono）会自动转发给 FunASR 子系统
 * - 具备自动重连（指数退避），在重连后按期望状态恢复（设备/录音）
 * - 暴露基础控制到蓝图
 */
UCLASS(ClassGroup=(Audio), meta=(BlueprintSpawnableComponent))
class CUSTOMINPUTCONTROLLER_API UNetMicWsComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UNetMicWsComponent();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	// 自动连接的服务器地址（在 BeginPlay 自动连接；为空则不自动）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="NetMic")
	FString AutoConnectWsUrl = TEXT("");

	// 连接/断开
	UFUNCTION(BlueprintCallable, Category="NetMic")
	void Connect(const FString& InWsUrl);

	UFUNCTION(BlueprintCallable, Category="NetMic")
	void Disconnect();

	// 录音控制（将发送 <start>/<end>，并自动触发 ASR 任务开始/结束）
	UFUNCTION(BlueprintCallable, Category="NetMic")
	void StartRecording();

	UFUNCTION(BlueprintCallable, Category="NetMic")
	void StopRecording();

	// 设备控制
	UFUNCTION(BlueprintCallable, Category="NetMic")
	void RequestDeviceList();

	UFUNCTION(BlueprintCallable, Category="NetMic")
	void SetDeviceIndex(int32 Index);



	// 转发到 ASR 的总开关（默认开启）
	UFUNCTION(BlueprintCallable, Category="NetMic|ASR")
	void EnableForwardToASR(bool bEnable) { bForwardToASR = bEnable; }

	UFUNCTION(BlueprintPure, Category="NetMic|ASR")
	bool IsForwardToASREnabled() const { return bForwardToASR; }

	// 重连参数
	UFUNCTION(BlueprintCallable, Category="NetMic|Reconnect")
	void ConfigureReconnect(bool bEnable, float InBaseDelaySeconds = 1.0f, float InMaxDelaySeconds = 30.0f);

	UFUNCTION(BlueprintPure, Category="NetMic|Reconnect")
	bool IsAutoReconnectEnabled() const { return bAutoReconnect; }

	// 状态查询
	UFUNCTION(BlueprintPure, Category="NetMic")
	bool IsConnected() const { return bIsConnected; }

	UFUNCTION(BlueprintPure, Category="NetMic")
	bool IsRecordingExpected() const { return bShouldBeRecording; }

	// 事件
	UPROPERTY(BlueprintAssignable, Category="NetMic|Event")
	FNetMicSimpleEvent OnConnected;

	UPROPERTY(BlueprintAssignable, Category="NetMic|Event")
	FNetMicSimpleEvent OnDisconnected;

	UPROPERTY(BlueprintAssignable, Category="NetMic|Event")
	FNetMicErrorEvent OnError;

	// 服务器文本消息（设备列表等，原样转出，方便蓝图自定义处理）
	UPROPERTY(BlueprintAssignable, Category="NetMic|Event")
	FNetMicTextEvent OnServerMessage;

	// 音频数据（当开关启用时自动转发到 ASR）
	UPROPERTY(BlueprintAssignable, Category="NetMic|Event")
	FNetMicAudioEvent OnAudioFrame;

private:
	void OpenWebSocket(const FString& Url);
	void CloseWebSocket(bool bIsManual);

	void OnWsConnected();
	void OnWsConnectionError(const FString& Error);
	void OnWsClosed(int32 StatusCode, const FString& Reason, bool bWasClean);
	void OnWsMessage(const FString& Message);
	void OnWsBinaryFrame(const TArray<uint8>& Data);

	void TryRestoreDesiredStateAfterConnect();

	// 重连
	void ScheduleReconnect();
	void CancelReconnect();
	float GetNextBackoffSeconds() const;

	// 向服务器发送一行控制命令
	void SendCtrl(const FString& Ctrl);

	// 将 PCM 帧投递到 ASR 子系统（由子系统内部管理任务态）
	void ForwardAudioToASR(const TArray<uint8>& Bytes);

private:
	TSharedPtr<FCICWebSocketSession> WebSocketSession;
	TWeakObjectPtr<UNetMicWsSubsystem> CompatibilitySubsystem;
	FString LastUrl;
	bool bIsConnected = false;
	bool bManualClose = false;
	bool bIsConnecting = false; // 防止并发连接/重连

	// 期望状态（用于重连之后恢复）
	bool bShouldBeRecording = false;
	int32 DesiredDeviceIndex = -1; // <0 表示未指定
	bool bForwardToASR = true;


	// 重连参数/状态
	bool bAutoReconnect = true;
	int32 ReconnectAttempts = 0;
	float ReconnectBaseDelaySeconds = 1.0f;
	float ReconnectMaxDelaySeconds = 30.0f;
	FTimerHandle ReconnectTimerHandle;

};
