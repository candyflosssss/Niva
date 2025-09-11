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
#include "TTSAsyncNode.generated.h"



/*
* TTS实际所需返回
*/
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnNivaTTSRequestComplete, const TArray<uint8>&, Sound, UNivaTTSRequest*, TTSRequest);
/*
* TTS请求完成
* 需要额外转发给异步节点
*/
DECLARE_DELEGATE_TwoParams(FOnNivaAsyncTTSRequestComplete, const TArray<uint8>&, UNivaTTSRequest*);

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnWebSocketConnected);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnWebSocketDisconnected, int32, StatusCode);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnWebSocketMessage, const FString&, Message);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnWebSocketError, const FString&, Error);




/*
* 古希腊掌管TTS的神
*
*
* TTS 异步处理节点的内核
*
* 1、管理一个request类，用于请求
* 2、基于TTS的特殊性，该类需要实现一个Complete,用于异步节点的输出
* request 完成 -> 调用 该类的Complete 对数据进行处理 -> 调用异步节点的Complete输出
* 3、该类需要实现一个CancelRequest,用于取消请求
* 4、该类需要实现一个init,用于初始化请求
* 5、该类需要实现一个Bin2SoundWave,用于将二进制数据转换为SoundWave (TODO)
*
*
*/
UCLASS(BlueprintType)
class NETWORKCOREPLUGIN_API UNivaTTSRequest : public UObject
{
	GENERATED_BODY()

public:
	//  先来个构造函数
	UNivaTTSRequest(const FObjectInitializer& ObjectInitializer)
		: Super(ObjectInitializer)
	{
		// 这里可以进行一些初始化操作
	};
public:
	// 绑定委托
	UPROPERTY(BlueprintAssignable, Category = "NivaTTSRequest")
	FOnNivaTTSRequestComplete OnNivaTTSRequestComplete;
	FOnNivaAsyncTTSRequestComplete OnNivaAsyncTTSRequestComplete;


	// 通过IHTTPRequest来初始化UNivaTTSRequest
	UFUNCTION(BlueprintCallable, Category = "NetCore")
	virtual void init(
		const FString& APIKey,
		const FString& Message,
		const FString& ReferenceID
	);

	// request:
	// UPROPERTY(BlueprintReadOnly, Category = "Request")
	TSharedPtr<IHttpRequest> TTSRequest;

	// 超时检测的定时器
	FTimerHandle TimerHandle;

	// 最终解析的sound：
	// TODO::最终应该直接在Cpp中解析成SoundWave
	// 目前未实现
	UPROPERTY(BlueprintReadOnly, Category = "Sound")
	USoundWave* Sound = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Sound")
	bool Completed = false;

	// complete事件及解析：
	// 改成虚函数
	virtual void OnTTSComplete(
		FHttpRequestPtr request,
		FHttpResponsePtr response,
		bool bWasSuccessful
	);


	// 中断请求
	UFUNCTION(BlueprintCallable, Category = "NetCore")
	void CancelRequest();

	// wav转soundwave
	USoundWave* Bin2SoundWave(const TArray<uint8>& AudioData);

};


UCLASS(BlueprintType)
class NETWORKCOREPLUGIN_API UAliyunTTSRequest : public UNivaTTSRequest
{
	GENERATED_BODY()

public:
	
	UAliyunTTSRequest(const FObjectInitializer& ObjectInitializer);
	
	// 重载基类方法
	
	virtual void init(
		const FString& APIKey,
		const FString& Message,
		const FString& ReferenceID
	) override;

	virtual void OnTTSComplete(
		FHttpRequestPtr request,
		FHttpResponsePtr response,
		bool bWasSuccessful
	) override;

	
	UFUNCTION(BlueprintCallable, Category = "WebSocket")
	void ConnectWebSocket(const FString& URL);

	UFUNCTION(BlueprintCallable, Category = "WebSocket")
	void DisconnectWebSocket();

	UFUNCTION(BlueprintCallable, Category = "WebSocket")
	void SendWebSocketMessage(const FString& Message);

	FString TaskId;

	FString Text;
private:
	// 任务状态跟踪
	bool bTaskStarted = false;
	bool bTaskFinished = false;

	// 音频数据缓冲区
	TArray<uint8> AudioDataBuffer;
	FWebSocketsModule* WebSocketModule;
	TSharedPtr<IWebSocket> WebSocket;

	void OnWebSocketConnected();
	void OnWebSocketClosed(int32 StatusCode, const FString& Reason, bool bWasClean);
	void OnWebSocketMessage(const FString& Message);
    void OnWebSocketBinaryMessage(const void* Data, SIZE_T Size, bool bIsLastFragment);
	void OnWebSocketError(const FString& Error);
	
	void SendFinishTask();

};


// 重载一个TTSRequest
UCLASS(BlueprintType)
class NETWORKCOREPLUGIN_API UNivaMelotteTTSRequest : public UNivaTTSRequest
{
	GENERATED_BODY()
public:
	



	// complete事件及解析：
	// 改成虚函数
	//virtual void OnTTSComplete(
	//	FHttpRequestPtr request,
	//	FHttpResponsePtr response,
	//	bool bWasSuccessful
	//);

	// 通过IHTTPRequest来初始化UNivaTTSRequest
	virtual void init(
		const FString& APIKey,
		const FString& Message,
		const FString& ReferenceID
	) override;

};





/*
* 古希腊掌管TTS的神
*
* 通过设定不同的TTSRequest类型来实现不同的TTS请求
* 
*/
UCLASS()
class NETWORKCOREPLUGIN_API UTTSNode : public UBlueprintAsyncActionBase
{
	GENERATED_BODY()

	// 完成
	UPROPERTY(BlueprintAssignable)
	FOnNivaTTSRequestComplete CompleteDelegate;

	// 异步节点结果
	UFUNCTION(BlueprintCallable, meta = (BlueprintInternalUseOnly = "true"))
	static UTTSNode* SendTTSRequest(
		const FString& Message
	);

	// 处理请求
	void OnCompleteDelegate(
		UNivaTTSRequest* Request,
		TArray<uint8> ResponsePtr
	);

};


