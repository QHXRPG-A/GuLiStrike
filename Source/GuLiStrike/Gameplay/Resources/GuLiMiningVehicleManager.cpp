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

bool UGuLiMiningVehicleManager::AssignNode(AGuLiMiningVehiclePawn& Vehicle, const uint16 PreferredCluster,
	uint16& OutCluster, uint32& OutNode, FVector& OutApproach, float& OutPathLength)
{
	if (!Vehicle.HasAuthority()) return false;
	ReleaseNode(Vehicle);
	OutCluster = 0; OutNode = 0; OutPathLength = TNumericLimits<float>::Max();
	const auto* Resources = GetWorld()->GetSubsystem<UGuLiResourceWorldSubsystem>();
	const auto* Map = Resources->GetMapDefinition();
	if (!Resources->IsRuntimeReady() || !Map) return false;
	for (const FGuLiResourceClusterDefinition& Cluster : Map->Clusters)
	{
		if ((PreferredCluster && Cluster.ClusterId != PreferredCluster)
			|| !Resources->CanTeamMineAt(Vehicle.GetTeam(), Cluster.ClusterId) || Resources->IsClusterEmpty(Cluster.ClusterId)) continue;
		TArray<uint32, TInlineAllocator<32>> Candidates;
		for (int32 Index = 0; Index < Cluster.NodeCount; ++Index)
		{
			const uint32 NodeId = Map->Nodes[Cluster.FirstNodeIndex + Index].NodeId;
			if (Resources->GetNodeRemainingRaw(NodeId) > 0 && !NodeOwners.Contains(NodeId)) Candidates.Add(NodeId);
		}
		Candidates.Sort([&](uint32 A, uint32 B)
		{
			return FVector::DistSquared(Vehicle.GetActorLocation(), Map->Nodes[A - 1].WorldTransform.GetLocation())
				< FVector::DistSquared(Vehicle.GetActorLocation(), Map->Nodes[B - 1].WorldTransform.GetLocation());
		});
		for (const uint32 NodeId : Candidates)
		{
			FVector Approach; float Length;
			if (!Vehicle.FindReachableMiningApproach(NodeId, Approach, Length)) continue;
			if (Length < OutPathLength || (FMath::IsNearlyEqual(Length, OutPathLength) && NodeId < OutNode))
			{
				OutCluster = Cluster.ClusterId; OutNode = NodeId; OutApproach = Approach; OutPathLength = Length;
			}
			break;
		}
	}
	if (!OutNode) return false;
	NodeOwners.Add(OutNode, &Vehicle);
	AssignedNodes.Add(&Vehicle, OutNode);
	return true;
}
