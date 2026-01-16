#include "Log/CoreLogHelpers.h"
#include "Async/Async.h"

void FCoreLogHelpers::CoreLog(const UObject* Self, ECoreLogSeverity Severity, const FString& Module, const FString& Flow, const FString& Message)
{
    if (!Self) return;

    if (IsInGameThread())
    {
        if (UCoreLogSubsystem* LogSS = UCoreLogSubsystem::Get(Self))
        {
            LogSS->Log(*Module, *Flow, Severity, Message);
        }
    }
    else
    {
        // Capture Self as WeakPtr to avoid accessing invalid pointer on GT
        TWeakObjectPtr<const UObject> WeakSelf(Self);
        const FString M = Module; 
        const FString F = Flow; 
        const FString Msg = Message;
        
        AsyncTask(ENamedThreads::GameThread, [WeakSelf, Severity, M, F, Msg]()
        {
            if (const UObject* S = WeakSelf.Get())
            {
                if (UCoreLogSubsystem* LogSS = UCoreLogSubsystem::Get(S))
                {
                    LogSS->Log(*M, *F, Severity, Msg);
                }
            }
        });
    }
}

void FCoreLogHelpers::CoreLog(const UObject* Self, ECoreLogSeverity Severity, const FString& Module, const FString& Flow, const FString& Message, const TMap<FString, FString>& Data)
{
    if (!Self) return;

    if (IsInGameThread())
    {
        if (UCoreLogSubsystem* LogSS = UCoreLogSubsystem::Get(Self))
        {
            LogSS->Log(*Module, *Flow, Severity, Message, Data);
        }
    }
    else
    {
        TWeakObjectPtr<const UObject> WeakSelf(Self);
        const FString M = Module; 
        const FString F = Flow; 
        const FString Msg = Message;
        const TMap<FString, FString> D = Data;
        
        AsyncTask(ENamedThreads::GameThread, [WeakSelf, Severity, M, F, Msg, D]()
        {
            if (const UObject* S = WeakSelf.Get())
            {
                if (UCoreLogSubsystem* LogSS = UCoreLogSubsystem::Get(S))
                {
                    LogSS->Log(*M, *F, Severity, Msg, D);
                }
            }
        });
    }
}
