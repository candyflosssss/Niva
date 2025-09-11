// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "Interfaces/IHttpRequest.h"
#include "Kismet/BlueprintAsyncActionBase.h"
#include "HttpModule.h"
#include "Sound/SoundWave.h"
#include "Serialization/JsonReader.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "Engine/Engine.h"         // for GEngine
#include "Engine/GameViewportClient.h"  // for GameViewport
#include "TimerManager.h"          // for FTimerHandle, GetTimerManager()
#include "Audio.h"
#include "AudioDecompress.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Engine/TimerHandle.h"
#include "Interfaces/IHttpResponse.h" 
#include "NivaNetworkCoreSettings.h"
#include "UObject/NoExportTypes.h"
#include "IWebSocket.h"
#include "Modules/ModuleManager.h"
#include "WebSocketsModule.h"
#include "WebSocketsModule.h"
#include "LLMAsyncNode.generated.h"

/*
* LLM实际所需返回
*/
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FNivaLLMDelegate, bool, bConnectedSuccessfully, FString, Contant);
/*
* LLM请求完成
* 需要额外转发给异步节点
*/
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FCompleteDelegate, bool, bConnectedSuccessfully, FString, Contant);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FProgressDelegate, int32, BytesSent, int32, BytesReceived, FString, Contant);

DECLARE_DELEGATE_TwoParams(FNivaLLMAsyncDelegate, bool, FString);


/*
* 古希腊掌管LLM的神
* 
* 1、管理一个request类，用于请求
* 2、基于LLM的特殊性，该类需要实现一个OnProgressDelegate来实现对流式与非流式的支持
* request 处理 -> 检查新json -> 调用OnProgressDelegate对新json进行处理 -> 返回最终结果
* 3、该类需要实现一个CancelRequest,用于取消请求
* 4、该类需要实现一个init,用于初始化请求
* 5、该类需要实现一个BuildChatRequestJson，用于适配不同LLM模型所需的body内容
* 
*/
UCLASS(BlueprintType)
class NETWORKCOREPLUGIN_API UNivaLLMRequest : public UObject
{
	GENERATED_BODY()

public:
	//  先来个构造函数
	UNivaLLMRequest(const FObjectInitializer& ObjectInitializer)
		: Super(ObjectInitializer)
	{
		// 这里可以进行一些初始化操作
	};
public:
	// 绑定委托
	UPROPERTY(BlueprintAssignable, Category = "NivaTTSRequest")
	FProgressDelegate OnNivaLLMRequestProgress;
	UPROPERTY(BlueprintAssignable, Category = "NivaTTSRequest")
	FCompleteDelegate OnNivaLLMRequestComplete;
	// 转发给Async节点
	FNivaLLMAsyncDelegate OnNivaAsyncLLMRequestProgress;
	FNivaLLMAsyncDelegate OnNivaAsyncLLMRequestComplete;


	// 通过IHTTPRequest来初始化UNivaTTSRequest
	virtual void init(
		TMap<FString/*user*/, FString/*assistant*/> Chated,
		FString Chat
	);


	TSharedPtr<IHttpRequest> LLMRequest;

	UPROPERTY(BlueprintReadOnly, Category = "Sound")
	bool Completed = false;

	// complete事件及解析：

	// 处理请求
	virtual void OnCompleteDelegate(
		FHttpRequestPtr Request,
		FHttpResponsePtr ResponsePtr,
		bool a);

	virtual void OnProgressDelegate(
		FHttpRequestPtr Request,
		uint64 BytesSent,
		uint64 BytesReceived);

	// 中断请求
	UFUNCTION(BlueprintCallable, Category = "NetCore")
	void CancelRequest();


	// 测试
	UFUNCTION(BlueprintCallable, Category = "NetCore")
	static UNivaLLMRequest* CreateLLMRequest();

	virtual FString BuildChatRequestJson(const TMap<FString, FString>& ChatHistory, FString Chat);

	static FString EscapeChatContent(const FString& Content);

protected:

	FString PreviousContent; // 保存上一次接收的完整内容

	int32 LastProcessedPosition; // 记录上一次处理到的位置

	bool ProcessBuffer(const FString& NewContent, int32 BaseOffset, TArray<FString>& Result, int& GlobalStart, int& GlobalEnd);

	FString LastSentence = "123";


};


// 重载一个LLMRequest
UCLASS(BlueprintType)
class NETWORKCOREPLUGIN_API UNivaAliyunLLMRequest : public UNivaLLMRequest
{
	GENERATED_BODY()

public:
	virtual void init(
		TMap<FString/*user*/, FString/*assistant*/> Chated,
		FString Chat
	) override;

	virtual FString BuildChatRequestJson(const TMap<FString, FString>& ChatHistory, FString Chat) override;

	// 处理请求
	virtual void OnProgressDelegate(
		FHttpRequestPtr Request,
		uint64 BytesSent,
		uint64 BytesReceived) override;

};


// 重载一个LLMRequest
UCLASS(BlueprintType)
class NETWORKCOREPLUGIN_API UNivaAgentLLMRequest : public UNivaLLMRequest
{
	GENERATED_BODY()
public:
	// 智能体专用初始化方法
	virtual void init(
		TMap<FString, FString> Chated,
		FString Chat
	) override;
	
	virtual FString BuildChatRequestJson(const TMap<FString, FString>& ChatHistory, FString Chat) override;

	// 重写进度处理以支持SSE流式响应
	virtual void OnProgressDelegate(
		FHttpRequestPtr Request,
		uint64 BytesSent,
		uint64 BytesReceived) override;

	virtual void OnCompleteDelegate(
		FHttpRequestPtr Request,
		FHttpResponsePtr ResponsePtr,
		bool bA) override;

private:
	// SSE解析状态
	FString AccumulatedResponse;
	bool bIsStreamingComplete = false;
	FString ReceivedBuffer;
	int32 ParseOffset = 0;

};

// 新增 Runner LLM 请求
UCLASS(BlueprintType)
class NETWORKCOREPLUGIN_API UNivaRunnerLLMRequest : public UNivaLLMRequest
{
	GENERATED_BODY()
public:
	virtual void init(
		TMap<FString, FString> Chated,
		FString Chat
	) override;

	virtual FString BuildChatRequestJson(const TMap<FString, FString>& ChatHistory, FString Chat) override;
};


/*
 * 古希腊掌管LLM的神
 */
UCLASS()
class NETWORKCOREPLUGIN_API UBlueprintAsyncNode : public UBlueprintAsyncActionBase
{
	GENERATED_BODY()

public:
	// 进度
	UPROPERTY(BlueprintAssignable)
	FNivaLLMDelegate ProgressDelegate;
	void OnProgressDelegate(
		bool success,
		FString Content
	) {
		ProgressDelegate.Broadcast(success, Content);
	}

	//// 完成
	UPROPERTY(BlueprintAssignable)
	FNivaLLMDelegate CompleteDelegate;
	void OnCompleteDelegate(
		bool success,
		FString Content
	) {
		CompleteDelegate.Broadcast(success, Content);
	}


	// 异步节点结果
	UFUNCTION(BlueprintCallable, meta = (BlueprintInternalUseOnly = "true"))
	static UBlueprintAsyncNode* LLMChat(
		TMap<FString/*user*/, FString/*assistant*/> Chatted,
		FString Chat
	);


private:
	UPROPERTY()
	TObjectPtr<UNivaLLMRequest>LLMRequest = nullptr;


};
