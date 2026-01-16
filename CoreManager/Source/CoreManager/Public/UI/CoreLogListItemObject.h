// Copyright Epic Games, Inc.

#pragma once

#include "CoreMinimal.h"
#include "Log/CoreLogTypes.h"
#include "CoreLogListItemObject.generated.h"

/**
 * ListView 的数据对象，封装一条 FCoreLogEntry
 */
UCLASS(BlueprintType)
class COREMANAGER_API UCoreLogListItemObject : public UObject
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="CoreLog")
    FCoreLogEntry Entry;

    // 游戏时间（毫秒级），在初始化/加入 ListView 时自动赋值；默认 -1 表示未设置
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="CoreLog")
    int64 GameTimeMs = -1;
};
