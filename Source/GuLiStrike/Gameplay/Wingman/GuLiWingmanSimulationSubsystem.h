// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Battle/Contracts/GuLiWingmanProtocolTypes.h"
#include "MassArchetypeTypes.h"
#include "MassEntityHandle.h"
#include "MassEntityTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "GuLiWingmanSimulationSubsystem.generated.h"

class UMassEntitySubsystem;
class UStateTree;
class AGuLiWingmanGroupBehaviorRunner;
enum class EGuLiWingmanBehaviorPolicy : uint8;
struct FGuLiWingmanAcceptedBatch;
struct FGuLiWingmanRosterEntry;
struct FGuLiWingmanFlightNavigationRuntime;

/** Lightweight, read-only counters used by runtime diagnostics and automation. */
struct GULISTRIKE_API FGuLiWingmanNavigationDiagnostics
{
	int32 PendingFlightRequests = 0;
	int32 ActiveFlightPaths = 0;
	int32 NavigationGuidedEntities = 0;
	uint64 AsyncRequestsIssued = 0;
	uint64 ResultsAccepted = 0;
	uint64 SafeFallbackResults = 0;
	uint64 PendingRequestsCancelled = 0;
};

/** Last-frame owner-only avoidance state aggregated without mutating Mass. */
struct GULISTRIKE_API FGuLiWingmanAvoidanceDiagnostics
{
	int32 EvaluatedEntities = 0;
	int32 OccupiedSpatialCells = 0;
	int32 SeparationContributors = 0;
	int32 FullLookAheadBlockedEntities = 0;
	int32 SafeHeadingEntities = 0;
	int32 ControlledRecoveryEntities = 0;
	int32 VerifiedRecoveryPointEntities = 0;
	int32 FlightNavBoundaryThreatEntities = 0;
	int32 WorldStaticThreatEntities = 0;
	int32 WorldDynamicThreatEntities = 0;
	uint64 SpatialNeighborTests = 0;
	uint64 HeadingProbes = 0;
};

/** Read-only live motion summary used to distinguish Mass motion from presentation staleness. */
struct GULISTRIKE_API FGuLiWingmanMotionDiagnostics
{
	int32 EvaluatedEntities = 0;
	int32 AliveEntities = 0;
	int32 OrbitEntities = 0;
	int32 FollowEntities = 0;
	int32 CatchUpEntities = 0;
	int32 RecoverEntities = 0;
	int32 StaleEntities = 0;
	float MinimumCarrierDistanceCentimeters = 0.0f;
	float MeanCarrierDistanceCentimeters = 0.0f;
	float MaximumCarrierDistanceCentimeters = 0.0f;
	float MeanSpeedCentimetersPerSecond = 0.0f;
	FVector FirstAliveLocation = FVector::ZeroVector;
};

struct FGuLiWingmanLocalGroupRuntime
{
	FGuLiGroupAbilityConfigSnapshot AbilityConfig;
	TArray<FMassEntityHandle> Entities;
	TWeakObjectPtr<AGuLiWingmanGroupBehaviorRunner> BehaviorRunner;
	TArray<TSharedPtr<FGuLiWingmanFlightNavigationRuntime>> FlightNavigation;
	float NavigationEvaluationAccumulator = 0.0f;
	TArray<TPair<uint8, FGuLiWingmanAttackFireRecord>> PendingAttackShots;
	uint32 LastAttackTick = 0;
};

struct FGuLiWingmanAttackDiagnostic
{
	FGuLiWingmanHandle Wingman;
	uint8 Phase = 0;
	uint8 CancelReason = 0;
	FVector Position, Forward, Entry, PreferredVelocity;
	FVector RetreatPoint, TurnControlPoint;
	float TurnYawDegrees = 0.0f;
	float TurnPitchDegrees = 0.0f;
	uint32 RunId = 0;
	uint32 StateEntrySerial = 0;
	int32 NextShot = 0;
	double StartTime = 0;
	bool bGuiding = false;
};

/**
 * Client/listen-host/standalone owner of Wingman Mass entities. Dedicated
 * Server worlds never create this subsystem, so they cannot spawn or tick the
 * owner-motion archetype accidentally.
 */
UCLASS(Config=Game)
class GULISTRIKE_API UGuLiWingmanSimulationSubsystem final : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** Creates exactly 25 entities. Repeating an identical active config is a no-op. */
	bool CreateOrResetOwnedGroup(const FGuLiWingmanGroupHandle& Group,
		const FGuLiGroupAbilityConfigSnapshot& AbilityConfig,
		const FTransform& CarrierTransform, const FVector& CarrierVelocity,
		const FGuLiCarrierSourceRef& CarrierSource);

	/** Updates source data only; Mass Processors remain the only owner-motion writers. */
	bool UpdateOwnedGroupCarrier(const FGuLiWingmanGroupHandle& Group,
		const FTransform& CarrierTransform, const FVector& CarrierVelocity,
		const FGuLiCarrierSourceRef& CarrierSource);

	/** Applies a committed projection without modifying current entity transforms. */
	bool ApplyCommittedAbilityConfig(const FGuLiWingmanGroupHandle& Group,
		const FGuLiGroupAbilityConfigSnapshot& AbilityConfig);
	void TickAttackRuns(const FGuLiWingmanGroupHandle& Group, const FGuLiWingmanAttackAuthorityState& State,
		uint32 LeaseEpoch, uint32 ClientTick, double ServerTime, bool bActive);
	void AppendAttackFireRecords(FGuLiWingmanCandidateBatch& Candidate);
	void GetPendingAttackCaptureTicks(const FGuLiWingmanGroupHandle& Group, uint8 FlightIndex, TArray<uint32>& Out) const;
	void GetAttackDiagnostics(TArray<FGuLiWingmanAttackDiagnostic>& Out) const;
	bool GetCompletedSimulationTick(const FGuLiWingmanGroupHandle& Group, uint32& OutTick) const;

	/** Applies stable-slot identity/alive changes without touching local motion state. */
	bool ApplyRosterCut(const FGuLiWingmanGroupHandle& Group,
		const TArray<FGuLiWingmanRosterEntry>& Roster);

	/** Invalidates guidance immediately while preserving the last poses for relay fade-out. */
	bool InvalidateOwnedGroupAbilities(const FGuLiWingmanGroupHandle& Group);

	bool DestroyOwnedGroup(const FGuLiWingmanGroupHandle& Group);
	void DestroyAllOwnedGroups();

	bool BuildCandidate(const FGuLiWingmanGroupHandle& Group, uint32 MatchEpoch, uint32 LeaseEpoch,
		uint32 CandidateSequence, uint32 ClientSimTick, FGuLiWingmanCandidateBatch& OutCandidate) const;

	/** Strict protocol-v8 capture: exactly one Flight and its complete live member mask. */
	bool BuildFlightCandidate(const FGuLiWingmanGroupHandle& Group, uint32 MatchEpoch, uint32 LeaseEpoch,
		uint32 ConnectionGeneration, uint32 RosterRevision, uint8 FlightIndex, uint8 RequiredMemberMask,
		EGuLiWingmanUploadRateClass RequestedRateClass, uint32 ObservedGrantRevision,
		uint32 CandidateSequence, uint32 FrameSequence, uint32 BaseAcceptedSequence,
		uint32 ClientSimTick, double CaptureEstimatedServerTimeSeconds,
		uint32 NavSchemaRevision, uint64 NavDataChecksum, uint32 TuningRevision,
		uint32 ObstacleRevision, FGuLiWingmanCandidateBatch& OutCandidate) const;

	/**
	 * Commits an authority-accepted cut to each sample's independent weapon state.
	 * The cut must match the currently committed ability projection exactly.
	 */
	bool ApplyAcceptedBatch(const FGuLiWingmanAcceptedBatch& AcceptedBatch);

	/**
	 * Builds one protocol-v9 automatic-channel intent without sending it. Domain sequence
	 * remains per emitter while cooldown is isolated by WeaponSlotId.
	 */
	bool TryBuildWeaponFireIntent(const FGuLiWingmanGroupHandle& Group,
		const FGuLiWingmanHandle& Emitter, uint32 MatchEpoch, uint32 LeaseEpoch,
		const FGuLiWingmanWeaponChannelConfig& Channel,
		const FGuLiTargetHandle& Target, const FVector& TargetLocation,
		double NowSeconds, uint32 ClientFireTick,
		bool bClientPredictedLineOfSight, FGuLiWingmanFireIntent& OutIntent);

	/**
	 * Source-compatible adapter for the first BasicAutomatic channel.
	 * Sequence and cooldown
	 * are owned by the emitter and advance only when a complete intent is produced.
	 */
	bool TryBuildBasicFireIntent(const FGuLiWingmanGroupHandle& Group,
		const FGuLiWingmanHandle& Emitter, uint32 MatchEpoch, uint32 LeaseEpoch,
		const FGuLiTargetHandle& Target, const FVector& TargetLocation,
		double NowSeconds, double CooldownSeconds, uint32 ClientFireTick,
		bool bClientPredictedLineOfSight, FGuLiWingmanFireIntent& OutIntent);

	/** Low-frequency policy fallback called only by the explicit local group runner. Never writes Transform. */
	void TickFallbackBehavior(const FGuLiWingmanGroupHandle& Group, float DeltaSeconds);

	/** Pure observation used by StateTree conditions and signal generation; never mutates Mass. */
	EGuLiWingmanBehaviorPolicy EvaluateBehaviorPolicy(const FGuLiWingmanGroupHandle& Group) const;

	/** StateTree task sink. Updates FlightMode only; never writes Transform, velocity, health, or damage. */
	bool ApplyStateTreePolicy(const FGuLiWingmanGroupHandle& Group,
		EGuLiWingmanBehaviorPolicy Policy, float DeltaSeconds);

	/**
	 * Client-only coordinator shared by StateTree and fallback runners. It polls at
	 * most one asynchronous FlightNav request per Flight and publishes waypoint
	 * cuts; it never writes an entity Transform.
	 */
	void TickNavigationBehavior(const FGuLiWingmanGroupHandle& Group, float DeltaSeconds);

	/** Shared hard gate used by subsystem creation, runner creation and tests. */
	static bool IsOwnerSimulationNetMode(ENetMode NetMode);

	bool IsGroupUsingStateTree(const FGuLiWingmanGroupHandle& Group) const;
	bool IsGroupUsingControlledFallback(const FGuLiWingmanGroupHandle& Group) const;
	bool IsGroupNavigationCoordinationEnabled(const FGuLiWingmanGroupHandle& Group) const;
	FSoftObjectPath GetConfiguredBehaviorStateTreePath() const
	{
		return GroupBehaviorStateTree.ToSoftObjectPath();
	}
	bool GetNavigationDiagnostics(const FGuLiWingmanGroupHandle& Group,
		FGuLiWingmanNavigationDiagnostics& OutDiagnostics) const;
	bool GetAvoidanceDiagnostics(const FGuLiWingmanGroupHandle& Group,
		FGuLiWingmanAvoidanceDiagnostics& OutDiagnostics) const;
	bool GetMotionDiagnostics(FGuLiWingmanMotionDiagnostics& OutDiagnostics) const;

#if WITH_DEV_AUTOMATION_TESTS
	/** Explicit test-only escape hatch for transient Worlds that cannot contain authored navigation assets. */
	void SetNavigationRequirementBypassForTests(bool bBypass) { bBypassNavigationRequirementForTests = bBypass; }
	void SetGroupBehaviorStateTreeForTests(UStateTree* StateTree);

	/** Injects only the observation inputs used by the group behavior policy. */
	bool SetGroupBehaviorPolicyInputsForTests(const FGuLiWingmanGroupHandle& Group,
		const FVector& SteeringAcceleration, float ConsecutiveBlockedSeconds,
		bool bControlledRecovery, bool bDetectedWorldStatic,
		bool bDetectedWorldDynamic, bool bNavigationSafeFallback);
	const AGuLiWingmanGroupBehaviorRunner* GetBehaviorRunnerForTests(
		const FGuLiWingmanGroupHandle& Group) const;

	/** Seeds a pending/path state without requiring a cooked nav asset. Never writes Transform. */
	bool PrimeFlightNavigationStateForTests(const FGuLiWingmanGroupHandle& Group,
		uint8 FlightIndex, const TArray<FVector>& PathPoints, bool bPendingRequest);
#endif

	bool HasOwnedGroup(const FGuLiWingmanGroupHandle& Group) const;
	int32 GetOwnedEntityCount(const FGuLiWingmanGroupHandle& Group) const;
	int32 GetTotalOwnedEntityCount() const;

private:
	bool CanOwnSimulation() const;
	bool InitializeOwnedEntity(FMassEntityHandle Entity, const FGuLiWingmanGroupHandle& Group,
		int32 GroupMemberIndex, const FGuLiGroupAbilityConfigSnapshot& AbilityConfig,
		const FVector& InitialPosition, const FTransform& CarrierTransform, const FVector& CarrierVelocity,
		const FGuLiCarrierSourceRef& CarrierSource);
	bool StartBehaviorRunner(FGuLiWingmanLocalGroupRuntime& Runtime, const FGuLiWingmanGroupHandle& Group);
	FMassEntityHandle FindOwnedEntity(const FGuLiWingmanLocalGroupRuntime& Runtime,
		const FGuLiWingmanHandle& Wingman) const;
	void CancelNavigationForRuntime(FGuLiWingmanLocalGroupRuntime& Runtime, bool bClearEntityGuidance);
	void ClearFlightNavigationGuidance(FGuLiWingmanLocalGroupRuntime& Runtime, uint8 FlightIndex,
		bool bUsingSafeFallback);
	void PublishFlightNavigationGuidance(FGuLiWingmanLocalGroupRuntime& Runtime, uint8 FlightIndex,
		const FVector& Waypoint, const FVector& PathGoal, uint32 RequestSerial, uint16 PathPointIndex);

	UPROPERTY(Transient)
	TObjectPtr<UMassEntitySubsystem> MassEntitySubsystem;

	FMassArchetypeHandle OwnerArchetype;
	TMap<FGuLiWingmanGroupHandle, FGuLiWingmanLocalGroupRuntime> OwnedGroups;
	/** Prevents a failed bootstrap from flooding logs while retaining one precise failure per group. */
	TSet<FGuLiWingmanGroupHandle> InitialFormationNavigationFailuresLogged;
	uint64 NavigationAsyncRequestsIssued = 0;
	uint64 NavigationResultsAccepted = 0;
	uint64 NavigationSafeFallbackResults = 0;
	uint64 NavigationPendingRequestsCancelled = 0;

	/** Optional authored policy. Empty is intentional and selects the deterministic C++ fallback. */
	UPROPERTY(Config, EditAnywhere, Category="Wingman|Behavior")
	TSoftObjectPtr<UStateTree> GroupBehaviorStateTree;

#if WITH_DEV_AUTOMATION_TESTS
	bool bBypassNavigationRequirementForTests = false;
#endif
};
