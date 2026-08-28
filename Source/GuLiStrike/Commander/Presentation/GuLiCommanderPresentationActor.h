// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Commander/Network/GuLiCommanderTypes.h"
#include "GameFramework/Actor.h"
#include "MassArchetypeTypes.h"
#include "MassEntityHandle.h"
#include "MassEntityTypes.h"
#include "GuLiCommanderPresentationActor.generated.h"

class AGuLiCommanderPlayerController;
class AGuLiSoldierStateReplicator;
class UInstancedStaticMeshComponent;
class UMaterialInterface;
class UMassEntitySubsystem;
class USceneComponent;
class UStaticMesh;

/** One authoritative 10 Hz sample after resolving its chunk-relative position. */
struct FGuLiCommanderBufferedSoldierPose
{
	double ServerTimeSeconds = 0.0;
	uint32 FrameSequence = 0u;
	FVector Location = FVector::ZeroVector;
	FVector Velocity = FVector::ZeroVector;
	FVector ChunkAnchor = FVector::ZeroVector;
	FVector RelativeLocation = FVector::ZeroVector;
	float FacingYawDegrees = 0.0f;
	uint32 ActiveOrderId = 0u;
	uint16 ChunkIndex = 0u;
	uint8 SampleIndex = 0u;
	bool bTeleport = false;
};

/** Client-only interpolation state. It intentionally contains no Mass movement fragments. */
struct FGuLiCommanderPresentedSoldier
{
	TArray<FGuLiCommanderBufferedSoldierPose> Samples;
	FTransform AuthoritativeTransform = FTransform::Identity;
	FTransform PresentedTransform = FTransform::Identity;
	EGuLiSoldierLifeState LastLifeState = EGuLiSoldierLifeState::Alive;
	double LastPoseReceiptLocalTimeSeconds = 0.0;
	double MaximumPoseReceiptGapSeconds = 0.0;
	FVector LastUntaggedHardSnapDelta = FVector::ZeroVector;
	FVector LastHardSnapPriorSampleDelta = FVector::ZeroVector;
	FVector LastHardSnapSampleVelocity = FVector::ZeroVector;
	FVector LastHardSnapCurrentAnchor = FVector::ZeroVector;
	FVector LastHardSnapCurrentRelative = FVector::ZeroVector;
	FVector LastHardSnapPreviousAnchor = FVector::ZeroVector;
	FVector LastHardSnapPreviousRelative = FVector::ZeroVector;
	double LastHardSnapServerTimeGapSeconds = 0.0;
	uint32 LastHardSnapFrameGap = 0u;
	uint16 LastHardSnapCurrentChunkIndex = 0u;
	uint16 LastHardSnapPreviousChunkIndex = 0u;
	uint8 LastHardSnapCurrentSampleIndex = 0u;
	uint8 LastHardSnapPreviousSampleIndex = 0u;
	uint64 UntaggedHardSnapCount = 0u;
	uint64 TeleportSnapCount = 0u;
	bool bHasAuthoritativeTransform = false;
	bool bHasPresentedTransform = false;
	bool bLifeStateInitialized = false;
};

/** Non-authoritative diagnostics used by the development network acceptance gate. */
struct FGuLiCommanderSoldierPresentationDiagnostics
{
	double MaximumPoseReceiptGapSeconds = 0.0;
	FVector LastUntaggedHardSnapDelta = FVector::ZeroVector;
	FVector LastHardSnapPriorSampleDelta = FVector::ZeroVector;
	FVector LastHardSnapSampleVelocity = FVector::ZeroVector;
	FVector LastHardSnapCurrentAnchor = FVector::ZeroVector;
	FVector LastHardSnapCurrentRelative = FVector::ZeroVector;
	FVector LastHardSnapPreviousAnchor = FVector::ZeroVector;
	FVector LastHardSnapPreviousRelative = FVector::ZeroVector;
	double LastHardSnapServerTimeGapSeconds = 0.0;
	uint32 LastHardSnapFrameGap = 0u;
	uint16 LastHardSnapCurrentChunkIndex = 0u;
	uint16 LastHardSnapPreviousChunkIndex = 0u;
	uint8 LastHardSnapCurrentSampleIndex = 0u;
	uint8 LastHardSnapPreviousSampleIndex = 0u;
	uint64 UntaggedHardSnapCount = 0u;
	uint64 TeleportSnapCount = 0u;
};

/** Bounded, presentation-only movement anticipation for one selected soldier. */
struct FGuLiCommanderPredictedMove
{
	FGuLiControlCohortId CohortId;
	uint32 ClientCommandId = 0u;
	uint32 ExpectedOrderId = 0u;
	double StartTimeSeconds = 0.0;
	double ResolveStartTimeSeconds = 0.0;
	FVector Direction = FVector::ZeroVector;
	float MaximumDistance = 0.0f;
	float TargetYawDegrees = 0.0f;
	FVector LastAppliedOffset = FVector::ZeroVector;
	float LastAppliedYawOffsetDegrees = 0.0f;
	FVector ResolveStartOffset = FVector::ZeroVector;
	float ResolveStartYawOffsetDegrees = 0.0f;
	bool bAwaitingAuthoritativeOrder = false;
	bool bResolving = false;
};

/**
 * Replicated discovery actor whose visual state is rebuilt locally from the reliable
 * Soldier roster plus owner-only unreliable pose chunks. ISM transforms never replicate.
 */
UCLASS(Config = Game)
class GULISTRIKE_API AGuLiCommanderPresentationActor : public AActor
{
	GENERATED_BODY()

public:
	AGuLiCommanderPresentationActor();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;

	UFUNCTION(BlueprintPure, Category = "Commander|Presentation")
	UInstancedStaticMeshComponent* GetUnitInstances() const { return UnitInstances; }

	UFUNCTION(BlueprintPure, Category = "Commander|Presentation")
	UInstancedStaticMeshComponent* GetRingInstances() const { return RingInstances; }

	/** Returns the current interpolated/predicted visual transform without exposing authority writes. */
	bool TryGetPresentedSoldierTransform(FGuLiSoldierId SoldierId, FTransform& OutTransform) const;

	/** Starts a bounded visual prediction immediately before the caller sends the move RPC. */
	void BeginPredictedMove(
		const FGuLiCommanderSelectionState& Selection,
		const FVector& Target,
		uint32 ClientCommandId);

	/** Accepts or rejects only the matching prediction; authority state is never mutated here. */
	void ResolvePredictedMove(const FGuLiCommandAck& Ack);

	/** Resets and reads a per-Soldier observation window without changing presentation state. */
	void ResetSoldierPresentationDiagnostics(FGuLiSoldierId SoldierId);
	bool TryGetSoldierPresentationDiagnostics(
		FGuLiSoldierId SoldierId,
		FGuLiCommanderSoldierPresentationDiagnostics& OutDiagnostics) const;
	float GetLatestClockRoundTripMilliseconds() const { return LatestClockRoundTripMilliseconds; }
	bool IsLatestClockRoundTripFromConnectionStats() const
	{
		return bLatestClockRoundTripFromConnectionStats;
	}

#if WITH_DEV_AUTOMATION_TESTS
	/** Pure clock helpers used to prove that RTT/2 is removed before the 100 ms render delay. */
	static double TestOnly_EstimateServerNowAtPoseReceipt(
		double SampleServerTimeSeconds,
		float RoundTripMilliseconds);
	static double TestOnly_CalculateRenderServerTime(
		double EstimatedServerNowSeconds,
		float BackTimeSeconds);
	static double TestOnly_AdvanceEstimatedServerTime(
		double CurrentEstimateSeconds,
		double MeasuredServerNowAtReceiptSeconds,
		double SecondsSinceReceipt,
		float DeltaSeconds);
	static float TestOnly_SelectClockRoundTripMilliseconds(
		float ConnectionAverageLagSeconds,
		double ConnectionRawPingSeconds,
		float PlayerStateRoundTripMilliseconds,
		bool& bOutFromConnectionStats);
#endif

private:
	void ResolveSoftAssets();
	AGuLiSoldierStateReplicator* FindStateReplicator();
	AGuLiCommanderPlayerController* FindLocalController();
	void ConsumePoseChunks(double LocalNowSeconds);
	void IngestPoseChunk(const FGuLiSoldierPoseChunk& Chunk, double LocalNowSeconds);
	void InsertPoseSample(
		FGuLiSoldierId SoldierId,
		const FGuLiCommanderBufferedSoldierPose& Sample,
		double LocalNowSeconds);
	bool EvaluateAuthoritativeTransform(
		FGuLiCommanderPresentedSoldier& Soldier,
		double RenderServerTimeSeconds,
		FTransform& OutTransform) const;
	void ApplyPrediction(
		FGuLiSoldierId SoldierId,
		double LocalNowSeconds,
		FTransform& InOutTransform);
	void BeginPredictionResolution(FGuLiCommanderPredictedMove& Prediction, double LocalNowSeconds);
	bool EnsureClientMirrorArchetype();
	void EnsureClientMirrorEntity(const FGuLiSoldierStateItem& ReliableState);
	void UpdateClientMirrorEntity(
		const FGuLiSoldierStateItem& ReliableState,
		const FTransform* PresentedTransform);
	void DestroyClientMirrorEntities();
	void ResetNetworkPresentationState();
	void EnsureStableInstancePool(const AGuLiSoldierStateReplicator& Replicator);
	void RebuildLocalInstances(float DeltaSeconds);
	FTransform BuildRingTransform(const FTransform& SoldierTransform) const;
	static bool IsAckResultAccepted(EGuLiCommandAckResult Result);

	UPROPERTY(VisibleAnywhere, Category = "Commander|Presentation")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, Category = "Commander|Presentation")
	TObjectPtr<UInstancedStaticMeshComponent> UnitInstances;

	UPROPERTY(VisibleAnywhere, Category = "Commander|Presentation")
	TObjectPtr<UInstancedStaticMeshComponent> RingInstances;

	UPROPERTY(EditDefaultsOnly, Category = "Commander|Presentation")
	TSoftObjectPtr<UStaticMesh> UnitMeshAsset;

	UPROPERTY(EditDefaultsOnly, Category = "Commander|Presentation")
	TSoftObjectPtr<UStaticMesh> RingMeshAsset;

	UPROPERTY(EditDefaultsOnly, Category = "Commander|Presentation")
	TSoftObjectPtr<UMaterialInterface> UnitMaterialAsset;

	UPROPERTY(EditDefaultsOnly, Category = "Commander|Presentation")
	TSoftObjectPtr<UMaterialInterface> RingMaterialAsset;

	UPROPERTY(Config, EditDefaultsOnly, Category = "Commander|Presentation|Smoothing", meta = (ClampMin = "0.0"))
	float InterpolationBackTimeSeconds = 0.1f;

	UPROPERTY(Config, EditDefaultsOnly, Category = "Commander|Presentation|Smoothing", meta = (ClampMin = "0.0"))
	float MaximumExtrapolationSeconds = 0.1f;

	UPROPERTY(Config, EditDefaultsOnly, Category = "Commander|Presentation|Smoothing", meta = (ClampMin = "0.0", Units = "cm"))
	float HardSnapDistanceCentimeters = 1000.0f;

	UPROPERTY(Config, EditDefaultsOnly, Category = "Commander|Presentation|Prediction", meta = (ClampMin = "0.0"))
	float PredictionDurationSeconds = 0.25f;

	UPROPERTY(Config, EditDefaultsOnly, Category = "Commander|Presentation|Prediction", meta = (ClampMin = "0.0", Units = "cm"))
	float MaximumPredictionDistanceCentimeters = 900.0f;

	UPROPERTY(Config, EditDefaultsOnly, Category = "Commander|Presentation|Prediction", meta = (ClampMin = "0.0"))
	float PredictionResolveSeconds = 0.15f;

	UPROPERTY(Config, EditDefaultsOnly, Category = "Commander|Presentation", meta = (ClampMin = "0.0"))
	float WreckLifetimeSeconds = 5.0f;

	TWeakObjectPtr<AGuLiSoldierStateReplicator> StateReplicator;
	TWeakObjectPtr<AGuLiCommanderPlayerController> LocalController;
	TWeakObjectPtr<UMassEntitySubsystem> ClientMirrorMassSubsystem;
	FMassArchetypeHandle ClientMirrorArchetype;
	TMap<FGuLiSoldierId, FMassEntityHandle> ClientMirrorEntities;
	TMap<FGuLiSoldierId, int32> SoldierInstanceIndices;
	TMap<FGuLiSoldierId, FGuLiCommanderPresentedSoldier> PresentedSoldiers;
	TMap<FGuLiSoldierId, FGuLiCommanderPredictedMove> PredictedMoves;
	TMap<FGuLiSoldierId, double> WreckExpireTimes;
	TArray<FTransform> CachedUnitTransforms;
	TArray<FTransform> CachedRingTransforms;
	TArray<FLinearColor> CachedRingColors;
	double LatestMeasuredServerNowSeconds = 0.0;
	double LatestClockMeasurementLocalTimeSeconds = 0.0;
	double EstimatedServerTimeSeconds = 0.0;
	float LatestClockRoundTripMilliseconds = 0.0f;
	uint32 LatestClockFrameSequence = 0u;
	uint32 CurrentAuthorityEpoch = 0u;
	uint32 LastObservedSyncGeneration = 0u;
	bool bServerClockInitialized = false;
	bool bLatestClockRoundTripFromConnectionStats = false;
	bool bLoggedInstancePoolFailure = false;
};
