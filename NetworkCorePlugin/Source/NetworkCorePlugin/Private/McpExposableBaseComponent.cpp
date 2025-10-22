#include "McpExposableBaseComponent.h"
#include "McpComponentRegistrySubsystem.h"
#include "Engine/World.h"

FString UMcpExposableBaseComponent::GetMcpReadableName_Implementation() const
{
	AActor* Owner = GetOwner();
	const FString OwnerName = Owner ? Owner->GetName() : TEXT("<None>");
	const FString TypeName = GetClass() ? GetClass()->GetName() : TEXT("Component");
	return FString::Printf(TEXT("%s • %s • %s"), *OwnerName, *TypeName, *GetName());
}

void UMcpExposableBaseComponent::OnRegister()
{
	Super::OnRegister();
	if (UWorld* World = GetWorld())
	{
		if (UMcpComponentRegistrySubsystem* Reg = World->GetSubsystem<UMcpComponentRegistrySubsystem>())
		{
			Reg->RegisterComponent(this);
		}
	}
}

void UMcpExposableBaseComponent::OnUnregister()
{
	if (UWorld* World = GetWorld())
	{
		if (UMcpComponentRegistrySubsystem* Reg = World->GetSubsystem<UMcpComponentRegistrySubsystem>())
		{
			Reg->UnregisterComponent(this);
		}
	}
	Super::OnUnregister();
}
