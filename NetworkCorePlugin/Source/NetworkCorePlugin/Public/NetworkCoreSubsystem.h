// JasmineLatte

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "IHttpRouter.h"
#include "HttpServerModule.h"
#include "HttpServerResponse.h"
#include "HttpServerRequest.h"
#include "NivaNetworkCoreSettings.h"
#include "Kismet/BlueprintAsyncActionBase.h"
#include "Interfaces/IHttpRequest.h"
#include "HttpModule.h"
#include "Dom/JsonObject.h"
#include "Templates/SharedPointer.h"
#include "Interfaces/IHttpResponse.h"
#include "civetweb.h"
#include "HAL/Runnable.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Containers/Queue.h"
#include "HAL/RunnableThread.h"
#include "NetworkCoreSubsystem.generated.h"




DECLARE_DYNAMIC_DELEGATE_RetVal_OneParam(FNivaHttpResponse, FNetworkCoreHttpServerDelegate, FNivaHttpRequest, HttpServerRequest);


DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FBlueprintAsyncNodePinResult, int32, Result);



/**
 * @brief 用于管理网络操作的核心子系统。
 *
 * 该类作为应用程序中处理各种网络相关功能的基础组件。它负责初始化、
 * 管理和控制网络的核心方面。
 */


UCLASS()
class NETWORKCOREPLUGIN_API UNetworkCoreSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

	/**
	 * @brief 初始化必要的组件或资源。
	 *
	 * 此方法负责在系统或应用程序使用前设置和准备所需的组件或资源。
	 *
	 * @param config 初始化所需的配置对象或参数。
	 * @param mode 指示初始化发生的模式或环境。
	 */
public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	/**
	 * @brief 清理和释放与模块或系统关联的资源。
	 *
	 * 此方法负责执行必要的清理操作，如释放内存、关闭文件描述符或终止
	 * 任何正在进行的进程，以确保适当的资源管理和应用程序稳定性。
	 */
	virtual void Deinitialize() override;



// 发送请求的方法列表
private:
	/**
	 * @brief 表示HTTP请求对象。
	 *
	 * 该变量封装了与HTTP请求相关的详细信息和数据，包括请求头、
	 * 方法、URL和主体。它作为管理和访问HTTP请求信息的结构化方式。
	 */
	TSharedPtr<IHttpRequest> HttpRequest = FHttpModule::Get().CreateRequest();


	/**
	 * @brief 处理并获取HTTP请求对象。
	 *
	 * 此方法负责处理和返回HTTP请求。
	 * 它便于从传入流中提取请求详细信息。
	 *
	 * @return 表示HTTP请求的对象，包含所有必要的
	 *         请求头、主体和进一步处理所需的元数据。
	 */
public:
	TSharedPtr<IHttpRequest> getHttpRequest() {
		//if (HttpRequest.IsValid() == false)
		//{
		//	HttpRequest = FHttpModule::Get().CreateRequest();
		//}
		return HttpRequest;
	}

	/**
	 * @brief 设置HTTP请求的详细信息以进行处理。
	 *
	 * 此方法配置HTTP请求数据，包括请求头和其他相关信息，
	 * 以便在系统内进行进一步处理或处理。
	 *
	 * @param request 对HTTP请求对象的引用，包含相关的
	 *                请求参数和数据。
	 * @return 一个布尔值，指示请求是否成功设置。
	 *         成功返回true，否则返回false。
	 */
	bool setHttpRequest(TSharedPtr<IHttpRequest> _HttpRequest) {
		if (_HttpRequest.IsValid())
		{
			HttpRequest = _HttpRequest;
			return true;
		}
		return false;
	}


	// TTS 请求
//	UFUNCTION(BlueprintCallable)
//	static UNivaTTSRequest* CreateTTSRequest(
//		const FString& Message
//	);

public:
	/**
	 * @brief 表示在应用程序或系统中使用的配置或选项。
	 *
	 * 该变量旨在存储各种决定系统行为的设置或首选项。它包括
	 * 可配置的参数，如用户首选项、应用程序模式或特定环境的值。
	 * 确切的结构和内容取决于应用程序的需求。
	 */
	const UNivaNetworkCoreSettings* Settings;

	/**
	 * @brief 将给定对象转换为其字符串表示形式。
	 *
	 * 此方法接受输入对象并将其转换为字符串表示形式，
	 * 通过使用其`toString`或等效方法（如果可用），
	 * 或默认字符串格式。
	 *
	 * @param obj 要转换为字符串表示的对象。
	 * @return 表示输入对象的字符串。
	 */
	UFUNCTION(BlueprintCallable, Category = "NetworkCore|Tool")
	static FString UnitoString(FString uni)
	{
		FString DecodedUnicode;
		TArray<FString> UnicodeCodes;
		uni.ParseIntoArray(UnicodeCodes, TEXT("\\u"), true);

		for (auto& HexCode : UnicodeCodes)
		{
			if (HexCode.Len() > 0)
			{
				int32 CharacterCode = FCString::Strtoi(*HexCode, nullptr, 16);
				DecodedUnicode += FString::Chr(CharacterCode);
			}
		}

		return DecodedUnicode;
	}
// 接收服务器的方法列表
private:
	/**
	 * @brief 表示HTTP服务器的实例。
	 *
	 * 此变量用于管理和控制HTTP服务器的生命周期。
	 * 它提供对服务器特定配置和功能的访问。
	 */
	FHttpServerModule* HttpServerInstance;

	/**
	 * @brief 管理HTTP请求到其对应处理程序的路由。
	 *
	 * HttpRouter负责将传入的HTTP请求路径和方法映射
	 * 到适当的处理函数。它确保每个请求都根据定义的路由
	 * 被引导到正确配置的端点。
	 */
	TSharedPtr<IHttpRouter> HttpRouter;

	/**
	 * @brief 已创建的路由处理程序集合。
	 *
	 * 此变量存储与应用程序中特定路由关联的处理程序。
	 * 它用于有效地管理和组织路由处理程序。
	 */
	TArray<FHttpRouteHandle> CreatedRouteHandlers;

public:
    // UFUNCTION(BlueprintCallable, Category = "NetworkCore")
	/**
	 * @brief 将特定路由绑定到处理程序。
	 *
	 * 此方法用于将HTTP路由或端点绑定到相应的
	 * 处理函数，该函数处理给定路由的传入请求。
	 *
	 * @param route 指定要绑定的路由或URL模式的字符串。
	 * @param handler 一个将处理指定路由请求的函数。
	 */

	UFUNCTION(BlueprintCallable, Category = "NetworkCore")
	void BindRoute(FString path, ENivaHttpRequestVerbs HttpVerbs, FNetworkCoreHttpServerDelegate OnHttpServerRequest);

	/**
	 * @brief 处理传入的"Hello"请求。
	 *
	 * 此函数处理服务器收到的"Hello"请求，并
	 * 生成适当的响应发送回客户端。
	 *
	 * @param request 包含传入请求所有详细信息的请求对象。
	 * @param response 用于向客户端发送数据的响应对象。
	 */
	void HandleHelloRequest(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);

	/**
	 * @brief 为给定请求创建HTTP响应。
	 *
	 * 此方法根据输入参数生成并返回HTTP响应。
	 * 它负责根据请求数据和指定的响应设置构建
	 * 格式正确的响应，包括请求头和主体。
	 *
	 * @param request 包含客户端请求详细信息的HTTP请求对象。
	 * @param statusCode 要在响应中设置的HTTP状态码。
	 * @param headers 要包含在响应中的请求头集合。
	 * @param body 要作为响应主体发送的内容或有效负载。
	 * @return 准备发送给客户端的完全构建的HTTP响应对象。
	 */
	UFUNCTION(BlueprintCallable, Category = "NetworkCore")
	static FNivaHttpResponse MakeResponse(FString Text, FString ContentType = "application/json", int32 Code = 200);

	/**
	 * @brief 指示进程或操作的启动状态。
	 *
	 * 该变量持有一个布尔值，用于确定特定的进程
	 * 或操作是否已经启动。它通常用作状态指示器。
	 */
	UPROPERTY(BlueprintReadOnly, Category = "NetworkCore")
	bool IsStarted = false;

	/**
	 * @brief 检查音频设备状态的函数。
	 *
	 * 此函数用于验证音频设备的当前状态、可用性或属性，
	 * 以确保它符合所需规格或可运行。
	 * 它执行必要的检查并根据结果返回*/
	UFUNCTION(Blueprintcallable, Category = "NetworkCore")
	static void CheckAudioDevice();


// 主动请求
	//UFUNCTION(BlueprintCallable, Category = "NetworkCore")
	//void SendHttpRequest(
	//	FNivaHttpRequest HttpRequest,
	//	FCompleteDelegate CompleteDelegate
	//);




};


/**
 * @brief 表示FMCP框架内的请求。
 *
 * 此类封装了与FMCP系统中的请求相关的属性和行为。它管理
 * 处理特定类型的FMCP交易或操作所需的数据和方法，并
 * 确保与相关协议的兼容性。
 */
USTRUCT()
struct FMCPRequest
{
	GENERATED_BODY()
public:
	/**
	 * @brief 表示JSON对象。
	 *
	 * 此变量用于存储和操作JSON格式的结构化数据。它作为
	 * 键值对的容器，其中键是字符串，值可以是字符串、数字、对象、
	 * 数组或其他支持的JSON类型。
	 */
	UPROPERTY()
	FString Json;
};

/**
 * @brief 表示可能的JSON数据类型的枚举。
 *
 * 此枚举定义了可以在JSON结构中表示的各种数据类型，
 * 如对象、数组、字符串、数字、布尔值和null。
 */
UENUM(BlueprintType)
enum class EMCPJsonType : uint8
{

	/*暂不支持*/
    None = 0,
    String = 1,
    Number = 2,
	Integer = 3,
	Boolean = 4,
	Object = 5,
	Array = 6

};

/*
 * 
 */
UCLASS(BlueprintType)
class NETWORKCOREPLUGIN_API UMCPToolProperty : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintReadWrite, Category = "NetworkCore|MCP|Tool")
	FString Name;

	UPROPERTY(BlueprintReadWrite, Category = "NetworkCore|MCP|Tool")
	EMCPJsonType Type;
	
	UPROPERTY(BlueprintReadWrite, Category = "NetworkCore|MCP|Tool")
	FString Description;

	//构造函数，构造一个string类型的参数
	UMCPToolProperty(FString InName, EMCPJsonType InType, FString InDescription)
		: Name(InName)
		, Type(InType)
		, Description(InDescription)
	{
	}
	
	//构造函数，构造一个string类型的参数
	UMCPToolProperty()
		: Name(TEXT(""))
		, Type(EMCPJsonType::None)
		, Description(TEXT(""))
	{
	}
	// 根据属性返回一个jsonObject，虚函数以供子类重载
	virtual TSharedPtr<FJsonObject> GetJsonObject()
	{
		return nullptr;
	}

	// 获取可用对象
	virtual TArray<FString> GetAvailableTargets()
	{
		return TArray<FString>();
	}

	
};

//继承UMCPToolProperty，并支持string类型的参数
UCLASS(BlueprintType)
class NETWORKCOREPLUGIN_API UMCPToolPropertyString : public UMCPToolProperty
{
	GENERATED_BODY()
public:
	//创建一个本类的实例
	UFUNCTION(BlueprintCallable,BlueprintPure, Category = "NetworkCore|MCP|Tool")
	static UMCPToolProperty* CreateStringProperty(FString InName, FString InDescription);

	//重载虚函数，返回一个jsonObject
	virtual TSharedPtr<FJsonObject> GetJsonObject() override;

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "NetworkCore|MCP|Tool")
	FString GetValue(FString InJson);
};


//继承UMCPToolProperty，并支持Num类型的参数
UCLASS(BlueprintType)
class NETWORKCOREPLUGIN_API UMCPToolPropertyNumber : public UMCPToolProperty
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintReadWrite, Category = "NetworkCore|MCP|Tool")
	int Min;
	UPROPERTY(BlueprintReadWrite, Category = "NetworkCore|MCP|Tool")
	int Max;
	UPROPERTY(BlueprintReadWrite, Category = "NetworkCore|MCP|Tool")
	int Default;
	

	
	
	//创建一个本类的实例
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "NetworkCore|MCP|Tool", meta=(AdvancedDisplay=" InMin, InMax"))
	static UMCPToolProperty* CreateNumberProperty(FString InName,FString InDescription, int InMin = -99999, int InMax = 99999);
	
	//重载虚函数，返回一个jsonObject
	virtual TSharedPtr<FJsonObject> GetJsonObject() override;

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "NetworkCore|MCP|Tool")
	float GetValue(FString InJson);
};

//继承UMCPToolProperty，并支持int类型的参数
UCLASS(BlueprintType)
class NETWORKCOREPLUGIN_API UMCPToolPropertyInt : public UMCPToolProperty
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintReadWrite, Category = "NetworkCore|MCP|Tool")
	int Min;
	UPROPERTY(BlueprintReadWrite, Category = "NetworkCore|MCP|Tool")
	int Max;
	UPROPERTY(BlueprintReadWrite, Category = "NetworkCore|MCP|Tool")
	int Default;
	

	
	
	//创建一个本类的实例
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "NetworkCore|MCP|Tool", meta=(AdvancedDisplay=" InMin, InMax"))
	static UMCPToolProperty* CreateIntProperty(FString InName,FString InDescription, int InMin = -99999, int InMax = 99999);
	
	//重载虚函数，返回一个jsonObject
	virtual TSharedPtr<FJsonObject> GetJsonObject() override;

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "NetworkCore|MCP|Tool")
	int GetValue(FString InJson);
};

//继承UMCPToolProperty，并支持UE对象引用特殊类型的参数
UCLASS(BlueprintType)
class NETWORKCOREPLUGIN_API UMCPToolPropertyActorPtr: public UMCPToolProperty
{
	GENERATED_BODY()

public:
	// 所需的静态类
	UPROPERTY(BlueprintReadOnly, Category = "NetworkCore|MCP|Tool")
	TSubclassOf<AActor> ActorClass;

	// 根据静态类，查找场景中所有符合条件的对象
	// 搜索并保存、返回对象引用，以便在后续使用时快速查找
	UFUNCTION(BlueprintCallable, Category = "NetworkCore|MCP|Tool")
	TArray<AActor*> FindActors();

	// 存储对象引用图表,用于快速查找，每次FindActor时，都需要重新构建图表
	TMap<FString, TWeakObjectPtr<AActor>> ActorMap;

	//创建一个本类的实例
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "NetworkCore|MCP|Tool")
	static UMCPToolProperty* CreateActorPtrProperty(FString InName, FString InDescription, TSubclassOf<AActor> InActorClass);
	

	
	
	//重载虚函数，返回一个jsonObject
	virtual TSharedPtr<FJsonObject> GetJsonObject() override;

	// 获取有效对象
	virtual TArray<FString> GetAvailableTargets() override;

	UFUNCTION(BlueprintCallable)
	AActor* GetActor(FString InName);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "NetworkCore|MCP|Tool")
	AActor* GetValue(FString InJson);
};


//继承UMCPToolProperty，并支持数组类型的参数
UCLASS(BlueprintType)
class NETWORKCOREPLUGIN_API UMCPToolPropertyArray: public UMCPToolProperty
{
	GENERATED_BODY()
public:
	UPROPERTY(BlueprintReadOnly, Category = "NetworkCore|MCP|Tool")
	UMCPToolProperty* Property;

	//创建一个本类的实例
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "NetworkCore|MCP|Tool")
	static UMCPToolProperty* CreateArrayProperty(FString InName, FString InDescription, UMCPToolProperty* InProperty);

	//重载虚函数，返回一个jsonObject
	virtual TSharedPtr<FJsonObject> GetJsonObject() override;

	
	
};
/**
 * @class FMCPTool
 * @brief 提供一组用于FMCP相关操作的实用工具和方法。
 *
 * FMCPTool旨在简化在处理和管理FMCP数据和资源中使用的
 * 各種功能和工具。该类封装了所有必要的逻辑以便于
 * FMCP特定任务，确保模块化和可重用性*/
USTRUCT(BlueprintType)
struct FMCPTool

{

	GENERATED_BODY()

public:
	/**
	 * @brief A variable to store a name or identifier.
	 *
	 * This variable is used to hold a name value, typically represented
	 * as a string or character array, and may be used for identification
	 * purposes within the application.
	 */
	UPROPERTY(BlueprintReadWrite, Category = "NetworkCore|MCP|Tool")
	FString Name;
	/**
	 * @brief Brief description of the variable.
	 *
	 * Detailed description providing additional context or usage information
	 * about this variable.
	 */
	UPROPERTY(BlueprintReadWrite, Category = "NetworkCore|MCP|Tool")
	FString Description;

	/**
	 * @brief 包含配置属性的字符串。
	 *
	 * 此变量存储配置设置的键值对。
	 * 它用于管理和检索可配置的属性。
	 */
	UPROPERTY(BlueprintReadWrite, Category = "NetworkCore|MCP|Tool")
	TArray<UMCPToolProperty*> Properties;

};


FORCEINLINE bool operator==(const FMCPTool& A, const FMCPTool& B)
{
	return A.Name == B.Name;
}

FORCEINLINE uint32 GetTypeHash(const FMCPTool& Tool)
{
	return GetTypeHash(Tool.Name);
}

DECLARE_DYNAMIC_DELEGATE_ThreeParams(FMCPRouteDelegate, const FString&, Result, UMCPToolHandle*, MCPToolHandle, const FMCPTool&, MCPTool);

UCLASS()
class NETWORKCOREPLUGIN_API UMCPToolBlueprintLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	// === 通配符版本的 GetValue ===
	UFUNCTION(BlueprintCallable, Category = "MCP Tool")
	static bool GetIntValue(const FMCPTool& MCPTool, const FString& Name, const FString& InJson,int32& OutValue);
	// string
	UFUNCTION(BlueprintCallable, Category = "MCP Tool")
	static bool GetStringValue(const FMCPTool& MCPTool, const FString& Name, const FString& InJson, FString& OutValue);
	// number
	UFUNCTION(BlueprintCallable, Category = "MCP Tool")
	static bool GetNumberValue(const FMCPTool& MCPTool, const FString& Name, const FString& InJson, float& OutValue);
	// actor
	UFUNCTION(BlueprintCallable, Category = "MCP Tool")
	static bool GetActorValue(const FMCPTool& MCPTool, const FString& Name, const FString& InJson, AActor*& OutValue);

	// 向 FMCPTool 中追加一个参数属性（蓝图辅助）
	UFUNCTION(BlueprintCallable, Category = "MCP Tool")
	static void AddProperty(UPARAM(ref) FMCPTool& MCPTool, UMCPToolProperty* Property);

	// 第一步，先取到特定的property
	static UMCPToolProperty* GetProperty(const FMCPTool& MCPTool, const FString& Name);

};

/**
 * @brief 负责管理FMCP系统内工具存储的类。
 *
 * 此类提供接口和机制来管理、检索和存储
 * 工具相关资源。它确保系统中工具存储操作的
 * 高效和安全处理。
 */
USTRUCT(BlueprintType)
struct FMCPToolStorage

{

	GENERATED_BODY()

public:
	/**
	 * @brief 表示用于管理MCP相关操作的工具。
	 *
	 * 此变量用于处理与MCP（模块化控制平台）相关的特定功能或任务。
	 * 它可用于与系统内的MCP组件进行交互、配置或控制。
	 */
	UPROPERTY(BlueprintReadWrite, Category = "NetworkCore|MCP|Tool")
	FMCPTool MCPTool;

	/**
	 * @brief 表示系统或应用程序中的工具数量。
	 *
	 * 此变量用于存储和管理系统特定上下文中可用或
	 * 正在跟踪的工具数量。它通常在添加或删除工具时更新。
	 */
	UPROPERTY(BlueprintReadWrite, Category = "NetworkCore|MCP|Tool")
	int ToolNum = 0;

	/**
	 * @brief 用于管理工具的存储容器。
	 *
	 * 此变量用于存储工具的集合，以便在系统中进行管理。
	 * 它通常用于存储工具的元数据或配置信息。
	 */
	UPROPERTY(BlueprintReadWrite, Category = "NetworkCore|MCP|Tool")
	TArray<FMCPRouteDelegate> RouteDelegates;

};


/**
 * @brief 表示基于UMCP的工具的句柄。
 *
 * 此类作为资源管理器或包装器，用于与
 * UMCP（统一模块化控制平台）工具功能交互，提供
 * 一个接口以有效管理工具特定的操作。
 */
UCLASS(BlueprintType)  
class NETWORKCOREPLUGIN_API UMCPToolHandle : public UObject  
{  
   GENERATED_BODY()  

public:
   /**
    * @brief MCP进程的唯一标识符。
    *
    * 此变量表示与MCP（主控制程序）相关联的进程ID。
    * 它用于跟踪和管理MCP进程的生命周期。
    */
	UPROPERTY(BlueprintReadOnly, Category = "NetworkCore|MCP|Tool")
	int MCPid = -1;

   /**
    * @brief 用户会话的唯一标识符。
    *
    * 此变量用于唯一标识和跟踪用户在应用程序中的会话。
    * 它便于会话管理并确保用户交互过程中的一致状态。
    */
	UPROPERTY(BlueprintReadOnly, Category = "NetworkCore|MCP|Tool")
	FString SessionId = "none";

   /**
    * @brief 表示MCP（消息通信协议）的传输子系统。
    *
    * 此变量负责管理MCP基础设施内的传输级操作。
    * 它协调通信并确保协议所需的传输机制得到适当处理。
    */
	UPROPERTY(BlueprintReadOnly, Category = "NetworkCore|MCP|Tool")
	UMCPTransportSubsystem* MCPTransportSubsystem = nullptr;  

public:
   /**
    * @brief UMCP工具系统的句柄。
    *
    * 此变量表示在UMCP工具系统中使用的特定句柄，
    * 用于管理和交互其范围内的资源或组件。
    *
    * @return UMCP工具句柄的指针或引用。
    */
   UMCPToolHandle() : MCPid(-1), SessionId("none"), MCPTransportSubsystem(nullptr) {}

   /**
    * @brief 初始化工具句柄以供使用。
    *
    * 此函数设置并初始化提供的工具句柄，
    * 为系统内的进一步操作或交互做准备。
    *
    * @return 返回指示成功或失败的状态码。非零值
    * 通常表示初始化期间发生了错误。
    */
   static UMCPToolHandle* initToolHandle(int id, const FString& _SessionID, UMCPTransportSubsystem* _subsystem);

public:
   /**
    * @brief 用于处理工具特定操作的回调函数。
    *
    * 此函数指针作为自定义工具操作的入口点。
    * 它通常被分配来调用特定于某个工具或进程的功能。
    *
    */
	UFUNCTION(BlueprintCallable, Category = "NetworkCore|MCP", meta = (HidePin = "json"))
	void ToolCallback(bool isError, /*处理结果*/FString text);

	void ToolCallback(bool isError, TSharedPtr<FJsonObject> json);
};


/**
 * @brief 表示UMCP的传输子系统。
 *
 * 此类负责管理UMCP系统的传输层，
 * 处理通信和与传输相关的功能。它提供
 * 必要的方法以促进数据交换并确保UMCP架构内
 * 的可靠操作。
 */
UCLASS()
class NETWORKCOREPLUGIN_API UMCPTransportSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	/**
	 * @brief 初始化指定的上下文或系统。
	 *
	 * 此方法通过分配资源或根据需要设置初始配置，
	 * 为使用做好必要的准备。
	 *
	 */
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	/**
	 * @brief 清理并释放组件使用的资源。
	 *
	 * 此方法释放任何资源并执行必要的清理，
	 * 以确保组件操作的正确终止。当不再需要组件时，
	 * 应调用此方法，以避免资源泄漏或意外行为。
	 */
	virtual void Deinitialize() override;

	/**
	 * @brief 确定是否应创建子系统。
	 *
	 * 此方法评估必要条件以决定是否应实例化子系统。
	 *
	 * @return 如果应创建子系统则返回true，否则返回false。
	 */
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;

	/**
	 * @brief 向连接的客户端发送服务器发送事件(SSE)。
	 *
	 * 此方法用于向已建立持久连接的一个或多个
	 * 连接客户端传输服务器发送的事件。
	 *
	 */
	void SendSSE(const FString& SessionId, const FString& Event, const FString& Data);

	/**
	 * @brief 处理服务器接收到的HTTP POST请求。
	 *
	 * 此方法处理传入的HTTP POST请求，根据请求数据执行
	 * 必要的操作，并生成适当的响应。
	 *
	 * @param Request 包含数据和请求头的HTTP请求对象。
	 */
	void HandlePostRequest(const FMCPRequest& Request, const FString& SessionId);

	/**
	 * @brief 启动MCP（模块化通信协议）服务器。
	 *
	 * 此方法初始化并启动MCP服务器，使其能够处理
	 * 传入的通信请求。它设置必要的资源和
	 */
	UFUNCTION(BlueprintCallable, Category = "NetworkCore|MCP")
	void StartMCPServer();

// MCP基础工具
private:
	/**
	 * @brief 指示系统是否正在关闭过程中。
	 *
	 * 此变量用作信号正在进行的关闭操作的标志。
	 * 它有助于协调清理任务或在关闭序列期间防止进一步处理。
	 */
	bool bIsShuttingDown = false;

	/**
	 * @brief 用于管理会话并发的互斥锁或锁对象。
	 *
	 * 此变量通过防止来自多个线程的同时修改，
	 * 确保对会话相关操作的访问是线程安全的。
	 */
	FCriticalSection SessionLock;
	/**
	 * @brief 指向CivetWeb服务器上下文的指针。
	 *
	 * 此变量保存CivetWeb库的服务器上下文对象。
	 * 默认情况下它被初始化为`nullptr`。上下文用于管理
	 * CivetWeb服务器的生命周期和配置。
	 *
	 * `mg_context`结构在CivetWeb库中定义，并提供
	 * 处理服务器运行状态、路由和连接会话的功能。
	 *
	 * 使用此变量需要正确的初始化和去初始化，
	 * 以确保服务器正确运行并在关闭后释放资源。
	 */
	struct mg_context* ServerContext = nullptr;

	/**
	 * @brief 表示应用程序内的活动用户会话。
	 *
	 * 此变量用于管理和跟踪活动会话的状态，
	 * 包括它们的生命周期和关联数据。通常用于
	 * 维护用户认证和会话特定的信息。
	 */
	TMap<FString, TSharedPtr<TQueue<FString>>> Sessions;

	/**
	 * @brief 处理发布消息的处理。
	 *
	 * 此方法负责在收到发布消息时管理操作。
	 * 它处理消息并根据应用程序逻辑执行必要的操作。
	 *
	 * @return 一个布尔值，指示处理是否成功。
	 */
	static int OnPostMessage(struct mg_connection* Connection, void* UserData);
	/**
	 * @brief 处理服务器发送事件(SSE)通信。
	 *
	 * 此方法管理服务器发送事件连接的生命周期和事件，
	 * 允许与客户端进行实时通信。
	 *
	 * @return 指示SSE操作成功或失败的状态码。
	 */
	static int OnSSE(struct mg_connection* Connection, void* UserData);

	/**
	 * @brief 生成唯一的会话标识符。
	 *
	 * 此方法创建并返回唯一的会话ID字符串。
	 * 它通常用于跟踪用户会话或识别特定交互。
	 *
	 * @return 表示新生成的会话ID的字符串。
	 */
	FString GenerateSessionId() const;
	
public:
	/**
	 * @brief 解析JSON-RPC请求并提取相关信息。
	 *
	 * 此方法处理JSON-RPC请求字符串，根据JSON-RPC规范验证其结构，
	 * 并提取方法、参数和其他必要信息以进行进一步处理。
	 *
	 */
	static void ParseJsonRPC(const FString& JsonString, FString& Method, TSharedPtr<FJsonObject>& Params, int& ID, TSharedPtr<FJsonObject>& JsonObject);


	/**
	 * @brief 用于管理多个社区属性工具的实用对象。
	 *
	 * 此变量作为访问与社区属性(MCP)操作管理相关的各种工具
	 * 和功能的容器或入口点。它聚合共享的方法和实用工具，
	 * 以便于在MCP框架内进行操作。
	 */
	UPROPERTY()
	TMap<FString, FMCPToolStorage> MCPTools;
	
	/**
	 * @brief 在系统中注册工具属性。
	 *
	 * 此方法用于注册和配置应用程序或系统中使用的工具的属性。
	 *
	 * @param toolName 要注册的工具名称。
	 * @param properties 与工具关联的属性集。
	 * @param overwrite 指示如果工具已经注册，是否应覆盖现有属性。
	 */
	UFUNCTION(BlueprintCallable, Category = "NetworkCore")
	void RegisterToolProperties(FMCPTool tool, FMCPRouteDelegate MCPRouteDelegate);


	/**
	 * @brief 检索指定工具的属性。
	 *
	 * 此方法获取与给定工具相关的各种属性，如配置、设置或元数据。
	 *
	 *
	 * @return 包含指定工具属性的结构或对象。
	 */
	TSharedPtr<FJsonObject> GetToolbyTarget(FString ActorName);
	UFUNCTION(BlueprintCallable, Category = "NetworkCore")
	void OnToolRouteCallback(const FString& Result, UMCPToolHandle* MCPToolHandle, const FMCPTool& MCPTool)
	{
		// 将Result解析为json对象
		TSharedPtr<FJsonObject> JsonObject = MakeShareable(new FJsonObject());
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Result);
		if (FJsonSerializer::Deserialize(Reader, JsonObject) )
		{
			// 逐级获取目标名称
			TSharedPtr<FJsonObject> params = JsonObject->GetObjectField(TEXT("params"));
			TSharedPtr<FJsonObject> arguments = params->GetObjectField(TEXT("arguments"));
			const FString TargetName = arguments->GetStringField(TEXT("ObjectName"));
			// 调用工具
			MCPToolHandle->ToolCallback(false, GetToolbyTarget(TargetName));
		}
		else
		{
			MCPToolHandle->ToolCallback(true, TEXT("解析json失败"));
		}
		
		
	}



	/**
	 * @brief 检索与特定工具关联的目标。
	 *
	 * 此方法用于收集系统中与特定工具相关的所有目标。
	 * 它可用于与工具功能相关的各种处理、评估或管理目的。
	 *
	 * @return 与指定工具关联的目标列表。
	 */
	TSharedPtr<FJsonObject> GetToolTargets(FString ToolName);
	UFUNCTION(BlueprintCallable, Category = "NetworkCore")
	void OnToolTargetsCallback(const FString& Result, UMCPToolHandle* MCPToolHandle, const FMCPTool& MCPTool)
	{
		// 将Result解析为json对象
		TSharedPtr<FJsonObject> JsonObject = MakeShareable(new FJsonObject());
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Result);
		if (FJsonSerializer::Deserialize(Reader, JsonObject) )
		{
			// 逐级获取目标名称
			TSharedPtr<FJsonObject> params = JsonObject->GetObjectField(TEXT("params"));
			TSharedPtr<FJsonObject> arguments = params->GetObjectField(TEXT("arguments"));
			const FString TargetName = arguments->GetStringField(TEXT("ToolName"));
			// 调用工具
			MCPToolHandle->ToolCallback(false, GetToolTargets(TargetName));
		}
		else
		{
			MCPToolHandle->ToolCallback(true, TEXT("解析json失败"));
		}
	}
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnRefreshMCPComplete, bool, bSuccess, const FString&, Message);

UCLASS()
class NETWORKCOREPLUGIN_API URefreshMCPClientAsyncAction : public UBlueprintAsyncActionBase
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "MCP", meta = (BlueprintInternalUseOnly = "true", HidePin = "WorldContextObject"))
	static URefreshMCPClientAsyncAction* RefreshMCPClient(UObject* WorldContextObject);

	virtual void Activate() override;

	UPROPERTY(BlueprintAssignable)
	FOnRefreshMCPComplete OnSuccess;

	UPROPERTY(BlueprintAssignable)
	FOnRefreshMCPComplete OnFailure;

private:
	void HandleRequestComplete(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bSuccess);
};
