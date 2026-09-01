// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Commander/Network/GuLiCommanderTypes.h"
#include "GameFramework/Actor.h"
#include "GuLiCommanderHealthBarRenderer.generated.h"

class AGuLiCommanderPlayerController;
class AGuLiCommanderPresentationActor;
class AGuLiSoldierStateReplicator;
class UGuLiCommanderNetSyncComponent;
class UInstancedStaticMeshComponent;
class UMaterialInterface;
class USceneComponent;
class UStaticMesh;

/**
 * Local-only, single-batch world-space health-bar presentation.
 *
 * One stable ISM slot is allocated per SoldierId. Gameplay facts remain owned by
 * AGuLiSoldierStateReplicator and positions remain owned by the existing Commander
 * presentation actor; this actor only turns those read-only views into billboards.
 */
UCLASS(NotBlueprintable, Transient)
class GULISTRIKE_API AGuLiCommanderHealthBarRenderer final : public AActor
{
	GENERATED_BODY()

public:
	AGuLiCommanderHealthBarRenderer();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;

	/** Must be called by the owning local HUD. No network ownership is established. */
	void InitializeForController(AGuLiCommanderPlayerController* InController);

	/** Read-only local diagnostics for automation/performance capture. */
	int32 GetAllocatedInstanceCount() const;
	int32 GetVisibleInstanceCount() const { return VisibleInstanceCount; }
	double GetLastUpdateMilliseconds() const { return LastUpdateMilliseconds; }

#if WITH_DEV_AUTOMATION_TESTS
	/** Pure production-policy accessors used by focused automation tests. */
	static bool TestOnly_ShouldDisplayHealthBar(
		bool bAlive,
		float Health,
		float MaxHealth,
		bool bSelected,
		float DistanceCentimeters,
		float MaximumDistanceCentimeters);
	static float TestOnly_CalculateHealthFraction(float Health, float MaxHealth);
	static FVector2D TestOnly_CalculateWorldSizeCentimeters(
		float DistanceCentimeters,
		float HorizontalFieldOfViewDegrees,
		int32 ViewportWidth,
		int32 ViewportHeight);
#endif

private:
	void ResolveSoftAssets();
	void ResolveRuntimeDependencies();
	void BindNetSync(UGuLiCommanderNetSyncComponent* InNetSync);
	void BindStateReplicator(AGuLiSoldierStateReplicator* InReplicator);
	void BindPresentationActor(AGuLiCommanderPresentationActor* InPresentationActor);
	void UnbindRuntimeDependencies();
	void HandleSelectionChanged(const FGuLiCommanderSelectionState& Selection);
	void HandleSoldierStatesChanged(uint32 SnapshotRevision);
	void RebuildSelectedSoldiers(const FGuLiCommanderSelectionState& Selection);
	void EnsureStableInstancePool(const AGuLiSoldierStateReplicator& Replicator);
	void RefreshSoldierHeightOffset();
	void RebuildLocalInstances();
	void HideAllInstances();

	static bool ShouldDisplayHealthBar(
		bool bAlive,
		float Health,
		float MaxHealth,
		bool bSelected,
		float DistanceCentimeters,
		float MaximumDistanceCentimeters);
	static float CalculateHealthFraction(float Health, float MaxHealth);
	static FVector2D CalculateWorldSizeCentimeters(
		float DistanceCentimeters,
		float HorizontalFieldOfViewDegrees,
		int32 ViewportWidth,
		int32 ViewportHeight);

	UPROPERTY(VisibleAnywhere, Category = "Commander|UI|HealthBar")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, Category = "Commander|UI|HealthBar")
	TObjectPtr<UInstancedStaticMeshComponent> HealthBarInstances;

	UPROPERTY(EditDefaultsOnly, Category = "Commander|UI|HealthBar")
	TSoftObjectPtr<UStaticMesh> PlaneMeshAsset;

	UPROPERTY(EditDefaultsOnly, Category = "Commander|UI|HealthBar")
	TSoftObjectPtr<UMaterialInterface> HealthBarMaterialAsset;

	TWeakObjectPtr<AGuLiCommanderPlayerController> LocalController;
	TWeakObjectPtr<UGuLiCommanderNetSyncComponent> BoundNetSync;
	TWeakObjectPtr<AGuLiSoldierStateReplicator> StateReplicator;
	TWeakObjectPtr<AGuLiCommanderPresentationActor> PresentationActor;
	TWeakObjectPtr<UStaticMesh> CachedSoldierMesh;
	FDelegateHandle SelectionChangedHandle;
	FDelegateHandle SoldierStatesChangedHandle;
	TMap<FGuLiSoldierId, int32> SoldierInstanceIndices;
	TSet<FGuLiSoldierId> SelectedSoldiers;
	TArray<FTransform> CachedTransforms;
	TArray<float> CachedHealthFractions;
	TArray<float> CachedSelectedValues;
	TArray<float> CachedVisibleValues;
	float SoldierHeightOffsetCentimeters = 220.0f;
	int32 VisibleInstanceCount = 0;
	double LastUpdateMilliseconds = 0.0;
	bool bLoggedMissingPlane = false;
	bool bLoggedMissingMaterial = false;
};
