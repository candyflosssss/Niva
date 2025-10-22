#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "McpExposableBaseComponent.generated.h"

UCLASS(BlueprintType, Blueprintable, Abstract)
class NETWORKCOREPLUGIN_API UMcpExposableBaseComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	// 可读名（用于 MCP 参数的枚举与选择键）。默认: OwnerName • Type • Instance
	UFUNCTION(BlueprintNativeEvent, Category="NetworkCore|MCP|Component")
	FString GetMcpReadableName() const;
	virtual FString GetMcpReadableName_Implementation() const;

	// 可用性（调用期可选校验），默认可用
	UFUNCTION(BlueprintNativeEvent, Category="NetworkCore|MCP|Component")
	bool IsMcpUsable(const UObject* RequestContext, FString& OutReason) const;
	virtual bool IsMcpUsable_Implementation(const UObject* /*RequestContext*/, FString& /*OutReason*/) const { return true; }

protected:
	virtual void OnRegister() override;
	virtual void OnUnregister() override;
};
