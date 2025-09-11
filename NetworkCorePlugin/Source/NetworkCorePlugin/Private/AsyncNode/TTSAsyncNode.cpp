// Fill out your copyright notice in the Description page of Project Settings.


#include "AsyncNode/TTSAsyncNode.h"



void UNivaTTSRequest::init(const FString& APIKey, const FString& Message, const FString& ReferenceID)
{
	// 创建 HTTP 请求
	TTSRequest = FHttpModule::Get().CreateRequest();
	if (TTSRequest)
	{
		// 从项目设置里面读
		FString TTSURL = GetDefault<UNivaNetworkCoreSettings>()->TTSURL;
		TTSRequest->SetURL(TTSURL);
		TTSRequest->SetVerb("POST");

		// 设置请求头
		TTSRequest->SetHeader("Content-Type", "multipart/form-data; boundary=--------------------------876613294407296246331936");
		TTSRequest->SetHeader("Authorization", "Bearer " + APIKey);

		// 构造请求体
		FString Boundary = "--------------------------876613294407296246331936";
		FString Body;

		// 添加 text 字段
		Body += "--" + Boundary + "\r\n";
		Body += "Content-Disposition: form-data; name=\"text\"\r\n\r\n";
		Body += Message + "\r\n";

		// 添加 reference_id 字段
		Body += "--" + Boundary + "\r\n";
		Body += "Content-Disposition: form-data; name=\"reference_id\"\r\n\r\n";
		Body += ReferenceID + "\r\n";

		// 结束边界
		Body += "--" + Boundary + "--\r\n";

		// 设置请求体
		TTSRequest->SetContentAsString(Body);

		// 设置回调
		TTSRequest->OnProcessRequestComplete().BindUObject(this, &UNivaTTSRequest::OnTTSComplete);

		// 发送请求
		TTSRequest->ProcessRequest();

		// 创建超时检测，如果超过（2+（字数*0.2））秒，就触发OnTTSComplete，并解绑TTSRequest的所有回调
		float Timeout = 99999992.0f + (Message.Len() * 0.2f);
		// 获取不到世界，换一种方法
		// 先获取游戏实例
		GEngine->GameViewport->GetWorld()->GetTimerManager().SetTimer(TimerHandle, [this]()
			{
				if (TTSRequest.IsValid())
				{
					TTSRequest->OnProcessRequestComplete().Unbind();
					TTSRequest->CancelRequest();
					// 顺便print一下
					UE_LOG(LogTemp, Warning, TEXT("NetCore: TTSRequest Timeout"));
					OnTTSComplete(TTSRequest, nullptr, false);
				}
			}, Timeout, false);




	}
}

void UNivaTTSRequest::OnTTSComplete(FHttpRequestPtr request, FHttpResponsePtr response, bool bWasSuccessful)
{
	Completed = true;
	TimerHandle.Invalidate();
	if (bWasSuccessful && response.IsValid())
	{
		// 看一眼content的长度
		UE_LOG(LogTemp, Warning, TEXT("NetCore: TTS Request complete ContentLength: %d"), response->GetContentLength());
		// 看一眼request的内容
		TArray<uint8> ByteData = request->GetContent(); // 假设这是你的字节数组
		FString ResultString;

		// 将 UTF-8 数据转换为 FString
		FUTF8ToTCHAR Converter((ANSICHAR*)ByteData.GetData(), ByteData.Num());
		ResultString = FString(Converter.Length(), Converter.Get());
		UE_LOG(LogTemp, Warning, TEXT("NetCore: TTS Request complete Content: %s"), /*TArray<uint8>&转string*/*ResultString);
		// 解析音频数据
		TArray<uint8> AudioData = response->GetContent();
		// 这里解析音频
		// Sound = Bin2SoundWave(AudioData);
		OnNivaTTSRequestComplete.Broadcast(AudioData, this);
		OnNivaAsyncTTSRequestComplete.Execute(AudioData, this);
	}
	else
	{
		int32 StatusCode = response.IsValid() ? response->GetResponseCode() : -1;
		FString ResponseContentString;
		if (response.IsValid())
		{
			// 将返回内容（通常为JSON）转utf8字符串
			const TArray<uint8>& Content = response->GetContent();
			FUTF8ToTCHAR JsonConverter((ANSICHAR*)Content.GetData(), Content.Num());
			ResponseContentString = FString(JsonConverter.Length(), JsonConverter.Get());
		}
		else
		{
			ResponseContentString = TEXT("response is invalid!");
		}

		UE_LOG(LogTemp, Error, TEXT("NetCore: TTS Request failed, StatusCode: %d, Content: %s"),
			StatusCode, *ResponseContentString);

		OnNivaTTSRequestComplete.Broadcast({}, this);
		OnNivaAsyncTTSRequestComplete.Execute({}, this);
		// 处理错误
	}
}

void UNivaTTSRequest::CancelRequest()
{
	if (TTSRequest.IsValid())
	{
		TTSRequest->CancelRequest();
		TTSRequest->OnProcessRequestComplete().Unbind();
	}
	if (TimerHandle.IsValid())
	{
		GEngine->GameViewport->GetWorld()->GetTimerManager().ClearTimer(TimerHandle);
	}
}

USoundWave* UNivaTTSRequest::Bin2SoundWave(const TArray<uint8>& AudioData)
{
	FWaveModInfo WaveInfo;
	if (!WaveInfo.ReadWaveInfo(AudioData.GetData(), AudioData.Num()))
	{
		UE_LOG(LogTemp, Error, TEXT("Failed to read wave info"));
		return nullptr;
	}

	USoundWave* SoundWave = NewObject<USoundWave>();
	SoundWave->SetSampleRate(*WaveInfo.pSamplesPerSec);
	SoundWave->NumChannels = *WaveInfo.pChannels;
	SoundWave->Duration = (float)WaveInfo.SampleDataSize / (*WaveInfo.pSamplesPerSec * *WaveInfo.pChannels);
	SoundWave->RawPCMDataSize = WaveInfo.SampleDataSize;
	SoundWave->RawPCMData = static_cast<uint8*>(FMemory::Malloc(WaveInfo.SampleDataSize));
	FMemory::Memcpy(SoundWave->RawPCMData, WaveInfo.SampleDataStart, WaveInfo.SampleDataSize);

	return SoundWave;
}

UAliyunTTSRequest::UAliyunTTSRequest(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// 构造函数实现
}


void UAliyunTTSRequest::init(const FString& APIKey, const FString& Message, const FString& ReferenceID)
{
    // 检查module是否正常加载
	WebSocketModule = nullptr;

	if (!FModuleManager::Get().IsModuleLoaded("WebSockets"))
	{
		WebSocketModule = &FModuleManager::Get().LoadModuleChecked<FWebSocketsModule>(TEXT("WebSockets"));
	}
	else
	{
		WebSocketModule = &FModuleManager::Get().GetModuleChecked<FWebSocketsModule>("WebSockets");
	}
	
	// 1、初始化websocket - 构建WebSocket URL
	FString AliyunTTSURL = GetDefault<UNivaNetworkCoreSettings>()->TTSAliyunURL;
    
	Text = Message;
	
	// 2、然后链接到指定url
	ConnectWebSocket(AliyunTTSURL);
    
	// 3、发送TTS开始的初始消息将在WebSocket连接建立后自动发送
	// 我们需要保存Message以便在连接建立后发送
    
	UE_LOG(LogTemp, Log, TEXT("WebSocket TTS initialized - APIKey: %s, Message: %s, ReferenceID: %s"), 
		   *APIKey, *Message, *ReferenceID);

}

void UAliyunTTSRequest::OnTTSComplete(FHttpRequestPtr request, FHttpResponsePtr response, bool bWasSuccessful)
{
	Super::OnTTSComplete(request, response, bWasSuccessful);
}

void UAliyunTTSRequest::ConnectWebSocket(const FString& URL)
{
	// 如果已经连接，先断开
	if (WebSocket.IsValid() && WebSocket->IsConnected())
	{
		WebSocket->Close();
	}
	// 准备headers
	FString CurrentAPIKey = GetDefault<UNivaNetworkCoreSettings>()->TTSAliyunAccessKey;
	
	TMap<FString, FString> Headers;
	Headers.Add(TEXT("Authorization"), FString::Printf(TEXT("Bearer %s"), *CurrentAPIKey));
	Headers.Add(TEXT("Content-Type"), TEXT("application/json"));
	// 可以添加更多自定义headers
	Headers.Add(TEXT("X-DashScope-DataInspection"), TEXT("enable"));
    
	// 准备协议（可选）
	TArray<FString> Protocols;
	Protocols.Add(TEXT(""));  

	// 创建新的WebSocket连接
	// WebSocket = FWebSocketsModule::Get().CreateWebSocket(URL);
	
	WebSocket = WebSocketModule->CreateWebSocket(URL, Protocols, Headers);

    
	if (WebSocket.IsValid())
	{
		// 绑定事件
		WebSocket->OnConnected().AddUObject(this, &UAliyunTTSRequest::OnWebSocketConnected);
		WebSocket->OnConnectionError().AddUObject(this, &UAliyunTTSRequest::OnWebSocketError);
		WebSocket->OnClosed().AddUObject(this, &UAliyunTTSRequest::OnWebSocketClosed);
		WebSocket->OnMessage().AddUObject(this, &UAliyunTTSRequest::OnWebSocketMessage);
        WebSocket->OnBinaryMessage().AddUObject(this, &UAliyunTTSRequest::OnWebSocketBinaryMessage);


		// 开始连接
		WebSocket->Connect();
        
		UE_LOG(LogTemp, Log, TEXT("Connecting to WebSocket: %s"), *URL);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("Failed to create WebSocket for URL: %s"), *URL);
	}

}

void UAliyunTTSRequest::DisconnectWebSocket()
{
	if (WebSocket.IsValid() && WebSocket->IsConnected())
	{
		WebSocket->Close();
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("WebSocket is not connected"));
	}
}

void UAliyunTTSRequest::SendWebSocketMessage(const FString& Message)
{
	if (WebSocket.IsValid() && WebSocket->IsConnected())
	{
		WebSocket->Send(Message);
		UE_LOG(LogTemp, Log, TEXT("Sent WebSocket message: %s"), *Message);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("Failed to send WebSocket message: %s"), *Message);
	}
}

void UAliyunTTSRequest::OnWebSocketConnected()
{
	UE_LOG(LogTemp, Log, TEXT("WebSocket connected successfully"));
	// 生成任务ID (UUID)
	TaskId = FGuid::NewGuid().ToString();
    
	// 构建run-task指令的JSON消息
	// 改成读取setting里面的配置
	// FString RunTaskMessage = FString::Printf(
	// 	TEXT("{"
	// 		"\"header\": {"
	// 			"\"action\": \"run-task\","
	// 			"\"task_id\": \"%s\","
	// 			"\"streaming\": \"duplex\""
	// 		"},"
	// 		"\"payload\": {"
	// 			"\"task_group\": \"audio\","
	// 			"\"task\": \"tts\","
	// 			"\"function\": \"SpeechSynthesizer\","
	// 			"\"model\": \"cosyvoice-v2\","
	// 			"\"parameters\": {"
	// 				"\"text_type\": \"PlainText\","
	// 				"\"voice\": \"longxiaochun_v2\","
	// 				"\"format\": \"mp3\","
	// 				"\"sample_rate\": 22050,"
	// 				"\"volume\": 50,"
	// 				"\"rate\": 1,"
	// 				"\"pitch\": 1"
	// 			"},"
	// 			"\"input\": {}"
	// 		"}"
	// 	"}"),

	FString RunTaskMessage = FString::Printf(
		TEXT("{"
			"\"header\": {"
			"\"action\": \"run-task\","
			"\"task_id\": \"%s\","
			"\"streaming\": \"duplex\""
			"},"
				"\"payload\": {"
					"\"task_group\": \"audio\","
					"\"task\": \"tts\","
					"\"function\": \"SpeechSynthesizer\","
					"\"model\": \"cosyvoice-v2\","
					"\"parameters\": {"
						"\"text_type\": \"PlainText\","
						"\"voice\": \"%s\","
						"\"format\": \"%s\","
						"\"sample_rate\": %d,"
						"\"volume\": %d,"
						"\"rate\": %f,"
						"\"pitch\": %f"
				"},"
				"\"input\": {\"text\": \"%s\"}"
			"}"
		"}"),
		*TaskId,
		*GetDefault<UNivaNetworkCoreSettings>()->TTSAliyunVoice,
		*GetDefault<UNivaNetworkCoreSettings>()->TTSAliyunFormat,
		GetDefault<UNivaNetworkCoreSettings>()->TTSAliyunSampleRate,
		GetDefault<UNivaNetworkCoreSettings>()->TTSAliyunVolume,
		GetDefault<UNivaNetworkCoreSettings>()->TTSAliyunRate,
		GetDefault<UNivaNetworkCoreSettings>()->TTSAliyunPitch,
		*Text	
	);
    
	SendWebSocketMessage(RunTaskMessage);
	SendFinishTask();
	UE_LOG(LogTemp, Log, TEXT("Sent run-task message with task_id: %s"), *TaskId);
}

void UAliyunTTSRequest::OnWebSocketClosed(int32 StatusCode, const FString& Reason, bool bWasClean)
{
	// 打印一下信息
	UE_LOG(LogTemp, Log, TEXT("WebSocket closed with status code: %d, reason: %s, was clean: %s"), 
		   StatusCode, *Reason, bWasClean ? TEXT("true") : TEXT("false"));
    
	// 如果任务未完成，则触发回调
	if (!bTaskFinished)
	{
		OnNivaTTSRequestComplete.Broadcast({}, this);
		OnNivaAsyncTTSRequestComplete.Execute({}, this);
	}
}

void UAliyunTTSRequest::OnWebSocketMessage(const FString& Message)
{
    UE_LOG(LogTemp, Log, TEXT("收到WebSocket消息: %s"), *Message);
    
    // 处理JSON文本消息
    TSharedPtr<FJsonObject> JsonObject;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Message);
    
    if (FJsonSerializer::Deserialize(Reader, JsonObject) && JsonObject.IsValid())
    {
        UE_LOG(LogTemp, Log, TEXT("收到JSON消息"));
        
        // 检查是否有header字段
        if (JsonObject->HasField(TEXT("header")))
        {
            TSharedPtr<FJsonObject> Header = JsonObject->GetObjectField(TEXT("header"));
            
            if (Header.IsValid() && Header->HasField(TEXT("event")))
            {
                FString Event = Header->GetStringField(TEXT("event"));
                
                if (Event == TEXT("task-started"))
                {
                    UE_LOG(LogTemp, Log, TEXT("任务已启动"));
                    bTaskStarted = true;
                }
                else if (Event == TEXT("task-finished"))
                {
                    UE_LOG(LogTemp, Log, TEXT("任务已完成"));
                    bTaskFinished = true;
                    
                    // 任务完成，触发回调
                    if (AudioDataBuffer.Num() > 0)
                    {
                        // 调用完成回调
                        OnNivaTTSRequestComplete.Broadcast(AudioDataBuffer, this);
                        OnNivaAsyncTTSRequestComplete.ExecuteIfBound(AudioDataBuffer, this);
                        
                        UE_LOG(LogTemp, Log, TEXT("TTS任务完成，生成音频数据大小: %d 字节"), AudioDataBuffer.Num());
                    }
                    
                    // 关闭WebSocket连接
                    if (WebSocket.IsValid())
                    {
                        WebSocket->Close();
                    }
                }
                else if (Event == TEXT("task-failed"))
                {
                    FString ErrorMessage = JsonObject->HasField(TEXT("error_message")) ? 
                        JsonObject->GetStringField(TEXT("error_message")) : TEXT("未知错误");
                    
                    UE_LOG(LogTemp, Error, TEXT("任务失败: %s"), *ErrorMessage);
                    bTaskFinished = true;
                    
                    // 关闭WebSocket连接
                    if (WebSocket.IsValid())
                    {
                        WebSocket->Close();
                    }
                }
            }
        }
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("JSON解析失败: %s"), *Message);
    }
}

void UAliyunTTSRequest::OnWebSocketBinaryMessage(const void* Data, SIZE_T Size, bool bIsLastFragment)
{
	// 处理二进制消息（音频数据）
	UE_LOG(LogTemp, Log, TEXT("收到二进制消息，大小: %d 字节"), Size);
    
	if (Data && Size > 0)
	{
		// 将音频数据追加到缓冲区
		const uint8* AudioData = static_cast<const uint8*>(Data);
		AudioDataBuffer.Append(AudioData, Size);
        
		UE_LOG(LogTemp, Log, TEXT("已将音频数据写入缓冲区，当前总大小: %d 字节"), AudioDataBuffer.Num());
	}
}



void UAliyunTTSRequest::SendFinishTask()
{
	if (!WebSocket.IsValid() || !WebSocket->IsConnected())
	{
		UE_LOG(LogTemp, Error, TEXT("WebSocket未连接，无法发送finish-task指令"));
		return;
	}
    
	// 构建finish-task指令
	FString FinishTaskMessage = FString::Printf(
		TEXT("{"
			"\"header\": {"
				"\"action\": \"finish-task\","
				"\"task_id\": \"%s\","
				"\"streaming\": \"duplex\""
			"},"
			"\"payload\": {"
				"\"input\": {}"
			"}"
		"}"),
		*TaskId
	);
    
	WebSocket->Send(FinishTaskMessage);
	UE_LOG(LogTemp, Log, TEXT("发送finish-task指令，任务ID: %s"), *TaskId);
}


void UAliyunTTSRequest::OnWebSocketError(const FString& Error)
{
	// 打印错误信息
	UE_LOG(LogTemp, Error, TEXT("WebSocket错误: %s"), *Error);
	if (!bTaskFinished)
	{
		OnNivaTTSRequestComplete.Broadcast({}, this);
		OnNivaAsyncTTSRequestComplete.Execute({}, this);
	}
};

void UNivaMelotteTTSRequest::init(const FString& APIKey, const FString& Message, const FString& ReferenceID)
{
	TTSRequest = FHttpModule::Get().CreateRequest();
	if (TTSRequest)
	{
		// 从项目设置里面读
		FString TTSURL = GetDefault<UNivaNetworkCoreSettings>()->MelotteTTSURL;
		TTSRequest->SetURL(TTSURL);
		TTSRequest->SetVerb("POST");

		FString Body;
		// 通过json构造请求体
		TSharedPtr<FJsonObject> JsonObject = MakeShareable(new FJsonObject);
		JsonObject->SetStringField(TEXT("input"), Message);
		// 类型为数字
		JsonObject->SetNumberField(TEXT("sample_rate"), 44100);
		// 类型为浮点
		JsonObject->SetNumberField(TEXT("speed"), 0.8);
		JsonObject->SetStringField(TEXT("language"), TEXT("ZH_MIX_EN"));
		// 序列化为JSON字符串（确保中文不乱码）
		TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Body);
		FJsonSerializer::Serialize(JsonObject.ToSharedRef(), Writer);
		// 用UELOG看一眼
		UE_LOG(LogTemp, Warning, TEXT("NetCore:MelotteTTSContent: %s"), *Body);
		// 设置请求头
		TTSRequest->SetHeader("Content-Type", "application/json");



		// 设置请求体
		TTSRequest->SetContentAsString(Body);

		// 设置回调
		TTSRequest->OnProcessRequestComplete().BindUObject(this, &UNivaTTSRequest::OnTTSComplete);

		// 发送请求
		TTSRequest->ProcessRequest();

		// 创建超时检测，如果超过（2+（字数*0.2））秒，就触发OnTTSComplete，并解绑TTSRequest的所有回调
		float Timeout = 99999992.0f + (Message.Len() * 0.2f);
		// 获取不到世界，换一种方法
		// 先获取游戏实例
		GEngine->GameViewport->GetWorld()->GetTimerManager().SetTimer(TimerHandle, [this]()
			{
				if (TTSRequest.IsValid())
				{
					TTSRequest->OnProcessRequestComplete().Unbind();
					TTSRequest->CancelRequest();
					// 顺便print一下
					UE_LOG(LogTemp, Warning, TEXT("NetCore: TTSRequest Timeout"));
					OnTTSComplete(TTSRequest, nullptr, false);
				}
			}, Timeout, false);




	}
}

UTTSNode* UTTSNode::SendTTSRequest(const FString& Message)
{

	// 创建一个新的异步节点
	UTTSNode* AsyncNode = NewObject<UTTSNode>();

	// 穷举
	// 从setting读取配置
	// Update the variable type to match the enum type
	ENivaTTSModel NivaTTSRequestType = GetDefault<UNivaNetworkCoreSettings>()->TTSRequestType;
	UNivaTTSRequest* TTSRequest = nullptr;
	switch (NivaTTSRequestType) {
	case ENivaTTSModel::TTS_Melotte:

		TTSRequest = NewObject<UNivaMelotteTTSRequest>(AsyncNode);
		break;
	case ENivaTTSModel::TTS_Fish:

		TTSRequest = NewObject<UNivaTTSRequest>(AsyncNode);

		break;
	case ENivaTTSModel::TTS_Default:
	case ENivaTTSModel::TTS_Aliyun:
		TTSRequest = NewObject<UAliyunTTSRequest>(AsyncNode);
		break;
		
	}
	FString APIKey = GetDefault<UNivaNetworkCoreSettings>()->TTSFishAPIKey;
	FString ReferenceID = GetDefault<UNivaNetworkCoreSettings>()->ReferenceID;


	TTSRequest->OnNivaAsyncTTSRequestComplete.BindLambda(
		[AsyncNode](TArray<uint8> AudioData, UNivaTTSRequest* TTSRequest)
		{
			// 这里处理音频数据
			// AsyncNode->OnCompleteDelegate(TTSRequest, AudioData);
			AsyncNode->OnCompleteDelegate(TTSRequest, AudioData);
			AsyncNode->RemoveFromRoot();
		}
	);


	// 初始化
	TTSRequest->init(APIKey, Message, ReferenceID);

	return AsyncNode; // 返回AsyncNode而不是nullptr
}

void UTTSNode::OnCompleteDelegate(UNivaTTSRequest* Request, TArray<uint8> ResponsePtr)
{
	CompleteDelegate.IsBound() ? CompleteDelegate.Broadcast(ResponsePtr, Request) : void();
}


