#pragma once

#include "CoreMinimal.h"
#include "Components/Base/McpExposableBaseComponent.h"
#include "McpTwoPointComponent.generated.h"

/**
 * Component that exposes two editable points (vectors) in the level editor.
 *
 * - Points are editable and movable in the viewport via MakeEditWidget.
 * - Provides helper methods to read both points from code/Blueprints.
 */
UCLASS(BlueprintType, Blueprintable, ClassGroup=(MCP), meta=(BlueprintSpawnableComponent))
class NETWORKCOREPLUGIN_API UMcpTwoPointComponent : public UMcpExposableBaseComponent
{
    GENERATED_BODY()
public:
    UMcpTwoPointComponent();

    // First point (relative to owner actor). Visible and movable in editor.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MCP|TwoPoint", meta=(MakeEditWidget=true))
    FVector PointA;

    // Second point (relative to owner actor). Visible and movable in editor.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MCP|TwoPoint", meta=(MakeEditWidget=true))
    FVector PointB;

    // Get both points at once (component/local space).
    UFUNCTION(BlueprintCallable, BlueprintPure, Category="MCP|TwoPoint")
    void GetPoints(FVector& OutPointA, FVector& OutPointB) const;

    // Getters for individual points (component/local space).
    UFUNCTION(BlueprintCallable, BlueprintPure, Category="MCP|TwoPoint")
    FVector GetPointA() const { return PointA; }

    UFUNCTION(BlueprintCallable, BlueprintPure, Category="MCP|TwoPoint")
    FVector GetPointB() const { return PointB; }

    // World-space helpers
    UFUNCTION(BlueprintCallable, BlueprintPure, Category="MCP|TwoPoint")
    FVector GetPointAWorld() const;

    UFUNCTION(BlueprintCallable, BlueprintPure, Category="MCP|TwoPoint")
    FVector GetPointBWorld() const;

    UFUNCTION(BlueprintCallable, BlueprintPure, Category="MCP|TwoPoint")
    void GetPointsWorld(FVector& OutPointAWorld, FVector& OutPointBWorld) const;
};
