#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "MicAudioCaptureSettings.generated.h"

/**
 * 麦克风音频捕获设置
 * 在项目设置中提供全局配置选项
 */
UCLASS(config=Game, defaultconfig, meta=(DisplayName="麦克风音频捕获"))
class CUSTOMINPUTCONTROLLER_API UMicAudioCaptureSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UMicAudioCaptureSettings();

	// 获取设置对象实例
	static const UMicAudioCaptureSettings* Get();

	// 编辑器设置分类
	virtual FName GetCategoryName() const override;
	
#if WITH_EDITOR
	// 设置显示名称（仅编辑器）
	virtual FText GetSectionText() const override;
	
	// 设置描述（仅编辑器）
	virtual FText GetSectionDescription() const override;
#endif

#if WITH_EDITOR
	// 编辑器中设置修改回调
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	// 默认WebSocket服务器URL
	UPROPERTY(config, EditAnywhere, Category="Connection", meta=(DisplayName="默认WebSocket服务器URL"))
	FString DefaultServerUrl = TEXT("ws://10.1.20.57:8765");

	// 默认麦克风采样率
	UPROPERTY(config, EditAnywhere, Category="Audio", meta=(DisplayName="默认采样率", ClampMin="8000", ClampMax="48000"))
	int32 DefaultSampleRate = 16000;

	// 默认麦克风声道数
	UPROPERTY(config, EditAnywhere, Category="Audio", meta=(DisplayName="默认声道数", ClampMin="1", ClampMax="2"))
	int32 DefaultNumChannels = 1;

	// 默认缓冲区大小(毫秒)
	UPROPERTY(config, EditAnywhere, Category="Audio", meta=(DisplayName="缓冲区大小(毫秒)", ClampMin="10", ClampMax="1000"))
	int32 DefaultBufferSizeMs = 100;

	// 默认音量放大系数
	UPROPERTY(config, EditAnywhere, Category="Audio", meta=(DisplayName="音量放大系数", ClampMin="0.1", ClampMax="10.0"))
	float DefaultVolumeMultiplier = 1.0f;

	// 是否启用音量平滑
	UPROPERTY(config, EditAnywhere, Category="Audio", meta=(DisplayName="启用音量平滑"))
	bool bEnableVolumeLeveling = false;

	// 音量平滑速度
	UPROPERTY(config, EditAnywhere, Category="Audio", meta=(DisplayName="音量平滑速度", ClampMin="0.01", ClampMax="1.0", EditCondition="bEnableVolumeLeveling"))
	float VolumeLevelingSpeed = 0.1f;

	// 是否自动连接到服务器
	UPROPERTY(config, EditAnywhere, Category="Connection", meta=(DisplayName="游戏开始时自动连接服务器"))
	bool bAutoConnectOnGameStart = false;

	// 是否自动开始捕获
	UPROPERTY(config, EditAnywhere, Category="Connection", meta=(DisplayName="游戏开始时自动开始捕获"))
	bool bAutoStartCaptureOnGameStart = false;

	// WebSocket重连尝试次数
	UPROPERTY(config, EditAnywhere, Category="Connection", meta=(DisplayName="重连尝试次数", ClampMin="0", ClampMax="10"))
	int32 ReconnectAttempts = 3;

	// WebSocket重连间隔(秒)
	UPROPERTY(config, EditAnywhere, Category="Connection", meta=(DisplayName="重连间隔(秒)", ClampMin="0.5", ClampMax="10.0"))
	float ReconnectInterval = 2.0f;

	// 传输分包大小(字节)
	UPROPERTY(config, EditAnywhere, Category="Advanced", meta=(DisplayName="传输分包大小(字节)", ClampMin="512", ClampMax="65536"))
	int32 ChunkSizeBytes = 4096;

	// 是否启用调试日志
	UPROPERTY(config, EditAnywhere, Category="Debug", meta=(DisplayName="启用调试日志"))
	bool bEnableDebugLogs = false;

	// 是否在编辑器中显示音量级别
	UPROPERTY(config, EditAnywhere, Category="Debug", meta=(DisplayName="在编辑器中显示音量级别"))
	bool bShowAudioLevelInEditor = true;
};
