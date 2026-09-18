#include "Gameplay/Building/GuLiConstructionWorkComponent.h"
#include "Gameplay/Building/GuLiBuildingLifecycleComponent.h"
#include "Gameplay/Units/GuLiEngineeringTravelComponent.h"
#include "Gameplay/Units/GuLiExternalUnitControlComponent.h"
#include "Battle/Combat/GuLiCombatDamageLedger.h"
#include "AIController.h"
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
	StopWork();
	const FVector Ground = Building.GetGroundLocation();
	const FVector Direction = (GetOwner()->GetActorLocation() - Ground).GetSafeNormal2D();
	WorkPosition = Ground + Direction * (Building.GetDefinition().CollisionExtent.Size2D() + 240);
	if (!GetOwner()->FindComponentByClass<UGuLiEngineeringTravelComponent>()->BeginMove(WorkPosition, 100)) return false;
	Target = &Building; SetComponentTickEnabled(true); return true;
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
}
