#include "Gameplay/Building/GuLiBuildingRegistrySubsystem.h"
#include "Gameplay/Building/GuLiBuildingLifecycleComponent.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Character.h"
#include "Gameplay/Navigation/GuLiDynamicObstacleRegistry.h"
#include "NavigationSystem.h"
#include "Engine/World.h"

void UGuLiBuildingRegistrySubsystem::Register(UGuLiBuildingLifecycleComponent& Building)
{
	const uint32 Id = Building.GetState().InstanceId;
	check(Id != 0);
	if (Buildings.Contains(Id)) return;
	Buildings.Add(Id, &Building);
	if (Building.GetOwner()->HasAuthority())
	{
		OnSpawned.Broadcast(Building);
		if (ConstructionPrototype.IsValid()) Building.PrepareConstructionSlots(*ConstructionPrototype);
		if (Building.IsCompleted()) OnCompleted.Broadcast(Building);
	}
}
void UGuLiBuildingRegistrySubsystem::Unregister(UGuLiBuildingLifecycleComponent& Building)
{
	Buildings.Remove(Building.GetState().InstanceId);
	SlotQueue.Remove(&Building);
}

void UGuLiBuildingRegistrySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency<UGuLiDynamicObstacleRegistrySubsystem>();
	if (auto* Registry=GetWorld()->GetSubsystem<UGuLiDynamicObstacleRegistrySubsystem>())
		Registry->StaticRegionChanged.AddUObject(this,&ThisClass::DirtyConstructionSlots);
}
void UGuLiBuildingRegistrySubsystem::Deinitialize()
{
	if (auto* Registry=GetWorld()->GetSubsystem<UGuLiDynamicObstacleRegistrySubsystem>()) Registry->StaticRegionChanged.RemoveAll(this);
	SlotQueue.Reset(); Buildings.Reset(); ConstructionVehicles.Reset(); ConstructionPrototype.Reset(); Super::Deinitialize();
}
void UGuLiBuildingRegistrySubsystem::RegisterConstructionPrototype(ACharacter& Vehicle)
{
	if (!Vehicle.HasAuthority()) return;
	ConstructionVehicles.AddUnique(&Vehicle);
	ConstructionPrototype=&Vehicle;
	for (const auto& Pair : Buildings) if (auto* Building=Pair.Value.Get()) Building->PrepareConstructionSlots(Vehicle);
}
void UGuLiBuildingRegistrySubsystem::UnregisterConstructionPrototype(ACharacter& Vehicle)
{
	ConstructionVehicles.RemoveAll([&](const auto& Entry) { return !Entry.IsValid() || Entry.Get()==&Vehicle; });
	if (ConstructionPrototype.Get()==&Vehicle || !ConstructionPrototype.IsValid())
	{
		ConstructionPrototype=ConstructionVehicles.IsEmpty() ? nullptr : ConstructionVehicles[0].Get();
		if (auto* Prototype=ConstructionPrototype.Get())
			for (const auto& Pair : Buildings) if (auto* Building=Pair.Value.Get()) Building->PrepareConstructionSlots(*Prototype);
	}
}
void UGuLiBuildingRegistrySubsystem::QueueConstructionSlots(UGuLiBuildingLifecycleComponent& Building) { SlotQueue.AddUnique(&Building); }
void UGuLiBuildingRegistrySubsystem::DirtyConstructionSlots(const FBox& Bounds)
{
	for (const auto& Pair : Buildings) if (auto* Building=Pair.Value.Get()) Building->InvalidateConstructionSlots(Bounds);
}
void UGuLiBuildingRegistrySubsystem::Tick(float)
{
	if (GetWorld()->GetNetMode()==NM_Client || SlotQueue.IsEmpty() || UNavigationSystemV1::IsNavigationBeingBuiltOrLocked(GetWorld())) return;
	const double Start=FPlatformTime::Seconds(); const int32 Count=FMath::Min(4,SlotQueue.Num());
	for (int32 I=0; I<Count && FPlatformTime::Seconds()-Start<.001; ++I)
	{
		auto Weak=SlotQueue[0]; SlotQueue.RemoveAt(0,1,EAllowShrinking::No);
		if (auto* Building=Weak.Get(); Building && !Building->BuildNextConstructionSlot()) SlotQueue.AddUnique(Weak);
	}
}
TStatId UGuLiBuildingRegistrySubsystem::GetStatId() const { RETURN_QUICK_DECLARE_CYCLE_STAT(UGuLiBuildingRegistrySubsystem,STATGROUP_Tickables); }
UGuLiBuildingLifecycleComponent* UGuLiBuildingRegistrySubsystem::Find(uint32 Id) const
{
	const auto* Value = Buildings.Find(Id);
	return Value ? Value->Get() : nullptr;
}
void UGuLiBuildingRegistrySubsystem::Query(TArray<UGuLiBuildingLifecycleComponent*>& Out, int32 TerritoryIndex) const
{
	Out.Reset();
	for (const auto& Entry : Buildings)
		if (auto* Building = Entry.Value.Get())
			if (TerritoryIndex == INDEX_NONE || Building->GetState().TerritoryIndex == TerritoryIndex) Out.Add(Building);
	Out.Sort([](const auto& A, const auto& B) { return A.GetState().InstanceId < B.GetState().InstanceId; });
}
