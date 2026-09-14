#include "Gameplay/Stronghold/GuLiStrongholdFacilitiesComponent.h"
#include "Gameplay/Building/GuLiBuildingLifecycleComponent.h"
#include "Gameplay/Building/GuLiBuildingSpawner.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"

UGuLiStrongholdFacilitiesComponent::UGuLiStrongholdFacilitiesComponent()
{
	SetIsReplicatedByDefault(true);
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	PrimaryComponentTick.TickInterval = 1;
}
void UGuLiStrongholdFacilitiesComponent::InitializeFacilities()
{
	check(GetOwner()->HasAuthority());
	const auto& Lifecycle = *GetOwner()->FindComponentByClass<UGuLiBuildingLifecycleComponent>();
	check(Lifecycle.GetDefinition().FirstCaptureGiftIds.Num() <= 4);
	for (int32 Id : Lifecycle.GetDefinition().FirstCaptureGiftIds)
	{
		auto& Slot = Slots.AddDefaulted_GetRef(); Slot.DefinitionId = Id;
	}
	HandleOwnerChanged(Lifecycle.GetTeam());
}
void UGuLiStrongholdFacilitiesComponent::HandleOwnerChanged(EGuLiTeam Team)
{
	check(GetOwner()->HasAuthority());
	for (auto& Slot : Slots)
		if (IsValid(Slot.Building) && Slot.Building->FindComponentByClass<UGuLiBuildingLifecycleComponent>()->GetState().Phase != EGuLiBuildingPhase::Destroyed)
			CastChecked<IGuLiBuildingOwner>(Slot.Building)->SetBuildingTeamAuthority(Team);
	if (!GuLiResources::IsPlayableTeam(Team)) { SetComponentTickEnabled(false); return; }
	bFirstCaptured = true;
	DeliverPendingSlots();
}
void UGuLiStrongholdFacilitiesComponent::DeliverPendingSlots()
{
	const auto& Lifecycle = *GetOwner()->FindComponentByClass<UGuLiBuildingLifecycleComponent>();
	if (!GuLiResources::IsPlayableTeam(Lifecycle.GetTeam())) return;
	bool bPending = false;
	for (int32 Index = 0; Index < Slots.Num(); ++Index)
	{
		auto& Slot = Slots[Index];
		if (Slot.bDelivered) continue;
		// Fixed east/north/west/south slots stay inside the map's surrounding ore belt.
		const double Angle = Index * UE_HALF_PI;
		const FVector Position = Lifecycle.GetGroundLocation() + FVector(FMath::Cos(Angle), FMath::Sin(Angle),0) * 7000;
		FHitResult Ground;
		FCollisionQueryParams Query(SCENE_QUERY_STAT(GuLiGiftGround), false, GetOwner());
		if (!GetWorld()->LineTraceSingleByChannel(Ground, Position + FVector(0,0,50000), Position - FVector(0,0,50000), ECC_WorldStatic, Query))
		{ bPending = true; continue; }
		Slot.Building = GuLiBuildings::Spawn(*GetWorld(), Slot.DefinitionId, Lifecycle.GetTeam(),
			FTransform(FRotator(0, FMath::RadiansToDegrees(Angle), 0), Ground.ImpactPoint), *Ground.GetActor(),
			Lifecycle.GetState().TerritoryIndex, EGuLiBuildingOrigin::Gift, true);
		Slot.bDelivered = IsValid(Slot.Building);
		bPending |= !Slot.bDelivered;
	}
	SetComponentTickEnabled(bPending);
	GetOwner()->ForceNetUpdate();
}
void UGuLiStrongholdFacilitiesComponent::TickComponent(float Dt, ELevelTick TickType, FActorComponentTickFunction* TickFunction)
{
	Super::TickComponent(Dt, TickType, TickFunction);
	DeliverPendingSlots();
}
void UGuLiStrongholdFacilitiesComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UGuLiStrongholdFacilitiesComponent, bFirstCaptured);
	DOREPLIFETIME(UGuLiStrongholdFacilitiesComponent, Slots);
}
