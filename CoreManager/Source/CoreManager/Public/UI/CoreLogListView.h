// Copyright Epic Games, Inc.

#pragma once

#include "CoreMinimal.h"
#include "Components/ListView.h"
#include "Log/CoreLogTypes.h"
#include "CoreLogListView.generated.h"

class UCoreLogListItemObject;

/**
 * 可根据 ECoreLogSeverity 进行显示过滤的 ListView（不删除原始数据，仅隐藏不显示的项）。
 */
UCLASS(BlueprintType)
class COREMANAGER_API UCoreLogListView : public UListView
{
    GENERATED_BODY()

public:
    UCoreLogListView(const FObjectInitializer& ObjectInitializer);

    // 设置/读取过滤开关
    UFUNCTION(BlueprintCallable, Category="CoreLog|UI")
    void SetSeverityFilterEnabled(ECoreLogSeverity Severity, bool bEnabled);

    UFUNCTION(BlueprintCallable, Category="CoreLog|UI")
    void SetSeverityFilterMap(const TMap<ECoreLogSeverity, bool>& InMap);

    // 注意：UFUNCTION 不支持返回引用类型，因此按值返回，并标记 BlueprintPure
    UFUNCTION(BlueprintPure, Category="CoreLog|UI")
    TMap<ECoreLogSeverity, bool> GetSeverityFilterMap() const { return SeverityFilter; }

    // 刷新一次过滤（当 FullItems 有变化或过滤条件变更时调用）
    UFUNCTION(BlueprintCallable, Category="CoreLog|UI")
    void RefreshFilter();

    // 直接添加一条日志（会创建 UCoreLogListItemObject 存入 FullItems，并按过滤显示）
    UFUNCTION(BlueprintCallable, Category="CoreLog|UI")
    UCoreLogListItemObject* AddLogEntry(const FCoreLogEntry& Entry);

    // 一次性设置完整数据（FullItems），不会丢失，只按过滤显示
    UFUNCTION(BlueprintCallable, Category="CoreLog|UI")
    void SetAllItems(const TArray<UObject*>& InAllItems);

protected:
    // 同步 FullItems 与外部对 ListItems 的增删（例如蓝图直接 Add/Remove 到此 ListView）
    virtual void OnItemsChanged(const TArray<UObject*>& AddedItems, const TArray<UObject*>& RemovedItems) override;

    bool ShouldShowItem(UObject* Item) const;
    void RebuildFromFullItems();

protected:
    // 完整的数据列表（不随过滤删除）
    UPROPERTY(Transient)
    TArray<UObject*> FullItems;

    // 严重级别过滤表（true=显示，false=隐藏）
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="CoreLog|UI")
    TMap<ECoreLogSeverity, bool> SeverityFilter;

private:
    // 防止 SetListItems 触发的 OnItemsChanged 导致的递归重入（无限循环）
    bool bIsRebuilding = false;
};
