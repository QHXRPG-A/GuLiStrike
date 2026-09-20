#include "Gameplay/Building/GuLiConstructionWorkComponent.h"
#include "Gameplay/Building/GuLiBuildingLifecycleComponent.h"
#include "Gameplay/Units/GuLiEngineeringTravelComponent.h"
#include "Gameplay/Units/GuLiExternalUnitControlComponent.h"
#include "Battle/Combat/GuLiCombatDamageLedger.h"
#include "AIController.h"
#include "Navigation/PathFollowingComponent.h"
#include "GameFramework/Pawn.h"

UGuLiConstructionWorkComponent::UGuLiConstructionWorkComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	PrimaryComponentTick.TickInterval = .1f;
}
bool UGuLiConstructionWorkComponent::AssignBuilding(UGuLiBuildingLifecycleComponent& Building)
{
	check(GetOwner()->HasAuthority());
	if (Building.GetTeam() != CastChecked<IGuLiEngineeringVehicle>(GetOwner())->GetTeam()
		|| Building.GetState().Phase != EGuLiBuildingPhase::UnderConstruction) return false;
	FVector Prepared; float Length;
	if (!PrepareBuilding(Building, Prepared, Length)) return false;
	StopWork(); WorkPosition = Prepared;
	if (!GetOwner()->FindComponentByClass<UGuLiEngineeringTravelComponent>()->BeginMove(WorkPosition, 100)) return false;
	Target = &Building; SetComponentTickEnabled(true); return true;
}
bool UGuLiConstructionWorkComponent::PrepareBuilding(const UGuLiBuildingLifecycleComponent& Building, FVector& OutPosition, float& OutPathLength) const
{
	if (Building.GetTeam() != CastChecked<IGuLiEngineeringVehicle>(GetOwner())->GetTeam()
		|| Building.GetState().Phase != EGuLiBuildingPhase::UnderConstruction) return false;
	const FVector Ground = Building.GetGroundLocation();
	const float Radius = Building.GetDefinition().CollisionExtent.Size2D() + 240;
	const auto* Travel = GetOwner()->FindComponentByClass<UGuLiEngineeringTravelComponent>();
	OutPathLength = TNumericLimits<float>::Max();
	// Try all sides without changing the active path or work target.
	for (int32 Index = 0; Index < 8; ++Index)
	{
		const double Angle = Index * UE_PI / 4;
		const FVector Position = Ground + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0) * Radius;
		float Length = 0;
		if (Travel->FindGroundPath(Position, Length) && Length < OutPathLength) { OutPathLength = Length; OutPosition = Position; }
	}
	return OutPathLength < TNumericLimits<float>::Max();
}
void UGuLiConstructionWorkComponent::StopWork()
{
	Target.Reset(); SetComponentTickEnabled(false);
}
void UGuLiConstructionWorkComponent::TickComponent(float Dt, ELevelTick TickType, FActorComponentTickFunction* Function)
{
	Super::TickComponent(Dt, TickType, Function);
	auto* Building = Target.Get();
	if (!Building || Building->GetState().Phase != EGuLiBuildingPhase::UnderConstruction
		|| Building->GetTeam() != CastChecked<IGuLiEngineeringVehicle>(GetOwner())->GetTeam()
		|| !GetOwner()->FindComponentByClass<UGuLiCombatHealthComponent>()->IsAlive()) { StopWork(); return; }
	if (UGuLiExternalUnitControlComponent::AreActorActionsLocked(GetOwner())) return;
	if (GetOwner()->FindComponentByClass<UGuLiEngineeringTravelComponent>()->IsRouting()) return;
	if (FVector::Dist2D(GetOwner()->GetActorLocation(), WorkPosition) <= 320)
		Building->AddConstructionWork(Dt);
	else if (const auto* AI = Cast<AAIController>(CastChecked<APawn>(GetOwner())->GetController()); AI && AI->GetMoveStatus() == EPathFollowingStatus::Idle)
		StopWork(); // Let the task owner report failure and reselect; a dead path cannot hold the queue forever.
}
