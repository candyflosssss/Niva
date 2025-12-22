// CoreManager - CoreLog helper functions (public)
#pragma once

#include "CoreMinimal.h"
#include "Log/CoreLogSubsystem.h"
#include "Log/CoreLogTypes.h"

/**
 * Lightweight, central helper functions to forward logs into UCoreLogSubsystem.
 * These helpers are intentionally thin and null-safe so they can be used widely
 * across plugins and modules.
 */
class COREMANAGER_API FCoreLogHelpers
{
public:

    // Generic UObject-based helpers (alias names)
    static void CoreLog(const UObject* Self, ECoreLogSeverity Severity, const FString& Module, const FString& Flow, const FString& Message);
    static void CoreLog(const UObject* Self, ECoreLogSeverity Severity, const FString& Module, const FString& Flow, const FString& Message, const TMap<FString, FString>& Data);
};



