#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "McpComponentRegistrySubsystem.generated.h"

class UMcpExposableBaseComponent;

UCLASS()
class NETWORKCOREPLUGIN_API UMcpComponentRegistrySubsystem : public UWorldSubsystem
{
	GENERATED_BODY()
public:
	// Register / Unregister from components
	void RegisterComponent(UMcpExposableBaseComponent* Comp);
	void UnregisterComponent(UMcpExposableBaseComponent* Comp);

	// Enumerate all currently registered components derived from BaseClass
	void Enumerate(TSubclassOf<UMcpExposableBaseComponent> BaseClass, TArray<UMcpExposableBaseComponent*>& OutComponents) const;

private:
	UPROPERTY(Transient)
	TSet<TWeakObjectPtr<UMcpExposableBaseComponent>> Registered;
};
