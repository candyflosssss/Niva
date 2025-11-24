#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "McpExposableBaseComponent.generated.h"

class UMcpComponentRegistrySubsystem;

UCLASS(BlueprintType, Blueprintable, Abstract, ClassGroup=(MCP), meta=(BlueprintSpawnableComponent))
class NETWORKCOREPLUGIN_API UMcpExposableBaseComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UMcpExposableBaseComponent();

	// === Configurable defaults (editable variables) ===
	// Whether this component should be exposed to MCP by default.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MCP|Component")
	bool bExposeToMcp = true;

	// Optional override for the readable label shown in MCP lists. If empty, a fallback is used.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MCP|Component")
	FString McpLabel;

	// Default usability. If false, IsMcpUsable will fail and output NotUsableReason.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MCP|Component")
	bool bUsableByDefault = true;

	// When not usable by default, this reason will be returned to users.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MCP|Component", meta=(EditCondition="!bUsableByDefault"))
	FString NotUsableReason;

	// Should be exposed to MCP. Default reads from bExposeToMcp so subclasses can opt-out or data-drive.
	UFUNCTION(BlueprintNativeEvent, Category="MCP|Component")
	bool ShouldExposeToMcp() const;
	virtual bool ShouldExposeToMcp_Implementation() const;

	// Human readable label used for listing and selection. Defaults to McpLabel or auto-generated fallback.
	UFUNCTION(BlueprintNativeEvent, Category="MCP|Component")
	FString GetMcpLabel() const;
	virtual FString GetMcpLabel_Implementation() const;

	// Dynamic usability at call time and optionally at list time. Defaults to bUsableByDefault/NotUsableReason.
	UFUNCTION(BlueprintNativeEvent, Category="MCP|Component")
	bool IsMcpUsable(const UObject* Context, FString& OutReason) const;
	virtual bool IsMcpUsable_Implementation(const UObject* /*Context*/, FString& /*OutReason*/) const;

protected:
	virtual void OnRegister() override;
	virtual void OnUnregister() override;
};
