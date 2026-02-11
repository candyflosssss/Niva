#pragma once
#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "FunASRSettings.generated.h"
/**
 * Settings for FunASR (Aliyun DashScope ASR)
 */
UCLASS(Config=Game, DefaultConfig, meta=(DisplayName="Fun ASR Settings"))
class CUSTOMINPUTCONTROLLER_API UFunASRSettings : public UDeveloperSettings
{
GENERATED_BODY()
public:
	// 连接设置
	UPROPERTY(EditAnywhere, Config, Category="Connection", meta=(ToolTip="WebSocket 服务地址 (例如: wss://dashscope.aliyuncs.com/api-ws/v1/inference/)"))
	FString WebSocketUrl = TEXT("wss://dashscope.aliyuncs.com/api-ws/v1/inference/");

	UPROPERTY(EditAnywhere, Config, Category="Connection", meta=(ToolTip="API Key 鉴权 (无需加 Bearer 前缀)"))
	FString ApiKey;

	UPROPERTY(EditAnywhere, Config, Category="Connection", meta=(ToolTip="业务空间 ID (可选)"))
	FString WorkspaceId;

	UPROPERTY(EditAnywhere, Config, Category="Connection", meta=(ToolTip="是否开启数据合规检测 (X-DashScope-DataInspection)"))
	bool bEnableDataInspection = false;

	// 核心参数
	UPROPERTY(EditAnywhere, Config, Category="Parameters", meta=(ToolTip="模型名称 (例如: fun-asr-realtime)"))
	FString Model = TEXT("fun-asr-realtime");

	UPROPERTY(EditAnywhere, Config, Category="Parameters", meta=(ToolTip="音频格式: pcm, wav, opus 等"))
	FString Format = TEXT("pcm");

	UPROPERTY(EditAnywhere, Config, Category="Parameters", meta=(ToolTip="采样率 (Hz), fun-asr-realtime 通常为 16000"))
	int32 SampleRate = 16000;

	UPROPERTY(EditAnywhere, Config, Category="Parameters", meta=(ToolTip="是否开启语义断句 (true=语义断句更准; false=VAD断句更快)"))
	bool bSemanticPunctuation = false;

	UPROPERTY(EditAnywhere, Config, Category="Parameters", meta=(EditCondition="!bSemanticPunctuation", ToolTip="最大静音阈值(ms) (VAD断句模式下生效, 范围 200~6000)"))
	int32 MaxSentenceSilence = 1300;

	UPROPERTY(EditAnywhere, Config, Category="Parameters", meta=(ToolTip="语言提示 (例如: zh, en)"))
	TArray<FString> LanguageHints;

	// 自动化设置
	UPROPERTY(EditAnywhere, Config, Category="Automation", meta=(ToolTip="意外断开连接时是否尝试自动重连"))
	bool bAutoReconnect = true;

	UPROPERTY(EditAnywhere, Config, Category="Automation", meta=(EditCondition="bAutoReconnect", ToolTip="重连重试间隔 (秒)"))
	float ReconnectInterval = 3.0f;

	UPROPERTY(EditAnywhere, Config, Category="Automation", meta=(EditCondition="bAutoReconnect", ToolTip="最大重连尝试次数 (0 代表无限重试)"))
	int32 MaxReconnectAttempts = 5;

	static const UFunASRSettings* Get();
};
