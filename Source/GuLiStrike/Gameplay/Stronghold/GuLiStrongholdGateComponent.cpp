#include "Gameplay/Stronghold/GuLiStrongholdGateComponent.h"
#include "Gameplay/Stronghold/GuLiStrongholdGatePresentationComponent.h"
#include "Gameplay/Units/GuLiEngineeringTravelComponent.h"
#include "Gameplay/Units/GuLiExternalUnitControlComponent.h"
#include "Gameplay/Data/GuLiSpellFieldDataSubsystem.h"
#include "Gameplay/Resources/GuLiResourceWorldSubsystem.h"
#include "Battle/Combat/GuLiCombatDamageLedger.h"
#include "GameFramework/Pawn.h"
#include "NavigationSystem.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"

UGuLiStrongholdGateComponent::UGuLiStrongholdGateComponent() { SetIsReplicatedByDefault(true); }
void UGuLiStrongholdGateComponent::InitializeGate(int32 InFieldId, int32 InTerritoryIndex, const FVector& Ground)
{
	check(GetOwner()->HasAuthority());
	FieldId = InFieldId; TerritoryIndex = InTerritoryIndex; GroundLocation = Ground;
	check(GetConfig().Id == FieldId);
	OnRep_Gate(); GetOwner()->ForceNetUpdate();
}
const FGuLiStrongholdGateConfig& UGuLiStrongholdGateComponent::GetConfig() const
{
	const auto* Config = GetWorld()->GetSubsystem<UGuLiSpellFieldDataSubsystem>()->FindStrongholdGate(FieldId);
	check(Config); return *Config;
}
bool UGuLiStrongholdGateComponent::CanEnter(const APawn& Vehicle) const
{
	const auto* Engineering = Cast<IGuLiEngineeringVehicle>(&Vehicle);
	const auto* Health = Vehicle.FindComponentByClass<UGuLiCombatHealthComponent>();
	return Engineering && Health && Health->IsAlive() && !UGuLiExternalUnitControlComponent::AreActorActionsLocked(&Vehicle)
		&& GetWorld()->GetSubsystem<UGuLiResourceWorldSubsystem>()->CanUseStrongholdTransit(TerritoryIndex, Engineering->GetTeam());
}
bool UGuLiStrongholdGateComponent::FindEntry(const APawn& Vehicle, FVector& Entry) const
{
	const FVector Direction = (Vehicle.GetActorLocation() - GroundLocation).GetSafeNormal2D();
	const FVector Desired = GroundLocation + (Direction.IsNearlyZero() ? FVector::ForwardVector : Direction) * GetConfig().Radius * .75f;
	auto* Navigation = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	if (!Navigation) return false;
	const ANavigationData* Data = Navigation->GetNavDataForProps(Vehicle.GetNavAgentPropertiesRef(), Desired);
	FNavLocation Projected;
	if (!Data || !Navigation->ProjectPointToNavigation(Desired, Projected, FVector(750,750,5000), Data)) return false;
	Entry = Projected.Location;
	return FVector::DistSquared2D(Entry, GroundLocation) <= FMath::Square(GetConfig().Radius);
}
void UGuLiStrongholdGateComponent::OnRep_Gate()
{
	if (FieldId == 0 || TerritoryIndex == INDEX_NONE || GetWorld()->GetNetMode() == NM_DedicatedServer) return;
	if (!Presentation)
	{
		Presentation = NewObject<UGuLiStrongholdGatePresentationComponent>(GetOwner());
		Presentation->RegisterComponent();
	}
	CastChecked<UGuLiStrongholdGatePresentationComponent>(Presentation)->InitializePresentation(*this);
}
void UGuLiStrongholdGateComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UGuLiStrongholdGateComponent, FieldId);
	DOREPLIFETIME(UGuLiStrongholdGateComponent, TerritoryIndex);
	DOREPLIFETIME(UGuLiStrongholdGateComponent, GroundLocation);
}
