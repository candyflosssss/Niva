#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "AudioCaptureCore.h"
#include "AudioCaptureDeviceInterface.h"
#include "WebSocketsModule.h"
#include "IWebSocket.h"
#include "MicAudioCaptureComponent.generated.h"

DECLARE_LOG_CATEGORY_EXTERN(LogMicAudioCapture, Log, All);

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnMicCaptureStarted);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnMicCaptureStopped);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnMicDevicesUpdated, const TArray<FString>&, DeviceNames);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnAudioLevelUpdated, float, Level);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnWebSocketConnected, const FString&, ServerAddress);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnWebSocketError, const FString&, ErrorMsg, int32, ErrorCode);

/**
 * 麦克风音频捕获组件
 * 捕获麦克风音频并通过WebSocket推流到服务器
 */
UCLASS(ClassGroup=(Audio), meta=(BlueprintSpawnableComponent))
class CUSTOMINPUTCONTROLLER_API UMicAudioCaptureComponent : public UActorComponent
{
	GENERATED_BODY()

public:	
	UMicAudioCaptureComponent();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** 开始捕获麦克风音频 */
	UFUNCTION(BlueprintCallable, Category="MicAudioCapture")
	bool StartCapture(int32 DeviceIndex = 0);

	/** 停止捕获麦克风音频 */
	UFUNCTION(BlueprintCallable, Category="MicAudioCapture")
	void StopCapture();

	/** 刷新可用的麦克风设备列表 */
	UFUNCTION(BlueprintCallable, Category="MicAudioCapture")
	void RefreshMicDevices();

	/** 获取可用的麦克风设备列表 */
	UFUNCTION(BlueprintPure, Category="MicAudioCapture")
	TArray<FString> GetAvailableMicDevices() const;

	/** 获取当前音量级别(0.0-1.0) */
	UFUNCTION(BlueprintPure, Category="MicAudioCapture")
	float GetCurrentAudioLevel() const { return CurrentAudioLevel; }

	/** 连接到WebSocket服务器 */
	UFUNCTION(BlueprintCallable, Category="MicAudioCapture")
	bool ConnectToServer(const FString& ServerUrl);

	/** 断开与WebSocket服务器的连接 */
	UFUNCTION(BlueprintCallable, Category="MicAudioCapture")
	void DisconnectFromServer();

	/** 检查WebSocket连接状态 */
	UFUNCTION(BlueprintPure, Category="MicAudioCapture")
	bool IsConnectedToServer() const;

	/** 是否正在捕获 */
	UFUNCTION(BlueprintPure, Category="MicAudioCapture")
	bool IsCapturing() const { return bIsCapturing; }

	/** 当前设备索引 */
	UFUNCTION(BlueprintPure, Category="MicAudioCapture")
	int32 GetCurrentDeviceIndex() const { return CurrentDeviceIndex; }

	/** 当麦克风捕获开始时触发 */
	UPROPERTY(BlueprintAssignable, Category="MicAudioCapture|Events")
	FOnMicCaptureStarted OnMicCaptureStarted;

	/** 当麦克风捕获停止时触发 */
	UPROPERTY(BlueprintAssignable, Category="MicAudioCapture|Events")
	FOnMicCaptureStopped OnMicCaptureStopped;

	/** 当麦克风设备列表更新时触发 */
	UPROPERTY(BlueprintAssignable, Category="MicAudioCapture|Events")
	FOnMicDevicesUpdated OnMicDevicesUpdated;

	/** 当音量级别更新时触发 */
	UPROPERTY(BlueprintAssignable, Category="MicAudioCapture|Events")
	FOnAudioLevelUpdated OnAudioLevelUpdated;

	/** 当WebSocket连接成功时触发 */
	UPROPERTY(BlueprintAssignable, Category="MicAudioCapture|Events")
	FOnWebSocketConnected OnWebSocketConnected;

	/** 当WebSocket连接出错时触发 */
	UPROPERTY(BlueprintAssignable, Category="MicAudioCapture|Events")
	FOnWebSocketError OnWebSocketError;

	// 麦克风采样率设置
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MicAudioCapture|Settings")
	int32 SampleRate = 16000;

	// 麦克风声道数设置
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MicAudioCapture|Settings", meta=(ClampMin="1", ClampMax="2"))
	int32 NumChannels = 1;

	// 音频缓冲区大小(毫秒)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MicAudioCapture|Settings", meta=(ClampMin="10", ClampMax="1000"))
	int32 BufferSizeMs = 100;

	// 音量放大系数
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MicAudioCapture|Settings", meta=(ClampMin="0.1", ClampMax="10.0"))
	float VolumeMultiplier = 1.0f;

	// 音量检测更新频率(秒)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MicAudioCapture|Settings", meta=(ClampMin="0.01", ClampMax="1.0"))
	float AudioLevelUpdateInterval = 0.1f;

	// WebSocket重连尝试次数
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MicAudioCapture|Settings", meta=(ClampMin="0", ClampMax="10"))
	int32 ReconnectAttempts = 3;

	// WebSocket重连间隔(秒)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MicAudioCapture|Settings", meta=(ClampMin="0.5", ClampMax="10.0"))
	float ReconnectInterval = 2.0f;

	// 是否启用调试日志
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MicAudioCapture|Debug")
	bool bEnableDebugLogs = false;

	// 是否在编辑器中显示音量级别
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MicAudioCapture|Debug")
	bool bShowAudioLevelInEditor = true;

	// 传输分包大小(字节)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MicAudioCapture|Advanced", meta=(ClampMin="512", ClampMax="65536"))
	int32 ChunkSizeBytes = 4096;

private:
	// 音频捕获对象（延迟创建以避免在CDO阶段初始化底层音频）
	TUniquePtr<Audio::FAudioCapture> AudioCapture;
	// 音频捕获设备信息
	Audio::FCaptureDeviceInfo AudioCaptureDeviceInfo;
	// 可用麦克风设备列表
	TArray<FString> AvailableMicDevices;
	// 当前使用的麦克风设备索引
	int32 CurrentDeviceIndex = 0;
	// 当前音量级别(0.0-1.0)
	float CurrentAudioLevel = 0.0f;
	// 上次音量更新时间
	float LastAudioLevelUpdateTime = 0.0f;
	// 当前正在捕获
	bool bIsCapturing = false;
	// WebSocket客户端
	TSharedPtr<IWebSocket> WebSocket;
	// 当前服务器URL
	FString CurrentServerUrl;
	// 重连尝试次数计数
	int32 CurrentReconnectAttempts = 0;
	// 重连定时器句柄
	FTimerHandle ReconnectTimerHandle;
	// 音频捕获回调函数
	void OnAudioCaptured(const float* AudioData, int32 NumFrames, int32 InNumChannels, double StreamTime);
	// 计算当前音频级别
	float CalculateAudioLevel(const float* AudioData, int32 NumFrames, int32 InNumChannels);
	// PCM数据处理(可能需要转换格式)
	TArray<uint8> ProcessAudioData(const float* AudioData, int32 NumFrames, int32 InNumChannels);
	// 尝试重新连接WebSocket
	void TryReconnect();
	// WebSocket事件处理函数
	void HandleWebSocketConnected();
	void OnWebSocketConnectionError(const FString& Error);
	void OnWebSocketClosed(int32 StatusCode, const FString& Reason, bool bWasClean);
	void OnWebSocketMessageReceived(const FString& Message);
};
