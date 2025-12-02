#include "AsyncNode/LLMAsyncNode.h"
// CoreManager Log
#include "Log/CoreLogSubsystem.h"



namespace
{
    // 统一封装 CoreManager 日志调用，避免重复空指针判断
    inline void NC_CoreLog(const UObject* WorldContext, FName Category2, ECoreLogSeverity Severity, const FString& Message, const TMap<FString,FString>& Data = TMap<FString,FString>{})
    {
        if (const UCoreLogSubsystem* LogSS = UCoreLogSubsystem::Get(WorldContext))
        {
            const_cast<UCoreLogSubsystem*>(LogSS)->Log(TEXT("NetworkCore"), Category2, Severity, Message, Data);
        }
        else
        {
            // 回退到 UE_LOG，保证在早期阶段也能看到信息
            switch (Severity)
            {
            case ECoreLogSeverity::Error:
                UE_LOG(LogTemp, Error, TEXT("[NetworkCore/%s] %s"), *Category2.ToString(), *Message);
                break;
            case ECoreLogSeverity::Warning:
                UE_LOG(LogTemp, Warning, TEXT("[NetworkCore/%s] %s"), *Category2.ToString(), *Message);
                break;
            default:
                UE_LOG(LogTemp, Log, TEXT("[NetworkCore/%s] %s"), *Category2.ToString(), *Message);
                break;
            }
        }
    }

    inline FName NC_LLMToName(ENivaLLM In)
    {
        switch (In)
        {
        case ENivaLLM::LLM_OLLAMA: return TEXT("Ollama");
        case ENivaLLM::LLM_ALIYUN: return TEXT("Aliyun");
        case ENivaLLM::LLM_NIVA_AGENT: return TEXT("Agent");
        case ENivaLLM::LLM_RUNNER: return TEXT("Runner");
        default: return TEXT("None");
        }
    }
}

UBlueprintAsyncNode* UBlueprintAsyncNode::LLMChat(
	//NivaHttpRequest Request,
	TMap<FString/*user*/, FString/*assistant*/> Chatted,
	FString Chat
)
{
	// 创建一个新���异步节点
	UBlueprintAsyncNode* AsyncNode = NewObject<UBlueprintAsyncNode>();


	// 从setting读取配置
	// Update the variable type to match the enum type
 ENivaLLM NivaLLMRequestType = GetDefault<UNivaNetworkCoreSettings>()->LLM;
 NC_CoreLog(AsyncNode, TEXT("LLMChat"), ECoreLogSeverity::Normal, TEXT("LLMChat start"), {
     {TEXT("LLM"), NC_LLMToName(NivaLLMRequestType).ToString()},
 });
	AsyncNode->LLMRequest = nullptr;


 switch (NivaLLMRequestType) {
 case ENivaLLM::LLM_NONE:
 case ENivaLLM::LLM_OLLAMA:

     AsyncNode->LLMRequest = NewObject<UNivaLLMRequest>(AsyncNode);
     break;
	case ENivaLLM::LLM_ALIYUN:

		AsyncNode->LLMRequest = NewObject<UNivaAliyunLLMRequest>(AsyncNode);
		break;
	case ENivaLLM::LLM_NIVA_AGENT:
		AsyncNode->LLMRequest = NewObject<UNivaAgentLLMRequest>(AsyncNode);
		break;
	case ENivaLLM::LLM_RUNNER:
		AsyncNode->LLMRequest = NewObject<UNivaRunnerLLMRequest>(AsyncNode);
		break;
		
	}
// 回调委托
	AsyncNode->LLMRequest->OnNivaAsyncLLMRequestProgress.BindLambda(
		[AsyncNode](bool a, FString Content) {
			// 直接在蓝图中使用
			AsyncNode->ProgressDelegate.IsBound() ? AsyncNode->ProgressDelegate.Broadcast(a, Content) : void();
		}
	);
	AsyncNode->LLMRequest->OnNivaAsyncLLMRequestComplete.BindLambda(
		[AsyncNode](bool a, FString Content) {
			// 直接在蓝图中使用
			AsyncNode->CompleteDelegate.IsBound() ? AsyncNode->CompleteDelegate.Broadcast(a, Content) : void();
			AsyncNode->SetReadyToDestroy(); // 标记可以被垃圾回收

		}
	);
	// AsyncNode->LLMRequest->OnNivaAsyncLLMRequestComplete.BindUObject(AsyncNode, &UBlueprintAsyncNode::OnCompleteDelegate);


 AsyncNode->LLMRequest->init(
        Chatted,
        Chat
    );
    NC_CoreLog(AsyncNode, TEXT("LLMChat"), ECoreLogSeverity::Normal, TEXT("LLMChat request initialized"), {
        {TEXT("ChatLen"), FString::FromInt(Chat.Len())},
        {TEXT("HistoryCount"), FString::FromInt(Chatted.Num())}
    });

	return AsyncNode;
}

void UNivaLLMRequest::OnCompleteDelegate(FHttpRequestPtr Request, FHttpResponsePtr ResponsePtr, bool a)
{
    if (a) {
        UE_LOG(LogTemp, Warning, TEXT("NetCore:Request Compelete"));
        const int32 StatusCode = ResponsePtr.IsValid() ? ResponsePtr->GetResponseCode() : -1;
        NC_CoreLog(this, TEXT("LLM"), ECoreLogSeverity::Normal, TEXT("Request complete"), {
            {TEXT("Status"), FString::FromInt(StatusCode)}
        });
    }
    else {
        UE_LOG(LogTemp, Warning, TEXT("NetCore:Request failed "));
        int32 StatusCode = -1;
        FString ErrText = TEXT("unknown");
        if (ResponsePtr.IsValid())
        {
            StatusCode = ResponsePtr->GetResponseCode();
            ErrText = ResponsePtr->GetContentAsString().Left(256);
        }
        NC_CoreLog(this, TEXT("LLM"), ECoreLogSeverity::Error, TEXT("Request failed"), {
            {TEXT("Status"), FString::FromInt(StatusCode)},
            {TEXT("Error"), ErrText}
        });

        return;
    }

	// 以供直接在蓝图中使用
	OnNivaLLMRequestComplete.IsBound() ? OnNivaLLMRequestComplete.Broadcast(a, ResponsePtr->GetContentAsString()) : void();
	// 以供异��节点使用
	OnNivaAsyncLLMRequestComplete.IsBound() ? OnNivaAsyncLLMRequestComplete.Execute(a, ResponsePtr->GetContentAsString()) : void();

}


void UNivaLLMRequest::OnProgressDelegate(FHttpRequestPtr Request, uint64 BytesSent, uint64 BytesReceived)
{


	// 安全检查
	if (!Request.IsValid() || !Request->GetResponse().IsValid())
	{
		// 分辨一���是哪个无效
        if (!Request.IsValid())
        {
            UE_LOG(LogTemp, Error, TEXT("NetCore: OnProgressDelegate: Request is invalid"));
            NC_CoreLog(this, TEXT("LLM"), ECoreLogSeverity::Error, TEXT("OnProgress: Request invalid"));
        }
        else
        {
            UE_LOG(LogTemp, Error, TEXT("NetCore: OnProgressDelegate: Response is invalid"));
            NC_CoreLog(this, TEXT("LLM"), ECoreLogSeverity::Error, TEXT("OnProgress: Response invalid"));
        }
        // UE_LOG(LogTemp, Error, TEXT("NetCore: OnProgressDelegate: Invalid request or response"));
        return;
    }

	// 获取当前完整内容
	FString CurrentContent = Request->GetResponse()->GetContentAsString();
	if (CurrentContent.IsEmpty())
	{
		return;
	}

	// 如果是第一次接收，初始化PreviousContent
	if (PreviousContent.IsEmpty())
	{
		PreviousContent = CurrentContent;
		LastProcessedPosition = 0;
	}

	// 找出新增内容（从上次处理位置到当前内容的末尾）
	FString NewContent = CurrentContent.Mid(LastProcessedPosition);

 // 处理新增内容中的完整JSON对象
 TArray<FString> results;
	int32 GlobalStart = 0;
	int32 GlobalEnd = 0;
	if (ProcessBuffer(NewContent, LastProcessedPosition, results, GlobalStart, GlobalEnd))
	{
		for (FString result : results) {
			// 广播完整的JSON对象
			if (OnNivaLLMRequestProgress.IsBound())
			{
				OnNivaLLMRequestProgress.Broadcast(GlobalStart, GlobalEnd, result);
			}
			if (OnNivaAsyncLLMRequestProgress.IsBound()) {
				OnNivaAsyncLLMRequestProgress.Execute(false, result);
			}
		}
	}
 // 记录一次进度（避免刷屏，仅记录长度）
 NC_CoreLog(this, TEXT("LLM"), ECoreLogSeverity::Normal, TEXT("OnProgress"), {
     {TEXT("BytesSent"), FString::Printf(TEXT("%llu"), BytesSent)},
     {TEXT("BytesRecv"), FString::Printf(TEXT("%llu"), BytesReceived)},
     {TEXT("ChunkLen"), FString::FromInt(NewContent.Len())}
 });

	// 更新记录
	PreviousContent = CurrentContent;
	LastProcessedPosition = CurrentContent.Len();
}



bool UNivaLLMRequest::ProcessBuffer(const FString& NewContent, int32 BaseOffset,TArray<FString>& Result,int& GlobalStart,int& GlobalEnd)
{
	int32 ParsePos = 0;
	int32 ContentLength = NewContent.Len();
	FString Buffer = NewContent;
	bool successed = false;

	while (ParsePos < ContentLength)
	{
		// 查找JSON对象开始位置
		int32 JsonStart = Buffer.Find("{", ESearchCase::CaseSensitive, ESearchDir::FromStart, ParsePos);
		if (JsonStart == INDEX_NONE) break;

		// 查找匹配的结束大括号
		int32 BraceDepth = 0;
		int32 JsonEnd = INDEX_NONE;
		bool bInString = false;

		for (int32 i = JsonStart; i < ContentLength; ++i)
		{
			TCHAR CurrentChar = Buffer[i];

			// 处理字符串内的内容
			if (bInString)
			{
				if (CurrentChar == '"' && (i == 0 || Buffer[i - 1] != '\\'))
				{
					bInString = false;
				}
				continue;
			}

			// 处理JSON结构
			switch (CurrentChar)
			{
			case '"':
				bInString = true;
				break;
			case '{':
				BraceDepth++;
				break;
			case '}':
				BraceDepth--;
				if (BraceDepth == 0)
				{
					JsonEnd = i;
					break;
				}
				break;
			}

			if (JsonEnd != INDEX_NONE) break;
		}

		if (JsonEnd == INDEX_NONE)
		{
			// 不完整的JSON对象，停止处理
			break;
		}

		// 提取完整的JSON对象
		FString JsonObject = Buffer.Mid(JsonStart, JsonEnd - JsonStart + 1);
		ParsePos = JsonEnd + 1;

		// 计算在完整内容中的实际位置
		GlobalStart = BaseOffset + JsonStart;
		GlobalEnd = BaseOffset + JsonEnd;
		Result.Add(JsonObject);
		successed = true;

	}

	return successed;
}

FString UNivaLLMRequest::BuildChatRequestJson(const TMap<FString, FString>& ChatHistory, FString Chat)

{
	// 1. 创建根JSON对象
	TSharedPtr<FJsonObject> RootObject = MakeShareable(new FJsonObject);
	FString Model;
	// 切换模型
	switch (GetDefault<UNivaNetworkCoreSettings>()->LLMModel)
	{
	case ENivaLLMModel::LLM_qwen2_5_latest:
		Model = TEXT("qwen2.5:latest");
		break;
	case ENivaLLMModel::LLM_deepseek_r1_8b:
		Model = TEXT("deepseek-r1:8b");
		break;
	case ENivaLLMModel::LLM_qwq_latest:
		Model = TEXT("qwq:latest");
		break;
	default:
		Model = TEXT("qwen2.5:latest");
		break;

	}

	RootObject->SetStringField(TEXT("model"), Model);
	RootObject->SetBoolField(TEXT("stream"), true);

	// 2. 构建消息数组
	TArray<TSharedPtr<FJsonValue>> MessagesArray;

	// 添加系统提示（中文不会乱码）
	if (GetDefault<UNivaNetworkCoreSettings>()->ShouldPrompt)
	{
		FString Prompt = GetDefault<UNivaNetworkCoreSettings>()->LLMPrompt;
		TSharedPtr<FJsonObject> SystemMessage = MakeShareable(new FJsonObject);
		SystemMessage->SetStringField(TEXT("role"), TEXT("system"));
		SystemMessage->SetStringField(TEXT("content"), FString(EscapeChatContent(Prompt)));
		MessagesArray.Add(MakeShareable(new FJsonValueObject(SystemMessage)));
	}


	// 添加初始回复
	{
		TSharedPtr<FJsonObject> InitReply = MakeShareable(new FJsonObject);
		InitReply->SetStringField(TEXT("role"), TEXT("assistant"));
		InitReply->SetStringField(TEXT("content"), TEXT("sure."));
		MessagesArray.Add(MakeShareable(new FJsonValueObject(InitReply)));
	}

	// 3. 添加对话历史（处理换行和特殊字符）
	for (const auto& ChatPair : ChatHistory)
	{
		// 用户消息
		{
			TSharedPtr<FJsonObject> UserMsg = MakeShareable(new FJsonObject);
			UserMsg->SetStringField(TEXT("role"), TEXT("user"));
			UserMsg->SetStringField(TEXT("content"), FString(EscapeChatContent(ChatPair.Key)));
			MessagesArray.Add(MakeShareable(new FJsonValueObject(UserMsg)));
		}

		// AI回复
		{
			TSharedPtr<FJsonObject> AssistantMsg = MakeShareable(new FJsonObject);
			AssistantMsg->SetStringField(TEXT("role"), TEXT("assistant"));
			AssistantMsg->SetStringField(TEXT("content"), FString(EscapeChatContent(ChatPair.Value)));
			MessagesArray.Add(MakeShareable(new FJsonValueObject(AssistantMsg)));
		}
	}
	// 4. 添加当前消息
	{
		TSharedPtr<FJsonObject> CurrentMsg = MakeShareable(new FJsonObject);
		CurrentMsg->SetStringField(TEXT("role"), TEXT("user"));
		CurrentMsg->SetStringField(TEXT("content"), FString(EscapeChatContent(Chat)));
		MessagesArray.Add(MakeShareable(new FJsonValueObject(CurrentMsg)));
	}

	// 5. 将消息数组添加到根对象
	RootObject->SetArrayField(TEXT("messages"), MessagesArray);

	// 6. 序列化为JSON字符串（确保中文不乱码）
	FString OutputJsonString;
	TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
		TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&OutputJsonString);
	FJsonSerializer::Serialize(RootObject.ToSharedRef(), Writer);

	return OutputJsonString;
}

FString UNivaLLMRequest::EscapeChatContent(const FString& Content)
{
	// 使用UE内置的字符串转义函数
	FString Escaped = Content;

	// 替换换行符为\\n（保留单行JSON结构）
	Escaped.ReplaceInline(TEXT("\r"), TEXT(""));
	Escaped.ReplaceInline(TEXT("\n"), TEXT("\\n"));

	// 其他需要转义的特殊字符
	Escaped.ReplaceInline(TEXT("\\"), TEXT("\\\\"));
	Escaped.ReplaceInline(TEXT("\""), TEXT("\\\""));
	Escaped.ReplaceInline(TEXT("\t"), TEXT("\\t"));

	return Escaped;
}


void UNivaLLMRequest::init( TMap<FString, FString> Chated, FString Chat)
{
    // 创建 HTTP 请求
    LLMRequest = FHttpModule::Get().CreateRequest();
    if (LLMRequest)
    {
        // 设置请求的URL
        FString LLMUrl = GetDefault<UNivaNetworkCoreSettings>()->LLMOllamaURL;
        LLMRequest->SetURL(LLMUrl);
		// 设置请求的HTTP方法
		LLMRequest->SetVerb(TEXT("POST"));
		LLMRequest->SetHeader(TEXT("Connection"), TEXT("keep-alive"));
		LLMRequest->SetHeader("Content-Type", "application/json");


  // 预制对话
  FString MessagesArray = BuildChatRequestJson(Chated, Chat);

		// 用UELOG看一眼
		UE_LOG(LogTemp, Warning, TEXT("NetCore:Content: %s"), *MessagesArray);
		// 遍历Chat，并填入content。
		// 设置请求的body
		LLMRequest->SetContentAsString(MessagesArray);


		// 绑定返回事件 
		LLMRequest->OnProcessRequestComplete().BindUObject(this, &UNivaLLMRequest::OnCompleteDelegate);
		LLMRequest->OnRequestProgress64().BindUObject(this, &UNivaLLMRequest::OnProgressDelegate);

        LLMRequest->ProcessRequest();
        NC_CoreLog(this, TEXT("LLM"), ECoreLogSeverity::Normal, TEXT("Base init sent"), {
            {TEXT("URL"), LLMUrl},
            {TEXT("BodyLen"), FString::FromInt(MessagesArray.Len())}
        });
    };
}

void UNivaLLMRequest::CancelRequest()
{
    if (LLMRequest.IsValid())
    {
        LLMRequest->CancelRequest();
        LLMRequest->OnProcessRequestComplete().Unbind();
        NC_CoreLog(this, TEXT("LLM"), ECoreLogSeverity::Warning, TEXT("Request canceled"));
    }
}

UNivaLLMRequest* UNivaLLMRequest::CreateLLMRequest()
{
    UNivaLLMRequest* NivaLLMRequest = NewObject<UNivaLLMRequest>();
    TMap<FString, FString> Chated;
    FString Chat = "hello";
    Chated.Add(TEXT("user"), TEXT("assistant"));
    NivaLLMRequest->init(Chated, Chat);
    NC_CoreLog(NivaLLMRequest, TEXT("LLM"), ECoreLogSeverity::Normal, TEXT("CreateLLMRequest test created"));
    return NivaLLMRequest;
}

void UNivaAliyunLLMRequest::init(TMap<FString, FString> Chated, FString Chat)
{

	// 创建 HTTP 请求
	LLMRequest = FHttpModule::Get().CreateRequest();
	if (LLMRequest)
	{
		// 设置请求的URL
		FString LLMUrl = GetDefault<UNivaNetworkCoreSettings>()->LLMAliyunURL;
		LLMRequest->SetURL(LLMUrl);
		// 设置请求的HTTP方法
		LLMRequest->SetVerb(TEXT("POST"));
		LLMRequest->SetHeader(TEXT("Connection"), TEXT("keep-alive"));
		LLMRequest->SetHeader("Content-Type", "application/json");
		// 设置apikey,格式： "Bearer " + APIKey
		LLMRequest->SetHeader("Authorization", "Bearer " + GetDefault<UNivaNetworkCoreSettings>()->LLMAliyunAccessKey);



		// 预制对话
		FString MessagesArray = BuildChatRequestJson(Chated, Chat);

		// 用UELOG看一眼
		UE_LOG(LogTemp, Warning, TEXT("NetCore:Content: %s"), *MessagesArray);
		// 遍历Chat，并填入content。
		// 设置请求的body
		LLMRequest->SetContentAsString(MessagesArray);


		// 绑定返回事件 
		LLMRequest->OnProcessRequestComplete().BindUObject(this, &UNivaLLMRequest::OnCompleteDelegate);
		LLMRequest->OnRequestProgress64().BindUObject(this, &UNivaLLMRequest::OnProgressDelegate);

        LLMRequest->ProcessRequest();
        NC_CoreLog(this, TEXT("LLM.Aliyun"), ECoreLogSeverity::Normal, TEXT("Aliyun init sent"), {
            {TEXT("URL"), LLMUrl},
            {TEXT("BodyLen"), FString::FromInt(MessagesArray.Len())}
        });
    };
}

FString UNivaAliyunLLMRequest::BuildChatRequestJson(const TMap<FString, FString>& ChatHistory, FString Chat)
{
	// 1. 创建根JSON对象
	TSharedPtr<FJsonObject> RootObject = MakeShareable(new FJsonObject);
	FString Model;
	// 切换模型
	switch (GetDefault<UNivaNetworkCoreSettings>()->LLMAliyunModel)
	{
	case ENivaAliyunModel::Aliyun_qwq_plus:
		Model = TEXT("qwq-plus");
		break;
	case ENivaAliyunModel::Aliyun_qwen_plus:
		Model = TEXT("qwen-plus");
		break;
	case ENivaAliyunModel::Aliyun_qwen_max:
		Model = TEXT("qwen-max");
		break;
	case ENivaAliyunModel::Aliyun_qwen_long:
		Model = TEXT("qwen-long");
		break;
	case ENivaAliyunModel::Aliyun_NONE:
	case ENivaAliyunModel::Aliyun_qwen_turbo:
	default:
		Model = TEXT("qwen-turbo");
		break;
	}



	RootObject->SetStringField(TEXT("model"), Model);
	RootObject->SetBoolField(TEXT("stream"), true);

	// 2. 构建消息数组
	TArray<TSharedPtr<FJsonValue>> MessagesArray;

	// 添加系统提示（中文不会乱码）
	if (GetDefault<UNivaNetworkCoreSettings>()->ShouldPrompt)
	{
		FString Prompt = GetDefault<UNivaNetworkCoreSettings>()->LLMPrompt;
		TSharedPtr<FJsonObject> SystemMessage = MakeShareable(new FJsonObject);
		SystemMessage->SetStringField(TEXT("role"), TEXT("system"));
		SystemMessage->SetStringField(TEXT("content"), FString(EscapeChatContent(Prompt)));
		MessagesArray.Add(MakeShareable(new FJsonValueObject(SystemMessage)));
	}

	// 3. 添加对话历史（处理换行和特殊字符）
	for (const auto& ChatPair : ChatHistory)
	{
		// 用户消息
		{
			TSharedPtr<FJsonObject> UserMsg = MakeShareable(new FJsonObject);
			UserMsg->SetStringField(TEXT("role"), TEXT("user"));
			UserMsg->SetStringField(TEXT("content"), FString(EscapeChatContent(ChatPair.Key)));
			MessagesArray.Add(MakeShareable(new FJsonValueObject(UserMsg)));
		}

		// AI回复
		{
			TSharedPtr<FJsonObject> AssistantMsg = MakeShareable(new FJsonObject);
			AssistantMsg->SetStringField(TEXT("role"), TEXT("assistant"));
			AssistantMsg->SetStringField(TEXT("content"), FString(EscapeChatContent(ChatPair.Value)));
			MessagesArray.Add(MakeShareable(new FJsonValueObject(AssistantMsg)));
		}
	}
	// 4. 添加当前消息
	{
		TSharedPtr<FJsonObject> CurrentMsg = MakeShareable(new FJsonObject);
		CurrentMsg->SetStringField(TEXT("role"), TEXT("user"));
		CurrentMsg->SetStringField(TEXT("content"), FString(EscapeChatContent(Chat)));
		MessagesArray.Add(MakeShareable(new FJsonValueObject(CurrentMsg)));
	}

	// 5. 将消息数组添加到根对象
	RootObject->SetArrayField(TEXT("messages"), MessagesArray);

	// 6. 序列化为JSON字符串（确保中文不乱码）
	FString OutputJsonString;
	TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
		TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&OutputJsonString);
	FJsonSerializer::Serialize(RootObject.ToSharedRef(), Writer);

	return OutputJsonString;
}

void UNivaAliyunLLMRequest::OnProgressDelegate(FHttpRequestPtr Request, uint64 BytesSent, uint64 BytesReceived)
{



	// 安全检查
	if (!Request.IsValid() || !Request->GetResponse().IsValid())
	{
		// 分辨一下是哪个无效
		if (!Request.IsValid())
		{
			UE_LOG(LogTemp, Error, TEXT("NetCore: OnProgressDelegate: Request is invalid"));
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("NetCore: OnProgressDelegate: Response is invalid"));
		}
		// UE_LOG(LogTemp, Error, TEXT("NetCore: OnProgressDelegate: Invalid request or response"));
		return;
	}

	// 获取当前完整内容
	FString CurrentContent = Request->GetResponse()->GetContentAsString();
	if (CurrentContent.IsEmpty())
	{
		return;
	}

	// 如果是第一次接收，初始化PreviousContent
	if (PreviousContent.IsEmpty())
	{
		PreviousContent = CurrentContent;
		LastProcessedPosition = 0;
	}

	// 找出新增内容（从上次处理位置到当前内容的末尾）
	FString NewContent = CurrentContent.Mid(LastProcessedPosition);

	// 处理新增内容中的完整JSON对象
	TArray<FString> results;
	int32 GlobalStart = 0;
	int32 GlobalEnd = 0;
	if (ProcessBuffer(NewContent, LastProcessedPosition, results, GlobalStart, GlobalEnd))
	{
		for (const FString& result : results) {

			// 处理result
			// 原格式
			/*{
				"choices": [
				{
					"finish_reason": "stop",
						"delta" : {
						"content": ""
					},
						"index" : 0,
						"logprobs" : null
				}
				] ,
					"object": "chat.completion.chunk",
					"usage" : null,
					"created" : 1745388793,
					"system_fingerprint" : null,
					"model" : "qwen-plus",
					"id" : "chatcmpl-e26b3693-263d-9d64-ac56-b01567b556f4"
			}*/


			//处理result
			// 1. 解析JSON
			TSharedPtr<FJsonObject> JsonObject;
			TSharedRef<TJsonReader<>> JsonReader = TJsonReaderFactory<>::Create(result);

			FString Content;
			bool Done = false;
			FString Model;

			// 获取choices->delta->content
			if (FJsonSerializer::Deserialize(JsonReader, JsonObject))
			{
				// 2. 获取内容
				if (JsonObject->HasField(TEXT("choices")))
				{
					TArray<TSharedPtr<FJsonValue>> ChoicesArray = JsonObject->GetArrayField(TEXT("choices"));
					if (ChoicesArray.Num() > 0)
					{
						TSharedPtr<FJsonObject> ChoicesObject = ChoicesArray[0]->AsObject();
						if (ChoicesObject.IsValid() && ChoicesObject->HasField(TEXT("delta")))
						{
							TSharedPtr<FJsonObject> DeltaObject = ChoicesObject->GetObjectField(TEXT("delta"));
							if (DeltaObject.IsValid() && DeltaObject->HasField(TEXT("content")))
							{
								Content = DeltaObject->GetStringField(TEXT("content"));
							}
						}

						// 4. 获取done
						// 如果finish_reason为stop，则done为true
						if (ChoicesObject->HasField(TEXT("finish_reason")))
						{
							FString FinishReason = ChoicesObject->GetStringField(TEXT("finish_reason"));
							if (FinishReason == "stop")
							{
								Done = true;
							}
						}
					}
				}
				// 3. 获取model
				if (JsonObject->HasField(TEXT("model")))
				{
					Model = JsonObject->GetStringField(TEXT("model"));
				}
			}
			// 组合成新的json
			// 新格式
			/*{
				"model": "qwen2.5:latest",
					"created_at" :  "2025-04-23T06:50:59.9432702Z",
					"message" : {
					"role": "assistant",
						"content" : "我们"
				},
					"done" : false
			}*/
			TSharedPtr<FJsonObject> MessageJsonObject = MakeShareable(new FJsonObject);
			MessageJsonObject->SetStringField("role", TEXT("assistant"));
			MessageJsonObject->SetStringField("content", Content);

			TSharedPtr<FJsonObject> NewJsonObject = MakeShareable(new FJsonObject);
			NewJsonObject->SetStringField("model", Model);
			NewJsonObject->SetObjectField("message", MessageJsonObject);
			NewJsonObject->SetBoolField("done", Done);
			NewJsonObject->SetStringField("created_at", FDateTime::Now().ToString());
			// 序列化为JSON字符串（确保中文不乱码）
			FString OutputJsonString;
			TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
				TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&OutputJsonString);
			FJsonSerializer::Serialize(NewJsonObject.ToSharedRef(), Writer);


			// 广播完整的JSON对象
			if (OnNivaLLMRequestProgress.IsBound())
			{
				OnNivaLLMRequestProgress.Broadcast(GlobalStart, GlobalEnd, OutputJsonString);
			}
			if (OnNivaAsyncLLMRequestProgress.IsBound()) {
				OnNivaAsyncLLMRequestProgress.Execute(false, OutputJsonString);
			}
		}
	}
 // 记录一次进度
 NC_CoreLog(this, TEXT("LLM.Aliyun"), ECoreLogSeverity::Normal, TEXT("OnProgress"), {
     {TEXT("BytesSent"), FString::Printf(TEXT("%llu"), BytesSent)},
     {TEXT("BytesRecv"), FString::Printf(TEXT("%llu"), BytesReceived)},
     {TEXT("ChunkLen"), FString::FromInt(NewContent.Len())}
 });

	// 更新记录
	PreviousContent = CurrentContent;
	LastProcessedPosition = CurrentContent.Len();
}



void UNivaAgentLLMRequest::init(TMap<FString, FString> Chated, FString Chat)
{
	// 获取网络核心设置
	const UNivaNetworkCoreSettings* Settings = GetDefault<UNivaNetworkCoreSettings>();
	if (!Settings)
	{
		UE_LOG(LogTemp, Error, TEXT("UNivaAgentLLMRequest::init - 无法获取网络核心设置"));
		return;
	}

	// 创建HTTP请求
	LLMRequest = FHttpModule::Get().CreateRequest();
	if (!LLMRequest.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("UNivaAgentLLMRequest::init - 无法创建HTTP请求"));
		return;
	}
	
	// 设置请求URL - 假设智能体聊天API端点
	FString AgentChatURL = Settings->AgentChatURL + TEXT("/chat/stream/");
	LLMRequest->SetURL(AgentChatURL);
    
	// 设置请求方法
	LLMRequest->SetVerb(TEXT("POST"));
    
	// 设置请求头
	LLMRequest->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	LLMRequest->SetHeader(TEXT("Accept"), TEXT("text/event-stream")); // 支持SSE流式响应

	// 构建请求体
	FString RequestBody = UNivaAgentLLMRequest::BuildChatRequestJson(Chated, Chat);
	LLMRequest->SetContentAsString(RequestBody);

	// 绑定回调函数
	LLMRequest->OnProcessRequestComplete().BindUObject(this, &UNivaAgentLLMRequest::OnCompleteDelegate);
	LLMRequest->OnRequestProgress64().BindUObject(this, &UNivaAgentLLMRequest::OnProgressDelegate);

	// // 初始化状态
	// AccumulatedResponse.Empty();
	// bIsStreamingComplete = false;
	// Completed = false;

	// 发送请求
 if (!LLMRequest->ProcessRequest())
 {
     UE_LOG(LogTemp, Error, TEXT("UNivaAgentLLMRequest::init - 发送请求失败"));
     NC_CoreLog(this, TEXT("LLM.Agent"), ECoreLogSeverity::Error, TEXT("Agent init send failed"));
 }
 else
 {
     UE_LOG(LogTemp, Log, TEXT("UNivaAgentLLMRequest::init - 智能体聊天请求已发送"));
     NC_CoreLog(this, TEXT("LLM.Agent"), ECoreLogSeverity::Normal, TEXT("Agent init sent"), {
         {TEXT("URL"), AgentChatURL},
         {TEXT("BodyLen"), FString::FromInt(RequestBody.Len())}
     });
 }

	
}

FString UNivaAgentLLMRequest::BuildChatRequestJson(const TMap<FString, FString>& ChatHistory, FString Chat)
{
	// 创建JSON对象
	TSharedPtr<FJsonObject> JsonObject = MakeShareable(new FJsonObject);
    
	// 从ChatHistory中获取AgentId
	// 默认值从setting里面取
	const UNivaNetworkCoreSettings* Settings = GetDefault<UNivaNetworkCoreSettings>();
	FString AgentId = Settings->DefaultAgentID;
	if (const FString* FoundAgentId = ChatHistory.Find(TEXT("agent_id")))
	{
		AgentId = *FoundAgentId;
	}
    
	// 设置智能体ID
	JsonObject->SetStringField(TEXT("agent_id"), AgentId);
    
	// 设置当前消息
	JsonObject->SetStringField(TEXT("message"), Chat);
    
	// 序列化JSON
	FString OutputString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
	FJsonSerializer::Serialize(JsonObject.ToSharedRef(), Writer);
    
	UE_LOG(LogTemp, Log, TEXT("UNivaAgentLLMRequest::BuildChatRequestJson - 请求体: %s"), *OutputString);
    
	return OutputString;

}

void UNivaAgentLLMRequest::OnProgressDelegate(FHttpRequestPtr Request, uint64 BytesSent, uint64 BytesReceived)
{
	// 安全检查
	if (!Request.IsValid() || !Request->GetResponse().IsValid())
	{
		// 分辨一下是哪个无效
		if (!Request.IsValid())
		{
			UE_LOG(LogTemp, Error, TEXT("NetCore: OnProgressDelegate: Request is invalid"));
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("NetCore: OnProgressDelegate: Response is invalid"));
		}
		// UE_LOG(LogTemp, Error, TEXT("NetCore: OnProgressDelegate: Invalid request or response"));
		return;
	}
	
	// 1. 获取到目前为止的所有内容
	const FString Resp = Request->GetResponse() ? Request->GetResponse()->GetContentAsString() : TEXT("");
	if (Resp.Len() <= ParseOffset) return;

	// 2. 只取本次新收到的内容
	FString NewChunk = Resp.Mid(ParseOffset);
	ReceivedBuffer += NewChunk;
	ParseOffset = Resp.Len();

	// 3. 拆分并查找每个 data: ...，提取 content
	TArray<FString> Lines;
	ReceivedBuffer.ParseIntoArrayLines(Lines, true);
	for (const FString& Line : Lines)
	{
		FString Prefix = TEXT("data: ");
		if (Line.StartsWith(Prefix))
		{
			FString JsonPart = Line.Mid(Prefix.Len()).TrimStartAndEnd();

				// UE_LOG 打印 data: 后所有内容
				UE_LOG(LogTemp, Log, TEXT("Raw Data after 'data:': %s"), *JsonPart);

            
			// 解析JSON内容
			TSharedPtr<FJsonObject> JsonObj;
			TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonPart);
			if (FJsonSerializer::Deserialize(Reader, JsonObj) && JsonObj.IsValid())
			{
				UE_LOG(LogTemp, Log, TEXT("Streaming Data: %s"), *JsonPart);
				/*
				 * 输入有两种
				 * {"role": "assistant", "content": "你的吗？"}
				 * {"status": "processing"}
				 *
				 * 转换成
				 *
				 * 	{
					"model": "qwen2.5:latest",
					"created_at" : "2025-04-23T06:50:59.9432702Z",
					"message" : {
						"role": "assistant",
						"content" : "我们"
					},
					"done" : false
					}
				 */
				// 先提取status
				// 处理 assistant content
				// FString Role;
				// FString Content;
				// if (JsonObj->TryGetStringField(TEXT("role"), Role) && JsonObj->TryGetStringField(TEXT("content"), Content))
				// {
				// 	// 在这里将content返回给Blueprint或其它逻辑
				// 	UE_LOG(LogTemp, Warning, TEXT("流式输出: %s"), *Content);
				// 	// 可以广播一个代理（如OnReceiveContent.Broadcast(Content);）
				// 	
				// 	if (OnNivaAsyncLLMRequestProgress.IsBound()) {
				// 		OnNivaAsyncLLMRequestProgress.Execute(false, Content);
				// 	}
				// 	
				// }
				// 解析新的 OpenAI 风格的流式返回：choices[0].delta.content 和 finish_reason
				FString AssistantContent;
				bool bDone = false;
				// 默认角色为 assistant
				FString AssistantRole = TEXT("assistant");

				const TArray<TSharedPtr<FJsonValue>>* ChoicesArrayPtr = nullptr;
				if (JsonObj->TryGetArrayField(TEXT("choices"), ChoicesArrayPtr) && ChoicesArrayPtr && ChoicesArrayPtr->Num() > 0)
				{
					const TSharedPtr<FJsonObject>* FirstChoiceObjPtr = nullptr;
					if ((*ChoicesArrayPtr)[0].IsValid() && (*ChoicesArrayPtr)[0]->TryGetObject(FirstChoiceObjPtr) && FirstChoiceObjPtr && (*FirstChoiceObjPtr).IsValid())
					{
						const TSharedPtr<FJsonObject> FirstChoiceObj = *FirstChoiceObjPtr;
						// finish_reason 非空表示结束
						FString FinishReason;
						if (FirstChoiceObj->TryGetStringField(TEXT("finish_reason"), FinishReason))
						{
							bDone = !FinishReason.IsEmpty();
						}
						// 解析 delta.content
						const TSharedPtr<FJsonObject>* DeltaObjPtr = nullptr;
						if (FirstChoiceObj->TryGetObjectField(TEXT("delta"), DeltaObjPtr) && DeltaObjPtr && (*DeltaObjPtr).IsValid())
						{
							(*DeltaObjPtr)->TryGetStringField(TEXT("content"), AssistantContent);
							// 如果服务端在 delta 中给了 role，也一并更新
							(*DeltaObjPtr)->TryGetStringField(TEXT("role"), AssistantRole);
						}
					}
				}

				// 构造最后结果的 json（保持下游兼容）
				TSharedPtr<FJsonObject> OutJson = MakeShared<FJsonObject>();
				// 如果上游给了 created 字段，可用；否则使用当前 UTC 时间
				int64 CreatedUnix = 0;
				if (JsonObj->TryGetNumberField(TEXT("created"), CreatedUnix))
				{
					// 将 Unix 秒转为 ISO8601
					FDateTime Epoch(1970,1,1);
					FDateTime CreatedDT = Epoch + FTimespan::FromSeconds(CreatedUnix);
					OutJson->SetStringField(TEXT("created_at"), CreatedDT.ToIso8601());
				}
				else
				{
					OutJson->SetStringField(TEXT("created_at"), FDateTime::UtcNow().ToIso8601());
				}
				OutJson->SetBoolField(TEXT("done"), bDone);

				TSharedPtr<FJsonObject> MsgObj = MakeShared<FJsonObject>();
				MsgObj->SetStringField(TEXT("role"), AssistantRole);
				MsgObj->SetStringField(TEXT("content"), AssistantContent);
				OutJson->SetObjectField(TEXT("message"), MsgObj);

				// 转为字符串，log输出
				FString OutStr;
				TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&OutStr);
				FJsonSerializer::Serialize(OutJson.ToSharedRef(), W);
    UE_LOG(LogTemp, Log, TEXT("Final Composed Result: %s"), *OutStr);
    NC_CoreLog(this, TEXT("LLM.Agent"), ECoreLogSeverity::Normal, TEXT("Streaming chunk"), {
        {TEXT("Done"), bDone ? TEXT("true") : TEXT("false")},
        {TEXT("Len"), FString::FromInt(OutStr.Len())}
    });
				// 广播
				if (OnNivaAsyncLLMRequestProgress.IsBound()) {
					OnNivaAsyncLLMRequestProgress.Execute(bDone, OutStr);
				}
				// 蓝图
				if (OnNivaLLMRequestProgress.IsBound())
				{
					OnNivaLLMRequestProgress.Broadcast(0, 0, OutStr);
				}
				
			}
		}
	}
	// 若要避免重复解析，可将ReceivedBuffer置空或只保存未处理的残留
	ReceivedBuffer.Empty();
}


void UNivaAgentLLMRequest::OnCompleteDelegate(FHttpRequestPtr Request, FHttpResponsePtr ResponsePtr, bool bA)
{
    Super::OnCompleteDelegate(Request, ResponsePtr, bA);
    if (bA)
    {
        NC_CoreLog(this, TEXT("LLM.Agent"), ECoreLogSeverity::Normal, TEXT("Agent request complete"));
    }
    else
    {
        NC_CoreLog(this, TEXT("LLM.Agent"), ECoreLogSeverity::Error, TEXT("Agent request failed"));
    }
}

// Runner 实现
void UNivaRunnerLLMRequest::init(TMap<FString, FString> Chated, FString Chat)
{
	const UNivaNetworkCoreSettings* Settings = GetDefault<UNivaNetworkCoreSettings>();
	if (!Settings)
	{
		UE_LOG(LogTemp, Error, TEXT("UNivaRunnerLLMRequest::init - 无法获取网络核心设置"));
		return;
	}

	LLMRequest = FHttpModule::Get().CreateRequest();
	if (!LLMRequest.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("UNivaRunnerLLMRequest::init - 无法创建HTTP请求"));
		return;
	}

	// 设置请求
	LLMRequest->SetURL(Settings->LLMRunnerURL);
	LLMRequest->SetVerb(TEXT("POST"));
	LLMRequest->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	LLMRequest->SetHeader(TEXT("type"), Settings->LLMRunnerType);

	// 构建 body
	const FString Body = BuildChatRequestJson(Chated, Chat);
	UE_LOG(LogTemp, Log, TEXT("UNivaRunnerLLMRequest Body: %s"), *Body);
	LLMRequest->SetContentAsString(Body);

	// 绑定
	LLMRequest->OnProcessRequestComplete().BindUObject(this, &UNivaLLMRequest::OnCompleteDelegate);
	// Runner 可能非流式，仍绑定进度以兼容
	LLMRequest->OnRequestProgress64().BindUObject(this, &UNivaLLMRequest::OnProgressDelegate);

    LLMRequest->ProcessRequest();
    NC_CoreLog(this, TEXT("LLM.Runner"), ECoreLogSeverity::Normal, TEXT("Runner init sent"), {
        {TEXT("URL"), Settings->LLMRunnerURL},
        {TEXT("Type"), Settings->LLMRunnerType},
        {TEXT("BodyLen"), FString::FromInt(Body.Len())}
    });
}

FString UNivaRunnerLLMRequest::BuildChatRequestJson(const TMap<FString, FString>& ChatHistory, FString Chat)
{
	const UNivaNetworkCoreSettings* Settings = GetDefault<UNivaNetworkCoreSettings>();

	TSharedPtr<FJsonObject> Root = MakeShareable(new FJsonObject);
	Root->SetStringField(TEXT("text"), Chat);
	Root->SetStringField(TEXT("callback_url"), Settings ? Settings->LLMRunnerCallbackURL : TEXT(""));

	FString Output;
	TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
		TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Output);
	FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);
	return Output;
}
