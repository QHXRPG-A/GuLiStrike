// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Commander/Network/GuLiCommanderTypes.h"
#include "GameFramework/Actor.h"
#include "GuLiCommanderHealthBarRenderer.generated.h"

class AGuLiCommanderPlayerController;
class APlayerController;
class AGuLiCommanderPresentationActor;
class AGuLiSoldierStateReplicator;
class UGuLiCommanderNetSyncComponent;
class UInstancedStaticMeshComponent;
class UMaterialInterface;
class USceneComponent;
class UStaticMesh;
class UGuLiUnitFeedbackSubsystem;
class UGuLiCombatHealthComponent;
struct FGuLiSoldierRosterDelta;

/**
 * Local-only, single-batch world-space health-bar presentation.
 *
 * Slots are shared and recycled by the active selected/hit soldier and Actor sets. Gameplay facts remain owned by
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

	/** One world-owned batch per local view, shared by the Commander selection HUD and all unit hit feedback. */
	static AGuLiCommanderHealthBarRenderer* FindOrSpawn(UWorld* World, APlayerController* Controller);
	void InitializeForController(APlayerController* InController);

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
	static float TestOnly_CalculateSoldierHeightOffset(
		const FBoxSphereBounds& Bounds);
#endif

private:
	friend class FGuLiCommanderHealthBarActivityTest;
	void MaintainActivity();
	void RefreshSoldierActivity(FGuLiSoldierId Id);
	void HandleRosterDelta(const FGuLiSoldierRosterDelta& Delta);
	void HandleVisualStatesChanged(const TArray<FGuLiSoldierId>& Ids);
	void HandleActorBarChanged(TWeakObjectPtr<AActor> Actor);
	UFUNCTION() void HandleActorHealthChanged(float Health, float MaxHealth);
	void UpdateTickActivity();
	void ReleaseInstanceSlot(int32 Index);
	void WriteInstance(int32 Index, const FTransform& Transform, float Health, float Selected, float Visible);
	void ResolveSoftAssets();
	void ResolveRuntimeDependencies();
	void BindNetSync(UGuLiCommanderNetSyncComponent* InNetSync);
	void BindStateReplicator(AGuLiSoldierStateReplicator* InReplicator);
	void BindPresentationActor(AGuLiCommanderPresentationActor* InPresentationActor);
	void UnbindRuntimeDependencies();
	void HandleSelectionChanged(const FGuLiCommanderSelectionState& Selection);
	void RebuildSelectedSoldiers(const FGuLiCommanderSelectionState& Selection);
	void EnsureStableInstancePool(const AGuLiSoldierStateReplicator& Replicator);
	int32 AllocateInstanceSlot();
	void RefreshActorInstancePool();
	float ResolveSoldierHeightOffset(uint16 UnitTypeId);
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
	static float CalculateSoldierHeightOffset(const FBoxSphereBounds& Bounds);

	UPROPERTY(VisibleAnywhere, Category = "Commander|UI|HealthBar")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, Category = "Commander|UI|HealthBar")
	TObjectPtr<UInstancedStaticMeshComponent> HealthBarInstances;

	UPROPERTY(EditDefaultsOnly, Category = "Commander|UI|HealthBar")
	int32 PlaneMeshVfxId = 0;

	UPROPERTY(EditDefaultsOnly, Category = "Commander|UI|HealthBar")
	int32 HealthBarVfxId = 0;
	FVector HealthBarBaseScale = FVector::OneVector;

	TWeakObjectPtr<APlayerController> LocalController;
	TWeakObjectPtr<UGuLiCommanderNetSyncComponent> BoundNetSync;
	TWeakObjectPtr<AGuLiSoldierStateReplicator> StateReplicator;
	TWeakObjectPtr<AGuLiCommanderPresentationActor> PresentationActor;
	TMap<uint16, TWeakObjectPtr<UStaticMesh>> CachedSoldierMeshes;
	TMap<uint16, float> SoldierHeightOffsetsCentimeters;
	FDelegateHandle SelectionChangedHandle;
	FDelegateHandle SoldierStatesChangedHandle;
	FDelegateHandle VisualStatesChangedHandle;
	FDelegateHandle ActorBarsChangedHandle;
	TWeakObjectPtr<UGuLiUnitFeedbackSubsystem> BoundFeedback;
	FTimerHandle MaintenanceTimer;
	TMap<FGuLiSoldierId, FGuLiSoldierStateItem> ActiveSoldierStates;
	TMap<FGuLiSoldierId, float> ActiveHitStartTimes;
	TMap<TWeakObjectPtr<AActor>, TWeakObjectPtr<UGuLiCombatHealthComponent>> ActorHealthProviders;
	TMap<TWeakObjectPtr<AActor>, float> ActorHealthFractions;
	TArray<int32> FreeInstanceSlots;
	TMap<FGuLiSoldierId, int32> SoldierInstanceIndices;
	TMap<TWeakObjectPtr<AActor>, int32> ActorInstanceIndices;
	TSet<FGuLiSoldierId> PendingActivityIds;
	TSet<FGuLiSoldierId> SelectedSoldiers;
	TArray<FTransform> CachedTransforms;
	TArray<float> CachedHealthFractions;
	TArray<float> CachedSelectedValues;
	TArray<float> CachedVisibleValues;
	int32 VisibleInstanceCount = 0;
	double LastUpdateMilliseconds = 0.0;
	bool bLoggedMissingPlane = false;
	bool bLoggedMissingMaterial = false;
};
