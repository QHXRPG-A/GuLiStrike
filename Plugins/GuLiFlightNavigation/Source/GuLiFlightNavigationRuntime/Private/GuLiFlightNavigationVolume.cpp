#include "GuLiFlightNavigationVolume.h"

#include "GuLiFlightNavigationData.h"
#include "GuLiFlightNavigationSubsystem.h"
#include "Components/BrushComponent.h"
#include "Engine/World.h"

AGuLiFlightNavigationVolume::AGuLiFlightNavigationVolume(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = false;
	SetCanBeDamaged(false);
	EnforceNoPhysicalCollision();
}

void AGuLiFlightNavigationVolume::EnforceNoPhysicalCollision()
{
	SetActorEnableCollision(false);
	if (UBrushComponent* NavigationBrush = GetBrushComponent())
	{
		NavigationBrush->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		NavigationBrush->SetCollisionResponseToAllChannels(ECR_Ignore);
		NavigationBrush->SetGenerateOverlapEvents(false);
	}
}

void AGuLiFlightNavigationVolume::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	EnforceNoPhysicalCollision();
}

void AGuLiFlightNavigationVolume::PostInitializeComponents()
{
	Super::PostInitializeComponents();
	EnforceNoPhysicalCollision();
}

bool AGuLiFlightNavigationVolume::ContainsNavigationPoint(const FVector& Point) const
{
	if (!bNavigationEnabled || NavigationData == nullptr || NavigationData->Metadata.Bounds.IsValid == 0)
	{
		return false;
	}

	// AVolume::EncompassesPoint calls GetSquaredDistanceToCollision and therefore
	// stops working when this query-only envelope correctly disables physical
	// collision. FlightNav bakes and validates the volume's axis-aligned bounds,
	// so use those same bounds for runtime containment instead of re-enabling a
	// Brush that would block Wingman world-validation sweeps.
	const FBox CurrentVolumeBounds = GetBounds().GetBox();
	return CurrentVolumeBounds.IsValid != 0
		&& CurrentVolumeBounds.IsInsideOrOn(Point)
		&& NavigationData->Metadata.Bounds.IsInsideOrOn(Point);
}

void AGuLiFlightNavigationVolume::BeginPlay()
{
	Super::BeginPlay();
	EnforceNoPhysicalCollision();
	if (UGuLiFlightNavigationSubsystem* Subsystem = GetWorld()->GetSubsystem<UGuLiFlightNavigationSubsystem>())
	{
		Subsystem->RegisterVolume(this);
	}
}

void AGuLiFlightNavigationVolume::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		if (UGuLiFlightNavigationSubsystem* Subsystem = World->GetSubsystem<UGuLiFlightNavigationSubsystem>())
		{
			Subsystem->UnregisterVolume(this);
		}
	}
	Super::EndPlay(EndPlayReason);
}
