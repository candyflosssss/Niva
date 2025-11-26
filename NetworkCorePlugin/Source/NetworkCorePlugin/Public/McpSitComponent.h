#pragma once

#include "CoreMinimal.h"
#include "McpExposableBaseComponent.h"
#include "McpSitComponent.generated.h"

/**
 * Component exposing two editable transforms in the level: SitPoint and WayPoint.
 *
 * - Only translation and rotation are meant to be edited in viewport (no scale handling in visualizer).
 * - Provides helpers to read both points in local and world space.
 */
UCLASS(BlueprintType, Blueprintable, ClassGroup=(MCP), meta=(BlueprintSpawnableComponent))
class NETWORKCOREPLUGIN_API UMcpSitComponent : public UMcpExposableBaseComponent
{
	GENERATED_BODY()
public:
	UMcpSitComponent();

	// Sit point (relative to owner). Edited via custom component visualizer in editor.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MCP|Sit")
	FTransform SitPoint;

	// Way point (relative to owner). Edited via custom component visualizer in editor.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MCP|Sit")
	FTransform WayPoint;

	// Get both points (local/component space)
	UFUNCTION(BlueprintCallable, BlueprintPure, Category="MCP|Sit")
	void GetPoints(FTransform& OutSitPoint, FTransform& OutWayPoint) const;

	// Individual getters (local/component space)
	UFUNCTION(BlueprintCallable, BlueprintPure, Category="MCP|Sit")
	FTransform GetSitPoint() const { return SitPoint; }

	UFUNCTION(BlueprintCallable, BlueprintPure, Category="MCP|Sit")
	FTransform GetWayPoint() const { return WayPoint; }

	// World-space helpers
	UFUNCTION(BlueprintCallable, BlueprintPure, Category="MCP|Sit")
	FTransform GetSitPointWorld() const;

	UFUNCTION(BlueprintCallable, BlueprintPure, Category="MCP|Sit")
	FTransform GetWayPointWorld() const;

	UFUNCTION(BlueprintCallable, BlueprintPure, Category="MCP|Sit")
	void GetPointsWorld(FTransform& OutSitPointWorld, FTransform& OutWayPointWorld) const;
};
