#pragma once

#include "CoreMinimal.h"
#include "Gameplay/Resources/GuLiResourceTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "GuLiMiningVehicleManager.generated.h"

class AGuLiMiningVehiclePawn;
class AGuLiResourceFactoryActor;
struct FCollisionQueryParams;

/** World-owned miner registry and authority-only per-node allocation. Vehicles own path following and mining. */
UCLASS()
class GULISTRIKE_API UGuLiMiningVehicleManager final : public UWorldSubsystem
{
	GENERATED_BODY()
public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Deinitialize() override;
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Resources|Mining")
	AGuLiMiningVehiclePawn* SpawnMiningVehicle(AGuLiResourceFactoryActor* Factory, FTransform Transform);
	void RegisterVehicle(AGuLiMiningVehiclePawn& Vehicle);
	void UnregisterVehicle(AGuLiMiningVehiclePawn& Vehicle);
	bool AssignNode(AGuLiMiningVehiclePawn& Vehicle, uint16 PreferredCluster, uint16& OutCluster,
		uint32& OutNode, FVector& OutApproach, float& OutPathLength);
	void ReleaseNode(const AGuLiMiningVehiclePawn& Vehicle);
	bool OwnsNode(const AGuLiMiningVehiclePawn& Vehicle, uint32 NodeId) const;
	void IgnoreVehicles(FCollisionQueryParams& Params) const;
	UFUNCTION(BlueprintPure, Category="Resources|Mining")
	int32 GetAssignedNode(const AGuLiMiningVehiclePawn* Vehicle) const;
	const TArray<TObjectPtr<AGuLiMiningVehiclePawn>>& GetVehicles() const { return Vehicles; }
private:
	UPROPERTY(Transient) TArray<TObjectPtr<AGuLiMiningVehiclePawn>> Vehicles;
	TMap<uint32, TWeakObjectPtr<AGuLiMiningVehiclePawn>> NodeOwners;
	TMap<TWeakObjectPtr<const AGuLiMiningVehiclePawn>, uint32> AssignedNodes;
};
