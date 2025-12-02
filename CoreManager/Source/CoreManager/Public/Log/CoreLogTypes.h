// Copyright Epic Games, Inc.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/ScriptMacros.h"
#include "CoreLogTypes.generated.h"

// 严重级别
UENUM(BlueprintType)
enum class ECoreLogSeverity : uint8
{
    Normal,
    Warning,
    Error
};

// 额外数据由键值表承载（蓝图/C++ 统一使用 TMap<String, String>）

// 一条日志记录
USTRUCT(BlueprintType)
struct COREMANAGER_API FCoreLogEntry
{
    GENERATED_BODY()

    // 一级分类
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CoreLog")
    FName Category1;

    // 二级分类
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CoreLog")
    FName Category2;

    // 严重级别
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CoreLog")
    ECoreLogSeverity Severity = ECoreLogSeverity::Normal;

    // 主要文本
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CoreLog")
    FString Message;

    // 附加键值表（支持多键值）
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CoreLog")
    TMap<FString, FString> Data;
};

// 动态多播委托（供蓝图订阅）
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnCoreLogEvent, const FCoreLogEntry&, Entry);

// 原生多播委托（供 C++ 订阅）
DECLARE_MULTICAST_DELEGATE_OneParam(FOnCoreLogEventNative, const FCoreLogEntry& /*Entry*/);
