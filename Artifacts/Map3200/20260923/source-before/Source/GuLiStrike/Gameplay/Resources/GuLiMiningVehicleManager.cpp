#include "Gameplay/Resources/GuLiMiningVehicleManager.h"
#include "Gameplay/Resources/GuLiMiningVehiclePawn.h"
#include "Gameplay/Resources/GuLiResourceFactoryActor.h"
#include "Gameplay/Resources/GuLiResourceWorldSubsystem.h"
#include "Gameplay/Resources/GuLiResourceMapDefinition.h"
#include "Gameplay/Data/GuLiUnitDataSubsystem.h"
#include "Components/CapsuleComponent.h"
#include "Components/ChildActorComponent.h"
#include "Engine/World.h"

bool UGuLiMiningVehicleManager::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	return Super::ShouldCreateSubsystem(Outer) && World && World->IsGameWorld();
}

void UGuLiMiningVehicleManager::Deinitialize()
{
	GetWorld()->GetSubsystem<UGuLiDynamicObstacleRegistrySubsystem>()->StaticRegionChanged.RemoveAll(this);
	SlotProfiles.Reset(); RejectedSlots.Reset();
	Vehicles.Reset(); NodeOwners.Reset(); AssignedNodes.Reset();
	Super::Deinitialize();
}

AGuLiMiningVehiclePawn* UGuLiMiningVehicleManager::SpawnMiningVehicle(
	AGuLiResourceFactoryActor* Factory, FTransform Transform)
{
	if (GetWorld()->GetNetMode() == NM_Client || !IsValid(Factory)) return nullptr;
	const auto* Economy = GetWorld()->GetSubsystem<UGuLiResourceWorldSubsystem>()->GetEconomyConfig();
	const auto* Unit = Economy ? GetWorld()->GetSubsystem<UGuLiUnitDataSubsystem>()->FindDefinition(Economy->MiningVehicleUnitTypeId) : nullptr;
	if (!Unit || !Unit->ActorClass || !Unit->ActorClass->IsChildOf(AGuLiMiningVehiclePawn::StaticClass())) return nullptr;
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	auto* Vehicle = GetWorld()->SpawnActor<AGuLiMiningVehiclePawn>(Unit->ActorClass, Transform, Params);
	if (Vehicle) Vehicle->InitializeVehicle(Factory->GetTeam(),
		GetWorld()->GetSubsystem<UGuLiResourceWorldSubsystem>()->AllocateControllableActorId(), *Unit, *Economy, *Factory);
	return Vehicle;
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
	ReleaseNode(Vehicle);
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
	const auto* Owner = NodeOwners.Find(NodeId);
	return Owner && Owner->Get() == &Vehicle;
}

int32 UGuLiMiningVehicleManager::GetAssignedNode(const AGuLiMiningVehiclePawn* Vehicle) const
{
	const uint32* Node = AssignedNodes.Find(Vehicle);
	return Node ? *Node : 0;
}

void UGuLiMiningVehicleManager::ReleaseNode(const AGuLiMiningVehiclePawn& Vehicle)
{
	uint32 Node;
	if (AssignedNodes.RemoveAndCopyValue(&Vehicle, Node)) NodeOwners.Remove(Node);
}
