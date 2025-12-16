#include "Audio/AudioStreamHttpWsComponent.h"
#include "Audio/AudioStreamHttpWsSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"

// CoreManager logging
#include "Log/CoreLogSubsystem.h"
#include "Log/CoreLogTypes.h"

static void CoreLogForComponent(const UObject* Self, ECoreLogSeverity Severity, const FString& Message)
{
    if (!Self) return;
    if (UCoreLogSubsystem* LogSS = UCoreLogSubsystem::Get(Self))
    {
        LogSS->Log(TEXT("StreamRegistry"), TEXT("RegistrationProcess"), Severity, Message);
    }
}

UAudioStreamHttpWsComponent::UAudioStreamHttpWsComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
}

void UAudioStreamHttpWsComponent::BeginPlay()
{
    Super::BeginPlay();
    RegisterToSubsystem();
}

void UAudioStreamHttpWsComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    UnregisterFromSubsystem();
    Super::EndPlay(EndPlayReason);
}

void UAudioStreamHttpWsComponent::RegisterToSubsystem()
{
    if (bRegistered) return;
    UGameInstance* GI = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
    if (!GI) return;
    UAudioStreamHttpWsSubsystem* Subsys = GI->GetSubsystem<UAudioStreamHttpWsSubsystem>();
    if (!Subsys) return;

    FString OutUuid;
    if (Subsys->RegisterComponent(this, OutUuid))
    {
        RegisteredUuid = OutUuid;
        bRegistered = true;
        const FString Msg = FString::Printf(TEXT("Component registered uuid=%s"), *RegisteredUuid);
        UE_LOG(LogTemp, Log, TEXT("[AudioStream][Registry] %s"), *Msg);
        CoreLogForComponent(this, ECoreLogSeverity::Info, Msg);
    }
    else
    {
        const FString Msg = FString(TEXT("Component registration failed"));
        UE_LOG(LogTemp, Warning, TEXT("[AudioStream][Registry] %s"), *Msg);
        CoreLogForComponent(this, ECoreLogSeverity::Warn, Msg);
    }
}

void UAudioStreamHttpWsComponent::UnregisterFromSubsystem()
{
    if (!bRegistered) return;
    UGameInstance* GI = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
    if (!GI) return;
    UAudioStreamHttpWsSubsystem* Subsys = GI->GetSubsystem<UAudioStreamHttpWsSubsystem>();
    if (!Subsys) return;

    Subsys->UnregisterComponent(this);
    const FString Msg = FString::Printf(TEXT("Component unregistered uuid=%s"), *RegisteredUuid);
    UE_LOG(LogTemp, Log, TEXT("[AudioStream][Registry] %s"), *Msg);
    CoreLogForComponent(this, ECoreLogSeverity::Info, Msg);

    bRegistered = false;
    RegisteredUuid.Reset();
}
