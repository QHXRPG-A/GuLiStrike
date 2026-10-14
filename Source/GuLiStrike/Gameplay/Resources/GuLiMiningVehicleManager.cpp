#include "Gameplay/Resources/GuLiMiningVehicleManager.h"
#include "Gameplay/Resources/GuLiMiningVehiclePawn.h"
#include "Gameplay/Resources/GuLiResourceFactoryActor.h"
#include "Gameplay/Resources/GuLiResourceWorldSubsystem.h"
#include "Gameplay/Resources/GuLiResourceMapDefinition.h"
#include "Gameplay/Data/GuLiUnitDataSubsystem.h"
#include "Components/CapsuleComponent.h"
#include "Components/ChildActorComponent.h"
#include "Engine/World.h"
#include "Network/GuLiNetworkSettings.h"
#include "Network/GuLiHybridActorComponent.h"

bool UGuLiMiningVehicleManager::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	return Super::ShouldCreateSubsystem(Outer) && World && World->IsGameWorld();
}

void UGuLiMiningVehicleManager::Deinitialize()
{
	GetWorld()->GetSubsystem<UGuLiDynamicObstacleRegistrySubsystem>()->StaticRegionChanged.RemoveAll(this);
	SlotProfiles.Reset(); RejectedSlots.Reset();
	ClearHybridOperations();
	Vehicles.Reset();
	Super::Deinitialize();
}

void UGuLiMiningVehicleManager::RegisterVehicle(AGuLiMiningVehiclePawn& Vehicle)
{
	for (AGuLiMiningVehiclePawn* Other : Vehicles)
	{
		Vehicle.GetCapsuleComponent()->IgnoreActorWhenMoving(Other, true);
		Other->GetCapsuleComponent()->IgnoreActorWhenMoving(&Vehicle, true);
	}
	Vehicles.AddUnique(&Vehicle);
}

void UGuLiMiningVehicleManager::UnregisterVehicle(AGuLiMiningVehiclePawn& Vehicle)
{
	RejectedSlots.Remove(&Vehicle);
	for (auto& Profile : SlotProfiles) for (auto& Cluster : Profile.Value.Clusters) for (auto& Slot : Cluster.Slots)
		if (Slot.Owner.Get()==&Vehicle) { Slot.Owner.Reset(); Slot.Task=0; }
	Vehicles.Remove(&Vehicle);
	for (AGuLiMiningVehiclePawn* Other : Vehicles) Other->GetCapsuleComponent()->IgnoreActorWhenMoving(&Vehicle, false);
}

void UGuLiMiningVehicleManager::IgnoreVehicles(FCollisionQueryParams& Params) const
{
	for (const AGuLiMiningVehiclePawn* Vehicle : Vehicles)
	{
		Params.AddIgnoredActor(Vehicle);
		if (const auto* Presentation = Vehicle->FindComponentByClass<UChildActorComponent>())
			Params.AddIgnoredActor(Presentation->GetChildActor());
	}
}

bool UGuLiMiningVehicleManager::OwnsNode(const AGuLiMiningVehiclePawn& Vehicle, const uint32 NodeId) const
{
	if (const auto* Binding=UGuLiHybridActorComponent::Find(&Vehicle))
	{
		FGuLiHybridMiningLease Lease; const auto* H=GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>();
		return H && H->GetMiningLease(Binding->GetObjectKey(),Lease) && Lease.NodeId==static_cast<int32>(NodeId) && NodeId!=0;
	}
	return false;
}

int32 UGuLiMiningVehicleManager::GetAssignedNode(const AGuLiMiningVehiclePawn* Vehicle) const
{
	if (const auto* Binding=UGuLiHybridActorComponent::Find(Vehicle))
	{
		FGuLiHybridMiningLease Lease; const auto* H=GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>();
		return H && H->GetMiningLease(Binding->GetObjectKey(),Lease)?Lease.NodeId:0;
	}
	return 0;
}
