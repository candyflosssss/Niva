// Fill out your copyright notice in the Description page of Project Settings.


#include "NetworkCoreSubsystem.h"
#include "HAL/PlatformProcess.h"
#include "Async/Async.h"
#include "Misc/OutputDeviceDebug.h"
#include "Delegates/Delegate.h"
#include "Kismet/GameplayStatics.h"

void UNetworkCoreSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    HttpServerInstance = &FHttpServerModule::Get();
    Super::Initialize(Collection);
    // StartHttpServer();
	Settings = GetDefault<UNivaNetworkCoreSettings>();

	// 创建一个Router
	// 先从networkcoresettings中获取端口号
	int32 Port = Settings->Port;
	FHttpServerModule& HttpServerModule = FHttpServerModule::Get();
	HttpRouter = HttpServerModule.GetHttpRouter(Port);

	if (!HttpRouter.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("无法在端口 %d 初始化 IHttpRouter"), Port);
		return;
	}
	

}


void UNetworkCoreSubsystem::BindRoute(FString path, ENivaHttpRequestVerbs HttpVerbs, FNetworkCoreHttpServerDelegate OnHttpServerRequest){


    FHttpPath HttpPath(path);
	if (!HttpRouter)
		return;
    // 注册路由
    FHttpRouteHandle RouterHandle = HttpRouter->BindRoute(
            HttpPath, 
			(EHttpServerRequestVerbs)HttpVerbs,
            FHttpRequestHandler::CreateLambda([this, OnHttpServerRequest](const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete) {

			// 狗屎UE 不会自动销毁RouterHandle，这里要检测一下
				if ((OnHttpServerRequest).IsBound()/*相当于检测OnHttpServerRequest是否为空*/)
				{
					// 不为空就广播收到的信息，并得到传入的OnHttpServerRequest绑定的函数的返回值
					FNivaHttpResponse HttpServerResponse = (OnHttpServerRequest).Execute(FNivaHttpRequest(Request));
					// 并创建回复，
					TUniquePtr<FHttpServerResponse> Response = MakeUnique<FHttpServerResponse>();
					Response->Body = HttpServerResponse.HttpServerResponse.Body;
					Response->Code = HttpServerResponse.HttpServerResponse.Code;
					Response->Headers = HttpServerResponse.HttpServerResponse.Headers;
					Response->HttpVersion = HttpServerResponse.HttpServerResponse.HttpVersion;

					OnComplete(MoveTemp(Response));
					return true;
				}
			TUniquePtr<FHttpServerResponse> response = FHttpServerResponse::Error(EHttpServerResponseCodes::NotFound);
			OnComplete(MoveTemp(response));
			return true;
				})
	);

	CreatedRouteHandlers.Add(RouterHandle);

    HttpServerInstance->StartAllListeners();
    // return HttpServerModule.StartAllListeners();
}

void UNetworkCoreSubsystem::Deinitialize()
{
    if (HttpRouter.IsValid()) {
        HttpServerInstance->StopAllListeners();
		if (HttpRouter.IsValid())
		{
			for (FHttpRouteHandle HttpRouteHandle : CreatedRouteHandlers)
			{
				HttpRouter->UnbindRoute(HttpRouteHandle);
			}
		}
        HttpRouter.Reset();
    }
	if (IsStarted) {
		IsStarted = false;
	}
    Super::Deinitialize();
}




void UNetworkCoreSubsystem::HandleHelloRequest(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
    // 构建响应
    TUniquePtr<FHttpServerResponse> Response = MakeUnique<FHttpServerResponse>();
    Response->Code = EHttpServerResponseCodes::Ok;
    Response->Headers.Add("Content-Type", { "text/html" });
	// 从text转换为bytes
	Response->Body = {
		0x3C, 0x68, 0x74, 0x6D, 0x6C, 0x3E, 0x3C, 0x68, 0x65, 0x61, 0x64,
		0x3E, 0x3C, 0x74, 0x69, 0x74, 0x6C, 0x65, 0x3E, 0x48, 0x65,
		0x6C, 0x6C, 0x6F, 0x20, 0x57, 0x6F, 0x72, 0x6C, 0x64, 0x3C,
		// ... (rest of the HTML content)
	};

    OnComplete(MoveTemp(Response));
    return;
}

FNivaHttpResponse UNetworkCoreSubsystem::MakeResponse(FString Text, FString ContentType, int32 Code)
{
	FNivaHttpResponse HttpServerResponse;
	HttpServerResponse.HttpServerResponse.Code = (EHttpServerResponseCodes)Code;

	FTCHARToUTF8 ConvertToUtf8(*Text);
	const uint8* ConvertToUtf8Bytes = (reinterpret_cast<const uint8*>(ConvertToUtf8.Get()));
	HttpServerResponse.HttpServerResponse.Body.Append(ConvertToUtf8Bytes, ConvertToUtf8.Length());

	FString Utf8CharsetContentType = FString::Printf(TEXT("%s;charset=utf-8"), *ContentType);
	TArray<FString> ContentTypeValue = { MoveTemp(Utf8CharsetContentType) };
	HttpServerResponse.HttpServerResponse.Headers.Add(TEXT("content-type"), MoveTemp(ContentTypeValue));

	return HttpServerResponse;
}

void UNetworkCoreSubsystem::CheckAudioDevice()
{
#ifdef USE_ANDROID_AUDIO
#if USE_ANDROID_AUDIO
	UE_LOG(LogTemp, Warning, TEXT("USE_ANDROID_AUDIO is enabled!"));
#else
	UE_LOG(LogTemp, Warning, TEXT("USE_ANDROID_AUDIO is disabled!"));
#endif
	// 如果没定义
#else
	UE_LOG(LogTemp, Warning, TEXT("USE_ANDROID_AUDIO is not defined!"));
#endif

}









FNivaHttpRequest::FNivaHttpRequest(const FHttpServerRequest& Request)
{

	Verb = (ENivaHttpRequestVerbs)Request.Verb;
	RelativePath = *Request.RelativePath.GetPath();

	for (const auto& Header : Request.Headers)
	{
		FString StrHeaderVals;
		for (const auto& val : Header.Value)
		{
			StrHeaderVals += val + TEXT(" ");
		}

		Headers.Add(Header.Key, StrHeaderVals);
	}

	PathParams = Request.PathParams;
	QueryParams = Request.QueryParams;


	// Convert UTF8 to FString
	FUTF8ToTCHAR BodyTCHARData(reinterpret_cast<const ANSICHAR*>(Request.Body.GetData()), Request.Body.Num());
	FString StrBodyData{ BodyTCHARData.Length(), BodyTCHARData.Get() };

	Body = *StrBodyData;
	BodyBytes = Request.Body;
}




// 初始化 CivetWeb 服务器并注册 HTTP 处理函数
void UMCPTransportSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);

}

// 停止 CivetWeb 服务器并清理资源
void UMCPTransportSubsystem::Deinitialize()
{
    bIsShuttingDown = true; // 标志游戏正在关闭

    if (ServerContext)
    {
        mg_stop(ServerContext);
        ServerContext = nullptr;
    }
    Super::Deinitialize();
}

bool UMCPTransportSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
    return true;
}
// 生成新的唯一会话 ID（GUID）
FString UMCPTransportSubsystem::GenerateSessionId() const
{
    return FGuid::NewGuid().ToString(EGuidFormats::Digits);
}

void UMCPTransportSubsystem::ParseJsonRPC(const FString& JsonString, FString& Method, TSharedPtr<FJsonObject>& Params, int& ID, TSharedPtr<FJsonObject>& JsonObject)
{
	TSharedRef<TJsonReader<>> JsonReader = TJsonReaderFactory<>::Create(JsonString);
	JsonObject = MakeShareable(new FJsonObject());
	if (FJsonSerializer::Deserialize(JsonReader, JsonObject))
	{
		Method = JsonObject->GetStringField(TEXT("method"));
		Params = JsonObject->GetObjectField(TEXT("params"));
		ID = JsonObject->GetNumberField(TEXT("id"));
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("SSE:JSONRPC : %s"), *JsonString);
	}
}

void UMCPTransportSubsystem::RegisterToolProperties(FMCPTool tool, FMCPRouteDelegate MCPRouteDelegate)
{
	// 允许同名工具重复注册：将同名的路由累计在一起进行广播
	FMCPToolStorage& Storage = MCPTools.FindOrAdd(tool.Name);

	// 1) 记录路由与注册计数（不覆盖）
	Storage.RouteDelegates.Add(MCPRouteDelegate);
	Storage.ToolNum += 1;

	// 2) 保存本次注册的完整变体定义（与路由索引对齐）
	Storage.MCPToolVariants.Add(tool);

	// 3) 维护“规范展示定义”（Canonical Tool）：
	//    - 对 Owner 参数的 ActorClass 采用“父类优先”进行合并；
	//    - 其他字段保持首次已存在的定义，除非首次注册。
	auto GetOwnerClassFromTool = [](const FMCPTool& T) -> UClass*
	{
		for (UMCPToolProperty* Prop : T.Properties)
		{
			if (Prop && Prop->Name == TEXT("Owner"))
			{
				if (UMCPToolPropertyActorPtr* ActorProp = Cast<UMCPToolPropertyActorPtr>(Prop))
				{
					return ActorProp->ActorClass;
				}
			}
		}
		return nullptr;
	};
	auto SetOwnerClassInTool = [](FMCPTool& T, UClass* NewClass)
	{
		if (!NewClass) return;
		for (UMCPToolProperty* Prop : T.Properties)
		{
			if (Prop && Prop->Name == TEXT("Owner"))
			{
				if (UMCPToolPropertyActorPtr* ActorProp = Cast<UMCPToolPropertyActorPtr>(Prop))
				{
					ActorProp->ActorClass = NewClass;
					return;
				}
			}
		}
	};
	auto IsSameOrParent = [](UClass* MaybeParent, UClass* MaybeChild) -> bool
	{
		if (!MaybeParent || !MaybeChild) return false;
		return MaybeChild->IsChildOf(MaybeParent);
	};

	if (Storage.MCPTool.Properties.Num() == 0)
	{
		// 首次注册：直接采用该定义作为规范定义
		Storage.MCPTool = tool;
	}
	else
	{
		// 后续注册：如新注册的 OwnerClass 是现有规范 OwnerClass 的父类，则提升规范到父类
		UClass* IncomingOwnerClass = GetOwnerClassFromTool(tool);
		UClass* CanonOwnerClass = GetOwnerClassFromTool(Storage.MCPTool);
		if (IncomingOwnerClass && CanonOwnerClass)
		{
			if (IsSameOrParent(IncomingOwnerClass, CanonOwnerClass) && IncomingOwnerClass != CanonOwnerClass)
			{
				SetOwnerClassInTool(Storage.MCPTool, IncomingOwnerClass);
			}
		}
		else if (IncomingOwnerClass && !CanonOwnerClass)
		{
			SetOwnerClassInTool(Storage.MCPTool, IncomingOwnerClass);
		}
		// 若两者都没有 OwnerClass，保持现有规范定义不变
	}

	UE_LOG(LogTemp, Log, TEXT("RegisterToolProperties: %s (TotalRoutes=%d, TotalRegs=%d, Variants=%d, CanonOwner=%s, IncomingOwner=%s)"),
		*tool.Name,
		Storage.RouteDelegates.Num(),
		Storage.ToolNum,
		Storage.MCPToolVariants.Num(),
		*GetNameSafe(GetOwnerClassFromTool(Storage.MCPTool)),
		*GetNameSafe(GetOwnerClassFromTool(tool)));
}

TSharedPtr<FJsonObject> UMCPTransportSubsystem::GetToolbyTarget(FString ActorName)
{
	// 用json来存储结果
	TSharedPtr<FJsonObject> result = MakeShareable(new FJsonObject);
	TArray<TSharedPtr<FJsonValue>> ToolsArray;
	for (auto i : MCPTools)
	{
		for (auto j : i.Value.MCPTool.Properties)
		{
			// 检查空指针
			if (!j)
			{
				continue;
			}
			// 检查是否为目标
			if (j->GetAvailableTargets().Contains(ActorName))
			{
				// 构建对象
				TSharedPtr<FJsonObject> ToolObject = MakeShareable(new FJsonObject);
			}
		}
	}
	// 将json数组添加到根对象
	result->SetArrayField("tools", ToolsArray);
	
	return result;
}

TSharedPtr<FJsonObject> UMCPTransportSubsystem::GetToolTargets(FString ToolName)
{
	// 通过json来存储结果
    // 构建一个JSON对象
    TSharedPtr<FJsonObject> RootObject = MakeShareable(new FJsonObject);
    TArray<TSharedPtr<FJsonValue>> TargetsArray;

    for (const auto& i : MCPTools) 
    {
        if (i.Key == ToolName)
        {
            for (UMCPToolProperty* j : i.Value.MCPTool.Properties)
            {
                if (!j) // 检查空指针
                {
                    continue;
                }

                const TArray<FString> TargetList = j->GetAvailableTargets(); // 缓存结果
                if (TargetList.Num() == 0)
                {
                    continue;
                }

                // 构造JSON数组
                TArray<TSharedPtr<FJsonValue>> JsonArray;
                for (const auto& k : TargetList)
                {
                    JsonArray.Add(MakeShareable(new FJsonValueString(k))); // 减少MakeShareable调用频率
                }

					// 构建对象
                TSharedPtr<FJsonObject> ToolObject = MakeShareable(new FJsonObject);
                ToolObject->SetArrayField(j->Name, JsonArray);
                
                // 检查Name是否重复, 决定是否合并/覆盖
                if (RootObject->HasField(j->Name))
                {
                    UE_LOG(LogTemp, Warning, TEXT("Duplicate field detected: %s"), *j->Name);
                    // 决定如何处理，当前逻辑直接覆盖
                }

                // 添加到targets数组
                TargetsArray.Add(MakeShareable(new FJsonValueObject(ToolObject)));
            }
        }
    }

    // 设置根对象
    RootObject->SetArrayField("targets", TargetsArray);
    return RootObject;
}



URefreshMCPClientAsyncAction* URefreshMCPClientAsyncAction::RefreshMCPClient(UObject* WorldContextObject)
{
    URefreshMCPClientAsyncAction* Action = NewObject<URefreshMCPClientAsyncAction>();
    Action->RegisterWithGameInstance(WorldContextObject);
    return Action;
}

void URefreshMCPClientAsyncAction::Activate()
{
    TSharedRef<IHttpRequest> Request = FHttpModule::Get().CreateRequest();
    const UNivaNetworkCoreSettings* SettingsLocal = GetDefault<UNivaNetworkCoreSettings>();
    FString BaseURL = SettingsLocal ? SettingsLocal->MCPBaseURL : TEXT("");
    if (BaseURL.IsEmpty())
    {
        BaseURL = TEXT("http://192.168.10.201:8081");
    }
    const FString RefreshURL = BaseURL + TEXT("/api/servers/refresh");
    Request->SetURL(RefreshURL);
    Request->SetVerb(TEXT("POST"));
    Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
    Request->SetContentAsString(FString::Printf(TEXT("{\"config_path\":\"%s\",\"request_id\":\"%s\"}"), *RefreshURL, TEXT("17516252572787568552130849533")));
    
    Request->OnProcessRequestComplete().BindUObject(this, &URefreshMCPClientAsyncAction::HandleRequestComplete);
    Request->ProcessRequest();
}

void URefreshMCPClientAsyncAction::HandleRequestComplete(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bSuccess)
{
    bool bSuccessFlag = false;
    FString Message = TEXT("Request failed");
    
    if (bSuccess && Response.IsValid())
    {
        FString ResponseString = Response->GetContentAsString();
        TSharedPtr<FJsonObject> JsonObject;
        TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ResponseString);
        
        if (FJsonSerializer::Deserialize(Reader, JsonObject))
        {
            bSuccessFlag = JsonObject->GetBoolField(TEXT("success"));
            Message = JsonObject->GetStringField(TEXT("message"));
            
            UE_LOG(LogTemp, Log, TEXT("Success: %s, Message: %s"), 
                   bSuccessFlag ? TEXT("true") : TEXT("false"), *Message);
        }
    }
    
    if (bSuccessFlag)
    {
        OnSuccess.Broadcast(bSuccessFlag, Message);
    }
    else
    {
        OnFailure.Broadcast(bSuccessFlag, Message);
    }
    
    SetReadyToDestroy();
}

// 将事件和数据打包并加入指定会话队列
void UMCPTransportSubsystem::SendSSE(const FString& SessionId, const FString& Event, const FString& Data)
{
    if (Sessions.Contains(SessionId))
    {
        // 格式化 SSE 消息：event 和 data 一体化
        FString Msg = FString::Printf(TEXT("event: %s\n"), *Event) +
            FString::Printf(TEXT("data: %s\n\n"), *Data);
        Sessions[SessionId]->Enqueue(Msg);
        UE_LOG(LogTemp, Verbose, TEXT("sendSSE: Queued: message for session %s: %s"), *SessionId, *Msg);
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("sendSSE: UNKNOW session %s"), *SessionId);
    }
}

// 可扩展的业务处理函数示例
void UMCPTransportSubsystem::HandlePostRequest(const FMCPRequest& Request, const FString& SessionId)
{
    // 解析 JSON 或根据方法执行操作
    UE_LOG(LogTemp, Log, TEXT("Post: SSE: PostRequest: for session %s: %s"), *SessionId, *Request.Json);


    // 解析参数
    FString Method;
    TSharedPtr<FJsonObject> Params;
    int id;
    TSharedPtr<FJsonObject> JsonObject;

    ParseJsonRPC(Request.Json, Method, Params, id, JsonObject);


    if (Method == "initialize") {
		// 处理初始化逻辑
        /*{
            "jsonrpc": "2.0",
                "id" : 1,
                "result" : {
                "protocolVersion": "2024-11-05",
                    "capabilities" : {
                    "logging": {},
                        "prompts" : {
                        "listChanged": true
                    },
                        "resources" : {
                        "subscribe": true,
                            "listChanged" : true
                    },
                        "tools" : {
                        "listChanged": true
                    }
                },
                    "serverInfo": {
                    "name": "ExampleServer",
                        "version" : "1.0.0"
                },
                    "instructions" : "Optional instructions for the client"
            }
        }*/
        FString InitMessage;
        // 通过json的形式初始化InitMessage
            TSharedPtr<FJsonObject> RootObject = MakeShareable(new FJsonObject);

            // 设置基本字段
            RootObject->SetStringField("jsonrpc", "2.0");
            RootObject->SetNumberField("id", id);

            // 构建 result 对象
            TSharedPtr<FJsonObject> ResultObject = MakeShareable(new FJsonObject);
            ResultObject->SetStringField("protocolVersion", "2024-11-05");

            // 构建 capabilities 对象
            TSharedPtr<FJsonObject> CapabilitiesObject = MakeShareable(new FJsonObject);

            // logging
            CapabilitiesObject->SetObjectField("logging", MakeShareable(new FJsonObject()));

            // prompts
            /*TSharedPtr<FJsonObject> PromptsObject = MakeShareable(new FJsonObject);
            PromptsObject->SetBoolField("listChanged", true);
            CapabilitiesObject->SetObjectField("prompts", PromptsObject);*/

            // resources
            /*TSharedPtr<FJsonObject> ResourcesObject = MakeShareable(new FJsonObject);
            ResourcesObject->SetBoolField("subscribe", true);
            ResourcesObject->SetBoolField("listChanged", true);
            CapabilitiesObject->SetObjectField("resources", ResourcesObject);*/

            // tools
            TSharedPtr<FJsonObject> ToolsObject = MakeShareable(new FJsonObject);
            ToolsObject->SetBoolField("listChanged", true);
            CapabilitiesObject->SetObjectField("tools", ToolsObject);

            // 将 capabilities 添加到 result
            ResultObject->SetObjectField("capabilities", CapabilitiesObject);

            // serverInfo
            TSharedPtr<FJsonObject> ServerInfoObject = MakeShareable(new FJsonObject);
            ServerInfoObject->SetStringField("name", "ExampleServer");
            ServerInfoObject->SetStringField("version", "1.0.0");
            ResultObject->SetObjectField("serverInfo", ServerInfoObject);

            // instructions
            ResultObject->SetStringField("instructions", "Optional instructions for the client");

            // 将 result 添加到根对象
            RootObject->SetObjectField("result", ResultObject);

            // 序列化为字符串
            TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&InitMessage);
            FJsonSerializer::Serialize(RootObject.ToSharedRef(), Writer);


            // 先剔除所有的换行符
			InitMessage.ReplaceInline(TEXT("\n"), TEXT(""));
			InitMessage.ReplaceInline(TEXT("\r"), TEXT(""));
			InitMessage.ReplaceInline(TEXT("\t"), TEXT(""));


            //InitMessage += "\n\n";

        SendSSE(SessionId, TEXT("message"), InitMessage);
	}
    else if (Method == "tools/list") {
        // 展示工具

        // 用json的形式构建返回
		FString ToolListMessage;

		TSharedPtr<FJsonObject> RootObject = MakeShareable(new FJsonObject);
		// 设置基本字段
		RootObject->SetStringField("jsonrpc", "2.0");
		RootObject->SetNumberField("id", id); 

		// 构建 result 对象
		TSharedPtr<FJsonObject> ResultObject = MakeShareable(new FJsonObject);

        // 构建 tools 数组
        TArray<TSharedPtr<FJsonValue>> ToolsArray;

		// 构建 tools 数组
		for (auto k : MCPTools)
		{
            FMCPTool i = k.Value.MCPTool;
            // 构建工具对象
            TSharedPtr<FJsonObject> ToolObject = MakeShareable(new FJsonObject);

            // 工具名称和描述
            FString ToolName = i.Name;
			FString ToolDescription = i.Description;

            ToolObject->SetStringField("name", ToolName);
            ToolObject->SetStringField("description", ToolDescription);

            // 构建 inputSchema 对象
            TSharedPtr<FJsonObject> InputSchemaObject = MakeShareable(new FJsonObject);
            InputSchemaObject->SetStringField("type", "object");
            // 构建 properties 对象
            TSharedPtr<FJsonObject> PropertiesObject = MakeShareable(new FJsonObject);
            // 构建 required 数组
            TArray<FString> RequiredArray;
			// 遍历工具的属性
            for (UMCPToolProperty* j : i.Properties)
            {
                PropertiesObject->SetObjectField(j->Name, j->GetJsonObject());
                RequiredArray.Add(j->Name);
            }
            // 将 properties 添加到 inputSchema
            InputSchemaObject->SetObjectField("properties", PropertiesObject);
			// 将 required 数组添加到 inputSchema
            // Replace the problematic line with the following code to fix the error:
            TArray<TSharedPtr<FJsonValue>> JsonArray;
            for (const FString& RequiredItem : RequiredArray)
            {
                JsonArray.Add(MakeShareable(new FJsonValueString(RequiredItem)));
            }
            InputSchemaObject->SetArrayField("required", JsonArray);
            
            // 将 inputSchema 添加到工具对象
            ToolObject->SetObjectField("inputSchema", InputSchemaObject);
            // 将工具对象添加到 tools 数组
            ToolsArray.Add(MakeShareable(new FJsonValueObject(ToolObject)));
        }
		// 将 tools 数组添加到 result 对象
		ResultObject->SetArrayField("tools", ToolsArray);

		// 设置 nextCursor
		ResultObject->SetStringField("nextCursor", "next-page-cursor");
		// 将 result 添加到根对象
		RootObject->SetObjectField("result", ResultObject);
		// 序列化为字符串
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ToolListMessage);
		FJsonSerializer::Serialize(RootObject.ToSharedRef(), Writer);
		// 先剔除所有的换行符
		ToolListMessage.ReplaceInline(TEXT("\n"), TEXT(""));
		ToolListMessage.ReplaceInline(TEXT("\r"), TEXT(""));
		ToolListMessage.ReplaceInline(TEXT("\t"), TEXT(""));

		//ToolListMessage += "\n\n";
		SendSSE(SessionId, TEXT("message"), ToolListMessage);
	}
	else if (Method == "resources/list") {
		// 展示资源
		// TODO
	}
	else if (Method == "prompts/list") {
		// 展示提示
		// TODO
	}
	else if (Method == "logging/list") {
		// 展示日志
		// TODO

    }
    else if (Method == "tools/call") {

        // 从params里面获取name
		FString ToolName = Params->GetStringField(TEXT("name"));


    	
		// print 一下时间
    	UE_LOG(LogTemp, Log, TEXT("SSE: tools/call: time: %s"), *FDateTime::Now().ToString());
      		// 直接print一下
		UE_LOG(LogTemp, Log, TEXT("SSE: tools/call: %s"), *ToolName);

		// 提取 _meta.progressToken（字符串或数字），用于进度回报
		FString ProgressToken;
		if (Params.IsValid() && Params->HasField(TEXT("_meta")))
		{
			TSharedPtr<FJsonObject> MetaObj = Params->GetObjectField(TEXT("_meta"));
			if (MetaObj.IsValid())
			{
				if (MetaObj->HasTypedField<EJson::String>(TEXT("progressToken")))
				{
					ProgressToken = MetaObj->GetStringField(TEXT("progressToken"));
				}
				else if (MetaObj->HasField(TEXT("progressToken")))
				{
					// 兼容数字类型的 token
					const double NumToken = MetaObj->GetNumberField(TEXT("progressToken"));
					ProgressToken = FString::SanitizeFloat(NumToken);
				}
			}
		}

		// 调用绑定的函数
        if (MCPTools.Contains(ToolName)){
        	// 计数器
        	int Num = 0 ;
            FMCPToolStorage& Storage = MCPTools[ToolName];

            // 参数校验：验证变体中的 Actor 指针参数是否有效
            auto ValidateVariant = [&](const FMCPTool& Variant, TArray<FString>& BadParams) -> bool
            {
                BadParams.Reset();
                for (UMCPToolProperty* Prop : Variant.Properties)
                {
                    if (!Prop) continue;
                    if (UMCPToolPropertyActorPtr* ActorProp = Cast<UMCPToolPropertyActorPtr>(Prop))
                    {
                        AActor* Resolved = nullptr;
                        const bool bHasValue = UMCPToolBlueprintLibrary::GetActorValue(Variant, Prop->Name, Request.Json, Resolved);
                        const bool bClassOk = (Resolved != nullptr) && (!ActorProp->ActorClass || Resolved->IsA(ActorProp->ActorClass));
                        if (!bHasValue || !bClassOk)
                        {
                            BadParams.Add(Prop->Name);
                        }
                    }
                }
                return BadParams.Num() == 0;
            };

            // 优先：尝试根据调用参数中的 Owner 选择“最匹配”的变体（子类优先，子类缺失则回退到父类）
            AActor* ProvidedOwner = nullptr;
            int32 BestIdx = INDEX_NONE;
            int32 BestDepth = MAX_int32; // 越小越具体（0=完全相同）

            auto GetOwnerClassFromVariant = [](const FMCPTool& Variant) -> UClass*
            {
                for (UMCPToolProperty* Prop : Variant.Properties)
                {
                    if (Prop && Prop->Name == TEXT("Owner"))
                    {
                        if (UMCPToolPropertyActorPtr* ActorProp = Cast<UMCPToolPropertyActorPtr>(Prop))
                        {
                            return ActorProp->ActorClass;
                        }
                    }
                }
                return nullptr;
            };

            auto ComputeDepth = [](UClass* Child, UClass* Parent) -> int32
            {
                if (!Child || !Parent) return MAX_int32;
                if (!Child->IsChildOf(Parent)) return MAX_int32;
                int32 Depth = 0;
                UClass* C = Child;
                while (C && C != Parent)
                {
                    C = C->GetSuperClass();
                    ++Depth;
                }
                return Depth; // 0 表示完全相同，越小越具体
            };

            // 先尽力解析出 ProvidedOwner（用任一变体的定义尝试解析）
            for (int32 idx = 0; idx < Storage.MCPToolVariants.Num() && ProvidedOwner == nullptr; ++idx)
            {
                const FMCPTool& Variant = Storage.MCPToolVariants[idx];
                AActor* TryOwner = nullptr;
                if (UMCPToolBlueprintLibrary::GetActorValue(Variant, TEXT("Owner"), Request.Json, TryOwner) && TryOwner)
                {
                    ProvidedOwner = TryOwner;
                }
            }

            if (ProvidedOwner)
            {
                UClass* ProvidedOwnerClass = ProvidedOwner->GetClass();
                // 选择与 ProvidedOwnerClass 最接近的父类/本类定义
                for (int32 idx = 0; idx < Storage.RouteDelegates.Num(); ++idx)
                {
                    const FMCPTool& Variant = Storage.MCPToolVariants.IsValidIndex(idx) ? Storage.MCPToolVariants[idx] : Storage.MCPTool;
                    UClass* OwnerClassInVariant = GetOwnerClassFromVariant(Variant);
                    const int32 Depth = ComputeDepth(ProvidedOwnerClass, OwnerClassInVariant);
                    if (Depth < BestDepth)
                    {
                        BestDepth = Depth;
                        BestIdx = idx;
                    }
                }

                if (BestIdx != INDEX_NONE)
                {
                    FMCPRouteDelegate& Delegate = Storage.RouteDelegates[BestIdx];
                    if (Delegate.IsBound())
                    {
                        UMCPToolHandle* MCPToolHandle = UMCPToolHandle::initToolHandle(id, SessionId, this, ProgressToken);
                        const FMCPTool& ToolVariant = Storage.MCPToolVariants.IsValidIndex(BestIdx) ? Storage.MCPToolVariants[BestIdx] : Storage.MCPTool;
                        UE_LOG(LogTemp, Verbose, TEXT("tools/call: Selected variant %d for tool %s (Owner=%s, Depth=%d)"), BestIdx, *ToolName, *GetNameSafe(ProvidedOwner), BestDepth);
                        // 校验 Actor 参数
                        TArray<FString> BadParams;
                        if (!ValidateVariant(ToolVariant, BadParams))
                        {
                            const FString Msg = FString::Printf(TEXT("工具参数错误：以下 Actor 参数无效或类型不匹配：%s"), *FString::Join(BadParams, TEXT(", ")));
                            UE_LOG(LogTemp, Warning, TEXT("tools/call validation failed: %s"), *Msg);
                            if (MCPToolHandle)
                            {
                                MCPToolHandle->ToolCallbackRaw(true, Msg, true, -1, -1);
                            }
                            Num = 1; // 已处理（错误回调），不再进入后续委托
                        }
                        else
                        {
                            Delegate.ExecuteIfBound(Request.Json, MCPToolHandle, ToolVariant);
                            Num = 1;
                        }
                    }
                }
            }

            // 如果无法解析 Owner 或未找到任何匹配项，则回退：
            if (Num == 0)
            {
                // 1) 优先回退到“父类”定义（无法判断时取第一个可用委托）
                for (int32 idx = 0; idx < Storage.RouteDelegates.Num() && Num == 0; ++idx)
                {
                    FMCPRouteDelegate& Delegate = Storage.RouteDelegates[idx];
                    if (Delegate.IsBound())
                    {
                        UMCPToolHandle* MCPToolHandle = UMCPToolHandle::initToolHandle(id, SessionId, this, ProgressToken);
                        const FMCPTool& ToolVariant = Storage.MCPToolVariants.IsValidIndex(idx) ? Storage.MCPToolVariants[idx] : Storage.MCPTool;
                        UE_LOG(LogTemp, Verbose, TEXT("tools/call: Fallback to variant %d for tool %s"), idx, *ToolName);
                        // 校验 Actor 参数
                        TArray<FString> BadParams;
                        if (!ValidateVariant(ToolVariant, BadParams))
                        {
                            const FString Msg = FString::Printf(TEXT("工具参数错误：以下 Actor 参数无效或类型不匹配：%s"), *FString::Join(BadParams, TEXT(", ")));
                            UE_LOG(LogTemp, Warning, TEXT("tools/call validation failed (fallback): %s"), *Msg);
                            if (MCPToolHandle)
                            {
                                MCPToolHandle->ToolCallbackRaw(true, Msg, true, -1, -1);
                            }
                            Num = 1; // 已处理错误
                        }
                        else
                        {
                            Delegate.ExecuteIfBound(Request.Json, MCPToolHandle, ToolVariant);
                            Num = 1;
                        }
                    }
                }
            }

        	if (Num == 0)
        	{	// TODO::这里需要逐个检测有效性并删除
        		// 说明没有有效工具绑定，删除mcptools中的数据，并返回一个错误响应
        	}
        }
        else {
			FString ErrorMessage;
			TSharedPtr<FJsonObject> RootObject = MakeShareable(new FJsonObject);
			// 设置基本字段
			RootObject->SetStringField("jsonrpc", "2.0");
			RootObject->SetNumberField("id", id);
			// 构建 error 对象
			TSharedPtr<FJsonObject> ErrorObject = MakeShareable(new FJsonObject);
			ErrorObject->SetNumberField("code", -32602);
			ErrorObject->SetStringField("message", "Unknown tool: invalid_tool_name");
			// 将 error 添加到根对象
			RootObject->SetObjectField("error", ErrorObject);
			// 序列化为字符串
			TSharedRef<TJsonWriter<>> _Writer = TJsonWriterFactory<>::Create(&ErrorMessage);
			FJsonSerializer::Serialize(RootObject.ToSharedRef(), _Writer);
			// 先剔除所有的换行符
			ErrorMessage.ReplaceInline(TEXT("\n"), TEXT(""));
			ErrorMessage.ReplaceInline(TEXT("\r"), TEXT(""));
			ErrorMessage.ReplaceInline(TEXT("\t"), TEXT(""));

			SendSSE(SessionId, TEXT("message"), ErrorMessage);
        }
    }
    else if (Method == "ping" || Method == "Ping") {
        // 立马返回一个空响应
        /*{
            "jsonrpc": "2.0",
                "id" : "123",
                "result" : {}
        }*/
		FString PingMessage;
		TSharedPtr<FJsonObject> RootObject = MakeShareable(new FJsonObject);
		// 设置基本字段
		RootObject->SetStringField("jsonrpc", "2.0");
		RootObject->SetNumberField("id", id);
		// 构建 result 对象
		TSharedPtr<FJsonObject> ResultObject = MakeShareable(new FJsonObject);
		// 将 result 添加到根对象
		RootObject->SetObjectField("result", ResultObject);
		// 序��化为字符串
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&PingMessage);
		FJsonSerializer::Serialize(RootObject.ToSharedRef(), Writer);
		// 先剔除所有的换行符
		PingMessage.ReplaceInline(TEXT("\n"), TEXT(""));
		PingMessage.ReplaceInline(TEXT("\r"), TEXT(""));
		PingMessage.ReplaceInline(TEXT("\t"), TEXT(""));

		SendSSE(SessionId, TEXT("message"), PingMessage);
    }
    else {
		UE_LOG(LogTemp, Warning, TEXT("post: SSE: UNKNOW method: %s"), *Method);
	}
}

void UMCPTransportSubsystem::StartMCPServer()
{
    // 先获取UMCPTransportSubsystem
	// UMCPTransportSubsystem* This = WorldContextObject->GetWorld()->GetGameInstance()->GetSubsystem<UMCPTransportSubsystem>();

    // 开服
    // 获取settings
	const UNivaNetworkCoreSettings* Settings = GetDefault<UNivaNetworkCoreSettings>();
	// 先获取端口
	int32 MCPPort = Settings->MCPPort;
	// 设置端口
	FString Port = FString::Printf(TEXT("%d"), MCPPort);
    // 检查端口是否被占用
    FTCHARToUTF8 PortUtf8(*Port);
    const char* Options[] = { "listening_ports", PortUtf8.Get(), nullptr };
    ServerContext = mg_start(nullptr, this, Options);
    check(ServerContext);

    //AI写的 不知道干嘛用的
    // mg_set_request_handler(This->ServerContext, "/connect", OnConnect, This);
    
    
    // 消息端点，应该只支持post，用于客户端向服务器发送信息，只要能成功解析jsonrpc，就返回200
	// 如果有需要返回的内容，就用SendSSE
    mg_set_request_handler(ServerContext, "/message", OnPostMessage, this);
    // SSE服务器，用于服务器向客户端发送数据
    mg_set_request_handler(ServerContext, "/sse", OnSSE, this);

    // UE_LOG(LogTemp, Log, TEXT("SSE :server:start，Port：8080"));



	/* 注册两个工具：
	 * 1: 根据对象查询所有可用工具
	 * 2: 根据工具查询所有可用对象
	 */
	FMCPTool Tool1;
	Tool1.Name = TEXT("QueryObject");
	Tool1.Description = TEXT("根据对象查询所有可用工具");
	UMCPToolProperty *Property1 = UMCPToolPropertyString::CreateStringProperty(TEXT("ObjectName"), TEXT("要查询的对象名称"));
	Tool1.Properties.Add(Property1);
	// 创建调用回调的动态委托
	FMCPRouteDelegate MCPRouteDelegate1;
	MCPRouteDelegate1.BindDynamic(this, &UMCPTransportSubsystem::OnToolRouteCallback);

	RegisterToolProperties(Tool1,MCPRouteDelegate1);

	FMCPTool Tool2;
	Tool2.Name = TEXT("QueryTool");
	Tool2.Description = TEXT("根据工具查询所有可用对象");
	UMCPToolProperty *Property2 = UMCPToolPropertyString::CreateStringProperty(TEXT("ToolName"), TEXT("要查询的mcp工具名称"));
	Tool2.Properties.Add(Property2);
	// 创建调用回调的动态委托
	FMCPRouteDelegate MCPRouteDelegate2;
	MCPRouteDelegate2.BindDynamic(this, &UMCPTransportSubsystem::OnToolTargetsCallback);
	RegisterToolProperties(Tool2,MCPRouteDelegate2);
	
}

// 处理 /message 接口：读取 POST 内容并调用业务逻辑
int UMCPTransportSubsystem::OnPostMessage(struct mg_connection* Connection, void* UserData)
{
    auto* This = static_cast<UMCPTransportSubsystem*>(UserData);
    const struct mg_request_info* ReqInfo = mg_get_request_info(Connection);

    char sessionBuf[64] = {};
    mg_get_var(ReqInfo->query_string, strlen(ReqInfo->query_string), "session_id", sessionBuf, sizeof(sessionBuf));
    // 检查 session_id 是否存在
    if (strlen(sessionBuf) == 0) {
        mg_printf(Connection, "HTTP/1.1 400 Bad Request\r\n\r\n");
        return 400;
    }

    FString SessionId(ANSI_TO_TCHAR(sessionBuf));

    // 获取 body 内容
    char bodyBuf[4096] = {};
    int bytesRead = mg_read(Connection, bodyBuf, sizeof(bodyBuf) - 1);
    bodyBuf[bytesRead] = '\0'; // 确保 null 结尾

    // 正确解码 UTF-8 到 FString
    FString JsonBody = FString(FUTF8ToTCHAR(bodyBuf));

    // 构造请求对象
    FMCPRequest Req{ JsonBody };

    // 将 HandlePostRequest 调度到游戏线程执行
    AsyncTask(ENamedThreads::GameThread, [This, Req, SessionId]()
    {
        This->HandlePostRequest(Req, SessionId);
    });

    mg_printf(Connection,
        "HTTP/1.1 202 Accepted\r\nContent-Length: 0\r\n\r\n");
    return 1;
}

// 处理 /sse 接口：将队列中的消息通过 SSE 推送给客户端
int UMCPTransportSubsystem::OnSSE(struct mg_connection* Connection, /*附加数据*/void* UserData)
{
    
    // 只处理 GET 请求
    const struct mg_request_info* ReqInfo = mg_get_request_info(Connection);
    if (std::string(ReqInfo->request_method) != "GET") {
        mg_printf(Connection, "HTTP/1.1 405 Method Not Allowed\r\n\r\n");
        return 405;
    }
	// 获取子系统实例和会话 ID
    UMCPTransportSubsystem* This = static_cast<UMCPTransportSubsystem*>(UserData);
    // 检验This是否有效
	if (!This) {
		mg_printf(Connection, "HTTP/1.1 500 Internal Server Error\r\n\r\n");
		return 500;
	}

    // ―― 1) 打印收到的请求 ―― 
    const struct mg_request_info* ri = mg_get_request_info(Connection);
    FString RequestLine = FString::Printf(
        TEXT("%s %s"),
        ANSI_TO_TCHAR(ri->request_method),
        //ANSI_TO_TCHAR(ri->uri),
        ANSI_TO_TCHAR(ri->query_string ? ri->query_string : "")
    );
    UE_LOG(LogTemp, Log, TEXT("SSE: CONNECT: %s"), *RequestLine);
    // 发送 SSE 头部
    mg_send_http_ok(Connection, "text/event-stream; charset=utf-8", -1);
    // 发送初始消息

    // 3) 循环推送，并定期发送心跳
    const double PingInterval = 15.0; // 秒
    double LastPing = 0.f;

    // 2. 初始化 session
    FString SessionId = This->GenerateSessionId();
    {
        FScopeLock Lock(&This->SessionLock); // 加锁
        This->Sessions.Add(SessionId, MakeShared<TQueue<FString>>());
    }
    This->Sessions.Add(SessionId, MakeShared<TQueue<FString>>());

    FString PostUrl = FString::Printf(TEXT("/message?session_id=%s"), *SessionId);
    FString InitMsg = FString::Printf(TEXT("event: endpoint\ndata: %s\n\n"), *PostUrl);
    std::string InitMsgAnsi = TCHAR_TO_UTF8(*InitMsg);
    mg_send_chunk(Connection, InitMsgAnsi.c_str(), InitMsgAnsi.length());


    // 循环推送事件，直到客户端断开
    while (true)
    {
        // 检查是否正在关闭
        if (This->bIsShuttingDown) {
            UE_LOG(LogTemp, Warning, TEXT("SSE: Shutting down, exiting loop"));
            break;
        }
        if (!This->ServerContext) {
            UE_LOG(LogTemp, Warning, TEXT("SSE: ServerContext is null"));
            break;
        }

        if (LastPing >= PingInterval) {
            const char* heartbeat = ":\n\n";
            mg_send_chunk(Connection, heartbeat, strlen(heartbeat));
            LastPing = 0.0f;
        }

        // 发送消息
        FString Msg;
        {
            FScopeLock Lock(&This->SessionLock); // 加锁
            if (This->Sessions.Contains(SessionId) && This->Sessions[SessionId]->Dequeue(Msg)) {
                UE_LOG(LogTemp, Log, TEXT("SSE:send %s"), *Msg);
                std::string MsgAnsi = TCHAR_TO_UTF8(*Msg);
                mg_send_chunk(Connection, MsgAnsi.c_str(), MsgAnsi.length());
            }
        }

        if (Msg.IsEmpty()) {
            FPlatformProcess::Sleep(0.1f);
        }

        LastPing += 0.1f;
    }
    return 1;  // 表示已成功处理
}

UMCPToolProperty* UMCPToolPropertyString::CreateStringProperty(FString InName,
	FString InDescription)
{
	UMCPToolPropertyString* Property = NewObject<UMCPToolPropertyString>();
	Property->Name = InName;
	Property->Type = EMCPJsonType::String;
	Property->Description = InDescription;
	return Property;
}

TSharedPtr<FJsonObject> UMCPToolPropertyString::GetJsonObject()
{
	//创建，并读取自身的属性补全json
	TSharedPtr<FJsonObject> RootObject = MakeShareable(new FJsonObject);
	RootObject->SetStringField("name", Name);
	RootObject->SetStringField("type", StaticEnum<EMCPJsonType>()->GetNameStringByValue(static_cast<int64>(Type)));
	RootObject->SetStringField("description", Description);
	return RootObject;
}

FString UMCPToolPropertyString::GetValue(FString InJson)
{
	// 解析InJson
	/*
	*   参考
  "jsonrpc": "2.0",
  "id": 2,
  "method": "tools/call",
  "params": {
    "name": "get_weather",
    "arguments": {
      "location": "New York"
    }
  }
}
	* 取出其中的location
	*/
	TSharedPtr<FJsonObject> JsonObject;
	TSharedRef<TJsonReader<>> JsonReader = TJsonReaderFactory<>::Create(InJson);
	if (FJsonSerializer::Deserialize(JsonReader, JsonObject))
	{
		// 获取params字段
		TSharedPtr<FJsonObject> ParamsObject = JsonObject->GetObjectField(TEXT("params"));
		if (ParamsObject.IsValid())
		{
			// 获取arguments字段
			TSharedPtr<FJsonObject> ArgumentsObject = ParamsObject->GetObjectField(TEXT("arguments"));
			if (ArgumentsObject.IsValid())
			{
				// 获取指定的字段值
				return ArgumentsObject->GetStringField(Name);
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("GetValue: No arguments field found in params"));
			}
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("GetValue: No params field found in JSON"));
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("GetValue: Failed to parse JSON: %s"), *InJson);
	}
	return TEXT(""); // 返回空字符串表示未找到


}

UMCPToolProperty* UMCPToolPropertyNumber::CreateNumberProperty(FString InName,FString InDescription, int InMin , int InMax )
{
	UMCPToolPropertyNumber* Property = NewObject<UMCPToolPropertyNumber>();
	Property->Name = InName;
	Property->Type = EMCPJsonType::Number;
	Property->Description = InDescription;
	// jsonschemer
	Property->Min = InMin;
	Property->Max = InMax;
	
	return Property;
}

TSharedPtr<FJsonObject> UMCPToolPropertyNumber::GetJsonObject()
{
	TSharedPtr<FJsonObject> RootObject = MakeShareable(new FJsonObject);
	RootObject->SetStringField("name", Name);
	RootObject->SetStringField("type", StaticEnum<EMCPJsonType>()->GetNameStringByValue(static_cast<int64>(Type)));
	RootObject->SetStringField("description", Description);
	RootObject->SetNumberField("minimum", Min);
	RootObject->SetNumberField("maximum", Max);
	return RootObject;
}

float UMCPToolPropertyNumber::GetValue(FString InJson)
{
	// 解析InJson
	/*
	*   参考
  "jsonrpc": "2.0",
  "id": 2,
  "method": "tools/call",
  "params": {
	"name": "get_weather",
	"arguments": {
	  "location": "New York"
	}
  }
}
	* 取出其中的location
	*/
	TSharedPtr<FJsonObject> JsonObject;
	TSharedRef<TJsonReader<>> JsonReader = TJsonReaderFactory<>::Create(InJson);
	if (FJsonSerializer::Deserialize(JsonReader, JsonObject))
	{
		// 获取params字段
		TSharedPtr<FJsonObject> ParamsObject = JsonObject->GetObjectField(TEXT("params"));
		if (ParamsObject.IsValid())
		{
			// 获取arguments字段
			TSharedPtr<FJsonObject> ArgumentsObject = ParamsObject->GetObjectField(TEXT("arguments"));
			if (ArgumentsObject.IsValid())
			{
				// 获取指定的字段值
				return ArgumentsObject->GetNumberField(Name);
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("GetValue: No arguments field found in params"));
			}
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("GetValue: No params field found in JSON"));
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("GetValue: Failed to parse JSON: %s"), *InJson);
	}
	return Min - 1; // 返回小于最小值的数表示未找到
}

UMCPToolProperty* UMCPToolPropertyInt::CreateIntProperty(FString InName, FString InDescription, int InMin, int InMax)
{
	UMCPToolPropertyInt* Property = NewObject<UMCPToolPropertyInt>();
	Property->Name = InName;
	Property->Type = EMCPJsonType::Integer;
	Property->Description = InDescription;
	// jsonschemer
	Property->Min = InMin;
	Property->Max = InMax;
	return Property;
}

TSharedPtr<FJsonObject> UMCPToolPropertyInt::GetJsonObject()
{
	TSharedPtr<FJsonObject> RootObject = MakeShareable(new FJsonObject);
	RootObject->SetStringField("name", Name);
	RootObject->SetStringField("type", StaticEnum<EMCPJsonType>()->GetNameStringByValue(static_cast<int64>(Type)));
	RootObject->SetStringField("description", Description);
	RootObject->SetNumberField("minimum", Min);
	RootObject->SetNumberField("maximum", Max);
	return RootObject;
}

int UMCPToolPropertyInt::GetValue(FString InJson)
{
	// 解析InJson
	/*
	*   参考
  "jsonrpc": "2.0",
  "id": 2,
  "method": "tools/call",
  "params": {
	"name": "get_weather",
	"arguments": {
	  "location": "New York"
	}
  }
}
	* 取出其中的location
	*/
	TSharedPtr<FJsonObject> JsonObject;
	TSharedRef<TJsonReader<>> JsonReader = TJsonReaderFactory<>::Create(InJson);
	if (FJsonSerializer::Deserialize(JsonReader, JsonObject))
	{
		// 获取params字段
		TSharedPtr<FJsonObject> ParamsObject = JsonObject->GetObjectField(TEXT("params"));
		if (ParamsObject.IsValid())
		{
			// 获取arguments字段
			TSharedPtr<FJsonObject> ArgumentsObject = ParamsObject->GetObjectField(TEXT("arguments"));
			if (ArgumentsObject.IsValid())
			{
				// 获取指定的字段值
				return ArgumentsObject->GetNumberField(Name);
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("GetValue: No arguments field found in params"));
			}
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("GetValue: No params field found in JSON"));
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("GetValue: Failed to parse JSON: %s"), *InJson);
	}
	return Min - 1; // 返回小于最小值的数表示未找到
}

TArray<AActor*> UMCPToolPropertyActorPtr::FindActors()
{
    TArray<AActor*> Actors;

    // 获取当前World实例
    UWorld* World = nullptr;
    if (GEngine)
    {
        // 从GEngine获取第一个有效的World
        for (const FWorldContext& Context : GEngine->GetWorldContexts())
        {
            if (Context.World() && Context.World()->IsGameWorld())
            {
                World = Context.World();
                break;
            }
        }
    }

    // 如果找不到有效的World，记录警告并返回空数组
    if (!World)
    {
        UE_LOG(LogTemp, Warning, TEXT("FindActors: Failed to find valid World"));
        return Actors;
    }

    // 清空当前的ActorMap
    ActorMap.Empty();

    // 如果指定了ActorClass，则只查找该类型的Actor
    if (ActorClass)
    {
        UGameplayStatics::GetAllActorsOfClass(World, ActorClass, Actors);
        UE_LOG(LogTemp, Log, TEXT("FindActors: Searching for actors of class %s"), *ActorClass->GetName());
    }

    // 更新ActorMap，使用弱指针存储引用
    for (AActor* Actor : Actors)
    {
        if (IsValid(Actor))
        {
        	//应该存actor在场景中的用户设置的名字，可读可理解的名字
        	ActorMap.Add(Actor->GetName(), Actor);
            UE_LOG(LogTemp, Verbose, TEXT("FindActors: Found actor %s"), *Actor->GetName());
        }
    }

    // 输出找到的Actor数量
    UE_LOG(LogTemp, Log, TEXT("FindActors: Found %d actors"), Actors.Num());

    return Actors;
}

UMCPToolProperty* UMCPToolPropertyActorPtr::CreateActorPtrProperty(FString InName, FString InDescription,
	TSubclassOf<AActor> InActorClass)
{
	UE_LOG(LogTemp, Log, TEXT("CreateActorPtrProperty: InActorClass=%s"), InActorClass ? *InActorClass->GetName() : TEXT("<null>"));
	UMCPToolPropertyActorPtr* Property = NewObject<UMCPToolPropertyActorPtr>();
	Property->Name = InName;
	Property->Type = EMCPJsonType::String;
	Property->Description = InDescription;
	Property->ActorClass = InActorClass;
	UE_LOG(LogTemp, Log, TEXT("CreateActorPtrProperty: Stored ActorClass=%s"), Property->ActorClass ? *Property->ActorClass->GetName() : TEXT("<null>"));
	Property->FindActors();
	
	return Property;
}

TSharedPtr<FJsonObject> UMCPToolPropertyActorPtr::GetJsonObject()
{
	TSharedPtr<FJsonObject> RootObject = MakeShareable(new FJsonObject);
	RootObject->SetStringField("name", Name);
	RootObject->SetStringField("type", StaticEnum<EMCPJsonType>()->GetNameStringByValue(static_cast<int64>(Type)));
	RootObject->SetStringField("description", Description);
	// jsonschemer,这里用enum限制参数
	TArray<TSharedPtr<FJsonValue>> EnumArray;
	for (auto i : ActorMap) {
		TSharedPtr<FJsonValue> EnumValue = MakeShareable(new FJsonValueString(i.Key));
		EnumArray.Add(EnumValue);
	}
	RootObject->SetArrayField("enum", EnumArray);
	return RootObject;
}

TArray<FString> UMCPToolPropertyActorPtr::GetAvailableTargets()
{
	FindActors();
	TArray<FString> Targets;
	for (auto i : ActorMap) {
		if (i.Value.IsValid())
			Targets.Add(i.Key);
	}
	return Targets;
}

AActor* UMCPToolPropertyActorPtr::GetActor(FString InName)
{
    // 获取actormap中的actor指针
	if (ActorMap.Contains(InName)) {
		return ActorMap[InName].IsValid() ?
			ActorMap[InName].Get() :
			nullptr;
	}
	return nullptr;
}

AActor* UMCPToolPropertyActorPtr::GetValue(FString InJson)
{
	TSharedPtr<FJsonObject> JsonObject;
	TSharedRef<TJsonReader<>> JsonReader = TJsonReaderFactory<>::Create(InJson);
	if (FJsonSerializer::Deserialize(JsonReader, JsonObject))
	{
		// 获取params字段
		TSharedPtr<FJsonObject> ParamsObject = JsonObject->GetObjectField(TEXT("params"));
		if (ParamsObject.IsValid())
		{
			// 获取arguments字段
			TSharedPtr<FJsonObject> ArgumentsObject = ParamsObject->GetObjectField(TEXT("arguments"));
			if (ArgumentsObject.IsValid())
			{
				// 获取指定的字段值
				FString ActorName = ArgumentsObject->GetStringField(Name);
				// 在ActorMap中查找
				return GetActor(ActorName);
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("GetValue: No arguments field found in params"));
			}
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("GetValue: No params field found in JSON"));
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("GetValue: Failed to parse JSON: %s"), *InJson);
	}
	return nullptr;
	
}

UMCPToolProperty* UMCPToolPropertyArray::CreateArrayProperty(FString InName, FString InDescription,  UMCPToolProperty* InProperty)
{
	
	UMCPToolPropertyArray* Property = NewObject<UMCPToolPropertyArray>();
	Property->Name = InName;
	Property->Type = EMCPJsonType::String;
	Property->Description = InDescription;
	Property->Property = InProperty;
	
	return Property;
}

TSharedPtr<FJsonObject> UMCPToolPropertyArray::GetJsonObject()
{
	/*
	*"ids": {
	  "type": "array",
	  "items": { "type": "number" },
	  "description": "ID 列表"
	}
	* 
	 */
	TSharedPtr<FJsonObject> RootObject = MakeShareable(new FJsonObject);

	RootObject->SetStringField("name", Name);
	RootObject->SetStringField("type", StaticEnum<EMCPJsonType>()->GetNameStringByValue(static_cast<int64>(Type)));
	RootObject->SetStringField("description", Description);
	// jsonschemer
	TSharedPtr<FJsonObject> ItemsObject = MakeShareable(new FJsonObject);
	ItemsObject->SetStringField("type", StaticEnum<EMCPJsonType>()->GetNameStringByValue(static_cast<int64>(Property->Type)));
	RootObject->SetObjectField("items", ItemsObject);
	return RootObject;
	
}

bool UMCPToolBlueprintLibrary::GetIntValue(const FMCPTool& MCPTool, const FString& Name, const FString& InJson,int32& OutValue)
{
	if (UMCPToolProperty* Property = GetProperty(MCPTool,Name)) {
		//这里写死int类型
		//直接转换Property为UMCPToolPropertyInt
		if (UMCPToolPropertyInt* PropertyInt = Cast<UMCPToolPropertyInt>(Property)) {
			OutValue = PropertyInt->GetValue(InJson);
			return true;
		}
		
	}
	return false;
}

bool UMCPToolBlueprintLibrary::GetStringValue(const FMCPTool& MCPTool, const FString& Name, const FString& InJson,
	FString& OutValue)
{
	if (UMCPToolProperty* Property = GetProperty(MCPTool,Name)) {
		//直接转换Property为UMCPToolPropertyString
		if (UMCPToolPropertyString* PropertyString = Cast<UMCPToolPropertyString>(Property)) {
			OutValue = PropertyString->GetValue(InJson);
			return true;
		}
	}
	return false;
}

bool UMCPToolBlueprintLibrary::GetNumberValue(const FMCPTool& MCPTool, const FString& Name, const FString& InJson,
	float& OutValue)
{
	if (UMCPToolProperty* Property = GetProperty(MCPTool,Name)) {
		//直接转换Property为 UMCPToolPropertyNumber
		if (UMCPToolPropertyNumber* PropertyNumber = Cast<UMCPToolPropertyNumber>(Property)) {
			OutValue = PropertyNumber->GetValue(InJson);
			return true;
		}
	}
	return false;
}

bool UMCPToolBlueprintLibrary::GetActorValue(const FMCPTool& MCPTool, const FString& Name, const FString& InJson,
	AActor*& OutValue)
{
	if (UMCPToolProperty* Property = GetProperty(MCPTool,Name)) {
		//直接转换Property为UMCPToolPropertyActorPtr
		
		if (UMCPToolPropertyActorPtr* PropertyActorPtr = Cast<UMCPToolPropertyActorPtr>(Property)) {
			OutValue = PropertyActorPtr->GetValue(InJson);
			return true;
		}
	}
	return false;
}

UMCPToolProperty* UMCPToolBlueprintLibrary::GetProperty(const FMCPTool& MCPTool, const FString& Name)
{
	for (auto i : MCPTool.Properties) {
		if (i->Name == Name)
			return i;
	}
	return nullptr;
}

void UMCPToolBlueprintLibrary::AddProperty(FMCPTool& MCPTool, UMCPToolProperty* Property)
{
	if (Property)
	{
		MCPTool.Properties.Add(Property);
	}
}

UMCPToolHandle* UMCPToolHandle::initToolHandle(int _id, const FString& _SessionID ,UMCPTransportSubsystem* _subsystem, const FString& InProgressToken)
{
    if (_id >= 1 && _subsystem != nullptr) {

        // 创建一个FMCPToolHandle，用object的方式初始化

        UMCPToolHandle* Handle = NewObject<UMCPToolHandle>(_subsystem);
    
        Handle->MCPid = _id;

        Handle->MCPTransportSubsystem = _subsystem;

        Handle->SessionId = _SessionID;
        Handle->ProgressToken = InProgressToken;

        return Handle;
    }
    return nullptr;
}

// void UMCPToolHandle::ToolCallback(bool isError, FString text)
// {
// 	// 触发工具回调
// 	if (MCPTransportSubsystem != nullptr && MCPid >= 0 && SessionId != "none") {
//         // 构建通用部分
// 		FString JsonMessage;
// 		TSharedPtr<FJsonObject> RootObject = MakeShareable(new FJsonObject);
// 		// 设置基本字段
// 		RootObject->SetStringField("jsonrpc", "2.0");
// 		RootObject->SetNumberField("id", MCPid);
// 		// 构建 result 对象
// 		TSharedPtr<FJsonObject> ResultObject = MakeShareable(new FJsonObject);
// 		// 构建 content 数组
// 		TArray<TSharedPtr<FJsonValue>> ContentArray;
// 		// 构建 content 对象
// 		TSharedPtr<FJsonObject> ContentObject = MakeShareable(new FJsonObject);
// 		// 构建 text 对象
// 		TSharedPtr<FJsonObject> TextObject = MakeShareable(new FJsonObject);
// 		TextObject->SetStringField("type", "text");
// 		TextObject->SetStringField("text", text);
// 		// 将 text 对象添加到 content 数组
// 		ContentArray.Add(MakeShareable(new FJsonValueObject(TextObject)));
// 		// 将 content 数组添加到 result 对象
// 		ResultObject->SetArrayField("content", ContentArray);
// 		// 设置 isError
// 		ResultObject->SetBoolField("isError", isError);
// 		// 将 result 添加到根对象
// 		RootObject->SetObjectField("result", ResultObject);
// 		// 序列化为字符串
// 		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonMessage);
// 		FJsonSerializer::Serialize(RootObject.ToSharedRef(), Writer);
// 		// 先剔除所有的换行符
// 		JsonMessage.ReplaceInline(TEXT("\n"), TEXT(""));
// 		JsonMessage.ReplaceInline(TEXT("\r"), TEXT(""));
// 		JsonMessage.ReplaceInline(TEXT("\t"), TEXT(""));
// 		//JsonMessage += "\n\n";
//
//         // 通��子系统发送消息
// 		MCPTransportSubsystem->SendSSE(SessionId, TEXT("message"), JsonMessage);
//
// 	}
// }

void UMCPToolHandle::ToolCallbackRaw(bool isError, const FString& text, bool bFinal, int32 Completed, int32 Total)
{
	if (!MCPTransportSubsystem || MCPid < 0 || SessionId == "none") return;

	FString JsonMessage;
	TSharedPtr<FJsonObject> Root = MakeShareable(new FJsonObject);
	Root->SetStringField("jsonrpc", "2.0");

	if (bFinal) {
		// == 原来的 result 路径 ==
		Root->SetNumberField("id", MCPid);
		TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
		TArray<TSharedPtr<FJsonValue>> Content;
		TSharedPtr<FJsonObject> TextObj = MakeShareable(new FJsonObject);
		TextObj->SetStringField("type", "text");
		TextObj->SetStringField("text", text);
		Content.Add(MakeShareable(new FJsonValueObject(TextObj)));
		Result->SetArrayField("content", Content);
		Result->SetBoolField("isError", isError);
		Root->SetObjectField("result", Result);
	} else {
		// == 进度通知（符合 MCP notifications/progress 规范）==
		Root->SetStringField("method", "notifications/progress");
		TSharedPtr<FJsonObject> Params = MakeShareable(new FJsonObject);
		if (!ProgressToken.IsEmpty())
		{
			Params->SetStringField("progressToken", ProgressToken);
		}
		// 按规范：顶层包含 progress、total（可选）与 message（可选）
		if (Completed >= 0) { Params->SetNumberField("progress", Completed); }
		if (Total >= 0)     { Params->SetNumberField("total", Total); }
		Params->SetStringField("message", text);
		Root->SetObjectField("params", Params);
	}

	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&JsonMessage);
	FJsonSerializer::Serialize(Root.ToSharedRef(), W);
	JsonMessage.ReplaceInline(TEXT("\n"), TEXT("")); JsonMessage.ReplaceInline(TEXT("\r"), TEXT("")); JsonMessage.ReplaceInline(TEXT("\t"), TEXT(""));

	MCPTransportSubsystem->SendSSE(SessionId, TEXT("message"), JsonMessage); 
}


void UMCPToolHandle::ToolCallback(bool isError, TSharedPtr<FJsonObject> json)
{
	// 触发工具回调
	if (MCPTransportSubsystem != nullptr && MCPid >= 0 && SessionId != "none") {
		// 构建通用部分
		FString JsonMessage;
		TSharedPtr<FJsonObject> RootObject = MakeShareable(new FJsonObject);
		// 设置基本字段
		RootObject->SetStringField("jsonrpc", "2.0");
		RootObject->SetNumberField("id", MCPid);
		// 构建 result 对象
		TSharedPtr<FJsonObject> ResultObject = MakeShareable(new FJsonObject);
		// 构建 content 数组
		TArray<TSharedPtr<FJsonValue>> ContentArray;
		// 构建 content 对象
		TSharedPtr<FJsonObject> ContentObject = MakeShareable(new FJsonObject);
		// 构建 text 对象
		TSharedPtr<FJsonObject> TextObject = MakeShareable(new FJsonObject);
		TextObject->SetStringField("type", "text");
		// 序列化json为字符串，要防止中文乱码
		FString JsonString;
		TSharedRef<TJsonWriter<>> Writer1 = TJsonWriterFactory<>::Create(&JsonString);
		FJsonSerializer::Serialize(json.ToSharedRef(), Writer1);
		TextObject->SetStringField("text", JsonString);
		// 将 text 对象添加到 content 数组
		ContentArray.Add(MakeShareable(new FJsonValueObject(TextObject)));
		// 将 content 数组添加到 result 对象
		ResultObject->SetArrayField("content", ContentArray);
		// 设置 json
		ResultObject->SetObjectField("structuredContent", json);
		// 设置 isError
		ResultObject->SetBoolField("isError", isError);
		// 将 result 添加到根对象
		RootObject->SetObjectField("result", ResultObject);
		// 序列化为字符串
		TSharedRef<TJsonWriter<>> Writer2 = TJsonWriterFactory<>::Create(&JsonMessage);
		FJsonSerializer::Serialize(RootObject.ToSharedRef(), Writer2);
		// 先剔除所有的换行符
		JsonMessage.ReplaceInline(TEXT("\n"), TEXT(""));
		JsonMessage.ReplaceInline(TEXT("\r"), TEXT(""));
		JsonMessage.ReplaceInline(TEXT("\t"), TEXT(""));
		//JsonMessage += "\n\n";

		// 通过子系统发送消息
		MCPTransportSubsystem->SendSSE(SessionId, TEXT("message"), JsonMessage);

	}
}
