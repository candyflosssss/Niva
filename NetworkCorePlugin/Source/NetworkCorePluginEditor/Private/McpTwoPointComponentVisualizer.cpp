#include "McpTwoPointComponentVisualizer.h"

#include "McpTwoPointComponent.h"

#include "SceneManagement.h" // PDI helpers
#include "UnrealEdGlobals.h"
#include "Editor/UnrealEdEngine.h"
#include "Editor.h"
#include "ScopedTransaction.h"

IMPLEMENT_HIT_PROXY(HMcpTwoPointVisProxy, HComponentVisProxy);

FMcpTwoPointComponentVisualizer::FMcpTwoPointComponentVisualizer() = default;
FMcpTwoPointComponentVisualizer::~FMcpTwoPointComponentVisualizer() = default;

void FMcpTwoPointComponentVisualizer::DrawVisualization(const UActorComponent* Component, const FSceneView* View, FPrimitiveDrawInterface* PDI)
{
	const UMcpTwoPointComponent* Comp = Cast<const UMcpTwoPointComponent>(Component);
	if (!Comp)
	{
		return;
	}

	const AActor* Owner = Comp->GetOwner();
	const FTransform OwnerXform = Owner ? Owner->GetActorTransform() : FTransform::Identity;

	const FVector AWorld = OwnerXform.TransformPosition(Comp->GetPointA());
	const FVector BWorld = OwnerXform.TransformPosition(Comp->GetPointB());

	const FLinearColor LineColor = FLinearColor::Yellow;
	const FLinearColor PointColorA = FLinearColor::Green;
	const FLinearColor PointColorB = FLinearColor::Red;
	const float LineThickness = 2.0f;
	const float PointSize = 12.0f;

	// Line between A and B
	PDI->DrawLine(AWorld, BWorld, LineColor, SDPG_Foreground, LineThickness);

	// Draggable point A
	PDI->SetHitProxy(new HMcpTwoPointVisProxy(Comp, 0));
	PDI->DrawPoint(AWorld, PointColorA, PointSize, SDPG_Foreground);
	PDI->SetHitProxy(nullptr);

	// Draggable point B
	PDI->SetHitProxy(new HMcpTwoPointVisProxy(Comp, 1));
	PDI->DrawPoint(BWorld, PointColorB, PointSize, SDPG_Foreground);
	PDI->SetHitProxy(nullptr);
}

bool FMcpTwoPointComponentVisualizer::VisProxyHandleClick(FEditorViewportClient* InViewportClient, HComponentVisProxy* VisProxy, const FViewportClick& Click)
{
	SelectedComp.Reset();
	SelectedPointIndex = INDEX_NONE;

	if (VisProxy && VisProxy->Component.IsValid())
	{
		if (VisProxy->IsA(HMcpTwoPointVisProxy::StaticGetType()))
		{
			HMcpTwoPointVisProxy* Proxy = static_cast<HMcpTwoPointVisProxy*>(VisProxy);
			const UMcpTwoPointComponent* CompConst = Cast<UMcpTwoPointComponent>(Proxy->Component.Get());
			UMcpTwoPointComponent* CompNonConst = const_cast<UMcpTwoPointComponent*>(CompConst);
			SelectedComp = CompNonConst;
			SelectedPointIndex = Proxy->PointIndex;
			return true;
		}
	}
	return false;
}

bool FMcpTwoPointComponentVisualizer::GetWidgetLocation(const FEditorViewportClient* ViewportClient, FVector& OutLocation) const
{
	if (!SelectedComp.IsValid() || SelectedPointIndex == INDEX_NONE)
	{
		return false;
	}
	const UMcpTwoPointComponent* Comp = SelectedComp.Get();
	const AActor* Owner = Comp ? Comp->GetOwner() : nullptr;
	const FTransform OwnerXform = Owner ? Owner->GetActorTransform() : FTransform::Identity;
	FVector Local = (SelectedPointIndex == 0) ? Comp->GetPointA() : Comp->GetPointB();
	OutLocation = OwnerXform.TransformPosition(Local);
	return true;
}

bool FMcpTwoPointComponentVisualizer::HandleInputDelta(FEditorViewportClient* ViewportClient, FViewport* Viewport, FVector& InDrag, FRotator& InRot, FVector& InScale)
{
	if (!SelectedComp.IsValid() || SelectedPointIndex == INDEX_NONE)
	{
		return false;
	}

	UMcpTwoPointComponent* Comp = SelectedComp.Get();
	AActor* Owner = Comp ? Comp->GetOwner() : nullptr;
	if (!Comp || !Owner)
	{
		return false;
	}

	// Begin transaction on first delta
	if (!bEditing)
	{
		GEditor->BeginTransaction(NSLOCTEXT("McpTwoPoint", "MovePoint", "Move MCP TwoPoint Handle"));
		Comp->Modify();
		if (Owner)
		{
			Owner->Modify();
		}
		bEditing = true;
	}

	const FTransform OwnerXform = Owner->GetActorTransform();
	const FTransform InvOwner = OwnerXform.Inverse();

	FVector Local = (SelectedPointIndex == 0) ? Comp->GetPointA() : Comp->GetPointB();
	const FVector CurrentWorld = OwnerXform.TransformPosition(Local);
	const FVector NewWorld = CurrentWorld + InDrag; // InDrag provided in world space along widget axes
	const FVector NewLocal = InvOwner.TransformPosition(NewWorld);

	if (SelectedPointIndex == 0)
	{
		Comp->PointA = NewLocal;
	}
	else
	{
		Comp->PointB = NewLocal;
	}

	Comp->MarkPackageDirty();
	Comp->PostEditChange();

	return true;
}

void FMcpTwoPointComponentVisualizer::EndEditing()
{
	if (bEditing)
	{
		GEditor->EndTransaction();
		bEditing = false;
	}
	SelectedComp.Reset();
	SelectedPointIndex = INDEX_NONE;
}
