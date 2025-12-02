// JasmineLatte - CoreManager Log Subsystem

#include "Log/CoreLogSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"

DEFINE_LOG_CATEGORY_STATIC(LogCoreManagerLog, Log, All);

UCoreLogSubsystem* UCoreLogSubsystem::Get(const UObject* WorldContextObject)
{
    if (!WorldContextObject)
    {
        return nullptr;
    }
    const UWorld* World = WorldContextObject->GetWorld();
    if (!World)
    {
        return nullptr;
    }
    UGameInstance* GI = World->GetGameInstance();
    return GI ? GI->GetSubsystem<UCoreLogSubsystem>() : nullptr;
}

void UCoreLogSubsystem::Log(FName Category1, FName Category2, ECoreLogSeverity Severity, const FString& Message, const TMap<FString,FString>& Data)
{
    FCoreLogEntry Entry;
    Entry.Category1 = Category1;
    Entry.Category2 = Category2;
    Entry.Severity = Severity;
    Entry.Message = Message;
    Entry.Data = Data;

    // 将 Data 格式化为 a=b,c=d 形式
    FString DataStr;
    bool bFirst = true;
    for (const TPair<FString,FString>& KV : Data)
    {
        if (!bFirst)
        {
            DataStr += TEXT(", ");
        }
        bFirst = false;
        DataStr += FString::Printf(TEXT("%s=%s"), *KV.Key, *KV.Value);
    }

    // 输出到 UE 控制台（可按需调整）
    switch (Severity)
    {
    case ECoreLogSeverity::Error:
        UE_LOG(LogCoreManagerLog, Error, TEXT("[%s/%s] %s | %s"), *Category1.ToString(), *Category2.ToString(), *Message, *DataStr);
        break;
    case ECoreLogSeverity::Warning:
        UE_LOG(LogCoreManagerLog, Warning, TEXT("[%s/%s] %s | %s"), *Category1.ToString(), *Category2.ToString(), *Message, *DataStr);
        break;
    default:
        UE_LOG(LogCoreManagerLog, Log, TEXT("[%s/%s] %s | %s"), *Category1.ToString(), *Category2.ToString(), *Message, *DataStr);
        break;
    }

    // 广播给订阅者
    OnLog.Broadcast(Entry);
    OnLogNative.Broadcast(Entry);
}
