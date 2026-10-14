#pragma once

#include "CoreMinimal.h"
#include "Gameplay/Resources/GuLiResourceTypes.h"
#include "Gameplay/Resources/GuLiResourceInventoryComponent.h"
#include "GameFramework/Actor.h"
#include "Gameplay/Building/GuLiBuildingLifecycleComponent.h"
#include "Network/GuLiHybridWorldSubsystem.h"
#include "GuLiResourceFactoryActor.generated.h"

class UBoxComponent;
class UChildActorComponent;
class USceneComponent;
class UGuLiResourceEconomyConfig;

/** Authority factory: raw cargo is processed FIFO; its four unloading destinations are unrestricted. */
UCLASS(NotPlaceable)
class GULISTRIKE_API AGuLiResourceFactoryActor final : public AActor, public IGuLiBuildingOwner
{
	GENERATED_BODY()

public:
	AGuLiResourceFactoryActor();
	virtual void Tick(float DeltaSeconds) override;
	virtual void BeginPlay() override;
	virtual void EndPlay(EEndPlayReason::Type Reason) override;

	void InitializeFactory(
		EGuLiTeam InTeam,
		const UGuLiResourceEconomyConfig& Config,
		const FVector& InDockPoint, FGuLiControllableActorId InId = FGuLiControllableActorId(),
		int32 TerritoryIndex = INDEX_NONE, EGuLiBuildingOrigin Origin = EGuLiBuildingOrigin::Map,
		bool bCompleted = true, const FGuid& Builder = FGuid(), int32 DefinitionId = 6);
	virtual EGuLiTeam GetBuildingTeam() const override { return Team; }
	virtual void SetBuildingTeamAuthority(EGuLiTeam NewTeam) override;
	virtual FVector GetBuildingGroundLocation() const override { return GetActorLocation(); }

	UFUNCTION(BlueprintPure, Category = "Resources|Factory")
	EGuLiTeam GetTeam() const { return Team; }
	FGuLiControllableActorId GetStableActorId() const { return StableActorId; }
	UFUNCTION(BlueprintPure, Category = "Resources|Factory")
	FVector GetDockPoint() const { return DockPoint; }
	UFUNCTION(BlueprintPure, Category = "Resources|Factory")
	int32 GetQueuedRawAmount() const;
	FGuLiResourceFactoryPrivateState MakePrivateState() const;
    TConstArrayView<FGuLiFactoryQueueEntry> GetDeliveryQueue() const { return Inventory->GetQueue(); }
    FGuid GetPendingResourceEvent() const { return Inventory->GetPendingEvent(); }
    uint64 GetAppliedResourceRevision() const { return Inventory->GetAppliedRevision(); }
	void RegisterMiningVehicle(AActor& Vehicle);
	void UnregisterMiningVehicle(AActor& Vehicle);

	/** Four shared navigation destinations on the factory floor. No occupancy or reservation state. */
	UFUNCTION(BlueprintPure, Category = "Resources|Factory")
	TArray<FVector> GetUnloadPoints() const;
	UFUNCTION(BlueprintPure, Category = "Resources|Factory")
	float GetDoorAlpha() const;
	UFUNCTION(BlueprintPure, Category = "Resources|Factory")
	bool ShouldDoorBeOpen() const { return DoorState.bOpen; }

private:
	friend class FGuLiHybridResourceFactoryAdapter;
#if WITH_DEV_AUTOMATION_TESTS
	friend class FGuLiScale020FactoryDoorPresentationContract;
#endif
	UPROPERTY(VisibleAnywhere) TObjectPtr<UGuLiBuildingLifecycleComponent> Lifecycle;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UGuLiResourceInventoryComponent> Inventory;
	UFUNCTION()
	void OnRep_DockPoint();
	UFUNCTION()
	void OnRep_PresentationClass();
	UFUNCTION()
	void OnRep_DoorState();
	void UpdateDoorTarget();
	void ApplyDoorPose();
	/** Door is visual-only; the factory shell, floor and ramp own physical geometry. */
	void ConfigureDoorPresentation();
	void RefreshConstructionCollision();
	UPROPERTY()
	FGuLiFactoryDoorState DoorState;
	TSet<TWeakObjectPtr<AActor>> AssignedVehicles;
	TWeakObjectPtr<class USkeletalMeshComponent> DoorMesh;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UBoxComponent> CollisionBox;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USceneComponent> DockPointComponent;
	UPROPERTY(VisibleAnywhere)
	TArray<TObjectPtr<USceneComponent>> UnloadPoints;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UChildActorComponent> Presentation;

	UPROPERTY()
	TSubclassOf<AActor> PresentationClass;

	UPROPERTY()
	EGuLiTeam Team = EGuLiTeam::Unassigned;

	UPROPERTY()
	FGuLiControllableActorId StableActorId;

	UPROPERTY()
	FVector_NetQuantize10 DockPoint = FVector::ZeroVector;

};

