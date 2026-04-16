#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"

// HAS_CORE_MANAGER is defined dynamically in CustomInputController.Build.cs
#if HAS_CORE_MANAGER
    #include "Log/CoreLogTypes.h"
    #include "Log/CoreLogHelpers.h"
#else
    // If CoreManager isn't available, we define our own basic severity enum to compile cleanly
    enum class ECoreLogSeverity : uint8
    {
        Trace,
        Debug,
        Info,
        Warn,
        Error,
        Fatal
    };
#endif

class FCICLogHelpers
{
public:
    static void CoreLog(const UObject* Self, ECoreLogSeverity Severity, const FString& Module, const FString& Flow, const FString& Message)
    {
#if HAS_CORE_MANAGER
        FCoreLogHelpers::CoreLog(Self, Severity, Module, Flow, Message);
#else
        UE_LOG(LogTemp, Log, TEXT("[%s][%s] %s"), *Module, *Flow, *Message);
#endif
    }

    static void CoreLog(const UObject* Self, ECoreLogSeverity Severity, const FString& Module, const FString& Flow, const FString& Message, const TMap<FString, FString>& Data)
    {
#if HAS_CORE_MANAGER
        FCoreLogHelpers::CoreLog(Self, Severity, Module, Flow, Message, Data);
#else
        UE_LOG(LogTemp, Log, TEXT("[%s][%s] %s"), *Module, *Flow, *Message);
#endif
    }
};

