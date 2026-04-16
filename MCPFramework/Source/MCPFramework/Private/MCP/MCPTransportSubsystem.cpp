// Fill out your copyright notice in the Description page of Project Settings.


#include "MCP/MCPTransportSubsystem.h"
#include "MCPFrameworkSettings.h"
#include "HAL/PlatformProcess.h"
#include "HttpModule.h"
#include "Async/Async.h"
#include "Delegates/Delegate.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Subsystems/McpComponentRegistrySubsystem.h"
#include "Components/Base/McpExposableBaseComponent.h"
#include "UObject/FieldIterator.h"
#include "UObject/UnrealType.h"
// CoreManager Log subsystem
#if HAS_CORE_MANAGER
#include "Log/CoreLogSubsystem.h"
#endif

// Local helper to send logs via CoreManager
namespace
{
#if HAS_CORE_MANAGER
    inline void MCPLog_Impl(UObject* WorldContext, FName Category2, ECoreLogSeverity Severity, const FString& Message, const TMap<FString, FString>& Data = TMap<FString, FString>())
    {
        if (UCoreLogSubsystem* LogSys = UCoreLogSubsystem::Get(WorldContext))
        {
            LogSys->Log(TEXT("MCP"), Category2, Severity, Message, Data);
        }
    }
#define MCPLog MCPLog_Impl
#else
#define MCPLog(...) do {} while(false)
#endif

    inline FString CompactJsonString(const TSharedPtr<FJsonObject>& JsonObject)
    {
        if (!JsonObject.IsValid())
        {
            return TEXT("{}");
        }

        FString Out;
        TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
        FJsonSerializer::Serialize(JsonObject.ToSharedRef(), Writer);
        Out.ReplaceInline(TEXT("\n"), TEXT(""));
        Out.ReplaceInline(TEXT("\r"), TEXT(""));
        Out.ReplaceInline(TEXT("\t"), TEXT(""));
        return Out;
    }

    inline FString BuildJsonRpcErrorResponse(int32 Id, int32 Code, const FString& Message)
    {
        TSharedPtr<FJsonObject> Root = MakeShareable(new FJsonObject);
        Root->SetStringField(TEXT("jsonrpc"), TEXT("2.0"));
        Root->SetNumberField(TEXT("id"), Id);

        TSharedPtr<FJsonObject> Error = MakeShareable(new FJsonObject);
        Error->SetNumberField(TEXT("code"), Code);
        Error->SetStringField(TEXT("message"), Message);
        Root->SetObjectField(TEXT("error"), Error);

        return CompactJsonString(Root);
    }

    inline void SendJsonHttpResponse(struct mg_connection* Connection, int32 StatusCode, const FString& Body)
    {
        const TCHAR* StatusText = TEXT("OK");
        switch (StatusCode)
        {
        case 200: StatusText = TEXT("OK"); break;
        case 202: StatusText = TEXT("Accepted"); break;
        case 400: StatusText = TEXT("Bad Request"); break;
        case 404: StatusText = TEXT("Not Found"); break;
        case 405: StatusText = TEXT("Method Not Allowed"); break;
        case 408: StatusText = TEXT("Request Timeout"); break;
        case 500: StatusText = TEXT("Internal Server Error"); break;
        case 503: StatusText = TEXT("Service Unavailable"); break;
        case 504: StatusText = TEXT("Gateway Timeout"); break;
        default: break;
        }

        FTCHARToUTF8 BodyUtf8(*Body);
        const int32 Len = BodyUtf8.Length();
        FTCHARToUTF8 StatusUtf8(StatusText);
        mg_printf(Connection,
            "HTTP/1.1 %d %s\r\n"
            "Content-Type: application/json; charset=utf-8\r\n"
            "Access-Control-Allow-Origin: *\r\n"
            "Cache-Control: no-cache\r\n"
            "Content-Length: %d\r\n\r\n%.*s",
            StatusCode,
            StatusUtf8.Get(),
            (int)Len,
            (int)Len,
            BodyUtf8.Get());
    }

    inline int32 ExtractJsonRpcId(const TSharedPtr<FJsonObject>& JsonObject)
    {
        if (!JsonObject.IsValid())
        {
            return 0;
        }

        if (JsonObject->HasField(TEXT("id")))
        {
            return static_cast<int32>(JsonObject->GetNumberField(TEXT("id")));
        }

        return 0;
    }

    inline bool IsFinalJsonRpcPayload(const FString& Payload, int32 ExpectedId)
    {
        TSharedPtr<FJsonObject> JsonObject;
        TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Payload);
        if (!FJsonSerializer::Deserialize(Reader, JsonObject) || !JsonObject.IsValid())
        {
            return false;
        }

        if (!JsonObject->HasField(TEXT("id")))
        {
            return false;
        }

        const int32 PayloadId = static_cast<int32>(JsonObject->GetNumberField(TEXT("id")));
        return PayloadId == ExpectedId && (JsonObject->HasField(TEXT("result")) || JsonObject->HasField(TEXT("error")));
    }

    inline FString StripToolPrefix(const FString& FunctionName, const FString& Prefix)
    {
        if (!Prefix.IsEmpty() && FunctionName.StartsWith(Prefix))
        {
            return FunctionName.RightChop(Prefix.Len());
        }
        return FunctionName;
    }

    inline FString ToSnakeCase(const FString& InValue)
    {
        FString Out;
        Out.Reserve(InValue.Len() + 8);

        auto AppendSeparator = [&Out]()
        {
            if (!Out.IsEmpty() && !Out.EndsWith(TEXT("_")))
            {
                Out.AppendChar(TEXT('_'));
            }
        };

        for (int32 Index = 0; Index < InValue.Len(); ++Index)
        {
            const TCHAR Char = InValue[Index];
            if (FChar::IsUpper(Char))
            {
                const bool bHasPrev = Index > 0;
                const bool bPrevNeedsSeparator = bHasPrev && (FChar::IsLower(InValue[Index - 1]) || FChar::IsDigit(InValue[Index - 1]));
                const bool bNextLower = (Index + 1 < InValue.Len()) && FChar::IsLower(InValue[Index + 1]);
                if (bPrevNeedsSeparator || (bHasPrev && bNextLower))
                {
                    AppendSeparator();
                }
                Out.AppendChar(FChar::ToLower(Char));
            }
            else if (FChar::IsAlnum(Char))
            {
                Out.AppendChar(FChar::ToLower(Char));
            }
            else
            {
                AppendSeparator();
            }
        }

        while (Out.StartsWith(TEXT("_")))
        {
            Out.RightChopInline(1, EAllowShrinking::No);
        }
        while (Out.EndsWith(TEXT("_")))
        {
            Out.LeftChopInline(1, EAllowShrinking::No);
        }

        return Out.IsEmpty() ? InValue.ToLower() : Out;
    }

    inline FString GetToolDescriptionFromFunction(const UFunction* Function, const FString& OverrideDescription)
    {
        if (!OverrideDescription.IsEmpty())
        {
            return OverrideDescription;
        }
        if (!Function)
        {
            return TEXT("");
        }

        const FString Tooltip = Function->GetMetaData(TEXT("ToolTip"));
        if (!Tooltip.IsEmpty())
        {
            return Tooltip;
        }

        const FString DisplayName = Function->GetMetaData(TEXT("DisplayName"));
        if (!DisplayName.IsEmpty())
        {
            return DisplayName;
        }

        return FName::NameToDisplayString(StripToolPrefix(Function->GetName(), TEXT("MCP_")), false);
    }

    inline FString GetToolParamDescription(const FProperty* Property)
    {
        if (!Property)
        {
            return TEXT("");
        }

        const FString Tooltip = Property->GetMetaData(TEXT("ToolTip"));
        if (!Tooltip.IsEmpty())
        {
            return Tooltip;
        }

        const FString DisplayName = Property->GetMetaData(TEXT("DisplayName"));
        if (!DisplayName.IsEmpty())
        {
            return DisplayName;
        }

        return FName::NameToDisplayString(Property->GetName(), Property->IsA<FBoolProperty>());
    }

    inline bool TryGetArgumentValue(const TSharedPtr<FJsonObject>& Arguments, const FString& FieldName, TSharedPtr<FJsonValue>& OutValue)
    {
        if (!Arguments.IsValid())
        {
            return false;
        }

        const TSharedPtr<FJsonValue>* FoundValue = Arguments->Values.Find(FieldName);
        if (!FoundValue || !FoundValue->IsValid())
        {
            return false;
        }

        OutValue = *FoundValue;
        return true;
    }

    inline bool ParseToolArguments(const FString& JsonString, TSharedPtr<FJsonObject>& OutArguments, FString& OutError)
    {
        TSharedPtr<FJsonObject> JsonObject;
        TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
        if (!FJsonSerializer::Deserialize(Reader, JsonObject) || !JsonObject.IsValid())
        {
            OutError = TEXT("解析 JSON 失败");
            return false;
        }

        if (!JsonObject->HasTypedField<EJson::Object>(TEXT("params")))
        {
            OutError = TEXT("JSON-RPC 缺少 params 对象");
            return false;
        }

        const TSharedPtr<FJsonObject> ParamsObject = JsonObject->GetObjectField(TEXT("params"));
        if (!ParamsObject.IsValid() || !ParamsObject->HasTypedField<EJson::Object>(TEXT("arguments")))
        {
            OutError = TEXT("JSON-RPC 缺少 arguments 对象");
            return false;
        }

        OutArguments = ParamsObject->GetObjectField(TEXT("arguments"));
        if (!OutArguments.IsValid())
        {
            OutError = TEXT("arguments 对象无效");
            return false;
        }

        return true;
    }

    inline bool TryReadStringArgument(const TSharedPtr<FJsonObject>& Arguments, const FString& FieldName, FString& OutValue, FString& OutError)
    {
        TSharedPtr<FJsonValue> JsonValue;
        if (!TryGetArgumentValue(Arguments, FieldName, JsonValue))
        {
            OutError = FString::Printf(TEXT("缺少参数：%s"), *FieldName);
            return false;
        }

        switch (JsonValue->Type)
        {
        case EJson::String:
            OutValue = JsonValue->AsString();
            return true;
        case EJson::Number:
            OutValue = LexToString(JsonValue->AsNumber());
            return true;
        case EJson::Boolean:
            OutValue = JsonValue->AsBool() ? TEXT("true") : TEXT("false");
            return true;
        default:
            OutError = FString::Printf(TEXT("参数 %s 不是字符串"), *FieldName);
            return false;
        }
    }

    inline bool TryReadNumberArgument(const TSharedPtr<FJsonObject>& Arguments, const FString& FieldName, double& OutValue, FString& OutError)
    {
        TSharedPtr<FJsonValue> JsonValue;
        if (!TryGetArgumentValue(Arguments, FieldName, JsonValue))
        {
            OutError = FString::Printf(TEXT("缺少参数：%s"), *FieldName);
            return false;
        }

        switch (JsonValue->Type)
        {
        case EJson::Number:
            OutValue = JsonValue->AsNumber();
            return true;
        case EJson::String:
        {
            const FString ValueAsString = JsonValue->AsString();
            if (LexTryParseString(OutValue, *ValueAsString))
            {
                return true;
            }
            OutError = FString::Printf(TEXT("参数 %s 不是合法数字：%s"), *FieldName, *ValueAsString);
            return false;
        }
        case EJson::Boolean:
            OutValue = JsonValue->AsBool() ? 1.0 : 0.0;
            return true;
        default:
            OutError = FString::Printf(TEXT("参数 %s 不是数字"), *FieldName);
            return false;
        }
    }

    inline bool TryReadBoolArgument(const TSharedPtr<FJsonObject>& Arguments, const FString& FieldName, bool& OutValue, FString& OutError)
    {
        TSharedPtr<FJsonValue> JsonValue;
        if (!TryGetArgumentValue(Arguments, FieldName, JsonValue))
        {
            OutError = FString::Printf(TEXT("缺少参数：%s"), *FieldName);
            return false;
        }

        switch (JsonValue->Type)
        {
        case EJson::Boolean:
            OutValue = JsonValue->AsBool();
            return true;
        case EJson::Number:
            OutValue = !FMath::IsNearlyZero(JsonValue->AsNumber());
            return true;
        case EJson::String:
        {
            FString ValueAsString = JsonValue->AsString();
            ValueAsString.TrimStartAndEndInline();
            ValueAsString = ValueAsString.ToLower();
            if (ValueAsString == TEXT("true") || ValueAsString == TEXT("1") || ValueAsString == TEXT("yes"))
            {
                OutValue = true;
                return true;
            }
            if (ValueAsString == TEXT("false") || ValueAsString == TEXT("0") || ValueAsString == TEXT("no"))
            {
                OutValue = false;
                return true;
            }
            OutError = FString::Printf(TEXT("参数 %s 不是合法布尔值：%s"), *FieldName, *ValueAsString);
            return false;
        }
        default:
            OutError = FString::Printf(TEXT("参数 %s 不是布尔值"), *FieldName);
            return false;
        }
    }
}

// 初始化 CivetWeb 服务器并注册 HTTP 处理函数
void UMCPTransportSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    MCPLog(this, TEXT("Init"), ECoreLogSeverity::Info, TEXT("UMCPTransportSubsystem Initialize"));
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
    MCPLog(this, TEXT("Init"), ECoreLogSeverity::Info, TEXT("UMCPTransportSubsystem Deinitialize"));
    Super::Deinitialize();
}

bool UMCPTransportSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
    const bool bCreate = true;
    // CoreManager logging is optional when packaging this plugin standalone.
#if HAS_CORE_MANAGER
    if (const UWorld* World = Outer ? Outer->GetWorld() : nullptr)
    {
        if (UGameInstance* GI = World->GetGameInstance())
        {
            if (UCoreLogSubsystem* LogSys = GI->GetSubsystem<UCoreLogSubsystem>())
            {
                const TMap<FString,FString> Data; // empty
                LogSys->Log(TEXT("MCP"), TEXT("Init"), ECoreLogSeverity::Info, FString::Printf(TEXT("ShouldCreateSubsystem=%s"), bCreate ? TEXT("true") : TEXT("false")), Data);
            }
        }
    }
#endif
    return bCreate;
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
        UE_LOG(LogTemp, Verbose, TEXT("JSONRPC parse success: method=%s id=%d"), *Method, ID);
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("JSONRPC parse failed. payload(head)=%s"), *JsonString.Left(256));
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
	{
    TMap<FString, FString> DetailLog;

    // 基本信息
    DetailLog.Add(TEXT("ToolName"), tool.Name);
    DetailLog.Add(TEXT("Description"), tool.Description);
    DetailLog.Add(TEXT("TotalRoutes"), FString::FromInt(Storage.RouteDelegates.Num()));
    DetailLog.Add(TEXT("Registrations"), FString::FromInt(Storage.ToolNum));
    DetailLog.Add(TEXT("Variants"), FString::FromInt(Storage.MCPToolVariants.Num()));
    DetailLog.Add(TEXT("PropertiesCount"), FString::FromInt(tool.Properties.Num()));

    // Canon / Incoming owner
    DetailLog.Add(TEXT("CanonOwner"), GetNameSafe(GetOwnerClassFromTool(Storage.MCPTool)));
    DetailLog.Add(TEXT("IncomingOwner"), GetNameSafe(GetOwnerClassFromTool(tool)));

    // 属性清单与类型汇总（逗号分隔，简单字符串，最长 1024 字符）
    FString PropSummary;
    for (UMCPToolProperty* P : tool.Properties)
    {
        if (!P) continue;
        const FString TypeName = StaticEnum<EMCPJsonType>()->GetNameStringByValue(static_cast<int64>(P->Type));
        PropSummary += FString::Printf(TEXT("%s:%s,"), *P->Name, *TypeName);
    }
    if (PropSummary.Len() > 0)
    {
        // 去掉末尾逗号
        PropSummary.RemoveAt(PropSummary.Len() - 1);
    }
    // 限制长度，避免日志过长
    if (PropSummary.Len() > 1024) PropSummary = PropSummary.Left(1024) + TEXT("...");

    DetailLog.Add(TEXT("PropertyList"), MoveTemp(PropSummary));

    // 针对特殊类型（Actor / Component），记录可用目标数量及前几个示例
    for (UMCPToolProperty* P : tool.Properties)
    {
        if (!P) continue;
        if (UMCPToolPropertyActorPtr* AP = Cast<UMCPToolPropertyActorPtr>(P))
        {
            TArray<FString> Targets = AP->GetAvailableTargets();
            DetailLog.Add(FString::Printf(TEXT("TargetsCount_%s"), *P->Name), FString::FromInt(Targets.Num()));
            // 列出前 5 个示例
            FString FirstTargets;
            for (int32 i = 0; i < Targets.Num() && i < 5; ++i)
            {
                FirstTargets += Targets[i] + TEXT(", ");
            }
            if (FirstTargets.Len() > 2) FirstTargets.RemoveAt(FirstTargets.Len() - 2);
            if (FirstTargets.Len() > 256) FirstTargets = FirstTargets.Left(256) + TEXT("...");
            DetailLog.Add(FString::Printf(TEXT("TargetsSample_%s"), *P->Name), MoveTemp(FirstTargets));
        }
        else if (UMCPToolPropertyComponentPtr* CP = Cast<UMCPToolPropertyComponentPtr>(P))
        {
            TArray<FString> CTargets = CP->GetAvailableTargets();
            DetailLog.Add(FString::Printf(TEXT("CompTargetsCount_%s"), *P->Name), FString::FromInt(CTargets.Num()));
            FString FirstCTargets;
            for (int32 i = 0; i < CTargets.Num() && i < 5; ++i)
            {
                FirstCTargets += CTargets[i] + TEXT(", ");
            }
            if (FirstCTargets.Len() > 2) FirstCTargets.RemoveAt(FirstCTargets.Len() - 2);
            if (FirstCTargets.Len() > 256) FirstCTargets = FirstCTargets.Left(256) + TEXT("...");
            DetailLog.Add(FString::Printf(TEXT("CompTargetsSample_%s"), *P->Name), MoveTemp(FirstCTargets));
        }
    }

    // message要显示具体的工具名称
    MCPLog(this, TEXT("Tools"), ECoreLogSeverity::Off, tool.Name + TEXT(" registered."), DetailLog);
	}
}

int32 UMCPTransportSubsystem::AutoRegisterMCPTools(UObject* Target, FString Prefix)
{
    if (!IsValid(Target))
    {
        UE_LOG(LogTemp, Warning, TEXT("AutoRegisterMCPTools failed: invalid target"));
        return 0;
    }

    if (Prefix.IsEmpty())
    {
        Prefix = TEXT("MCP_");
    }

    UClass* TargetClass = Target->GetClass();
    if (!TargetClass)
    {
        UE_LOG(LogTemp, Warning, TEXT("AutoRegisterMCPTools failed: target %s has no class"), *GetNameSafe(Target));
        return 0;
    }

    int32 RegisteredCount = 0;
    for (TFieldIterator<UFunction> It(TargetClass, EFieldIteratorFlags::ExcludeSuper); It; ++It)
    {
        UFunction* Function = *It;
        if (!Function)
        {
            continue;
        }

        const FString FunctionName = Function->GetName();
        if (!FunctionName.StartsWith(Prefix))
        {
            continue;
        }
        if (!Function->HasAnyFunctionFlags(FUNC_BlueprintCallable | FUNC_BlueprintEvent))
        {
            continue;
        }
        if (Function->HasAnyFunctionFlags(FUNC_Static | FUNC_Delegate))
        {
            continue;
        }

        const FString DerivedToolName = ToSnakeCase(StripToolPrefix(FunctionName, Prefix));
        const bool bAlreadyRegistered = AutoToolBindings.Contains(DerivedToolName);
        if (RegisterFunctionAsMCPTool(Target, Function->GetFName(), DerivedToolName))
        {
            if (!bAlreadyRegistered)
            {
                ++RegisteredCount;
            }
        }
    }

    UE_LOG(LogTemp, Log, TEXT("AutoRegisterMCPTools: target=%s registered=%d prefix=%s"), *GetNameSafe(Target), RegisteredCount, *Prefix);
    return RegisteredCount;
}

bool UMCPTransportSubsystem::RegisterFunctionAsMCPTool(UObject* Target, FName FunctionName, FString ToolNameOverride, FString ToolDescriptionOverride)
{
    if (!IsValid(Target))
    {
        UE_LOG(LogTemp, Warning, TEXT("RegisterFunctionAsMCPTool failed: invalid target"));
        return false;
    }

    UFunction* Function = Target->FindFunction(FunctionName);
    if (!Function)
    {
        UE_LOG(LogTemp, Warning, TEXT("RegisterFunctionAsMCPTool failed: function %s not found on %s"), *FunctionName.ToString(), *GetNameSafe(Target));
        return false;
    }

    if (Function->HasAnyFunctionFlags(FUNC_Static | FUNC_Delegate))
    {
        UE_LOG(LogTemp, Warning, TEXT("RegisterFunctionAsMCPTool skipped: %s is static or delegate"), *FunctionName.ToString());
        return false;
    }

    const FString ToolName = !ToolNameOverride.IsEmpty()
        ? ToolNameOverride
        : ToSnakeCase(StripToolPrefix(Function->GetName(), TEXT("MCP_")));

    if (ToolName.IsEmpty())
    {
        UE_LOG(LogTemp, Warning, TEXT("RegisterFunctionAsMCPTool failed: tool name is empty for function %s"), *FunctionName.ToString());
        return false;
    }

    if (const FMCPAutoToolBinding* ExistingBinding = AutoToolBindings.Find(ToolName))
    {
        if (ExistingBinding->Target == Target && ExistingBinding->FunctionName == FunctionName)
        {
            UE_LOG(LogTemp, Verbose, TEXT("RegisterFunctionAsMCPTool skipped: tool %s already registered on %s"), *ToolName, *GetNameSafe(Target));
            return true;
        }

        UE_LOG(LogTemp, Warning, TEXT("RegisterFunctionAsMCPTool failed: duplicate auto tool name %s"), *ToolName);
        return false;
    }

    if (MCPTools.Contains(ToolName))
    {
        UE_LOG(LogTemp, Warning, TEXT("RegisterFunctionAsMCPTool failed: tool name %s already exists"), *ToolName);
        return false;
    }

    FMCPTool Tool;
    Tool.Name = ToolName;
    Tool.Description = GetToolDescriptionFromFunction(Function, ToolDescriptionOverride);

    FMCPAutoToolBinding Binding;
    Binding.Target = Target;
    Binding.Function = Function;
    Binding.FunctionName = FunctionName;

    int32 HandleParamCount = 0;
    FString ValidationError;

    for (TFieldIterator<FProperty> It(Function); It; ++It)
    {
        FProperty* Property = *It;
        if (!Property || !Property->HasAnyPropertyFlags(CPF_Parm))
        {
            continue;
        }

        if (Property->HasAnyPropertyFlags(CPF_ReturnParm))
        {
            ValidationError = FString::Printf(TEXT("函数 %s 不支持返回值"), *FunctionName.ToString());
            break;
        }

        if (Property->HasAnyPropertyFlags(CPF_OutParm) && !Property->HasAnyPropertyFlags(CPF_ConstParm))
        {
            ValidationError = FString::Printf(TEXT("函数 %s 不支持 Out 参数：%s"), *FunctionName.ToString(), *Property->GetName());
            break;
        }

        const FString ParamName = Property->GetName();
        const FString ParamDescription = GetToolParamDescription(Property);

        if (const FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
        {
            UClass* PropertyClass = ObjectProperty->PropertyClass;
            if (PropertyClass && PropertyClass->IsChildOf(UMCPToolHandle::StaticClass()))
            {
                ++HandleParamCount;
                if (HandleParamCount > 1)
                {
                    ValidationError = FString::Printf(TEXT("函数 %s 只能包含一个 MCPToolHandle 参数"), *FunctionName.ToString());
                    break;
                }

                Binding.HandleParameterName = Property->GetFName();
                continue;
            }

            if (PropertyClass && PropertyClass->IsChildOf(UMcpExposableBaseComponent::StaticClass()))
            {
                Tool.Properties.Add(UMCPToolPropertyComponentPtr::CreateComponentPtrProperty(ParamName, ParamDescription, PropertyClass));
                continue;
            }

            if (PropertyClass && PropertyClass->IsChildOf(AActor::StaticClass()))
            {
                Tool.Properties.Add(UMCPToolPropertyActorPtr::CreateActorPtrProperty(ParamName, ParamDescription, PropertyClass));
                continue;
            }

            ValidationError = FString::Printf(TEXT("函数 %s 包含不支持的对象参数类型：%s"), *FunctionName.ToString(), *GetNameSafe(PropertyClass));
            break;
        }

        if (CastField<FStrProperty>(Property))
        {
            Tool.Properties.Add(UMCPToolPropertyString::CreateStringProperty(ParamName, ParamDescription));
            continue;
        }

        if (CastField<FNameProperty>(Property))
        {
            Tool.Properties.Add(UMCPToolPropertyString::CreateStringProperty(ParamName, ParamDescription));
            continue;
        }

        if (CastField<FIntProperty>(Property))
        {
            Tool.Properties.Add(UMCPToolPropertyInt::CreateIntProperty(ParamName, ParamDescription, TNumericLimits<int32>::Lowest(), TNumericLimits<int32>::Max()));
            continue;
        }

        if (CastField<FFloatProperty>(Property) || CastField<FDoubleProperty>(Property))
        {
            Tool.Properties.Add(UMCPToolPropertyNumber::CreateNumberProperty(ParamName, ParamDescription, TNumericLimits<int32>::Lowest(), TNumericLimits<int32>::Max()));
            continue;
        }

        if (CastField<FBoolProperty>(Property))
        {
            Tool.Properties.Add(UMCPToolPropertyBool::CreateBoolProperty(ParamName, ParamDescription));
            continue;
        }

        ValidationError = FString::Printf(TEXT("函数 %s 包含不支持的参数类型：%s"), *FunctionName.ToString(), *Property->GetClass()->GetName());
        break;
    }

    if (!ValidationError.IsEmpty())
    {
        UE_LOG(LogTemp, Warning, TEXT("RegisterFunctionAsMCPTool failed: %s"), *ValidationError);
        return false;
    }

    if (HandleParamCount != 1)
    {
        UE_LOG(LogTemp, Warning, TEXT("RegisterFunctionAsMCPTool failed: function %s must contain exactly one MCPToolHandle parameter"), *FunctionName.ToString());
        return false;
    }

    FMCPRouteDelegate RouteDelegate;
    RouteDelegate.BindDynamic(this, &UMCPTransportSubsystem::OnAutoToolDispatch);
    RegisterToolProperties(Tool, RouteDelegate);
    AutoToolBindings.Add(ToolName, Binding);

    UE_LOG(LogTemp, Log, TEXT("RegisterFunctionAsMCPTool success: %s -> %s on %s"), *FunctionName.ToString(), *ToolName, *GetNameSafe(Target));
    return true;
}

FString UMCPTransportSubsystem::BeginRegisterCustomTool(UObject* Target, FName FunctionName, const FString& ToolDescription)
{
    if (!IsValid(Target))
    {
        UE_LOG(LogTemp, Warning, TEXT("BeginRegisterCustomTool failed: invalid target"));
        return TEXT("");
    }

    UFunction* Function = Target->FindFunction(FunctionName);
    if (!Function)
    {
        UE_LOG(LogTemp, Warning, TEXT("BeginRegisterCustomTool failed: function %s not found on %s"), *FunctionName.ToString(), *GetNameSafe(Target));
        return TEXT("");
    }

    if (Function->HasAnyFunctionFlags(FUNC_Static | FUNC_Delegate))
    {
        UE_LOG(LogTemp, Warning, TEXT("BeginRegisterCustomTool skipped: %s is static or delegate"), *FunctionName.ToString());
        return TEXT("");
    }

    const FString ToolName = ToSnakeCase(StripToolPrefix(Function->GetName(), TEXT("MCP_")));
    if (ToolName.IsEmpty())
    {
        UE_LOG(LogTemp, Warning, TEXT("BeginRegisterCustomTool failed: derived tool name is empty for %s"), *FunctionName.ToString());
        return TEXT("");
    }

    if (AutoToolBindings.Contains(ToolName) || MCPTools.Contains(ToolName))
    {
        UE_LOG(LogTemp, Warning, TEXT("BeginRegisterCustomTool failed: tool name %s already registered"), *ToolName);
        return TEXT("");
    }

    if (PendingToolRegistrations.Contains(ToolName))
    {
        UE_LOG(LogTemp, Warning, TEXT("BeginRegisterCustomTool failed: tool name %s already pending"), *ToolName);
        return TEXT("");
    }

    FMCPPendingToolRegistration Pending;
    Pending.Target = Target;
    Pending.Function = Function;
    Pending.FunctionName = FunctionName;
    Pending.ToolDescription = ToolDescription;

    // Extract param order and find Handle parameter
    int32 HandleCount = 0;
    for (TFieldIterator<FProperty> It(Function); It; ++It)
    {
        FProperty* Property = *It;
        if (!Property || !Property->HasAnyPropertyFlags(CPF_Parm) || Property->HasAnyPropertyFlags(CPF_ReturnParm))
        {
            continue;
        }

        if (const FObjectPropertyBase* ObjProp = CastField<FObjectPropertyBase>(Property))
        {
            if (ObjProp->PropertyClass && ObjProp->PropertyClass->IsChildOf(UMCPToolHandle::StaticClass()))
            {
                ++HandleCount;
                Pending.HandleParameterName = Property->GetFName();
                continue;
            }
        }
        Pending.CppParamOrder.Add(Property->GetFName());
    }

    if (HandleCount != 1)
    {
        UE_LOG(LogTemp, Warning, TEXT("BeginRegisterCustomTool failed: function %s must have exactly one UMCPToolHandle* param (found %d)"), *FunctionName.ToString(), HandleCount);
        return TEXT("");
    }

    PendingToolRegistrations.Add(ToolName, MoveTemp(Pending));
    UE_LOG(LogTemp, Log, TEXT("BeginRegisterCustomTool: started pending registration for %s (%d params)"), *ToolName, Pending.CppParamOrder.Num());
    return ToolName;
}

void UMCPTransportSubsystem::AddCustomToolProperty(const FString& ToolName, UMCPToolProperty* Property)
{
    FMCPPendingToolRegistration* Pending = PendingToolRegistrations.Find(ToolName);
    if (!Pending)
    {
        UE_LOG(LogTemp, Warning, TEXT("AddCustomToolProperty failed: no pending registration for %s"), *ToolName);
        return;
    }
    if (!Property)
    {
        UE_LOG(LogTemp, Warning, TEXT("AddCustomToolProperty failed: null property for %s"), *ToolName);
        return;
    }
    Pending->Properties.Add(Property);
}

bool UMCPTransportSubsystem::CommitCustomToolRegistration(const FString& ToolName)
{
    FMCPPendingToolRegistration* Pending = PendingToolRegistrations.Find(ToolName);
    if (!Pending)
    {
        UE_LOG(LogTemp, Warning, TEXT("CommitCustomToolRegistration failed: no pending registration for %s"), *ToolName);
        return false;
    }

    if (Pending->Properties.Num() != Pending->CppParamOrder.Num())
    {
        UE_LOG(LogTemp, Warning, TEXT("CommitCustomToolRegistration failed: property count (%d) != param count (%d) for %s"),
            Pending->Properties.Num(), Pending->CppParamOrder.Num(), *ToolName);
        PendingToolRegistrations.Remove(ToolName);
        return false;
    }

    // Build ParamToArgName mapping: C++ param name → custom property name
    FMCPAutoToolBinding Binding;
    Binding.Target = Pending->Target;
    Binding.Function = Pending->Function;
    Binding.FunctionName = Pending->FunctionName;
    Binding.HandleParameterName = Pending->HandleParameterName;

    for (int32 i = 0; i < Pending->CppParamOrder.Num(); ++i)
    {
        if (Pending->Properties[i])
        {
            Binding.ParamToArgName.Add(Pending->CppParamOrder[i], Pending->Properties[i]->Name);
        }
    }

    // Build FMCPTool
    FMCPTool Tool;
    Tool.Name = ToolName;
    Tool.Description = Pending->ToolDescription;
    Tool.Properties = Pending->Properties;

    // Register
    FMCPRouteDelegate RouteDelegate;
    RouteDelegate.BindDynamic(this, &UMCPTransportSubsystem::OnAutoToolDispatch);
    RegisterToolProperties(Tool, RouteDelegate);
    AutoToolBindings.Add(ToolName, Binding);

    PendingToolRegistrations.Remove(ToolName);
    UE_LOG(LogTemp, Log, TEXT("CommitCustomToolRegistration success: %s on %s"), *ToolName, *GetNameSafe(Binding.Target.Get()));
    return true;
}

TArray<FName> UMCPTransportSubsystem::GetEligibleMCPFunctions(UClass* InClass)
{
    TArray<FName> Result;
    if (!InClass)
    {
        return Result;
    }

    for (TFieldIterator<UFunction> It(InClass, EFieldIteratorFlags::IncludeSuper); It; ++It)
    {
        UFunction* Function = *It;
        if (!Function)
        {
            continue;
        }

        if (!Function->HasAnyFunctionFlags(FUNC_BlueprintCallable | FUNC_BlueprintEvent))
        {
            continue;
        }
        if (Function->HasAnyFunctionFlags(FUNC_Static | FUNC_Delegate))
        {
            continue;
        }

        int32 HandleCount = 0;
        bool bHasUnsupported = false;
        for (TFieldIterator<FProperty> PropIt(Function); PropIt; ++PropIt)
        {
            FProperty* Property = *PropIt;
            if (!Property || !Property->HasAnyPropertyFlags(CPF_Parm))
            {
                continue;
            }
            if (Property->HasAnyPropertyFlags(CPF_ReturnParm))
            {
                bHasUnsupported = true;
                break;
            }
            if (const FObjectPropertyBase* ObjProp = CastField<FObjectPropertyBase>(Property))
            {
                if (ObjProp->PropertyClass && ObjProp->PropertyClass->IsChildOf(UMCPToolHandle::StaticClass()))
                {
                    ++HandleCount;
                }
            }
        }

        if (!bHasUnsupported && HandleCount == 1)
        {
            Result.Add(Function->GetFName());
        }
    }

    return Result;
}

void UMCPTransportSubsystem::OnAutoToolDispatch(const FString& Result, UMCPToolHandle* MCPToolHandle, const FMCPTool& MCPTool)
{
    if (!MCPToolHandle)
    {
        return;
    }

    FMCPAutoToolBinding* Binding = AutoToolBindings.Find(MCPTool.Name);
    if (!Binding)
    {
        MCPToolHandle->ToolCallbackRaw(true, FString::Printf(TEXT("自动注册工具未找到绑定：%s"), *MCPTool.Name), true);
        return;
    }

    UObject* Target = Binding->Target.Get();
    if (!IsValid(Target))
    {
        MCPToolHandle->ToolCallbackRaw(true, FString::Printf(TEXT("自动注册工具的目标对象已失效：%s"), *MCPTool.Name), true);
        return;
    }

    UFunction* Function = Target->FindFunction(Binding->FunctionName);
    if (!Function)
    {
        Function = Binding->Function;
    }
    if (!Function)
    {
        MCPToolHandle->ToolCallbackRaw(true, FString::Printf(TEXT("自动注册工具未找到函数：%s"), *Binding->FunctionName.ToString()), true);
        return;
    }

    TSharedPtr<FJsonObject> ArgumentsObject;
    FString ParseError;
    if (!ParseToolArguments(Result, ArgumentsObject, ParseError))
    {
        MCPToolHandle->ToolCallbackRaw(true, ParseError, true);
        return;
    }

    TArray<uint8> ParamsBuffer;
    ParamsBuffer.SetNumZeroed(Function->ParmsSize);
    uint8* Buffer = ParamsBuffer.GetData();
    Function->InitializeStruct(Buffer);

    FString DispatchError;
    bool bDispatchOk = true;

    for (TFieldIterator<FProperty> It(Function); It; ++It)
    {
        FProperty* Property = *It;
        if (!Property || !Property->HasAnyPropertyFlags(CPF_Parm) || Property->HasAnyPropertyFlags(CPF_ReturnParm))
        {
            continue;
        }

        const FString ParamName = Property->GetName();

        // Use custom argument name if available (from custom tool registration)
        const FString* CustomArgName = Binding->ParamToArgName.Find(Property->GetFName());
        const FString ArgName = CustomArgName ? *CustomArgName : ParamName;

        if (FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
        {
            UClass* PropertyClass = ObjectProperty->PropertyClass;
            if (PropertyClass && PropertyClass->IsChildOf(UMCPToolHandle::StaticClass()))
            {
                ObjectProperty->SetObjectPropertyValue_InContainer(Buffer, MCPToolHandle);
                continue;
            }

            FString LabelValue;
            if (!TryReadStringArgument(ArgumentsObject, ArgName, LabelValue, DispatchError))
            {
                bDispatchOk = false;
                break;
            }

            if (PropertyClass && PropertyClass->IsChildOf(AActor::StaticClass()))
            {
                AActor* ResolvedActor = nullptr;
                if (UMCPToolPropertyActorPtr* ActorProperty = Cast<UMCPToolPropertyActorPtr>(UMCPToolBlueprintLibrary::GetProperty(MCPTool, ArgName)))
                {
                    ActorProperty->FindActors();
                    ResolvedActor = ActorProperty->GetActor(LabelValue);
                }

                if (!ResolvedActor || !ResolvedActor->IsA(PropertyClass))
                {
                    DispatchError = FString::Printf(TEXT("参数 %s 无法解析为有效 Actor：%s"), *ParamName, *LabelValue);
                    bDispatchOk = false;
                    break;
                }

                ObjectProperty->SetObjectPropertyValue_InContainer(Buffer, ResolvedActor);
                continue;
            }

            if (PropertyClass && PropertyClass->IsChildOf(UMcpExposableBaseComponent::StaticClass()))
            {
                UActorComponent* ResolvedComponent = nullptr;
                if (UMCPToolPropertyComponentPtr* ComponentProperty = Cast<UMCPToolPropertyComponentPtr>(UMCPToolBlueprintLibrary::GetProperty(MCPTool, ArgName)))
                {
                    ComponentProperty->GetAvailableTargets();
                    ResolvedComponent = ComponentProperty->GetComponentByLabel(LabelValue);
                }

                if (!ResolvedComponent || !ResolvedComponent->IsA(PropertyClass))
                {
                    DispatchError = FString::Printf(TEXT("参数 %s 无法解析为有效组件：%s"), *ParamName, *LabelValue);
                    bDispatchOk = false;
                    break;
                }

                ObjectProperty->SetObjectPropertyValue_InContainer(Buffer, ResolvedComponent);
                continue;
            }

            DispatchError = FString::Printf(TEXT("参数 %s 的对象类型不受支持：%s"), *ParamName, *GetNameSafe(PropertyClass));
            bDispatchOk = false;
            break;
        }

        if (FStrProperty* StringProperty = CastField<FStrProperty>(Property))
        {
            FString Value;
            if (!TryReadStringArgument(ArgumentsObject, ArgName, Value, DispatchError))
            {
                bDispatchOk = false;
                break;
            }
            StringProperty->SetPropertyValue_InContainer(Buffer, Value);
            continue;
        }

        if (FNameProperty* NameProperty = CastField<FNameProperty>(Property))
        {
            FString Value;
            if (!TryReadStringArgument(ArgumentsObject, ArgName, Value, DispatchError))
            {
                bDispatchOk = false;
                break;
            }
            NameProperty->SetPropertyValue_InContainer(Buffer, FName(*Value));
            continue;
        }

        if (FIntProperty* IntProperty = CastField<FIntProperty>(Property))
        {
            double Value = 0.0;
            if (!TryReadNumberArgument(ArgumentsObject, ArgName, Value, DispatchError))
            {
                bDispatchOk = false;
                break;
            }
            IntProperty->SetPropertyValue_InContainer(Buffer, static_cast<int32>(Value));
            continue;
        }

        if (FFloatProperty* FloatProperty = CastField<FFloatProperty>(Property))
        {
            double Value = 0.0;
            if (!TryReadNumberArgument(ArgumentsObject, ArgName, Value, DispatchError))
            {
                bDispatchOk = false;
                break;
            }
            *FloatProperty->ContainerPtrToValuePtr<float>(Buffer) = static_cast<float>(Value);
            continue;
        }

        if (FDoubleProperty* DoubleProperty = CastField<FDoubleProperty>(Property))
        {
            double Value = 0.0;
            if (!TryReadNumberArgument(ArgumentsObject, ArgName, Value, DispatchError))
            {
                bDispatchOk = false;
                break;
            }
            *DoubleProperty->ContainerPtrToValuePtr<double>(Buffer) = Value;
            continue;
        }

        if (FBoolProperty* BoolProperty = CastField<FBoolProperty>(Property))
        {
            bool Value = false;
            if (!TryReadBoolArgument(ArgumentsObject, ArgName, Value, DispatchError))
            {
                bDispatchOk = false;
                break;
            }
            BoolProperty->SetPropertyValue_InContainer(Buffer, Value);
            continue;
        }

        DispatchError = FString::Printf(TEXT("参数 %s 的类型暂不支持自动注入"), *ParamName);
        bDispatchOk = false;
        break;
    }

    if (!bDispatchOk)
    {
        Function->DestroyStruct(Buffer);
        MCPToolHandle->ToolCallbackRaw(true, DispatchError, true);
        return;
    }

    Target->ProcessEvent(Function, Buffer);
    Function->DestroyStruct(Buffer);
}

TSharedPtr<FJsonObject> UMCPTransportSubsystem::GetToolbyTarget(FString ActorName)
{
    MCPLog(this, TEXT("Tools"), ECoreLogSeverity::Debug, FString::Printf(TEXT("Query tools by target: %s"), *ActorName));
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
	
    MCPLog(this, TEXT("Tools"), ECoreLogSeverity::Info, FString::Printf(TEXT("Query tools by target done: %d items"), ToolsArray.Num()));
    return result;
}

TSharedPtr<FJsonObject> UMCPTransportSubsystem::GetToolTargets(FString ToolName)
{
    MCPLog(this, TEXT("Tools"), ECoreLogSeverity::Debug, FString::Printf(TEXT("Query targets by tool: %s"), *ToolName));
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
    MCPLog(this, TEXT("Tools"), ECoreLogSeverity::Info, FString::Printf(TEXT("Query targets by tool done: %d items"), TargetsArray.Num()));
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
    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
    const UMCPFrameworkSettings* SettingsLocal = GetDefault<UMCPFrameworkSettings>();
	FString BaseURL = SettingsLocal ? SettingsLocal->StreamBaseURL : TEXT("");
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

// 将 JSON payload 放入指定流上下文队列，输出格式为 NDJSON chunk。
void UMCPTransportSubsystem::QueueStreamPayload(const FString& StreamId, const FString& Payload)
{
    if (StreamQueues.Contains(StreamId))
    {
        FString Msg = Payload;
        if (!Msg.EndsWith(TEXT("\n")))
        {
            Msg += TEXT("\n");
        }
        StreamQueues[StreamId]->Enqueue(Msg);
        TMap<FString,FString> LogData;
        LogData.Add(TEXT("StreamId"), StreamId);
        MCPLog(this, TEXT("Stream"), ECoreLogSeverity::Trace, TEXT("Queued stream payload"), LogData);
    }
    else
    {
        TMap<FString,FString> LogData;
        LogData.Add(TEXT("StreamId"), StreamId);
        MCPLog(this, TEXT("Stream"), ECoreLogSeverity::Warn, TEXT("Unknown stream context"), LogData);
    }
}

// 可扩展的业务处理函数示例
void UMCPTransportSubsystem::HandleStreamRequest(const FMCPRequest& Request, const FString& StreamId)
{
    // 解析 JSON 或根据方法执行操作
    {
        TMap<FString,FString> LogData;
        LogData.Add(TEXT("StreamId"), StreamId);
        MCPLog(this, TEXT("Stream"), ECoreLogSeverity::Debug, TEXT("HandleStreamRequest received"), LogData);
    }


    // 解析参数
    FString Method;
    TSharedPtr<FJsonObject> Params;
    int id;
    TSharedPtr<FJsonObject> JsonObject;

    ParseJsonRPC(Request.Json, Method, Params, id, JsonObject);
    {
        TMap<FString,FString> LogData;
        LogData.Add(TEXT("StreamId"), StreamId);
        LogData.Add(TEXT("Method"), Method);
        // 如果是 tools/call，尝试附加工具名以便快速排查
        if (Method == TEXT("tools/call") && Params.IsValid() && Params->HasField(TEXT("name")))
        {
            FString ToolNameLocal = Params->GetStringField(TEXT("name"));
            LogData.Add(TEXT("ToolName"), ToolNameLocal);
        }
        MCPLog(this, TEXT("Message"), ECoreLogSeverity::Trace, TEXT("Parsed JSON-RPC"), LogData);
    }


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

        QueueStreamPayload(StreamId, InitMessage);
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
        QueueStreamPayload(StreamId, ToolListMessage);
    }
    else if (Method == "resources/list") {
        // 按 MCP 规范返回空资源列表
        FString Msg;
        TSharedPtr<FJsonObject> Root = MakeShareable(new FJsonObject);
        Root->SetStringField("jsonrpc", "2.0");
        Root->SetNumberField("id", id);
        TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
        TArray<TSharedPtr<FJsonValue>> Resources;
        Result->SetArrayField("resources", Resources);
        Result->SetStringField("nextCursor", ""); // 暂无分页
        Root->SetObjectField("result", Result);
        TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Msg);
        FJsonSerializer::Serialize(Root.ToSharedRef(), W);
        Msg.ReplaceInline(TEXT("\n"), TEXT("")); Msg.ReplaceInline(TEXT("\r"), TEXT("")); Msg.ReplaceInline(TEXT("\t"), TEXT(""));
        QueueStreamPayload(StreamId, Msg);
    }
    else if (Method == "prompts/list") {
        // 按 MCP 规范返回空提示列表
        FString Msg;
        TSharedPtr<FJsonObject> Root = MakeShareable(new FJsonObject);
        Root->SetStringField("jsonrpc", "2.0");
        Root->SetNumberField("id", id);
        TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
        TArray<TSharedPtr<FJsonValue>> Prompts;
        Result->SetArrayField("prompts", Prompts);
        Result->SetStringField("nextCursor", "");
        Root->SetObjectField("result", Result);
        TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Msg);
        FJsonSerializer::Serialize(Root.ToSharedRef(), W);
        Msg.ReplaceInline(TEXT("\n"), TEXT("")); Msg.ReplaceInline(TEXT("\r"), TEXT("")); Msg.ReplaceInline(TEXT("\t"), TEXT(""));
        QueueStreamPayload(StreamId, Msg);
    }
    else if (Method == "logging/list") {
        // 暂不支持：按 JSON-RPC 返回标准错误（-32601 方法不存在/不支持）
        FString Msg;
        TSharedPtr<FJsonObject> Root = MakeShareable(new FJsonObject);
        Root->SetStringField("jsonrpc", "2.0");
        Root->SetNumberField("id", id);
        TSharedPtr<FJsonObject> Err = MakeShareable(new FJsonObject);
        Err->SetNumberField("code", -32601);
        Err->SetStringField("message", TEXT("logging/list not supported"));
        Root->SetObjectField("error", Err);
        TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Msg);
        FJsonSerializer::Serialize(Root.ToSharedRef(), W);
        Msg.ReplaceInline(TEXT("\n"), TEXT("")); Msg.ReplaceInline(TEXT("\r"), TEXT("")); Msg.ReplaceInline(TEXT("\t"), TEXT(""));
        QueueStreamPayload(StreamId, Msg);

    }
    else if (Method == "tools/call") {

        // 从params里面获取name
        FString ToolName = Params->GetStringField(TEXT("name"));

        {
            TMap<FString,FString> LogData;
            LogData.Add(TEXT("StreamId"), StreamId);
            LogData.Add(TEXT("ToolName"), ToolName);
            MCPLog(this, TEXT("Message"), ECoreLogSeverity::Debug, FString::Printf(TEXT("tools/call received for %s"), *ToolName), LogData);
        }

        
        // print 一下时间
        UE_LOG(LogTemp, Log, TEXT("Stream: tools/call: time: %s"), *FDateTime::Now().ToString());
            // 直接print一下
        UE_LOG(LogTemp, Log, TEXT("Stream: tools/call: %s"), *ToolName);

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
                        UMCPToolHandle* MCPToolHandle = UMCPToolHandle::initToolHandle(id, StreamId, this, ProgressToken);
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

                            // 记录失败日志，包含工具名与无效参数
                            {
                                TMap<FString,FString> LogData;
                                LogData.Add(TEXT("ToolName"), ToolName);
                                LogData.Add(TEXT("BadParams"), FString::Join(BadParams, TEXT(",")));
                                LogData.Add(TEXT("Success"), TEXT("false"));
                                LogData.Add(TEXT("VariantIndex"), FString::FromInt(BestIdx));
                                LogData.Add(TEXT("Owner"), GetNameSafe(ProvidedOwner));
                                MCPLog(this, TEXT("Message"), ECoreLogSeverity::Warn, FString::Printf(TEXT("tools/call validation failed: %s"), *Msg), LogData);
                            }

                            Num = 1; // 已处理（错误回调），不再进入后续委托
                        }
                        else
                        {
                            Delegate.ExecuteIfBound(Request.Json, MCPToolHandle, ToolVariant);
                            Num = 1;

                            // 记录成功触发委托的日志
                            {
                                TMap<FString,FString> LogData;
                                LogData.Add(TEXT("ToolName"), ToolName);
                                LogData.Add(TEXT("Success"), TEXT("true"));
                                LogData.Add(TEXT("VariantIndex"), FString::FromInt(BestIdx));
                                LogData.Add(TEXT("Owner"), GetNameSafe(ProvidedOwner));
                                MCPLog(this, TEXT("Message"), ECoreLogSeverity::Info, FString::Printf(TEXT("tools/call executed: %s"), *ToolName), LogData);
                            }
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
                        UMCPToolHandle* MCPToolHandle = UMCPToolHandle::initToolHandle(id, StreamId, this, ProgressToken);
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

                            // 记录失败日志
                            {
                                TMap<FString,FString> LogData;
                                LogData.Add(TEXT("ToolName"), ToolName);
                                LogData.Add(TEXT("BadParams"), FString::Join(BadParams, TEXT(",")));
                                LogData.Add(TEXT("Success"), TEXT("false"));
                                LogData.Add(TEXT("VariantIndex"), FString::FromInt(idx));
                                MCPLog(this, TEXT("Message"), ECoreLogSeverity::Warn, FString::Printf(TEXT("tools/call validation failed (fallback): %s"), *Msg), LogData);
                            }

                            Num = 1; // 已处理错误
                        }
                        else
                        {
                            Delegate.ExecuteIfBound(Request.Json, MCPToolHandle, ToolVariant);
                            Num = 1;

                            // 记录成功触发委托的日志
                            {
                                TMap<FString,FString> LogData;
                                LogData.Add(TEXT("ToolName"), ToolName);
                                LogData.Add(TEXT("Success"), TEXT("true"));
                                LogData.Add(TEXT("VariantIndex"), FString::FromInt(idx));
                                MCPLog(this, TEXT("Message"), ECoreLogSeverity::Info, FString::Printf(TEXT("tools/call executed (fallback): %s"), *ToolName), LogData);
                            }
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

      QueueStreamPayload(StreamId, ErrorMessage);

            // 记录未知工具的日志
            TMap<FString,FString> LogData;
            LogData.Add(TEXT("ToolName"), ToolName);
            LogData.Add(TEXT("Success"), TEXT("false"));
            MCPLog(this, TEXT("Message"), ECoreLogSeverity::Warn, TEXT("tools/call: unknown tool"), LogData);
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
        // 序列��化为字符串
        TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&PingMessage);
        FJsonSerializer::Serialize(RootObject.ToSharedRef(), Writer);
        // 先剔除所有的换行符
        PingMessage.ReplaceInline(TEXT("\n"), TEXT(""));
        PingMessage.ReplaceInline(TEXT("\r"), TEXT(""));
        PingMessage.ReplaceInline(TEXT("\t"), TEXT(""));

        QueueStreamPayload(StreamId, PingMessage);
    }
    else {
        TMap<FString,FString> LogData;
        LogData.Add(TEXT("Method"), Method);
        LogData.Add(TEXT("StreamId"), StreamId);
        MCPLog(this, TEXT("Message"), ECoreLogSeverity::Warn, TEXT("Unknown JSON-RPC method"), LogData);
    }
}

// forward declaration for favicon handler
static int OnGetFavicon(struct mg_connection* Connection, void* UserData);

void UMCPTransportSubsystem::StartMCPServer()
{
    const UMCPFrameworkSettings* Settings = GetDefault<UMCPFrameworkSettings>();
    const int32 StreamPort = Settings ? Settings->StreamPort : 8080;
    const FString Port = FString::Printf(TEXT("%d"), StreamPort);
    const FString StreamPath = (Settings && !Settings->StreamPath.IsEmpty()) ? Settings->StreamPath : TEXT("/stream");
    const FString EndStreamPath = (Settings && !Settings->EndStreamPath.IsEmpty()) ? Settings->EndStreamPath : TEXT("/end-stream");

    FTCHARToUTF8 PortUtf8(*Port);
    const char* Options[] = { "listening_ports", PortUtf8.Get(), nullptr };
    ServerContext = mg_start(nullptr, this, Options);
    if (!ServerContext)
    {
        TMap<FString,FString> LogData; LogData.Add(TEXT("Port"), Port);
        MCPLog(this, TEXT("Server"), ECoreLogSeverity::Error, TEXT("Failed to start MCP stream server"), LogData);
        return;
    }

    FTCHARToUTF8 StreamPathUtf8(*StreamPath);
    FTCHARToUTF8 EndStreamPathUtf8(*EndStreamPath);
    mg_set_request_handler(ServerContext, StreamPathUtf8.Get(), OnStream, this);
    mg_set_request_handler(ServerContext, EndStreamPathUtf8.Get(), OnEndStream, this);
    mg_set_request_handler(ServerContext, "/tools", OnGetTools, this);
    mg_set_request_handler(ServerContext, "/tools/version", OnGetToolsVersion, this);
    mg_set_request_handler(ServerContext, "/ui/tools", OnGetToolsUI, this);
    mg_set_request_handler(ServerContext, "/favicon.ico", OnGetFavicon, this);

    {
        TMap<FString,FString> LogData;
        LogData.Add(TEXT("Port"), Port);
        LogData.Add(TEXT("StreamPath"), StreamPath);
        LogData.Add(TEXT("EndStreamPath"), EndStreamPath);
        MCPLog(this, TEXT("Server"), ECoreLogSeverity::Info, TEXT("MCP stream server started"), LogData);
    }



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

int UMCPTransportSubsystem::OnStream(struct mg_connection* Connection, void* UserData)
{
    auto* This = static_cast<UMCPTransportSubsystem*>(UserData);
    if (!This)
    {
        mg_printf(Connection, "HTTP/1.1 500 Internal Server Error\r\n\r\n");
        return 500;
    }

    const struct mg_request_info* ReqInfo = mg_get_request_info(Connection);
    const FString RequestMethod = ANSI_TO_TCHAR(ReqInfo && ReqInfo->request_method ? ReqInfo->request_method : "");

    if (RequestMethod == TEXT("GET"))
    {
        const UMCPFrameworkSettings* Settings = GetDefault<UMCPFrameworkSettings>();
        const FString StreamPath = (Settings && !Settings->StreamPath.IsEmpty()) ? Settings->StreamPath : TEXT("/stream");
        const FString EndStreamPath = (Settings && !Settings->EndStreamPath.IsEmpty()) ? Settings->EndStreamPath : TEXT("/end-stream");
        const FString StreamId = This->GenerateSessionId();

        {
            FScopeLock Lock(&This->SessionLock);
            This->StreamQueues.Add(StreamId, MakeShared<TQueue<FString>>());
            This->ClosingStreams.Remove(StreamId);
        }

        mg_printf(Connection,
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: application/x-ndjson; charset=utf-8\r\n"
            "Cache-Control: no-cache\r\n"
            "Connection: keep-alive\r\n"
            "Access-Control-Allow-Origin: *\r\n"
            "Transfer-Encoding: chunked\r\n\r\n");

        auto SendChunk = [Connection](const FString& Payload)
        {
            FTCHARToUTF8 PayloadUtf8(*Payload);
            mg_send_chunk(Connection, PayloadUtf8.Get(), PayloadUtf8.Length());
        };

        FString OpenFrame;
        {
            TSharedPtr<FJsonObject> RootObject = MakeShareable(new FJsonObject);
            RootObject->SetStringField(TEXT("type"), TEXT("stream.open"));
            RootObject->SetStringField(TEXT("streamId"), StreamId);
            RootObject->SetStringField(TEXT("writePath"), FString::Printf(TEXT("%s?stream_id=%s"), *StreamPath, *StreamId));
            RootObject->SetStringField(TEXT("endPath"), FString::Printf(TEXT("%s?stream_id=%s"), *EndStreamPath, *StreamId));
            RootObject->SetStringField(TEXT("contentType"), TEXT("application/x-ndjson"));
            TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OpenFrame);
            FJsonSerializer::Serialize(RootObject.ToSharedRef(), Writer);
            OpenFrame += TEXT("\n");
        }
        SendChunk(OpenFrame);

        const double HeartbeatInterval = 15.0;
        double LastHeartbeat = 0.0;

        while (true)
        {
            if (This->bIsShuttingDown || !This->ServerContext)
            {
                break;
            }

            bool bClosing = false;
            FString Msg;
            {
                FScopeLock Lock(&This->SessionLock);
                bClosing = This->ClosingStreams.Contains(StreamId);
                if (This->StreamQueues.Contains(StreamId))
                {
                    This->StreamQueues[StreamId]->Dequeue(Msg);
                }
            }

            if (!Msg.IsEmpty())
            {
                SendChunk(Msg);
            }
            else
            {
                FPlatformProcess::Sleep(0.1f);
            }

            LastHeartbeat += 0.1f;
            if (LastHeartbeat >= HeartbeatInterval)
            {
                SendChunk(TEXT("{\"type\":\"heartbeat\"}\n"));
                LastHeartbeat = 0.0f;
            }

            if (bClosing)
            {
                SendChunk(FString::Printf(TEXT("{\"type\":\"stream.close\",\"streamId\":\"%s\"}\n"), *StreamId));
                break;
            }
        }

        {
            FScopeLock Lock(&This->SessionLock);
            This->StreamQueues.Remove(StreamId);
            This->ClosingStreams.Remove(StreamId);
        }

        mg_send_chunk(Connection, "", 0);
        return 200;
    }

    if (RequestMethod == TEXT("DELETE"))
    {
        return OnEndStream(Connection, UserData);
    }

    if (RequestMethod == TEXT("OPTIONS"))
    {
        mg_printf(Connection,
            "HTTP/1.1 204 No Content\r\n"
            "Access-Control-Allow-Origin: *\r\n"
            "Access-Control-Allow-Methods: GET, POST, DELETE, OPTIONS\r\n"
            "Access-Control-Allow-Headers: Content-Type\r\n"
            "Content-Length: 0\r\n\r\n");
        return 204;
    }

    if (RequestMethod != TEXT("POST"))
    {
        mg_printf(Connection, "HTTP/1.1 405 Method Not Allowed\r\n\r\n");
        return 405;
    }

    // 获取 body 内容
    char bodyBuf[4096] = {};
    // mg_read 返回读取的字节数（可能为负数表示错误）
    int bytesRead = mg_read(Connection, bodyBuf, static_cast<int>(sizeof(bodyBuf) - 1));
    if (bytesRead <= 0)
    {
        bodyBuf[0] = '\0';
    }
    else
    {
        int len = bytesRead;
        if (len >= static_cast<int>(sizeof(bodyBuf)))
        {
            len = static_cast<int>(sizeof(bodyBuf)) - 1;
        }
        bodyBuf[len] = '\0';
    }

    // 正确解码 UTF-8 到 FString
    FString JsonBody = FString(FUTF8ToTCHAR(bodyBuf));

    char sessionBuf[64] = {};
    if (ReqInfo && ReqInfo->query_string)
    {
        const int32 QueryLen = static_cast<int32>(strlen(ReqInfo->query_string));
        mg_get_var(ReqInfo->query_string, QueryLen, "stream_id", sessionBuf, sizeof(sessionBuf));
        if (strlen(sessionBuf) == 0)
        {
            mg_get_var(ReqInfo->query_string, QueryLen, "streamId", sessionBuf, sizeof(sessionBuf));
        }
        if (strlen(sessionBuf) == 0)
        {
            mg_get_var(ReqInfo->query_string, QueryLen, "session_id", sessionBuf, sizeof(sessionBuf));
        }
    }

    // 兼容标准 MCP 的直接 POST：没有 stream_id 时，直接等待最终 JSON-RPC 响应并回包。
    if (strlen(sessionBuf) == 0)
    {
        TSharedPtr<FJsonObject> RequestObject;
        TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonBody);
        if (!FJsonSerializer::Deserialize(Reader, RequestObject) || !RequestObject.IsValid())
        {
            SendJsonHttpResponse(Connection, 400, BuildJsonRpcErrorResponse(0, -32700, TEXT("Invalid JSON-RPC payload")));
            return 400;
        }

        const int32 RequestId = ExtractJsonRpcId(RequestObject);
        const FString Method = RequestObject->HasField(TEXT("method")) ? RequestObject->GetStringField(TEXT("method")) : TEXT("");

        if (Method.IsEmpty())
        {
            SendJsonHttpResponse(Connection, 400, BuildJsonRpcErrorResponse(RequestId, -32600, TEXT("Missing JSON-RPC method")));
            return 400;
        }

        if (Method == TEXT("notifications/initialized") || Method == TEXT("initialized"))
        {
            mg_printf(Connection,
                "HTTP/1.1 202 Accepted\r\n"
                "Access-Control-Allow-Origin: *\r\n"
                "Content-Length: 0\r\n\r\n");
            return 202;
        }

        const FString DirectStreamId = FString::Printf(TEXT("direct-%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits));
        {
            FScopeLock Lock(&This->SessionLock);
            This->StreamQueues.Add(DirectStreamId, MakeShared<TQueue<FString>>());
            This->ClosingStreams.Remove(DirectStreamId);
        }

        FMCPRequest Req{ JsonBody };
        if (IsInGameThread())
        {
            This->HandleStreamRequest(Req, DirectStreamId);
        }
        else
        {
            AsyncTask(ENamedThreads::GameThread, [This, Req, DirectStreamId]()
            {
                This->HandleStreamRequest(Req, DirectStreamId);
            });
        }

        const double TimeoutSeconds = (Method == TEXT("tools/call")) ? 30.0 : 5.0;
        double ElapsedSeconds = 0.0;
        FString FinalPayload;
        while (ElapsedSeconds < TimeoutSeconds)
        {
            FString Payload;
            {
                FScopeLock Lock(&This->SessionLock);
                if (TSharedPtr<TQueue<FString>>* QueuePtr = This->StreamQueues.Find(DirectStreamId))
                {
                    (*QueuePtr)->Dequeue(Payload);
                }
            }

            if (!Payload.IsEmpty() && IsFinalJsonRpcPayload(Payload, RequestId))
            {
                FinalPayload = Payload;
                break;
            }

            FPlatformProcess::Sleep(0.01f);
            ElapsedSeconds += 0.01f;
        }

        {
            FScopeLock Lock(&This->SessionLock);
            This->StreamQueues.Remove(DirectStreamId);
            This->ClosingStreams.Remove(DirectStreamId);
        }

        if (FinalPayload.IsEmpty())
        {
            SendJsonHttpResponse(Connection, 504, BuildJsonRpcErrorResponse(RequestId, -32001, FString::Printf(TEXT("Timed out waiting for MCP response to %s"), *Method)));
            return 504;
        }

        FinalPayload.TrimStartAndEndInline();
        SendJsonHttpResponse(Connection, 200, FinalPayload);
        return 200;
    }

    FString StreamId(ANSI_TO_TCHAR(sessionBuf));

    FMCPRequest Req{ JsonBody };

    AsyncTask(ENamedThreads::GameThread, [This, Req, StreamId]()
    {
        This->HandleStreamRequest(Req, StreamId);
    });

    mg_printf(Connection,
        "HTTP/1.1 202 Accepted\r\nContent-Length: 0\r\n\r\n");
    return 1;
}

int UMCPTransportSubsystem::OnEndStream(struct mg_connection* Connection, void* UserData)
{
    UMCPTransportSubsystem* This = static_cast<UMCPTransportSubsystem*>(UserData);
	if (!This) {
		mg_printf(Connection, "HTTP/1.1 500 Internal Server Error\r\n\r\n");
		return 500;
	}

    const struct mg_request_info* ReqInfo = mg_get_request_info(Connection);
    if (!ReqInfo || !ReqInfo->query_string)
    {
        mg_printf(Connection, "HTTP/1.1 400 Bad Request\r\n\r\n");
        return 400;
    }

    char StreamBuf[64] = {};
    mg_get_var(ReqInfo->query_string, static_cast<int>(strlen(ReqInfo->query_string)), "stream_id", StreamBuf, sizeof(StreamBuf));
    if (strlen(StreamBuf) == 0)
    {
        mg_printf(Connection, "HTTP/1.1 400 Bad Request\r\n\r\n");
        return 400;
    }

    const FString StreamId(ANSI_TO_TCHAR(StreamBuf));
    {
        FScopeLock Lock(&This->SessionLock);
        This->ClosingStreams.Add(StreamId);
    }

    const FString Body = FString::Printf(TEXT("{\"accepted\":true,\"streamId\":\"%s\"}"), *StreamId);
    FTCHARToUTF8 BodyUtf8(*Body);
    const int32 Len = BodyUtf8.Length();
    mg_printf(Connection,
        "HTTP/1.1 202 Accepted\r\n"
        "Content-Type: application/json; charset=utf-8\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Content-Length: %d\r\n\r\n%.*s",
        (int)Len, (int)Len, BodyUtf8.Get());
    return 202;
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
	// JSON Schema fragment for this parameter
	TSharedPtr<FJsonObject> RootObject = MakeShareable(new FJsonObject);
	// Use JSON Schema fields: type/description/title
	RootObject->SetStringField("type", StaticEnum<EMCPJsonType>()->GetNameStringByValue(static_cast<int64>(Type)));
	RootObject->SetStringField("description", Description);
	RootObject->SetStringField("title", Name);
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

UMCPToolProperty* UMCPToolPropertyBool::CreateBoolProperty(FString InName, FString InDescription)
{
    UMCPToolPropertyBool* Property = NewObject<UMCPToolPropertyBool>();
    Property->Name = InName;
    Property->Type = EMCPJsonType::Boolean;
    Property->Description = InDescription;
    return Property;
}

TSharedPtr<FJsonObject> UMCPToolPropertyBool::GetJsonObject()
{
    TSharedPtr<FJsonObject> RootObject = MakeShareable(new FJsonObject);
    RootObject->SetStringField("type", StaticEnum<EMCPJsonType>()->GetNameStringByValue(static_cast<int64>(Type)));
    RootObject->SetStringField("description", Description);
    RootObject->SetStringField("title", Name);
    return RootObject;
}

bool UMCPToolPropertyBool::GetValue(FString InJson)
{
    TSharedPtr<FJsonObject> JsonObject;
    TSharedRef<TJsonReader<>> JsonReader = TJsonReaderFactory<>::Create(InJson);
    if (FJsonSerializer::Deserialize(JsonReader, JsonObject))
    {
        TSharedPtr<FJsonObject> ParamsObject = JsonObject->GetObjectField(TEXT("params"));
        if (ParamsObject.IsValid())
        {
            TSharedPtr<FJsonObject> ArgumentsObject = ParamsObject->GetObjectField(TEXT("arguments"));
            if (ArgumentsObject.IsValid() && ArgumentsObject->HasField(Name))
            {
                if (ArgumentsObject->HasTypedField<EJson::Boolean>(Name))
                {
                    return ArgumentsObject->GetBoolField(Name);
                }

                if (ArgumentsObject->HasTypedField<EJson::String>(Name))
                {
                    FString Value = ArgumentsObject->GetStringField(Name);
                    Value.TrimStartAndEndInline();
                    Value = Value.ToLower();
                    return Value == TEXT("true") || Value == TEXT("1") || Value == TEXT("yes");
                }
            }
        }
    }

    return false;
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
	// JSON Schema numeric type
	RootObject->SetStringField("type", StaticEnum<EMCPJsonType>()->GetNameStringByValue(static_cast<int64>(Type)));
	RootObject->SetStringField("description", Description);
	RootObject->SetStringField("title", Name);
	RootObject->SetNumberField("minimum", Min);
	RootObject->SetNumberField("maximum", Max);
	RootObject->SetNumberField("default", Default);
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
	RootObject->SetStringField("type", StaticEnum<EMCPJsonType>()->GetNameStringByValue(static_cast<int64>(Type)));
	RootObject->SetStringField("description", Description);
	RootObject->SetStringField("title", Name);
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
        // UE_LOG(LogTemp, Log, TEXT("FindActors: Searching for actors of class %s"), *ActorClass->GetName());
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
    // UE_LOG(LogTemp, Log, TEXT("FindActors: Found %d actors"), Actors.Num());

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
	RootObject->SetStringField("type", StaticEnum<EMCPJsonType>()->GetNameStringByValue(static_cast<int64>(Type)));
	RootObject->SetStringField("description", Description);
	RootObject->SetStringField("title", Name);
	// Use enum to constrain to available actor labels (if populated)
	TArray<TSharedPtr<FJsonValue>> EnumArray;
	for (auto i : ActorMap) {
		TSharedPtr<FJsonValue> EnumValue = MakeShareable(new FJsonValueString(i.Key));
		EnumArray.Add(EnumValue);
	}
	if (EnumArray.Num() > 0) {
		RootObject->SetArrayField("enum", EnumArray);
	}
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
		}
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
	// JSON Schema for array parameter
	TSharedPtr<FJsonObject> RootObject = MakeShareable(new FJsonObject);
	RootObject->SetStringField("type", "array");
	RootObject->SetStringField("description", Description);
	RootObject->SetStringField("title", Name);
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

bool UMCPToolBlueprintLibrary::GetBoolValue(const FMCPTool& MCPTool, const FString& Name, const FString& InJson,
    bool& OutValue)
{
    if (UMCPToolProperty* Property = GetProperty(MCPTool, Name)) {
        if (UMCPToolPropertyBool* PropertyBool = Cast<UMCPToolPropertyBool>(Property)) {
            OutValue = PropertyBool->GetValue(InJson);
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
//
//         // 通过流式 HTTP 通道发送消息
// 		MCPTransportSubsystem->QueueStreamPayload(SessionId, JsonMessage);
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

  MCPTransportSubsystem->QueueStreamPayload(SessionId, JsonMessage); 
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

    // 通过流式 HTTP 通道发送消息
    MCPTransportSubsystem->QueueStreamPayload(SessionId, JsonMessage);

	}
}


// ===== MCP ComponentPtr property implementation =====
UMCPToolProperty* UMCPToolPropertyComponentPtr::CreateComponentPtrProperty(FString InName, FString InDescription, TSubclassOf<UMcpExposableBaseComponent> InComponentClass)
{
	UMCPToolPropertyComponentPtr* Property = NewObject<UMCPToolPropertyComponentPtr>();
	Property->Name = InName;
	Property->Type = EMCPJsonType::String;
	Property->Description = InDescription;
	Property->ComponentClass = InComponentClass;
	// Pre-enumerate once
	Property->GetAvailableTargets();
	return Property;
}

static void BuildUniqueLabels(const TArray<UMcpExposableBaseComponent*>& Comps, TMap<FString, TWeakObjectPtr<UMcpExposableBaseComponent>>& OutMap)
{
	OutMap.Empty();
	// Build a sortable list with base label and a stable tie-breaker to ensure deterministic numbering
	struct FEntry { FString BaseLabel; FString TieBreaker; UMcpExposableBaseComponent* Comp; };
	TArray<FEntry> Entries; Entries.Reserve(Comps.Num());
	for (UMcpExposableBaseComponent* Comp : Comps)
	{
		if (!IsValid(Comp)) continue;
		FString BaseLabel = Comp->GetMcpLabel();
		if (BaseLabel.IsEmpty())
		{
			const AActor* Owner = Comp->GetOwner();
			BaseLabel = FString::Printf(TEXT("%s • %s • %s"), Owner ? *Owner->GetName() : TEXT("<NoOwner>"), *Comp->GetClass()->GetName(), *Comp->GetName());
		}
		// Use object path as stable tie-breaker within a session
		const FString Path = Comp->GetPathName();
		Entries.Add({ BaseLabel, Path, Comp });
	}
	Entries.Sort([](const FEntry& A, const FEntry& B){
		if (A.BaseLabel != B.BaseLabel) return A.BaseLabel < B.BaseLabel;
		return A.TieBreaker < B.TieBreaker;
	});
	TMap<FString, int32> Counts;
	for (const FEntry& E : Entries)
	{
		int32& C = Counts.FindOrAdd(E.BaseLabel);
		C++;
		const FString FinalLabel = (C > 1) ? FString::Printf(TEXT("%s #%d"), *E.BaseLabel, C) : E.BaseLabel;
		OutMap.Add(FinalLabel, E.Comp);
	}
}

TSharedPtr<FJsonObject> UMCPToolPropertyComponentPtr::GetJsonObject()
{
	// Build enum list from registry (if available on current thread)
	GetAvailableTargets();
	TSharedPtr<FJsonObject> RootObject = MakeShareable(new FJsonObject);
	RootObject->SetStringField("type", StaticEnum<EMCPJsonType>()->GetNameStringByValue(static_cast<int64>(Type)));
	RootObject->SetStringField("description", Description);
	RootObject->SetStringField("title", Name);

	TArray<TSharedPtr<FJsonValue>> EnumArray;
	for (const auto& KVP : ComponentMap)
	{
		EnumArray.Add(MakeShareable(new FJsonValueString(KVP.Key)));
	}
	if (EnumArray.Num() > 0) {
		RootObject->SetArrayField("enum", EnumArray);
	}
	return RootObject;
}

TArray<FString> UMCPToolPropertyComponentPtr::GetAvailableTargets()
{
	ComponentMap.Empty();
	// Find a valid game world
	UWorld* World = nullptr;
	if (GEngine)
	{
		for (const FWorldContext& Ctx : GEngine->GetWorldContexts())
		{
			if (Ctx.World() && Ctx.World()->IsGameWorld()) { World = Ctx.World(); break; }
		}
	}
	TArray<FString> Labels;
	if (!World) return Labels;
	UMcpComponentRegistrySubsystem* Sys = World->GetSubsystem<UMcpComponentRegistrySubsystem>();
	if (!Sys) return Labels;
	TArray<UMcpExposableBaseComponent*> Comps;
	TSubclassOf<UMcpExposableBaseComponent> BaseClassToUse = ComponentClass ? ComponentClass.Get() : UMcpExposableBaseComponent::StaticClass();
	Sys->Enumerate(BaseClassToUse, Comps);
	BuildUniqueLabels(Comps, ComponentMap);
	for (const auto& KVP : ComponentMap)
	{
		if (KVP.Value.IsValid()) Labels.Add(KVP.Key);
	}
	return Labels;
}

UActorComponent* UMCPToolPropertyComponentPtr::GetComponentByLabel(const FString& InLabel)
{
	if (const TWeakObjectPtr<UMcpExposableBaseComponent>* Found = ComponentMap.Find(InLabel))
	{
		return Found->IsValid() ? Found->Get() : nullptr;
	}
	return nullptr;
}

UActorComponent* UMCPToolPropertyComponentPtr::GetValue(FString InJson)
{
	TSharedPtr<FJsonObject> JsonObject;
	TSharedRef<TJsonReader<>> JsonReader = TJsonReaderFactory<>::Create(InJson);
	if (FJsonSerializer::Deserialize(JsonReader, JsonObject))
	{
		TSharedPtr<FJsonObject> ParamsObject = JsonObject->GetObjectField(TEXT("params"));
		if (ParamsObject.IsValid())
		{
			TSharedPtr<FJsonObject> ArgumentsObject = ParamsObject->GetObjectField(TEXT("arguments"));
			if (ArgumentsObject.IsValid())
			{
				const FString Label = ArgumentsObject->GetStringField(Name);
				return GetComponentByLabel(Label);
			}
		}
	}
	return nullptr;
}

// === Blueprint library: GetComponentValue ===
bool UMCPToolBlueprintLibrary::GetComponentValue(const FMCPTool& MCPTool, const FString& Name, const FString& InJson, UActorComponent*& OutValue)
{
	if (UMCPToolProperty* Property = GetProperty(MCPTool, Name))
	{
		if (UMCPToolPropertyComponentPtr* Prop = Cast<UMCPToolPropertyComponentPtr>(Property))
		{
			OutValue = Prop->GetValue(InJson);
			return OutValue != nullptr;
		}
	}
	OutValue = nullptr;
	return false;
}


// === Introspection: list all registered tools with their params and available targets ===
TSharedPtr<FJsonObject> UMCPTransportSubsystem::BuildAllRegisteredToolsJsonObject() const
{
	TSharedPtr<FJsonObject> Root = MakeShareable(new FJsonObject);
	TArray<TSharedPtr<FJsonValue>> ToolsArray;

	for (const TPair<FString, FMCPToolStorage>& Pair : MCPTools)
	{
		const FMCPTool& CanonTool = Pair.Value.MCPTool;
		TSharedPtr<FJsonObject> ToolObj = MakeShareable(new FJsonObject);
		ToolObj->SetStringField(TEXT("name"), CanonTool.Name);
		ToolObj->SetStringField(TEXT("description"), CanonTool.Description);

		// Legacy properties array (kept for backward compatibility)
		TArray<TSharedPtr<FJsonValue>> PropArray;
		// JSON Schema input schema
		TSharedPtr<FJsonObject> InputSchema = MakeShareable(new FJsonObject);
		InputSchema->SetStringField(TEXT("type"), TEXT("object"));
		TSharedPtr<FJsonObject> PropertiesObject = MakeShareable(new FJsonObject);
		TArray<TSharedPtr<FJsonValue>> RequiredArray;
		for (UMCPToolProperty* Prop : CanonTool.Properties)
		{
			if (!Prop) { continue; }
			TSharedPtr<FJsonObject> PropObj = Prop->GetJsonObject();
			// Attach available targets (legacy) and merge into schema as enum when possible
			TArray<FString> Targets;
			if (IsInGameThread())
			{
				Targets = Prop->GetAvailableTargets();
			}
			if (Targets.Num() > 0)
			{
				TArray<TSharedPtr<FJsonValue>> TargetVals;
				for (const FString& T : Targets)
				{
					TargetVals.Add(MakeShareable(new FJsonValueString(T)));
				}
				// legacy targets field
				PropObj->SetArrayField(TEXT("targets"), TargetVals);
				// schema enum
				TSharedPtr<FJsonObject> SchemaFrag = MakeShareable(new FJsonObject);
				// Start from Prop->GetJsonObject() to include type/desc/title
				SchemaFrag = Prop->GetJsonObject();
				SchemaFrag->SetArrayField(TEXT("enum"), TargetVals);
				PropertiesObject->SetObjectField(Prop->Name, SchemaFrag);
			}
			else
			{
				PropertiesObject->SetObjectField(Prop->Name, Prop->GetJsonObject());
			}
			RequiredArray.Add(MakeShareable(new FJsonValueString(Prop->Name)));
			PropArray.Add(MakeShareable(new FJsonValueObject(PropObj)));
		}
		InputSchema->SetObjectField(TEXT("properties"), PropertiesObject);
		InputSchema->SetArrayField(TEXT("required"), RequiredArray);
		ToolObj->SetObjectField(TEXT("inputSchema"), InputSchema);
		ToolObj->SetArrayField(TEXT("properties"), PropArray);

		// optionally include variant count
		ToolObj->SetNumberField(TEXT("variantCount"), Pair.Value.MCPToolVariants.Num());

		ToolsArray.Add(MakeShareable(new FJsonValueObject(ToolObj)));
	}

	Root->SetArrayField(TEXT("tools"), ToolsArray);
	Root->SetNumberField(TEXT("count"), ToolsArray.Num());
	return Root;
}

FString UMCPTransportSubsystem::GetAllRegisteredToolsJson()
{
	TSharedPtr<FJsonObject> Root = BuildAllRegisteredToolsJsonObject();
	FString Out;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);
	Out.ReplaceInline(TEXT("\n"), TEXT(""));
	Out.ReplaceInline(TEXT("\r"), TEXT(""));
	Out.ReplaceInline(TEXT("\t"), TEXT(""));
	return Out;
}

FString UMCPTransportSubsystem::GetAllRegisteredToolsJson_Safe()
{
	if (IsInGameThread())
	{
		return GetAllRegisteredToolsJson();
	}
	FString Result;
	FEvent* Done = FPlatformProcess::GetSynchEventFromPool(false);
	// 使用弱引用捕获，避免子系统销毁后悬空指针
	TWeakObjectPtr<UMCPTransportSubsystem> WeakThis(this);
	FFunctionGraphTask::CreateAndDispatchWhenReady([WeakThis, &Result, Done]()
	{
		if (WeakThis.IsValid())
		{
			Result = WeakThis->GetAllRegisteredToolsJson();
		}
		else
		{
			Result = TEXT("{\"count\":0,\"tools\":[]}");
		}
		Done->Trigger();
	}, TStatId(), nullptr, ENamedThreads::GameThread);
	// Avoid returning the FEvent to the pool before the GameThread task runs, which can crash when the
	// lambda calls Done->Trigger() after a timeout-based early return. Block until signaled to ensure
	// ownership remains valid and eliminate the use-after-free.
	Done->Wait();
	FPlatformProcess::ReturnSynchEventToPool(Done);
	if (Result.IsEmpty())
	{
		TSharedPtr<FJsonObject> Root = MakeShareable(new FJsonObject);
		TArray<TSharedPtr<FJsonValue>> ToolsArray;
		for (const TPair<FString, FMCPToolStorage>& Pair : MCPTools)
		{
			const FMCPTool& CanonTool = Pair.Value.MCPTool;
			TSharedPtr<FJsonObject> ToolObj = MakeShareable(new FJsonObject);
			ToolObj->SetStringField(TEXT("name"), CanonTool.Name);
			ToolObj->SetStringField(TEXT("description"), CanonTool.Description);
			// Legacy properties array
			TArray<TSharedPtr<FJsonValue>> PropArray;
			// JSON Schema input schema
			TSharedPtr<FJsonObject> InputSchema = MakeShareable(new FJsonObject);
			InputSchema->SetStringField(TEXT("type"), TEXT("object"));
			TSharedPtr<FJsonObject> PropertiesObject = MakeShareable(new FJsonObject);
			TArray<TSharedPtr<FJsonValue>> RequiredArray;
			for (UMCPToolProperty* Prop : CanonTool.Properties)
			{
				if (!Prop) continue;
				TSharedPtr<FJsonObject> PropObj = Prop->GetJsonObject();
				PropArray.Add(MakeShareable(new FJsonValueObject(PropObj)));
				PropertiesObject->SetObjectField(Prop->Name, Prop->GetJsonObject());
				RequiredArray.Add(MakeShareable(new FJsonValueString(Prop->Name)));
			}
			InputSchema->SetObjectField(TEXT("properties"), PropertiesObject);
			InputSchema->SetArrayField(TEXT("required"), RequiredArray);
			ToolObj->SetObjectField(TEXT("inputSchema"), InputSchema);
			ToolObj->SetArrayField(TEXT("properties"), PropArray);
			ToolObj->SetNumberField(TEXT("variantCount"), Pair.Value.MCPToolVariants.Num());
			ToolsArray.Add(MakeShareable(new FJsonValueObject(ToolObj)));
		}
		Root->SetArrayField(TEXT("tools"), ToolsArray);
		Root->SetNumberField(TEXT("count"), ToolsArray.Num());
		FString Out;
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
		FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);
		Out.ReplaceInline(TEXT("\n"), TEXT(""));
		Out.ReplaceInline(TEXT("\r"), TEXT(""));
		Out.ReplaceInline(TEXT("\t"), TEXT(""));
		return Out;
	}
	return Result;
}

// === Added HTTP GET handlers for tools listing and simple UI ===
int UMCPTransportSubsystem::OnGetTools(struct mg_connection* Connection, void* UserData)
{
	auto* This = static_cast<UMCPTransportSubsystem*>(UserData);
	FString Json = This ? This->GetAllRegisteredToolsJson_Safe() : TEXT("{\"count\":0,\"tools\":[]}");
	FTCHARToUTF8 JsonUtf8(*Json);
	int32 Len = JsonUtf8.Length();
	mg_printf(Connection,
		"HTTP/1.1 200 OK\r\n"
		"Content-Type: application/json; charset=utf-8\r\n"
		"Access-Control-Allow-Origin: *\r\n"
		"Content-Length: %d\r\n\r\n%.*s",
		(int)Len, (int)Len, JsonUtf8.Get());
	return 200;
}

int UMCPTransportSubsystem::OnGetToolsVersion(struct mg_connection* Connection, void* UserData)
{
	auto* This = static_cast<UMCPTransportSubsystem*>(UserData);
	FString Json = This ? This->GetAllRegisteredToolsJson_Safe() : TEXT("{}");
	uint32 Hash = GetTypeHash(Json);
	FString Version = FString::Printf(TEXT("%u"), Hash);
	FString Body = FString::Printf(TEXT("{\"version\":\"%s\"}"), *Version);
	FTCHARToUTF8 BodyUtf8(*Body);
	int32 Len = BodyUtf8.Length();
	mg_printf(Connection,
		"HTTP/1.1 200 OK\r\n"
		"Content-Type: application/json; charset=utf-8\r\n"
		"Access-Control-Allow-Origin: *\r\n"
		"Content-Length: %d\r\n\r\n%.*s",
		(int)Len, (int)Len, BodyUtf8.Get());
	return 200;
}

int UMCPTransportSubsystem::OnGetToolsUI(struct mg_connection* Connection, void* UserData)
{
	static const char* Html = R"HTML(<!DOCTYPE html><html lang="zh-CN"><head><meta charset="utf-8">
	<meta name="viewport" content="width=device-width, initial-scale=1">
	<title>MCP 工具监视器</title>
	<style>
	body{font-family:Segoe UI,Arial; margin:14px; background:transparent;}
	.muted{color:#6b7280} .pill{display:inline-block;padding:2px 8px;border-radius:999px;background:#eef2ff;color:#4f46e5;margin-left:8px}
	#grid{display:grid;grid-template-columns:1fr;gap:12px}
	.card{border:1px solid #e5e7eb;border-radius:10px;padding:12px;background:#fff;box-shadow:0 4px 12px rgba(0,0,0,.04)}
	pre{background:#0c0f14;color:#9fd;padding:10px;overflow:auto;max-height:64vh;border-radius:8px;font-size:12px}
	button{padding:4px 10px;border:0;border-radius:8px;background:#3066ff;color:#fff;cursor:pointer;margin-right:6px} button.ghost{background:#eef2ff;color:#374151} button:hover{filter:brightness(1.06)}
	input[type=text]{padding:6px 8px;border:1px solid #e5e7eb;border-radius:8px;width:220px}
	details{border:1px solid #eef2ff;border-radius:8px;margin:6px 0;background:#fafbff} details>summary{padding:8px 10px;cursor:pointer;list-style:none;font-weight:600} details[open]>summary{border-bottom:1px solid #eef2ff}
	.tool-head{display:flex;align-items:center;gap:8px} .tool-desc{font-size:12px;margin:4px 0 8px 0;color:#6b7280}
	.prop{display:flex;align-items:center;gap:8px;padding:6px 10px;border-bottom:1px dashed #eef2ff} .prop:last-child{border-bottom:0}
	.type{font-size:12px;padding:1px 6px;border-radius:999px;background:#f3f4f6;color:#374151} .chip{display:inline-block;background:#f0f3ff;border:1px solid #d9e2ff;color:#334155;border-radius:999px;padding:2px 8px;margin:2px 4px;font-size:12px}
	.targets{padding:4px 0 0 0} .tg summary{font-size:12px;color:#4f46e5} .right{float:right;color:#94a3b8;font-weight:500}
	.toolbar{display:flex;align-items:center;gap:8px;margin:6px 0 10px 0}
	</style></head><body>
	<div class="toolbar">
	  <input id="filter" type="text" placeholder="过滤工具名称...">
	  <span class="muted" id="ts" style="margin-left:auto"></span>
	</div>
	<div id="grid">
	  <div class="card"><h3 style="margin:6px 0 10px 0">工具</h3><div id="tools"></div></div>
	</div>
	<script>
	var OPEN = {}; function saveOpen(){ var o={}; Array.prototype.forEach.call(document.querySelectorAll('details.tool'), function(d){ if(d.open){o[d.getAttribute('data-name')] = 1;} }); OPEN=o; }
	function restoreOpen(){ Array.prototype.forEach.call(document.querySelectorAll('details.tool'), function(d){ var n=d.getAttribute('data-name'); if(OPEN[n]) d.open=true; }); }
	function esc(s){ return (s||'').toString().replace(/[&<>"']/g, function(c){return ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;','\'':'&#39;'}[c]);}); }
	function renderTargets(arr){ if(!arr||!arr.length) return '<span class="muted">-</span>'; var first=arr.slice(0,5).map(function(t){return '<span class="chip">'+esc(t)+'</span>';}).join(''); if(arr.length<=5) return first; var rest=arr.slice(5).map(function(t){return '<span class="chip">'+esc(t)+'</span>';}).join(''); return first + '<details class="tg"><summary>展开其余 '+(arr.length-5)+' 个</summary>'+rest+'</details>'; }
	function propRow(name, schema, legacy){ var typ=esc((schema && schema.type) || ''), desc=esc((schema && schema.description) || ''), title=esc((schema && schema.title) || name); var def=(schema&&schema.default!==undefined)?(' 默认: '+esc(schema.default)):''; var range=''; if(schema){ if(schema.minimum!==undefined||schema.maximum!==undefined){ range=' 范围: '+(schema.minimum!==undefined?schema.minimum:'-∞')+' ~ '+(schema.maximum!==undefined?schema.maximum:'+∞'); } }
	  var hasEnum = !!(schema && Array.isArray(schema.enum) && schema.enum.length);
	  var enumHtml=''; if(hasEnum){ enumHtml = '<div>可选值：'+schema.enum.map(function(v){ return '<span class="chip">'+esc(v)+'</span>'; }).join('')+'</div>'; }
	  var tgHtml=''; if(!hasEnum && legacy && Array.isArray(legacy.targets) && legacy.targets.length){ tgHtml = '<div class="targets">'+renderTargets(legacy.targets)+'</div>'; }
	  return '<div class="prop"><div><strong>'+title+'</strong> <span class="type">'+typ+'</span><div class="muted">'+desc+def+range+'</div>'+enumHtml+tgHtml+'</div></div>'; }
	function toolBlock(t){ var name=esc(t.name), desc=esc(t.description||''); var schema=t.inputSchema||{}; var propsObj=(schema.properties)||{}; var keys=Object.keys(propsObj); var body=''; if(keys.length){ body = keys.map(function(k){ return propRow(k, propsObj[k], (t.properties||[]).find(function(p){return p.title===k||p.name===k;})||{} ); }).join(''); } else { body='<div class="muted" style="padding:8px 10px">无参数</div>'; } return '<details class="tool" data-name="'+name+'"><summary><span class="tool-head">'+name+'<span class="right">' + keys.length + ' 参数</span></span></summary><div class="tool-desc">'+desc+'</div>'+body+'</details>'; }
	function applyFilter(){ var kw=(document.getElementById('filter').value||'').toLowerCase(); Array.prototype.forEach.call(document.querySelectorAll('details.tool'), function(d){ var n=(d.getAttribute('data-name')||'').toLowerCase(); d.style.display = (!kw || n.indexOf(kw)>=0) ? '' : 'none'; }); }
	async function refresh(){
	  try{ var r = await fetch('/tools'); var j = await r.json();
	    document.getElementById('ts').textContent = new Date().toLocaleTimeString();
	    var cont = document.getElementById('tools'); saveOpen(); cont.innerHTML='';
	    var tools = j.tools||[]; if(!tools.length){ cont.innerHTML = '<div class=\"muted\">暂无工具</div>'; } else { for(var i=0;i<tools.length;i++){ var t=tools[i]; var div=document.createElement('div'); div.innerHTML = toolBlock(t); cont.appendChild(div.firstChild); } }
	    restoreOpen(); applyFilter();
	  }catch(e){ document.getElementById('ts').textContent = '加载失败: '+e; }
	}
	document.getElementById('filter').addEventListener('input', applyFilter);
	let lastVersion = null; let base=15000, max=60000, cur=base; let paused=false;
	async function checkVersion(){
	  if(paused){ setTimeout(checkVersion, cur); return; }
	  try{
	    const r = await fetch('/tools/version'); if(!r.ok) throw new Error(r.status);
	    const j = await r.json();
	    if(j && j.version !== lastVersion){ lastVersion = j.version; await refresh(); cur = base; }
	    else { cur = base; }
	  }catch(e){ cur = Math.min(max, cur*2); }
	  setTimeout(checkVersion, cur);
	}
	document.addEventListener('visibilitychange', function(){ paused = document.hidden; if(!paused){ cur=100; checkVersion(); }});
	refresh(); checkVersion();
	</script></body></html>)HTML";
	mg_printf(Connection,
		"HTTP/1.1 200 OK\r\n"
		"Content-Type: text/html; charset=utf-8\r\n"
		"Access-Control-Allow-Origin: *\r\n\r\n%s", Html);
	return 200;
}


// Minimal favicon handler to avoid 404 noise in browser consoles
static int OnGetFavicon(struct mg_connection* Connection, void* UserData)
{
	(void)UserData;
	mg_printf(Connection,
		"HTTP/1.1 204 No Content\r\n"
		"Content-Length: 0\r\n"
		"Access-Control-Allow-Origin: *\r\n\r\n");
	return 204;
}



