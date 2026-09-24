#pragma once
#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Gameplay/Building/GuLiBuildingTypes.h"
#include "GuLiBuildingRegistrySubsystem.generated.h"
class UGuLiBuildingLifecycleComponent;
class ACharacter;
DECLARE_MULTICAST_DELEGATE_OneParam(FGuLiBuildingRegistryEvent, UGuLiBuildingLifecycleComponent&);

UCLASS()
class GULISTRIKE_API UGuLiBuildingRegistrySubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()
public:
	uint32 AllocateInstanceId() { return NextInstanceId++; }
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void Tick(float Dt) override;
	virtual TStatId GetStatId() const override;
	void RegisterConstructionPrototype(ACharacter& Vehicle);
	void UnregisterConstructionPrototype(ACharacter& Vehicle);
	void QueueConstructionSlots(UGuLiBuildingLifecycleComponent& Building);
	void Register(UGuLiBuildingLifecycleComponent& Building);
	void Unregister(UGuLiBuildingLifecycleComponent& Building);
	UGuLiBuildingLifecycleComponent* Find(uint32 Id) const;
	void Query(TArray<UGuLiBuildingLifecycleComponent*>& Out, int32 TerritoryIndex = INDEX_NONE) const;
	FGuLiBuildingRegistryEvent OnSpawned;
	FGuLiBuildingRegistryEvent OnCompleted;
	FGuLiBuildingRegistryEvent OnDestroyed;
private:
	uint32 NextInstanceId = 1;
	TMap<uint32, TWeakObjectPtr<UGuLiBuildingLifecycleComponent>> Buildings;
	TWeakObjectPtr<ACharacter> ConstructionPrototype;
	TArray<TWeakObjectPtr<ACharacter>> ConstructionVehicles;
	TArray<TWeakObjectPtr<UGuLiBuildingLifecycleComponent>> SlotQueue;
	void DirtyConstructionSlots(const FBox& Bounds);
};
