#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "MicAudioCaptureComponent.h"
#include "MicAudioCaptureSubsystem.generated.h"

/**
 * 麦克风音频捕获子系统
 * 提供全局访问麦克风设备和音频捕获功能的接口
 */
UCLASS()
class CUSTOMINPUTCONTROLLER_API UMicAudioCaptureSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	// 开始初始化
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	// 结束时清理
	virtual void Deinitialize() override;

	/** 刷新可用的麦克风设备列表 */
	UFUNCTION(BlueprintCallable, Category="MicAudioCapture")
	void RefreshMicDevices();

	/** 获取可用的麦克风设备列表 */
	UFUNCTION(BlueprintPure, Category="MicAudioCapture")
	TArray<FString> GetAvailableMicDevices() const;

	/** 开始捕获麦克风音频 */
	UFUNCTION(BlueprintCallable, Category="MicAudioCapture")
	bool StartCapture(int32 DeviceIndex = 0);

	/** 停止捕获麦克风音频 */
	UFUNCTION(BlueprintCallable, Category="MicAudioCapture")
	void StopCapture();

	/** 连接到WebSocket服务器 */
	UFUNCTION(BlueprintCallable, Category="MicAudioCapture")
	bool ConnectToServer(const FString& ServerUrl);

	/** 断开与WebSocket服务器的连接 */
	UFUNCTION(BlueprintCallable, Category="MicAudioCapture")
	void DisconnectFromServer();

	/** 检查是否正在捕获 */
	UFUNCTION(BlueprintPure, Category="MicAudioCapture")
	bool IsCapturing() const;

	/** 检查WebSocket连接状态 */
	UFUNCTION(BlueprintPure, Category="MicAudioCapture")
	bool IsConnectedToServer() const;

	/** 获取当前音量级别(0.0-1.0) */
	UFUNCTION(BlueprintPure, Category="MicAudioCapture")
	float GetCurrentAudioLevel() const;

	/** 设置音量倍增器 */
	UFUNCTION(BlueprintCallable, Category="MicAudioCapture")
	void SetVolumeMultiplier(float InMultiplier);

	/** 获取音量倍增器 */
	UFUNCTION(BlueprintPure, Category="MicAudioCapture")
	float GetVolumeMultiplier() const;

	/** 设置采样率 */
	UFUNCTION(BlueprintCallable, Category="MicAudioCapture")
	void SetSampleRate(int32 InSampleRate);

	/** 获取采样率 */
	UFUNCTION(BlueprintPure, Category="MicAudioCapture")
	int32 GetSampleRate() const;

	/** 设置声道数 */
	UFUNCTION(BlueprintCallable, Category="MicAudioCapture")
	void SetNumChannels(int32 InNumChannels);

	/** 获取声道数 */
	UFUNCTION(BlueprintPure, Category="MicAudioCapture")
	int32 GetNumChannels() const;

	/** 设置缓冲区大小(毫秒) */
	UFUNCTION(BlueprintCallable, Category="MicAudioCapture")
	void SetBufferSizeMs(int32 InBufferSizeMs);

	/** 获取缓冲区大小(毫秒) */
	UFUNCTION(BlueprintPure, Category="MicAudioCapture")
	int32 GetBufferSizeMs() const;

	/** 设置是否启用调试日志 */
	UFUNCTION(BlueprintCallable, Category="MicAudioCapture")
	void SetEnableDebugLogs(bool bEnable);

	/** 获取是否启用调试日志 */
	UFUNCTION(BlueprintPure, Category="MicAudioCapture")
	bool GetEnableDebugLogs() const;

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

private:
	// 内部麦克风捕获组件实例
	UPROPERTY()
	UMicAudioCaptureComponent* MicCapture;

	// 初始化内部组件
	void InitializeCaptureComponent();

	// 事件转发处理函数（用于绑定动态委托）
	UFUNCTION()
	void HandleMicDevicesUpdated(const TArray<FString>& DeviceNames);

	UFUNCTION()
	void HandleAudioLevelUpdated(float Level);

	UFUNCTION()
	void HandleWebSocketConnected(const FString& ServerAddress);

	UFUNCTION()
	void HandleWebSocketError(const FString& ErrorMsg, int32 ErrorCode);
};
