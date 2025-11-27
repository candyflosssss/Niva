#include "Components/Base/McpExposableBaseComponent.h"
#include "Engine/World.h"
#include "Subsystems/WorldSubsystem.h"
#include "Subsystems/McpComponentRegistrySubsystem.h"
#include "GameFramework/Actor.h"

UMcpExposableBaseComponent::UMcpExposableBaseComponent()
{
    // Defaults can also be set via default member initializers
}

bool UMcpExposableBaseComponent::ShouldExposeToMcp_Implementation() const
{
    return bExposeToMcp;
}

FString UMcpExposableBaseComponent::GetMcpLabel_Implementation() const
{
    if (!McpLabel.IsEmpty())
    {
        return McpLabel;
    }
    const AActor* Owner = GetOwner();
    const FString OwnerName = Owner ? Owner->GetName() : TEXT("<NoOwner>");
    return FString::Printf(TEXT("%s • %s • %s"), *OwnerName, *GetClass()->GetName(), *GetName());
}

bool UMcpExposableBaseComponent::IsMcpUsable_Implementation(const UObject* /*Context*/, FString& OutReason) const
{
    if (bUsableByDefault)
    {
        OutReason.Reset();
        return true;
    }
    OutReason = NotUsableReason.IsEmpty() ? TEXT("Not usable") : NotUsableReason;
    return false;
}

void UMcpExposableBaseComponent::OnRegister()
{
    Super::OnRegister();
    if (!GetWorld()) return;
    if (!ShouldExposeToMcp()) return;
    if (UMcpComponentRegistrySubsystem* Sys = GetWorld()->GetSubsystem<UMcpComponentRegistrySubsystem>())
    {
        Sys->RegisterComponent(this);
    }
}

void UMcpExposableBaseComponent::OnUnregister()
{
    if (GetWorld())
    {
        if (UMcpComponentRegistrySubsystem* Sys = GetWorld()->GetSubsystem<UMcpComponentRegistrySubsystem>())
        {
            Sys->UnregisterComponent(this);
        }
    }
    Super::OnUnregister();
}
