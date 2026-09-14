#pragma once

#include "CoreMinimal.h"
#include "Gameplay/Resources/GuLiResourceTypes.h"
#include "GameFramework/Actor.h"
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
};


/** Facility-local route data. The vehicle owns traversal and ground following. */
struct FGuLiFactoryDockRoute
{
	FVector Entry = FVector(4000, -600, 0);
	FVector Unload = FVector(-500, -600, 120);
	FVector Exit = FVector(4000, -600, 0);
};
/** Authority factory with a shared FIFO line; the existing Blueprint is presentation-only child content. */
UCLASS(NotPlaceable)
class GULISTRIKE_API AGuLiResourceFactoryActor final : public AActor
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
		const FVector& InDockPoint);
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
	bool TryReserveDock(AActor& Vehicle);
	void ReleaseDock(AActor& Vehicle);
	bool UploadCargo(AActor& Vehicle, const FGuLiResourceAmounts& Cargo);
	FGuLiFactoryDockRoute GetDockRoute() const;
	UFUNCTION(BlueprintPure, Category = "Resources|Factory")
	float GetDoorAlpha() const;
	UFUNCTION(BlueprintPure, Category = "Resources|Factory")
	bool ShouldDoorBeOpen() const { return DoorState.bOpen; }

private:
	UFUNCTION()
	void OnRep_DockPoint();
	UFUNCTION()
	void OnRep_PresentationClass();
	UFUNCTION()
	void OnRep_DoorState();
	void UpdateDoorTarget();
	void ApplyDoorPose();
	UPROPERTY(ReplicatedUsing = OnRep_DoorState)
	FGuLiFactoryDoorState DoorState;
	TSet<TWeakObjectPtr<AActor>> AssignedVehicles;
	TMap<TWeakObjectPtr<AActor>, bool> DockSessions;
	TWeakObjectPtr<class USkeletalMeshComponent> DoorMesh;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UBoxComponent> CollisionBox;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USceneComponent> DockPointComponent;

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

