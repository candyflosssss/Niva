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
// === MCP split headers ===
#include "MCP/MCPTypes.h"
#include "MCP/MCPToolProperty.h"
#include "MCP/MCPToolCore.h"
#include "MCP/MCPToolStorage.h"
#include "MCP/MCPToolHandle.h"
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

};

// MCP 相关类型与工具类已拆分到独立头文件，避免在此重复声明。


// 注：UMCPTransportSubsystem 类型及其成员已移动至 Public/MCP/MCPTransportSubsystem.h 中，
// 以功能分类形式进行整理，避免该头文件过于臃肿。

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
