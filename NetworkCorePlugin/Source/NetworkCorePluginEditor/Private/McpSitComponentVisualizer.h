#pragma once

#include "CoreMinimal.h"
#include "ComponentVisualizer.h"

class UMcpSitComponent;

struct HMcpSitVisProxy : public HComponentVisProxy
{
	DECLARE_HIT_PROXY();

	HMcpSitVisProxy(const UActorComponent* InComponent, int32 InIndex)
		: HComponentVisProxy(InComponent, HPP_Wireframe)
		, Index(InIndex)
	{}

	int32 Index; // 0 = SitPoint, 1 = WayPoint
};

class FMcpSitComponentVisualizer : public FComponentVisualizer
{
public:
	FMcpSitComponentVisualizer();
	virtual ~FMcpSitComponentVisualizer() override;

	virtual void DrawVisualization(const UActorComponent* Component, const FSceneView* View, FPrimitiveDrawInterface* PDI) override;
	virtual bool VisProxyHandleClick(FEditorViewportClient* InViewportClient, HComponentVisProxy* VisProxy, const FViewportClick& Click) override;
	virtual bool GetWidgetLocation(const FEditorViewportClient* ViewportClient, FVector& OutLocation) const override;
	virtual bool HandleInputDelta(FEditorViewportClient* ViewportClient, FViewport* Viewport, FVector& InDrag, FRotator& InRot, FVector& InScale) override;
	virtual void EndEditing() override;

private:
	TWeakObjectPtr<UMcpSitComponent> SelectedComp;
	int32 SelectedIndex = INDEX_NONE;
	bool bEditing = false;
};
