#pragma once
#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Gameplay/Building/GuLiBuildingTypes.h"
#include "GuLiBuildingRegistrySubsystem.generated.h"
class UGuLiBuildingLifecycleComponent;
DECLARE_MULTICAST_DELEGATE_OneParam(FGuLiBuildingRegistryEvent, UGuLiBuildingLifecycleComponent&);

UCLASS()
class GULISTRIKE_API UGuLiBuildingRegistrySubsystem : public UWorldSubsystem
{
	GENERATED_BODY()
public:
	uint32 AllocateInstanceId() { return NextInstanceId++; }
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
};
