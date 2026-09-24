#pragma once

#include "CoreMinimal.h"
#include "Gameplay/Resources/GuLiResourceTypes.h"
#include "GameFramework/Actor.h"
#include "Gameplay/Building/GuLiBuildingLifecycleComponent.h"
#include "GuLiResourceFactoryActor.generated.h"

class UBoxComponent;
class UChildActorComponent;
class USceneComponent;
class UGuLiResourceEconomyConfig;

USTRUCT()
struct GULISTRIKE_API FGuLiFactoryQueueEntry
{
	GENERATED_BODY()

	UPROPERTY()
	EGuLiResourceType ResourceType = EGuLiResourceType::Blue;

	UPROPERTY()
	int32 Amount = 0;
	UPROPERTY() EGuLiTeam SettlementTeam = EGuLiTeam::Unassigned;
};


/** Authority factory: raw cargo is processed FIFO; its four unloading destinations are unrestricted. */
UCLASS(NotPlaceable)
class GULISTRIKE_API AGuLiResourceFactoryActor final : public AActor, public IGuLiBuildingOwner
{
	GENERATED_BODY()

public:
	AGuLiResourceFactoryActor();
	virtual void Tick(float DeltaSeconds) override;
	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	void InitializeFactory(
		EGuLiTeam InTeam,
		const UGuLiResourceEconomyConfig& Config,
		const FVector& InDockPoint, FGuLiControllableActorId InId = FGuLiControllableActorId(),
		int32 TerritoryIndex = INDEX_NONE, EGuLiBuildingOrigin Origin = EGuLiBuildingOrigin::Map,
		bool bCompleted = true, const FGuid& Builder = FGuid(), int32 DefinitionId = 6);
	virtual EGuLiTeam GetBuildingTeam() const override { return Team; }
	virtual void SetBuildingTeamAuthority(EGuLiTeam NewTeam) override;
	virtual FVector GetBuildingGroundLocation() const override { return GetActorLocation(); }
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Resources|Factory")
	bool EnqueueCargo(const FGuLiResourceAmounts& Cargo);
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Resources|Factory")
	bool EnqueueRaw(EGuLiResourceType ResourceType, int32 Amount);
	UFUNCTION(BlueprintPure, Category = "Resources|Factory")
	EGuLiTeam GetTeam() const { return Team; }
	FGuLiControllableActorId GetStableActorId() const { return StableActorId; }
	UFUNCTION(BlueprintPure, Category = "Resources|Factory")
	FVector GetDockPoint() const { return DockPoint; }
	UFUNCTION(BlueprintPure, Category = "Resources|Factory")
	int32 GetQueuedRawAmount() const;
	FGuLiResourceFactoryPrivateState MakePrivateState() const;
	void RegisterMiningVehicle(AActor& Vehicle);
	void UnregisterMiningVehicle(AActor& Vehicle);
	bool UploadCargo(AActor& Vehicle, const FGuLiResourceAmounts& Cargo);
	/** Four shared navigation destinations on the factory floor. No occupancy or reservation state. */
	UFUNCTION(BlueprintPure, Category = "Resources|Factory")
	TArray<FVector> GetUnloadPoints() const;
	UFUNCTION(BlueprintPure, Category = "Resources|Factory")
	float GetDoorAlpha() const;
	UFUNCTION(BlueprintPure, Category = "Resources|Factory")
	bool ShouldDoorBeOpen() const { return DoorState.bOpen; }

private:
#if WITH_DEV_AUTOMATION_TESTS
	friend class FGuLiScale020FactoryDoorPresentationContract;
#endif
	UPROPERTY(VisibleAnywhere) TObjectPtr<UGuLiBuildingLifecycleComponent> Lifecycle;
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
	UPROPERTY(ReplicatedUsing = OnRep_DoorState)
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

	UPROPERTY(ReplicatedUsing = OnRep_PresentationClass)
	TSubclassOf<AActor> PresentationClass;

	UPROPERTY(Replicated)
	EGuLiTeam Team = EGuLiTeam::Unassigned;

	UPROPERTY(Replicated)
	FGuLiControllableActorId StableActorId;

	UPROPERTY(ReplicatedUsing = OnRep_DockPoint)
	FVector_NetQuantize10 DockPoint = FVector::ZeroVector;

	UPROPERTY()
	TArray<FGuLiFactoryQueueEntry> Queue;

	float ProcessingRatePerSecond = 2.0f;
	float ProcessingAccumulator = 0.0f;
};

