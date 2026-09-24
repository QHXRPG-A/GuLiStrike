#pragma once

#include "CoreMinimal.h"
#include "Gameplay/Resources/GuLiResourceTypes.h"
#include "Gameplay/Resources/GuLiMiningSlots.h"
#include "Subsystems/WorldSubsystem.h"
#include "GuLiMiningVehicleManager.generated.h"

class AGuLiMiningVehiclePawn;
class AGuLiResourceFactoryActor;
struct FCollisionQueryParams;

/** World-owned miner registry and authority-only per-node allocation. Vehicles own path following and mining. */
UCLASS()
class GULISTRIKE_API UGuLiMiningVehicleManager final : public UTickableWorldSubsystem
{
	GENERATED_BODY()
public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Deinitialize() override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	EGuLiWorkPositionAvailability TryReserveSlot(AGuLiMiningVehiclePawn& Vehicle, uint16 PreferredCluster, uint32 Task,
		FGuLiMiningSlotReservation& Out, FVector& Position);
	bool ValidateReservation(const AGuLiMiningVehiclePawn& Vehicle, const FGuLiMiningSlotReservation& Reservation) const;
	void ReleaseSlot(const AGuLiMiningVehiclePawn& Vehicle, const FGuLiMiningSlotReservation& Reservation);
	void RejectSlot(AGuLiMiningVehiclePawn& Vehicle, const FGuLiMiningSlotReservation& Reservation,
		EGuLiMiningSlotFailure Failure = EGuLiMiningSlotFailure::Movement);
	/** Select at the actual parked pose; one blocked node must not reject the whole slot. */
	EGuLiWorkPositionAvailability AssignCoveredNode(AGuLiMiningVehiclePawn& Vehicle,
		const FGuLiMiningSlotReservation& Reservation, const FTransform& MiningPose, uint32& Node, FVector& Target);
	bool GetSlotPose(const AGuLiMiningVehiclePawn& Vehicle, const FGuLiMiningSlotReservation& Reservation, FTransform& Out) const;
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Resources|Mining")
	AGuLiMiningVehiclePawn* SpawnMiningVehicle(AGuLiResourceFactoryActor* Factory, FTransform Transform);
	void RegisterVehicle(AGuLiMiningVehiclePawn& Vehicle);
	void UnregisterVehicle(AGuLiMiningVehiclePawn& Vehicle);
	void ReleaseNode(const AGuLiMiningVehiclePawn& Vehicle);
	bool OwnsNode(const AGuLiMiningVehiclePawn& Vehicle, uint32 NodeId) const;
	void IgnoreVehicles(FCollisionQueryParams& Params) const;
	UFUNCTION(BlueprintPure, Category="Resources|Mining")
	int32 GetAssignedNode(const AGuLiMiningVehiclePawn* Vehicle) const;
	const TArray<TObjectPtr<AGuLiMiningVehiclePawn>>& GetVehicles() const { return Vehicles; }
	UFUNCTION(BlueprintPure, Category="Resources|Mining") FString GetSlotDebug() const;
	UFUNCTION(BlueprintPure, Category="Resources|Mining") TArray<FTransform> GetClusterSlotPoses(int32 ClusterId, int32 UnitTypeId) const;
	UFUNCTION(BlueprintPure, Category="Resources|Mining") bool AreClusterSlotsReady(int32 ClusterId, int32 UnitTypeId) const;
private:
	UPROPERTY(Transient) TArray<TObjectPtr<AGuLiMiningVehiclePawn>> Vehicles;
	TMap<uint32, TWeakObjectPtr<AGuLiMiningVehiclePawn>> NodeOwners;
	TMap<TWeakObjectPtr<const AGuLiMiningVehiclePawn>, uint32> AssignedNodes;
	TMap<int32,FGuLiMiningSlotProfile> SlotProfiles;
	struct FRejectedSlot
	{
		FGuLiMiningSlotReservation Slot;
		FVector Start;
		double RetryAt;
		uint32 NavRevision;
		EGuLiMiningSlotFailure Failure;
		int32 ClusterRemaining;
		FTransform FailedPose;
		double NextVisibilityCheck;
	};
	TMap<TWeakObjectPtr<AGuLiMiningVehiclePawn>,TArray<FRejectedSlot>> RejectedSlots;
	void DirtySlots(const FBox& Bounds);
};
