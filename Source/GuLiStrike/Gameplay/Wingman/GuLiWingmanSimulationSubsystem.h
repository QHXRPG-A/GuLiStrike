// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Battle/Contracts/GuLiWingmanProtocolTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "GuLiWingmanSimulationSubsystem.generated.h"

class AGuLiWingmanPawn;
class AActor;
class UStateTree;
class UStaticMesh;
struct FGuLiWingmanAcceptedBatch;
struct FGuLiWingmanRosterEntry;
struct FGuLiWingmanFlightNavigationRuntime;

struct GULISTRIKE_API FGuLiWingmanNavigationDiagnostics
{
	int32 PendingFlightRequests = 0;
	int32 ActiveFlightPaths = 0;
	int32 NavigationGuidedEntities = 0;
	uint64 AsyncRequestsIssued = 0u;
	uint64 ResultsAccepted = 0u;
	uint64 SafeFallbackResults = 0u;
	uint64 PendingRequestsCancelled = 0u;
};

struct GULISTRIKE_API FGuLiWingmanAvoidanceDiagnostics
{
	int32 EvaluatedEntities = 0;
	int32 FullLookAheadBlockedEntities = 0;
	int32 SafeHeadingEntities = 0;
	int32 ControlledRecoveryEntities = 0;
	int32 VerifiedRecoveryPointEntities = 0;
	int32 FlightNavBoundaryThreatEntities = 0;
	int32 WorldStaticThreatEntities = 0;
	int32 WorldDynamicThreatEntities = 0;
	uint64 HeadingProbes = 0u;
};

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
	/** Fire/attack authorization may lapse without suspending client flight. */
	bool bCombatAuthorizationValid = true;
	TArray<TWeakObjectPtr<AGuLiWingmanPawn>> Pawns;
	TArray<TSharedPtr<FGuLiWingmanFlightNavigationRuntime>> FlightNavigation;
	TWeakObjectPtr<AActor> CarrierActor;
	float NavigationEvaluationAccumulator = 0.0f;
	TArray<TPair<uint8, FGuLiWingmanAttackFireRecord>> PendingAttackShots;
	uint32 LastAttackTick = 0u;
};

struct FGuLiWingmanAttackDiagnostic
{
	FGuLiWingmanHandle Wingman;
	FGuLiWingmanAttackTarget Target;
	FName SlotId;
	uint8 Phase = 0u;
	uint8 FlightMode = 0u;
	uint8 CancelReason = 0u;
	uint8 GroundPathFailureMask = 0u;
	uint8 GroundNavigationFailureMask = 0u;
	FVector Position = FVector::ZeroVector;
	FVector Forward = FVector::ForwardVector;
	FVector Entry = FVector::ZeroVector;
	FVector PreferredVelocity = FVector::ZeroVector;
	FVector RetreatPoint = FVector::ZeroVector;
	FVector TurnControlPoint = FVector::ZeroVector;
	float TurnYawDegrees = 0.0f;
	float TurnPitchDegrees = 0.0f;
	uint32 RunId = 0u;
	uint32 CompletedGroundRuns = 0u;
	uint32 StateEntrySerial = 0u;
	int32 NextShot = 0;
	double StartTime = 0.0;
	double PhaseStartTime = 0.0;
	float ConsecutiveBlockedSeconds = 0.0f;
	bool bControlledRecovery = false;
	bool bGuiding = false;
};

struct GULISTRIKE_API FGuLiWingmanLocalRebaseRequest
{
	FGuLiWingmanHandle Wingman;
	EGuLiWingmanEmergencyRebaseReason Reason =
		EGuLiWingmanEmergencyRebaseReason::MovementDeadlock;
};

/** Client-only stable-order coordinator for independent Wingman Pawns. */
UCLASS(Config=Game)
class GULISTRIKE_API UGuLiWingmanSimulationSubsystem final : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	UGuLiWingmanSimulationSubsystem();
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	bool CreateOrResetOwnedGroup(const FGuLiWingmanGroupHandle& Group,
		const FGuLiGroupAbilityConfigSnapshot& AbilityConfig,
		const FTransform& CarrierTransform, const FVector& CarrierVelocity,
		const FGuLiCarrierSourceRef& CarrierSource, AActor* CarrierActor = nullptr);
	bool UpdateOwnedGroupCarrier(const FGuLiWingmanGroupHandle& Group,
		const FTransform& CarrierTransform, const FVector& CarrierVelocity,
		const FGuLiCarrierSourceRef& CarrierSource, AActor* CarrierActor = nullptr);
	bool ApplyCommittedAbilityConfig(const FGuLiWingmanGroupHandle& Group,
		const FGuLiGroupAbilityConfigSnapshot& AbilityConfig);
	bool ApplyRosterCut(const FGuLiWingmanGroupHandle& Group,
		const TArray<FGuLiWingmanRosterEntry>& Roster);
	bool InvalidateOwnedGroupAbilities(const FGuLiWingmanGroupHandle& Group);
	bool DestroyOwnedGroup(const FGuLiWingmanGroupHandle& Group);
	void DestroyAllOwnedGroups();

	bool AdvanceOwnedGroup(const FGuLiWingmanGroupHandle& Group, float DeltaSeconds);
	AGuLiWingmanPawn* FindOwnedPawn(const FGuLiWingmanHandle& Wingman) const;
	int32 GetOwnedPawnCount(const FGuLiWingmanGroupHandle& Group) const;
	int32 GetTotalOwnedPawnCount() const;
	bool IsOwnedGroupCombatAuthorizationValid(const FGuLiWingmanGroupHandle& Group) const;

	bool BuildCandidate(const FGuLiWingmanGroupHandle& Group, uint32 MatchEpoch,
		uint32 LeaseEpoch, uint32 CandidateSequence, uint32 ClientSimTick,
		FGuLiWingmanCandidateBatch& OutCandidate) const;
	bool BuildFlightCandidate(const FGuLiWingmanGroupHandle& Group, uint32 MatchEpoch,
		uint32 LeaseEpoch, uint32 ConnectionGeneration, uint32 RosterRevision,
		uint8 FlightIndex, uint8 RequiredMemberMask,
		EGuLiWingmanUploadRateClass RequestedRateClass, uint32 ObservedGrantRevision,
		uint32 CandidateSequence, uint32 FrameSequence, uint32 BaseAcceptedSequence,
		uint32 ClientSimTick, double CaptureEstimatedServerTimeSeconds,
		uint32 NavSchemaRevision, uint64 NavDataChecksum, uint32 TuningRevision,
		uint32 ObstacleRevision, FGuLiWingmanCandidateBatch& OutCandidate) const;
	bool ApplyAcceptedBatch(const FGuLiWingmanAcceptedBatch& AcceptedBatch);

	bool TryBuildWeaponFireIntent(const FGuLiWingmanGroupHandle& Group,
		const FGuLiWingmanHandle& Emitter, uint32 MatchEpoch, uint32 LeaseEpoch,
		const FGuLiWingmanWeaponChannelConfig& Channel,
		const FGuLiWingmanAttackTarget& AssignedTarget,
		double NowSeconds, uint32 ClientFireTick,
		bool bClientPredictedLineOfSight, FGuLiWingmanFireIntent& OutIntent);
	bool TryBuildBasicFireIntent(const FGuLiWingmanGroupHandle& Group,
		const FGuLiWingmanHandle& Emitter, uint32 MatchEpoch, uint32 LeaseEpoch,
		const FGuLiWingmanAttackTarget& AssignedTarget,
		double NowSeconds, double CooldownSeconds, uint32 ClientFireTick,
		bool bClientPredictedLineOfSight, FGuLiWingmanFireIntent& OutIntent);

	void TickAttackRuns(const FGuLiWingmanGroupHandle& Group,
		const FGuLiWingmanAttackAuthorityState& State, uint32 LeaseEpoch,
		uint32 ClientTick, double ServerTime, bool bActive);
	void AppendAttackFireRecords(FGuLiWingmanCandidateBatch& Candidate);
	void GetPendingAttackCaptureTicks(const FGuLiWingmanGroupHandle& Group,
		uint8 FlightIndex, TArray<uint32>& Out) const;
	void GetAttackDiagnostics(TArray<FGuLiWingmanAttackDiagnostic>& Out) const;
	bool GetCompletedSimulationTick(const FGuLiWingmanGroupHandle& Group,
		uint32& OutTick) const;

	void TickNavigationBehavior(const FGuLiWingmanGroupHandle& Group, float DeltaSeconds);
	bool GetNavigationDiagnostics(const FGuLiWingmanGroupHandle& Group,
		FGuLiWingmanNavigationDiagnostics& OutDiagnostics) const;
	bool GetAvoidanceDiagnostics(const FGuLiWingmanGroupHandle& Group,
		FGuLiWingmanAvoidanceDiagnostics& OutDiagnostics) const;
	bool GetMotionDiagnostics(FGuLiWingmanMotionDiagnostics& OutDiagnostics) const;

	void DrainEmergencyRebaseRequests(const FGuLiWingmanGroupHandle& Group,
		TArray<FGuLiWingmanLocalRebaseRequest>& OutRequests);
	void QueueStaleEmergencyRebaseRetries(const FGuLiWingmanGroupHandle& Group,
		double EstimatedServerTimeSeconds);
	bool ApplyEmergencyRebaseResponse(
		const FGuLiWingmanEmergencyRebaseResponse& Response,
		const FGuLiWingmanAcceptedBatch* AcceptedBatch);

	static bool IsOwnerSimulationNetMode(ENetMode NetMode);
	bool HasOwnedGroup(const FGuLiWingmanGroupHandle& Group) const;
	FSoftObjectPath GetConfiguredBehaviorStateTreePath() const
	{
		return MemberBehaviorStateTree.ToSoftObjectPath();
	}

#if WITH_DEV_AUTOMATION_TESTS
	void SetNavigationRequirementBypassForTests(bool bBypass)
	{
		bBypassNavigationRequirementForTests = bBypass;
	}
	void SetMemberBehaviorStateTreeForTests(UStateTree* StateTree);
	bool PrimeFlightNavigationStateForTests(const FGuLiWingmanGroupHandle& Group,
		uint8 FlightIndex, const TArray<FVector>& PathPoints, bool bPendingRequest);
	bool PrimeGroundDiveForTests(const FGuLiWingmanGroupHandle& Group,
		const FGuLiWingmanHandle& Wingman, double StartTime);
#endif

private:
	bool CanOwnSimulation() const;
	bool InitializeOwnedPawn(AGuLiWingmanPawn& Pawn,
		const FGuLiWingmanGroupHandle& Group, int32 GroupMemberIndex,
		const FGuLiGroupAbilityConfigSnapshot& AbilityConfig,
		const FVector& InitialPosition, const FTransform& CarrierTransform,
		const FVector& CarrierVelocity, const FGuLiCarrierSourceRef& CarrierSource,
		AActor* CarrierActor);
	AGuLiWingmanPawn* FindOwnedPawn(const FGuLiWingmanLocalGroupRuntime& Runtime,
		const FGuLiWingmanHandle& Wingman) const;
	void CancelNavigationForRuntime(FGuLiWingmanLocalGroupRuntime& Runtime,
		bool bClearPawnGuidance);
	void ClearFlightNavigationGuidance(FGuLiWingmanLocalGroupRuntime& Runtime,
		uint8 FlightIndex, bool bUsingSafeFallback);
	void PublishFlightNavigationGuidance(FGuLiWingmanLocalGroupRuntime& Runtime,
		uint8 FlightIndex, const FVector& Waypoint, const FVector& PathGoal,
		uint32 RequestSerial, uint16 PathPointIndex);
	UStateTree* ResolveMemberBehaviorStateTree() const;
	UStaticMesh* ResolveWingmanMesh() const;

	TMap<FGuLiWingmanGroupHandle, FGuLiWingmanLocalGroupRuntime> OwnedGroups;
	TSet<FGuLiWingmanGroupHandle> InitialFormationNavigationFailuresLogged;
	uint64 NavigationAsyncRequestsIssued = 0u;
	uint64 NavigationResultsAccepted = 0u;
	uint64 NavigationSafeFallbackResults = 0u;
	uint64 NavigationPendingRequestsCancelled = 0u;

	UPROPERTY(Config, EditAnywhere, Category="Wingman|Behavior")
	TSoftObjectPtr<UStateTree> MemberBehaviorStateTree;

	UPROPERTY(Config, EditAnywhere, Category="Wingman|Presentation")
	TSoftObjectPtr<UStaticMesh> DefaultWingmanMesh;

#if WITH_DEV_AUTOMATION_TESTS
	bool bBypassNavigationRequirementForTests = false;
	bool bAllowBehaviorAssetBypassForTests = false;
#endif
};
