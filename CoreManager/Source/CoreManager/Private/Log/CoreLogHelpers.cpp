#include "Log/CoreLogHelpers.h"

void FCoreLogHelpers::CoreLog(const UObject* Self, ECoreLogSeverity Severity, const FString& Module, const FString& Flow, const FString& Message)
{
    if (!Self) return;
    if (UCoreLogSubsystem* LogSS = UCoreLogSubsystem::Get(Self))
    {
        LogSS->Log(*Module, *Flow, Severity, Message);
    }
}

void FCoreLogHelpers::CoreLog(const UObject* Self, ECoreLogSeverity Severity, const FString& Module, const FString& Flow, const FString& Message, const TMap<FString, FString>& Data)
{
    if (!Self) return;
    if (UCoreLogSubsystem* LogSS = UCoreLogSubsystem::Get(Self))
    {
        LogSS->Log(*Module, *Flow, Severity, Message, Data);
    }
}

