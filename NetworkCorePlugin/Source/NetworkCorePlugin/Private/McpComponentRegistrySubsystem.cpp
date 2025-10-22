#include "McpComponentRegistrySubsystem.h"
#include "McpExposableBaseComponent.h"

void UMcpComponentRegistrySubsystem::RegisterComponent(UMcpExposableBaseComponent* Comp)
{
	if (!Comp) return;
	Registered.Add(Comp);
}

void UMcpComponentRegistrySubsystem::UnregisterComponent(UMcpExposableBaseComponent* Comp)
{
	if (!Comp) return;
	Registered.Remove(Comp);
}

void UMcpComponentRegistrySubsystem::Enumerate(TSubclassOf<UMcpExposableBaseComponent> BaseClass, TArray<UMcpExposableBaseComponent*>& OutComponents) const
{
	OutComponents.Reset();
	for (const TWeakObjectPtr<UMcpExposableBaseComponent>& WeakComp : Registered)
	{
		if (WeakComp.IsValid())
		{
			UMcpExposableBaseComponent* C = WeakComp.Get();
			if (!BaseClass || C->IsA(BaseClass))
			{
				OutComponents.Add(C);
			}
		}
	}
}
