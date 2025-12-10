// Copyright Epic Games, Inc.

#include "UI/CoreLogListView.h"
#include "UI/CoreLogListItemObject.h"

UCoreLogListView::UCoreLogListView(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
    // 默认显示除 Off 之外的所有等级
    SeverityFilter.Empty();
    SeverityFilter.Add(ECoreLogSeverity::Trace, true);
    SeverityFilter.Add(ECoreLogSeverity::Debug, true);
    SeverityFilter.Add(ECoreLogSeverity::Info,  true);
    SeverityFilter.Add(ECoreLogSeverity::Warn,  true);
    SeverityFilter.Add(ECoreLogSeverity::Error, true);
    SeverityFilter.Add(ECoreLogSeverity::Fatal, true);
    SeverityFilter.Add(ECoreLogSeverity::Off,   false);
}

void UCoreLogListView::SetSeverityFilterEnabled(ECoreLogSeverity Severity, bool bEnabled)
{
    bool* Found = SeverityFilter.Find(Severity);
    if (Found)
    {
        *Found = bEnabled;
    }
    else
    {
        SeverityFilter.Add(Severity, bEnabled);
    }
    RefreshFilter();
}

void UCoreLogListView::SetSeverityFilterMap(const TMap<ECoreLogSeverity, bool>& InMap)
{
    SeverityFilter = InMap;
    RefreshFilter();
}

void UCoreLogListView::RefreshFilter()
{
    RebuildFromFullItems();
}

UCoreLogListItemObject* UCoreLogListView::AddLogEntry(const FCoreLogEntry& Entry)
{
    UCoreLogListItemObject* ItemObj = NewObject<UCoreLogListItemObject>(this);
    if (ItemObj)
    {
        ItemObj->Entry = Entry;
        FullItems.Add(ItemObj);
        RebuildFromFullItems();
    }
    return ItemObj;
}

void UCoreLogListView::SetAllItems(const TArray<UObject*>& InAllItems)
{
    FullItems = InAllItems;
    RebuildFromFullItems();
}

void UCoreLogListView::OnItemsChanged(const TArray<UObject*>& AddedItems, const TArray<UObject*>& RemovedItems)
{
    // 当我们内部调用 SetListItems 时，UE 会触发 OnItemsChanged。
    // 若此时再次重建会导致无限递归，这里直接透传给父类并返回。
    if (bIsRebuilding)
    {
        Super::OnItemsChanged(AddedItems, RemovedItems);
        return;
    }

    // 合并新增项到 FullItems；忽略移除项（保持完整数组），随后用 FullItems 重建显示
    for (UObject* Item : AddedItems)
    {
        if (Item)
        {
            FullItems.AddUnique(Item);
        }
    }

    // 不从 FullItems 删除，以满足“完整 object 数组”的需求
    RebuildFromFullItems();

    Super::OnItemsChanged(AddedItems, RemovedItems);
}

bool UCoreLogListView::ShouldShowItem(UObject* Item) const
{
    if (!Item)
    {
        return false;
    }

    const UCoreLogListItemObject* LogItem = Cast<UCoreLogListItemObject>(Item);
    if (!LogItem)
    {
        // 未知类型，默认显示
        return true;
    }

    const bool* bShow = SeverityFilter.Find(LogItem->Entry.Severity);
    return bShow ? *bShow : true;
}

void UCoreLogListView::RebuildFromFullItems()
{
    if (bIsRebuilding)
    {
        return;
    }

    bIsRebuilding = true;
    TArray<UObject*> Filtered;
    Filtered.Reserve(FullItems.Num());

    for (UObject* Item : FullItems)
    {
        if (ShouldShowItem(Item))
        {
            Filtered.Add(Item);
        }
    }

    // 仅更新显示项，不破坏 FullItems
    // 使用公开的 SetListItems 而不是私有的 BP_SetListItems
    SetListItems(Filtered);
    bIsRebuilding = false;
}
