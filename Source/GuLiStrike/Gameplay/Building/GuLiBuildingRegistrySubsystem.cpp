#include "Gameplay/Building/GuLiBuildingRegistrySubsystem.h"
#include "Gameplay/Building/GuLiBuildingLifecycleComponent.h"
#include "GameFramework/Actor.h"

void UGuLiBuildingRegistrySubsystem::Register(UGuLiBuildingLifecycleComponent& Building)
{
	const uint32 Id = Building.GetState().InstanceId;
	check(Id != 0);
	if (Buildings.Contains(Id)) return;
	Buildings.Add(Id, &Building);
	if (Building.GetOwner()->HasAuthority())
	{
		OnSpawned.Broadcast(Building);
		if (Building.IsCompleted()) OnCompleted.Broadcast(Building);
	}
}
void UGuLiBuildingRegistrySubsystem::Unregister(UGuLiBuildingLifecycleComponent& Building)
{
	Buildings.Remove(Building.GetState().InstanceId);
}
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
