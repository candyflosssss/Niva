#pragma once

#include "CoreMinimal.h"
#include "ComponentVisualizer.h"

class UMcpTwoPointComponent;

/** Hit proxy to select one of the two points */
struct HMcpTwoPointVisProxy : public HComponentVisProxy
{
	DECLARE_HIT_PROXY();

	HMcpTwoPointVisProxy(const UActorComponent* InComponent, int32 InPointIndex)
		: HComponentVisProxy(InComponent, HPP_Wireframe)
		, PointIndex(InPointIndex)
	{
	}

	int32 PointIndex;
};

/** Visualizer for UMcpTwoPointComponent to draw and edit two endpoints in the world */
class FMcpTwoPointComponentVisualizer : public FComponentVisualizer
{
public:
	FMcpTwoPointComponentVisualizer();
	virtual ~FMcpTwoPointComponentVisualizer() override;

	// FComponentVisualizer interface
	virtual void DrawVisualization(const UActorComponent* Component, const FSceneView* View, FPrimitiveDrawInterface* PDI) override;
	virtual bool VisProxyHandleClick(FEditorViewportClient* InViewportClient, HComponentVisProxy* VisProxy, const FViewportClick& Click) override;
	virtual bool GetWidgetLocation(const FEditorViewportClient* ViewportClient, FVector& OutLocation) const override;
	virtual bool HandleInputDelta(FEditorViewportClient* ViewportClient, FViewport* Viewport, FVector& InDrag, FRotator& InRot, FVector& InScale) override;
	virtual void EndEditing() override;

private:
	// Cached selection
	TWeakObjectPtr<UMcpTwoPointComponent> SelectedComp;
	int32 SelectedPointIndex = INDEX_NONE; // 0 for A, 1 for B

	// Transaction management
	bool bEditing = false;
};
