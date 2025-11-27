#include "Components/Interaction/McpSitComponent.h"
#include "GameFramework/Actor.h"

UMcpSitComponent::UMcpSitComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SitPoint = FTransform::Identity;
	WayPoint = FTransform(FRotator::ZeroRotator, FVector(100.f, 0.f, 0.f), FVector(1.f));
}

void UMcpSitComponent::GetPoints(FTransform& OutSitPoint, FTransform& OutWayPoint) const
{
	OutSitPoint = SitPoint;
	OutWayPoint = WayPoint;
}

FTransform UMcpSitComponent::GetSitPointWorld() const
{
	if (const AActor* Owner = GetOwner())
	{
		return SitPoint * Owner->GetActorTransform();
	}
	return SitPoint;
}

FTransform UMcpSitComponent::GetWayPointWorld() const
{
	if (const AActor* Owner = GetOwner())
	{
		return WayPoint * Owner->GetActorTransform();
	}
	return WayPoint;
}

void UMcpSitComponent::GetPointsWorld(FTransform& OutSitPointWorld, FTransform& OutWayPointWorld) const
{
	if (const AActor* Owner = GetOwner())
	{
		const FTransform OwnerXform = Owner->GetActorTransform();
		OutSitPointWorld = SitPoint * OwnerXform;
		OutWayPointWorld = WayPoint * OwnerXform;
	}
	else
	{
		OutSitPointWorld = SitPoint;
		OutWayPointWorld = WayPoint;
	}
}
