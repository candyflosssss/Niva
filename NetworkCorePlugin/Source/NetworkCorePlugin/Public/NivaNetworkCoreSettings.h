// 在描述页面中填写版权声明。

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "HttpServerResponse.h"
#include "HttpServerRequest.h"
#include "NivaNetworkCoreSettings.generated.h"


/**
 * 定义在Niva系统中用于大语言模型(LLM)的模型枚举。
 * 该枚举主要用于在网络设置或LLM请求等场景中指定要使用的LLM模型类型。
 */
UENUM(BlueprintType) 
enum class ENivaLLMModel : uint8
{
  LLM_NONE = 0 UMETA(DisplayName = "None"),
  LLM_qwen2_5_latest = 1 UMETA(DisplayName = "qwen2.5:latest"), 
  LLM_deepseek_r1_8b = 2 UMETA(DisplayName = "deepseek-r1:8b"),
  LLM_qwq_latest = 3 UMETA(DisplayName = "qwq:latest")
};

/**
 * 表示各种可选的阿里云大语言模型(LLM)的枚举。
 * 该枚举提供了系统中可使用的不同阿里云模型选项。
 */
UENUM(BlueprintType)
enum class ENivaAliyunModel : uint8
{
  Aliyun_NONE = 0 UMETA(DisplayName = "None"),
  Aliyun_qwq_plus = 1 UMETA(DisplayName = "qwq-plus"),
  Aliyun_qwen_max = 2 UMETA(DisplayName = "qwen-max"), 
  Aliyun_qwen_plus = 3 UMETA(DisplayName = "qwen-plus"),
  Aliyun_qwen_turbo = 4 UMETA(DisplayName = "qwen-turbo"),
  Aliyun_qwen_long = 5 UMETA(DisplayName = "qwen-long"),
  Aliyun_qwen_omni_turbo = 6 UMETA(DisplayName = "qwen-omni-turbo")
};

/**
 * 表示各种大语言模型(LLM)提供商的枚举。
 */
UENUM(BlueprintType)  
enum class ENivaLLM : uint8
{
  LLM_NONE = 0 UMETA(DisplayName = "None"),
  LLM_OLLAMA = 1 UMETA(DisplayName = "Ollama"), 
  LLM_ALIYUN = 2 UMETA(DisplayName = "Aliyun"),
  LLM_NIVA_AGENT = 3 UMETA(DisplayName = "NivaAgent"),
  LLM_RUNNER = 4 UMETA(DisplayName = "Runner")
};

/**
 * 用于定义系统中不同文本转语音(TTS)模型的枚举。
 * 该枚举用于在配置或运行时操作中指定TTS请求的类型。
 * 每个成员都有对应的有意义的标识名称。
 */
UENUM(BlueprintType)
enum class ENivaTTSModel : uint8
{
  TTS_Default = 0 UMETA(DisplayName = "Default"),
  TTS_Fish = 1 UMETA(DisplayName = "Fish"), 
  TTS_Melotte = 2 UMETA(DisplayName = "Melotte"),
  TTS_Aliyun = 3 UMETA(DisplayName = "Aliyun")
};

/**
 * 表示HTTP请求动词的枚举，用于定义符合HTTP规范的交互方法。
 *
 * 该枚举支持按位操作以组合多个动词。
 * 它用于网络相关功能,如指定HTTP请求/路由的动词。
 */
UENUM(BlueprintType)
enum class ENivaHttpRequestVerbs : uint8
{
  NONE = 0,
  GET = 1 << 0,
  POST = 1 << 1,
  PUT = 1 << 2,
  PATCH = 1 << 3,
  DELETE = 1 << 4,
  OPTIONS = 1 << 5
};

/**
 * 表示Niva网络框架中的HTTP响应。
 * 该结构体主要封装了一个FHttpServerResponse对象,
 * 用于定义HTTP服务器响应的主体、标头、HTTP版本和状态码。
 *
 * 它通常用于处理HTTP服务器请求并发送适当的响应。
 */
USTRUCT(BlueprintType)
struct FNivaHttpResponse
{
  GENERATED_BODY()

  /***/
  /**
   * 表示将在HTTP服务器请求处理流程中使用的HTTP响应。
   * 这包括HTTP响应的属性,如主体、标头、状态码和HTTP版本。
   * 它封装了用于响应HTTP服务器请求的数据。
   */
public: 
  FHttpServerResponse HttpServerResponse;
};

/**
 * 在FNiva框架中表示HTTP请求。
 */
USTRUCT(BlueprintType)
struct FNivaHttpRequest
{
  GENERATED_BODY()

public:
  /**
   * FNivaHttpRequest结构体的默认构造函数。
   * 
   * 该构造函数使用默认值初始化FNivaHttpRequest的实例。
   */
  FNivaHttpRequest() {};
  /**
   * 使用提供的FHttpServerRequest数据构造一个FNivaHttpRequest实例。
   *
   * 该构造函数从FHttpServerRequest对象中提取并初始化FNivaHttpRequest的属性,
   * 如HTTP动词、相对路径、标头、查询参数、路径参数、主体内容和原始主体字节。
   *
   * @param Request 包含HTTP请求信息的传入FHttpServerRequest对象。
   */
  FNivaHttpRequest(const FHttpServerRequest& Request);


  /**
   * HTTP请求的相对路径。
   *
   * 该变量存储包含在HTTP请求中的URL路径的相对部分。
   * 它从传入的HTTP服务器请求中提取,并为开发者提供
   * 客户端请求的特定路径,不包括域名和查询参数。
   *
   * 示例:对于URL http://example.com/api/test,RelativePath将包含 /api/test。
   *
   * 该属性标记为BlueprintReadOnly,允许在蓝图中访问
   * 但不可修改,并在"NativeHttpServerRequest"类别下分类。
   */
  UPROPERTY(BlueprintReadOnly, Category = "NativeHttpServerRequest")
  FString RelativePath;

  /**
   * 表示请求的HTTP兼容动词。
   *
   * 该属性的类型为ENivaHttpRequestVerbs,枚举了
   * 各种HTTP请求方法,如GET、POST、PUT、PATCH、DELETE、
   * OPTIONS和NONE(默认)。它用于指定与FNivaHttpRequest结构处理的
   * HTTP请求相关联的动词。
   *
   * 动词默认初始化为ENivaHttpRequestVerbs::NONE。
   */
  UPROPERTY(BlueprintReadOnly, Category = "Verb")
  /** HTTP兼容动词 */
  ENivaHttpRequestVerbs Verb = ENivaHttpRequestVerbs::NONE;

  /**
   * 表示一个请求的HTTP标头。
   *
   * Headers映射包含标头键值对,其中:
   * - 每个键是标头名称(如"Content-Type"、"Authorization")。 
   * - 每个值是对应的标头值。
   *
   * 在基于HTTP的通信中很常见,该属性跟踪
   * 客户端发送的关于元数据的所有请求标头
   * (例如编码、cookies、授权)或额外的请求上下文。
   */
  UPROPERTY(BlueprintReadOnly, Category = "Header")
  /** HTTP标头 */
  TMap<FString, FString> Headers;

  /**
   * 表示来自HTTP请求的查询参数的映射。
   * 每个条目由一个键值对组成,其中键和值
   * 都是字符串。查询参数通常附加在URL后面的'?'字符后,
   * 用于在HTTP请求中传递额外的数据。
   */
  UPROPERTY(BlueprintReadOnly, Category = "Params")
  /** 查询参数 */
  TMap<FString, FString> QueryParams;

  /**
   * 存储路径参数的映射,其中键表示参数名称,
   * 值表示相应的参数值。路径参数
   * 用于捕获HTTP请求中URL路径的动态部分。
   *
   * 示例场景:
   * 如果请求路径是"/users/{userId}/posts/{postId}",当提供这些值时,
   * PathParams映射将存储{"userId": "123", "postId": "456"}。
   * 
   * 该成员通常用于基于URL结构进行路由或
   * 识别特定资源。
   */
  UPROPERTY(BlueprintReadOnly, Category = "Params") 
  /** 路径参数 */
  TMap<FString, FString> PathParams;

  /**
   * 表示HTTP请求的原始主体内容。
   * 该变量将主体保存为字符串,可能包括
   * 在POST或PUT操作的负载中发送的数据。
   * 它从传入的HTTP服务器请求中解析并存储。
   */
  UPROPERTY(BlueprintReadOnly, Category = "NativeHttpServerRequest")
  /** 原始主体内容 */
  FString Body;

  /**
   * 保存HTTP请求主体的原始字节内容。
   * 该数组包含作为HTTP请求一部分接收的二进制数据,
   * 允许处理或处理非文本内容,如二进制
   * 文件或编码数据。
   */
  UPROPERTY(BlueprintReadOnly, Category = "NativeHttpServerRequest")
  /** 原始字节内容 */  
  TArray<uint8> BodyBytes;


};


/**
 * 用于配置和管理应用程序内网络相关功能的核心设置。
 */
UCLASS(config = Game, meta = (DisplayName = "Niva Network Core Settings"), defaultconfig)
class NETWORKCOREPLUGIN_API UNivaNetworkCoreSettings : public UDeveloperSettings
{
  GENERATED_BODY()


public:
  /**
   * 指定网络服务器将运行的端口号。
   * 该值可在1024到65535范围内配置。
   * 默认设置为9090。
   *
   * Config:表示该值从INI配置文件中读取。
   * EditAnywhere:允许在编辑器中编辑该属性。
   * Category:将该属性分组到"Network"类别下。
   * ClampMin:允许的最小值为1024。
   * ClampMax:允许的最大值为65535。
   */
  UPROPERTY(Config, EditAnywhere, Category = "Network", meta = (ClampMin = 1024, ClampMax = 65535))
  int Port = 9090;


  UPROPERTY(Config, EditAnywhere, Category = "Network", meta = (ClampMin = 1024, ClampMax = 65535))
  int MCPPort = 9091;


  UPROPERTY(Config, EditAnywhere, Category = "TurnGrid")
  TArray<FString> LocationDescriptions = {};


  UPROPERTY(Config, EditAnywhere, Category = "Network|LLM")
  ENivaLLM LLM = ENivaLLM::LLM_NONE;



  UPROPERTY(Config, EditAnywhere, Category = "Network|LLM")
  bool ShouldPrompt = false;
  

  UPROPERTY(Config, EditAnywhere, Category = "Network|LLM", meta = (EditCondition = "ShouldPrompt", EditConditionHides))
  FString LLMPrompt = "do nothing";


  UPROPERTY(Config, EditAnywhere, Category = "Network|LLM|Ollama")
  FString LLMOllamaURL = "http://192.168.1.236:11434/api/chat";


  UPROPERTY(Config, EditAnywhere, Category = "Network|LLM|Ollama")
  ENivaLLMModel LLMModel = ENivaLLMModel::LLM_qwen2_5_latest;


  UPROPERTY(Config, EditAnywhere, Category = "Network|LLM|Aliyun")
  FString LLMAliyunURL = "https://dashscope.aliyuncs.com/compatible-mode/v1/chat/completions";


  UPROPERTY(Config, EditAnywhere, Category = "Network|LLM|Aliyun")
  ENivaAliyunModel LLMAliyunModel = ENivaAliyunModel::Aliyun_qwen_plus;


  UPROPERTY(Config, EditAnywhere, Category = "Network|LLM|Aliyun")
  FString LLMAliyunAccessKey = "sk-afd8013e2f53479c886d2ce8b17990a8";

  UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Network|LLM|NivaAgent")
  FString AgentChatURL = TEXT("http://localhost:8081/api/agent/chat");
    
  UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Network|LLM|NivaAgent")
  FString DefaultAgentID = TEXT("test-agent-id");

  // MCP 服务基础 URL（不要包含具体端点），用于拼接各接口
  UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Network|LLM|NivaAgent")
  FString MCPBaseURL = TEXT("http://localhost:8081");

  // Runner LLM 配置
  UPROPERTY(Config, EditAnywhere, Category = "Network|LLM|Runner")
  FString LLMRunnerURL = TEXT("http://10.1.10.93:8000/run");

  UPROPERTY(Config, EditAnywhere, Category = "Network|LLM|Runner")
  FString LLMRunnerType = TEXT("wav");

  UPROPERTY(Config, EditAnywhere, Category = "Network|LLM|Runner")
  FString LLMRunnerCallbackURL = TEXT("http://10.1.10.119:9090/task/start");


  UPROPERTY(Config, EditAnywhere, Category = "Network|TTS")
  ENivaTTSModel TTSRequestType = ENivaTTSModel::TTS_Melotte;


  UPROPERTY(Config, EditAnywhere, Category = "Network|TTS", BlueprintReadOnly)
  bool shouldTTSWait = true;


  UPROPERTY(Config, EditAnywhere, Category = "Network|TTS|Fish")
  FString TTSURL = "";


  UPROPERTY(Config, EditAnywhere, Category = "Network|TTS|Fish")
  FString ReferenceID = "";


  UPROPERTY(Config, EditAnywhere, Category = "Network|TTS|Fish")
  FString TTSFishAPIKey = "";


  UPROPERTY(Config, EditAnywhere, Category = "Network|TTS|Melotte")
  FString MelotteTTSURL = "192.168.1.194:5000/v1/audio/speech";



  UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Network|TTS|Aliyun")
  FString TTSAliyunAccessKey = "";
  
  UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Network|TTS|Aliyun")
  FString TTSAliyunURL = "wss://dashscope.aliyuncs.com/api-ws/v1/inference";
  
  UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Network|TTS|Aliyun")
  FString TTSAliyunVoice = TEXT("longxiaochun_v2");

  UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Network|TTS|Aliyun")
  FString TTSAliyunFormat = TEXT("wav");

  UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Network|TTS|Aliyun")
  int32 TTSAliyunSampleRate = 22050;

  UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Network|TTS|Aliyun")
  int32 TTSAliyunVolume = 50;

  UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Network|TTS|Aliyun")
  float TTSAliyunRate = 1.0f;

  UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Network|TTS|Aliyun")
  float TTSAliyunPitch = 1.0f;

  UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Network|TTS|Aliyun")
  bool bEnableSSML = false;


  //UPROPERTY(Config, EditAnywhere, Category = "Network|TTS|Melotte")
  //FString MelotteReferenceID = "";

  //UPROPERTY(Config, EditAnywhere, Category = "Network|TTS|Melotte") 
  //FString MelotteAPIKey = "";


};