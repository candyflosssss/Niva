#include "McpSitComponentVisualizer.h"

#include "McpSitComponent.h"

#include "Editor.h"
#include "Editor/UnrealEdEngine.h"
#include "SceneManagement.h"
#include "ScopedTransaction.h"
#include "UnrealEdGlobals.h"
#include "EditorModeManager.h"

IMPLEMENT_HIT_PROXY(HMcpSitVisProxy, HComponentVisProxy);

FMcpSitComponentVisualizer::FMcpSitComponentVisualizer() = default;
FMcpSitComponentVisualizer::~FMcpSitComponentVisualizer() = default;

static void DrawTransformOriginAndForward(FPrimitiveDrawInterface* PDI, const FTransform& WorldXform, const FLinearColor& Color, float Length = 30.f, float Thickness = 2.f)
{
	// Draw a small point at origin
	PDI->DrawPoint(WorldXform.GetLocation(), Color, 12.f, SDPG_Foreground);
	// Draw forward arrow (X axis)
	const FMatrix ArrowTM = WorldXform.ToMatrixWithScale();
	DrawDirectionalArrow(PDI, ArrowTM, Color, Length, 8.f, SDPG_Foreground, Thickness);
}

void FMcpSitComponentVisualizer::DrawVisualization(const UActorComponent* Component, const FSceneView* View, FPrimitiveDrawInterface* PDI)
{
	const UMcpSitComponent* Comp = Cast<const UMcpSitComponent>(Component);
	if (!Comp)
	{
		return;
	}

	const AActor* Owner = Comp->GetOwner();
	const FTransform OwnerXform = Owner ? Owner->GetActorTransform() : FTransform::Identity;

	const FTransform SitW = Comp->GetSitPoint() * OwnerXform;
	const FTransform WayW = Comp->GetWayPoint() * OwnerXform;

	// Draw line between points
	PDI->DrawLine(SitW.GetLocation(), WayW.GetLocation(), FLinearColor::Yellow, SDPG_Foreground, 2.0f);

	// SitPoint visuals (green) with hit proxy
	PDI->SetHitProxy(new HMcpSitVisProxy(Comp, 0));
	DrawTransformOriginAndForward(PDI, SitW, FLinearColor::Green);
	PDI->SetHitProxy(nullptr);

	// WayPoint visuals (red) with hit proxy
	PDI->SetHitProxy(new HMcpSitVisProxy(Comp, 1));
	DrawTransformOriginAndForward(PDI, WayW, FLinearColor::Red);
	PDI->SetHitProxy(nullptr);
}

bool FMcpSitComponentVisualizer::VisProxyHandleClick(FEditorViewportClient* InViewportClient, HComponentVisProxy* VisProxy, const FViewportClick& Click)
{
	SelectedComp.Reset();
	SelectedIndex = INDEX_NONE;

	if (VisProxy && VisProxy->Component.IsValid() && VisProxy->IsA(HMcpSitVisProxy::StaticGetType()))
	{
		HMcpSitVisProxy* Proxy = static_cast<HMcpSitVisProxy*>(VisProxy);
		const UMcpSitComponent* CompConst = Cast<UMcpSitComponent>(Proxy->Component.Get());
		UMcpSitComponent* CompNonConst = const_cast<UMcpSitComponent*>(CompConst);
		SelectedComp = CompNonConst;
		SelectedIndex = Proxy->Index;

		// Ensure the transform widget becomes visible immediately and the viewport refreshes
		if (InViewportClient)
		{
			// In UE5.5, toggle the transform widget via Mode Tools (viewport client no longer exposes SetShowWidget)
			GLevelEditorModeTools().SetShowWidget(true);
			InViewportClient->Invalidate();
		}
		return true;
	}
	return false;
}

bool FMcpSitComponentVisualizer::GetWidgetLocation(const FEditorViewportClient* ViewportClient, FVector& OutLocation) const
{
	if (!SelectedComp.IsValid() || SelectedIndex == INDEX_NONE)
	{
		return false;
	}
	const UMcpSitComponent* Comp = SelectedComp.Get();
	const AActor* Owner = Comp ? Comp->GetOwner() : nullptr;
	const FTransform OwnerXform = Owner ? Owner->GetActorTransform() : FTransform::Identity;
	const FTransform Local = (SelectedIndex == 0) ? Comp->GetSitPoint() : Comp->GetWayPoint();
	const FTransform World = Local * OwnerXform;
	OutLocation = World.GetLocation();
	return true;
}

bool FMcpSitComponentVisualizer::HandleInputDelta(FEditorViewportClient* ViewportClient, FViewport* Viewport, FVector& InDrag, FRotator& InRot, FVector& InScale)
{
	if (!SelectedComp.IsValid() || SelectedIndex == INDEX_NONE)
	{
		return false;
	}

	UMcpSitComponent* Comp = SelectedComp.Get();
	AActor* Owner = Comp ? Comp->GetOwner() : nullptr;
	if (!Comp || !Owner)
	{
		return false;
	}

	// Begin transaction on first change
	if (!bEditing)
	{
		GEditor->BeginTransaction(NSLOCTEXT("McpSit", "EditSitWay", "Edit MCP Sit/Way Transform"));
		Comp->Modify();
		Owner->Modify();
		bEditing = true;
	}

	const FTransform OwnerXform = Owner->GetActorTransform();

	FTransform Local = (SelectedIndex == 0) ? Comp->SitPoint : Comp->WayPoint;
	FTransform World = Local * OwnerXform;

	// Apply world translation delta
	if (!InDrag.IsNearlyZero())
	{
		World.AddToTranslation(InDrag);
	}

	// Apply world rotation delta around its own origin
	if (!InRot.IsNearlyZero())
	{
		const FQuat DeltaQ = InRot.Quaternion();
		World.ConcatenateRotation(DeltaQ);
		World.NormalizeRotation();
	}

	// Convert back to local relative to owner
	const FTransform NewLocal = World.GetRelativeTransform(OwnerXform);

	if (SelectedIndex == 0)
	{
		Comp->SitPoint = NewLocal;
	}
	else
	{
		Comp->WayPoint = NewLocal;
	}

	Comp->MarkPackageDirty();
	Comp->PostEditChange();
	return true;
}

void FMcpSitComponentVisualizer::EndEditing()
{
	if (bEditing)
	{
		GEditor->EndTransaction();
		bEditing = false;
	}
	SelectedComp.Reset();
	SelectedIndex = INDEX_NONE;
}

