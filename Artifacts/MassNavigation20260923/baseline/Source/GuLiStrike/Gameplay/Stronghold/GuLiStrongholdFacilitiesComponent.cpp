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
	bool bSearching = false;
	for (int32 Index = 0; Index < Slots.Num(); ++Index)
	{
		auto& Slot = Slots[Index];
		if (Slot.bDelivered) continue;
		const TArray<FTransform> Candidates = GuLiBuildings::GetGiftPlacementCandidates(Lifecycle.GetGroundLocation(), Index);
		int32& Cursor = PlacementSearchCursor.FindOrAdd(Index);
		FString Failure;
		// Do not turn one capture into a long game-thread placement/navigation burst.
		// Resume the deterministic search next tick, and preserve undelivered slots.
		const int32 End = FMath::Min(Cursor + 4, Candidates.Num());
		for (; Cursor < End; ++Cursor)
		{
			FTransform GroundTransform;
			AActor* SupportingActor = nullptr;
			if (!GuLiBuildings::ProjectPlacementCandidate(*GetWorld(), Candidates[Cursor], GetOwner(), GroundTransform, SupportingActor))
			{
				Failure = TEXT("No valid supporting terrain at the candidate center.");
				continue;
			}
			Slot.Building = GuLiBuildings::Spawn(*GetWorld(), Slot.DefinitionId, Lifecycle.GetTeam(), GroundTransform,
				*SupportingActor, Lifecycle.GetState().TerritoryIndex, EGuLiBuildingOrigin::Gift, true, FGuid(), &Failure);
			Slot.bDelivered = IsValid(Slot.Building);
			if (Slot.bDelivered)
			{
				UE_LOG(LogTemp, Display, TEXT("[GULI_GIFT_PLACEMENT] Delivered owner=%s slot=%d definition=%d candidate=%d location=%s yaw=%.0f"),
					*GetOwner()->GetName(), Index, Slot.DefinitionId, Cursor, *GroundTransform.GetLocation().ToCompactString(), GroundTransform.Rotator().Yaw);
				LastPlacementFailure.Remove(Index);
				break;
			}
		}
		if (!Slot.bDelivered)
		{
			if (Cursor >= Candidates.Num())
			{
				Cursor = 0;
				if (!LastPlacementFailure.Contains(Index) || LastPlacementFailure[Index] != Failure)
				{
					UE_LOG(LogTemp, Warning, TEXT("[GULI_GIFT_PLACEMENT] Pending owner=%s slot=%d definition=%d: %s"),
						*GetOwner()->GetName(), Index, Slot.DefinitionId, *Failure);
					LastPlacementFailure.Add(Index, Failure);
				}
			}
			else bSearching = true;
		}
		bPending |= !Slot.bDelivered;
	}
	SetComponentTickInterval(bSearching ? 0.2f : 1.0f);
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
