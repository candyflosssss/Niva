#include "McpComponentRegistrySubsystem.h"
#include "McpExposableBaseComponent.h"

void UMcpComponentRegistrySubsystem::RegisterComponent(UMcpExposableBaseComponent* Comp)
{
	if (!IsValid(Comp)) return;
	Registered.Add(Comp);
}

void UMcpComponentRegistrySubsystem::UnregisterComponent(UMcpExposableBaseComponent* Comp)
{
	Registered.Remove(Comp);
}

void UMcpComponentRegistrySubsystem::Enumerate(TSubclassOf<UMcpExposableBaseComponent> BaseClass, TArray<UMcpExposableBaseComponent*>& OutComponents) const
{
	OutComponents.Reset();
	for (const TWeakObjectPtr<UMcpExposableBaseComponent>& Weak : Registered)
	{
		UMcpExposableBaseComponent* Comp = Weak.Get();
		if (!IsValid(Comp)) continue;
		if (BaseClass && !Comp->IsA(BaseClass)) continue;
		OutComponents.Add(Comp);
	}
}
