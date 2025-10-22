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
	// 注册/注销
	void RegisterComponent(UMcpExposableBaseComponent* Comp);
	void UnregisterComponent(UMcpExposableBaseComponent* Comp);

	// 按可选基类过滤枚举当前注册的组件
 void Enumerate(TSubclassOf<UMcpExposableBaseComponent> BaseClass, TArray<UMcpExposableBaseComponent*>& OutComponents) const;

private:
	UPROPERTY(Transient)
	TSet<TWeakObjectPtr<UMcpExposableBaseComponent>> Registered;
};
