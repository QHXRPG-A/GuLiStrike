#include "Gameplay/Stronghold/GuLiStrongholdFacilitiesComponent.h"
#include "Gameplay/Building/GuLiBuildingLifecycleComponent.h"
#include "Engine/World.h"

UGuLiStrongholdFacilitiesComponent::UGuLiStrongholdFacilitiesComponent()
{
	SetIsReplicatedByDefault(false);
	PrimaryComponentTick.bCanEverTick=false;
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

    HybridJobs.SetNum(Slots.Num());
}
