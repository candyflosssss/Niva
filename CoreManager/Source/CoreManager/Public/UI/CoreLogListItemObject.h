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
};
