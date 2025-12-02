// JasmineLatte - CoreManager Log Subsystem

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/ScriptMacros.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Log/CoreLogTypes.h"
#include "CoreLogSubsystem.generated.h"

/**
 * Core 日志 GI 子系统
 * - 支持：一级分类、二级分类、Normal/Warning/Error
 * - 数据：FString 消息 + 键值表（蓝图/C++ 统一使用 TMap<FString,FString>）
 * - 调用：蓝图/C++
 * - 订阅：蓝图（动态多播）/C++（原生多播）
 */
UCLASS()
class COREMANAGER_API UCoreLogSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    // 获取子系统（便捷）
    UFUNCTION(BlueprintCallable, Category = "CoreManager|Log", meta=(WorldContext="WorldContextObject"))
    static UCoreLogSubsystem* Get(const UObject* WorldContextObject);

    // 蓝图/C++ 调用：记录一条日志
    UFUNCTION(BlueprintCallable, Category = "CoreManager|Log")
    void Log(FName Category1, FName Category2, ECoreLogSeverity Severity, const FString& Message, const TMap<FString,FString>& Data);

    // 便捷重载：无 Data 时
    void Log(FName Category1, FName Category2, ECoreLogSeverity Severity, const FString& Message)
    {
        static const TMap<FString,FString> Empty;
        Log(Category1, Category2, Severity, Message, Empty);
    }

    // 蓝图订阅
    UPROPERTY(BlueprintAssignable, Category = "CoreManager|Log")
    FOnCoreLogEvent OnLog;

    // C++ 订阅
    FOnCoreLogEventNative OnLogNative;
};
