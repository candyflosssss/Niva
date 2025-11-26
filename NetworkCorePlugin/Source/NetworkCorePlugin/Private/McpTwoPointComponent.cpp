#include "McpTwoPointComponent.h"
#include "GameFramework/Actor.h"

UMcpTwoPointComponent::UMcpTwoPointComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	PointA = FVector::ZeroVector;
	PointB = FVector(100.f, 0.f, 0.f);
}

void UMcpTwoPointComponent::GetPoints(FVector& OutPointA, FVector& OutPointB) const
{
	OutPointA = PointA;
	OutPointB = PointB;
}

FVector UMcpTwoPointComponent::GetPointAWorld() const
{
	if (const AActor* Owner = GetOwner())
	{
		return Owner->GetActorTransform().TransformPosition(PointA);
	}
	return PointA;
}

FVector UMcpTwoPointComponent::GetPointBWorld() const
{
	if (const AActor* Owner = GetOwner())
	{
		return Owner->GetActorTransform().TransformPosition(PointB);
	}
	return PointB;
}

void UMcpTwoPointComponent::GetPointsWorld(FVector& OutPointAWorld, FVector& OutPointBWorld) const
{
	if (const AActor* Owner = GetOwner())
	{
		const FTransform T = Owner->GetActorTransform();
		OutPointAWorld = T.TransformPosition(PointA);
		OutPointBWorld = T.TransformPosition(PointB);
	}
	else
	{
		OutPointAWorld = PointA;
		OutPointBWorld = PointB;
	}
}
