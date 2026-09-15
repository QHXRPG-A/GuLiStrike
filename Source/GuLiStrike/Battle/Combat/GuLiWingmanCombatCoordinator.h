// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Battle/Combat/GuLiCombatDamageLedger.h"
#include "Battle/Relay/GuLiWingmanRelayServer.h"

class UGuLiLogicalMissileSubsystem;
class UGuLiShipHangarCapabilityComponent;

using FGuLiWingmanTargetResolver = TFunction<bool(
	const FGuLiTargetHandle& Handle, FGuLiCombatTargetSnapshot& OutSnapshot)>;
using FGuLiWingmanTargetCatalogResolver = TFunction<void(
	TArray<FGuLiCombatTargetSnapshot>& OutSnapshots)>;
using FGuLiWingmanLineOfSightResolver = TFunction<bool(
	const FVector& SourceLocation, const FGuLiCombatTargetSnapshot& Target)>;

struct GULISTRIKE_API FGuLiWingmanTargetingTuning
{
	float AcquireRadiusCentimeters = 150000.0f;
	float ReleaseRadiusCentimeters = 180000.0f;
	float GuardRejoinFraction = 0.8f;
	float ScanIntervalSeconds = 0.2f;
	float MaximumPoseAgeSeconds = 1.0f;
	bool IsWellFormed() const;
};

struct GULISTRIKE_API FGuLiWingmanGuardPoseObservation
{
	FVector Position = FVector::ZeroVector;
	bool bAlive = true;
	bool bHasFreshAcceptedPose = false;
};

namespace GuLiWingmanTargeting
{
	GULISTRIKE_API bool IsWithinAcquireRange(double DistanceCentimeters,
		const FGuLiWingmanTargetingTuning& Tuning);
	GULISTRIKE_API bool IsWithinReleaseRange(double DistanceCentimeters,
		const FGuLiWingmanTargetingTuning& Tuning);
	GULISTRIKE_API bool IsGuardRejoinComplete(TConstArrayView<FGuLiWingmanGuardPoseObservation> Observations,
		const FVector& ShipLocation, float CatchUpDistanceCentimeters,
		float RecoveryDistanceCentimeters, float RequiredFraction);
	/** Manual target wins; otherwise returns the exact member's automatic assignment. */
	GULISTRIKE_API const FGuLiWingmanAttackTarget* ResolveTargetForEmitter(
		const FGuLiWingmanAttackAuthorityState& State,
		const FGuLiWingmanHandle& Emitter);
}

namespace GuLiWingmanAttackAuthority
{
	/** Air bursts have one geometric start boundary; range, facing, LOS and flight mode are irrelevant. */
	GULISTRIKE_API bool IsAirBurstStartEligible(const FVector& SourceLocation,
		const FGuLiCombatTargetSnapshot& LiveTarget, float StartDistanceCentimeters);
}

/** Canonical wire representation for the free-look camera aim. */
namespace GuLiWingmanMissileAim
{
	constexpr int32 QuantizedUnit = 1000;
	GULISTRIKE_API bool Quantize(const FVector& Direction, FIntVector& OutDirectionMilli);
	GULISTRIKE_API bool Decode(const FIntVector& DirectionMilli, FVector& OutDirection);
}

/** Server dependencies and Ship combat identity. The owning Ship recreates this context on respawn. */
struct GULISTRIKE_API FGuLiWingmanCombatContext
{
	uint32 MatchEpoch = 0u;
	FGuLiTargetHandle ShipSource;
	EGuLiTeam ShipTeam = EGuLiTeam::Unassigned;
	TWeakObjectPtr<UGuLiShipHangarCapabilityComponent> HangarCapability;
	FGuLiWingmanRelayServer* Relay = nullptr;
	TWeakObjectPtr<UGuLiDamageLedgerSubsystem> DamageLedger;
	TWeakObjectPtr<UGuLiLogicalMissileSubsystem> LogicalMissiles;
	FGuLiWingmanTargetResolver TargetResolver;
	FGuLiWingmanTargetCatalogResolver TargetCatalogResolver;
	FGuLiWingmanLineOfSightResolver LineOfSightResolver;
	TFunction<double()> ServerTimeProvider;
	double MaximumAcceptedAgeSeconds = 0.35;

	bool IsWellFormed() const;
};

struct GULISTRIKE_API FGuLiWingmanBasicFireResult
{
	FGuLiWingmanSubmissionResult RelayResult;
	FGuLiDamageCommitResult DamageResult;
	FGuid ShotId;
	FGuid DamageEventId;

	bool WasCommitted() const { return RelayResult.Disposition == EGuLiWingmanSubmissionDisposition::Accepted
		&& DamageResult.WasAccepted(); }
};

/** Server-only authorization emitted by the Ship missile GA. Aim values must be server canonical. */
struct GULISTRIKE_API FGuLiWingmanMissileSalvoRequest
{
	FGuid ActivationId;
	FGuLiWeaponBindingKey Binding;
	FName SkillId;
	FGameplayTag MissileAbilityId;
	uint32 AbilitySetRevision = 0u;
	uint32 LoadoutRevision = 0u;
	uint32 ProfileRevision = 0u;
	uint32 MissileDefinitionRevision = 0u;
	/** Client camera forward, normalized then rounded to 1/1000 before the RPC. */
	FIntVector AimDirectionMilli = FIntVector::ZeroValue;

	bool IsWellFormed() const;
};

struct GULISTRIKE_API FGuLiWingmanMissileSalvoResult
{
	EGuLiWingmanRejectReason RejectReason = EGuLiWingmanRejectReason::InvalidIdentity;
	uint8 FlightIndex = MAX_uint8;
	int32 LaunchedCount = 0;
	bool bSharedCooldownStarted = false;
	/** Authority-selected target. No client-provided target handle participates. */
	FGuLiTargetHandle SelectedTarget;

	bool WasLaunched() const
	{
		return RejectReason == EGuLiWingmanRejectReason::None && LaunchedCount > 0 && bSharedCooldownStarted;
	}
};

/**
 * Server-only bridge from Ship GAS + ClientValidatedRelay to common combat truth.
 * It never creates a replicated missile Actor and never owns Wingman transforms.
 */
class GULISTRIKE_API FGuLiWingmanCombatCoordinator
{
public:
	bool Initialize(const FGuLiWingmanCombatContext& InContext, FString* OutError = nullptr);
	void Reset();
	bool IsReady() const;
	/** Migrates per-member/per-slot cadence without resetting formation or sibling channels. */
	bool ApplyCommittedAbilityConfig(
		const FGuLiGroupAbilityConfigSnapshot& NewConfig, double NowSeconds);

	/** Inject this into FGuLiWingmanRelayServer::SubmitFireIntent. */
	FGuLiFireIntentServerValidator MakeBasicFireIntentValidator();
	EGuLiWingmanRejectReason ValidateBasicFireIntent(const FGuLiWingmanFireIntent& Intent,
		const FGuLiWingmanAcceptedBatch& SourceBatch, double NowSeconds) const;

	/** Convenience path that validates/reserves in Relay and then commits one deterministic ledger event. */
	FGuLiWingmanBasicFireResult SubmitBasicFireIntent(const FGuid& SenderPlayerGuid,
		const FGuLiWingmanFireIntent& Intent, double NowSeconds);

	/** Use from an accepted-Intent callback when Relay submission happens in a network component. */
	FGuLiWingmanBasicFireResult CommitAcceptedBasicFireIntent(
		const FGuLiWingmanFireIntent& Intent, double NowSeconds);

	/** Server GA path. Chooses one stable Flight and launches at most its five legal members. */
	FGuLiWingmanMissileSalvoResult ActivateMissileSalvo(
		const FGuLiWingmanMissileSalvoRequest& Request, double NowSeconds);

	/** Removes per-emitter cooldown state for dead/replaced handles without disturbing surviving members. */
	int32 SynchronizeRosterState();
	void TickAttackTargeting(double NowSeconds, const FGuLiWingmanTargetingTuning& Tuning);
	bool SetSpecifiedAttackTarget(const FGuLiTargetHandle& Target);
	void ClearSpecifiedAttackTarget();
	FGuLiWingmanAttackTarget GetAttackTarget() const;
	int32 CommitValidatedAttackBatch(const FGuLiWingmanCandidateBatch& Candidate, double NowSeconds);
	/** Clears every late-fire authorization owned by a member that was authority-rebased. */
	void InvalidateMemberAfterEmergencyRebase(const FGuLiWingmanHandle& Emitter);

	int32 GetRememberedMissileActivationCount() const { return MissileResultsByActivation.Num(); }

private:
	bool ResolveTarget(const FGuLiTargetHandle& Handle, FGuLiCombatTargetSnapshot& OutSnapshot) const;
	void GetTargetCatalog(TArray<FGuLiCombatTargetSnapshot>& OutSnapshots) const;
	EGuLiWingmanRejectReason SelectMissileTarget(
		const FVector& AuthorityOrigin,
		const FVector& AimForward,
		const FGuLiWingmanWeaponRuntimeConfig& Runtime,
		FGuLiCombatTargetSnapshot& OutTarget) const;
	bool IsShipSourceAlive() const;
	bool HasLineOfSight(const FVector& SourceLocation, const FGuLiCombatTargetSnapshot& Target) const;
	bool IsEnemyTarget(const FGuLiCombatTargetSnapshot& Target) const;
	bool IsFreshAcceptedBatch(const FGuLiWingmanAcceptedBatch& Batch, double NowSeconds) const;
	bool IsRosterMemberAlive(const FGuLiWingmanHandle& Emitter) const;
	EGuLiWingmanRejectReason ValidateCurrentBasicDefinition(
		const FGuLiWingmanFireIntent& Intent) const;
	EGuLiWingmanRejectReason ValidateCurrentMissileDefinition(
		const FGuLiWingmanMissileSalvoRequest& Request) const;
	double GetServerTimeSeconds() const;
	void RememberMissileResult(const FGuid& ActivationId, const FGuLiWingmanMissileSalvoResult& Result);
	FGuLiWingmanMissileSalvoResult FinishMissileRequest(
		const FGuid& ActivationId,
		const FGuLiWingmanMissileSalvoResult& Result);
	static FGuid MakeStableShotId(const FGuLiWingmanFireIntent& Intent, uint32 Salt);
	static FGuid MakeStableMissileId(const FGuid& ActivationId, const FGuLiWingmanHandle& Emitter, uint32 Salt);
	void RecordAttackAuthorizationSnapshot();
	bool WasTargetAuthorized(const FGuLiWingmanHandle& Emitter,
		const FGuLiTargetHandle& Target, uint32 AssignmentRevision) const;
	bool WasTargetAuthorized(const FGuLiWingmanHandle& Emitter,
		const FGuLiWingmanAttackTarget& Target) const;

	struct FAutomaticTargetVersionState
	{
		FGuLiTargetHandle Target;
		uint32 Revision = 0u;
	};

	struct FAttackAuthorization
	{
		FGuLiWingmanHandle Emitter;
		FGuLiWingmanAttackTarget Target;
	};

	struct FAttackAuthorizationSnapshot
	{
		TArray<FAttackAuthorization> Entries;
	};

	struct FGroundCorridorCacheEntry
	{
		FVector TargetLocation = FVector::ZeroVector;
		double ValidUntilSeconds = 0.0;
		bool bClear = false;
	};

	FGuLiWingmanCombatContext Context;
	FGuLiTargetHandle SpecifiedAttackTarget;
	TMap<FGuLiWingmanHandle, FAutomaticTargetVersionState> AutomaticTargetVersions;
	TMap<FGuLiTargetHandle, FGroundCorridorCacheEntry> GroundCorridorCache;
	TArray<FAttackAuthorizationSnapshot> AttackTargetHistory;
	double NextAttackTargetScan = 0.0;
	FGuLiWingmanTargetingTuning TargetingTuning;
	FGuLiGroupAbilityConfigSnapshot LastWeaponConfig;
	TMap<FGuLiWingmanHandle, TMap<FName, double>> NextFireTimeByEmitterAndSlot;
	TMap<FGuid, FGuLiWingmanMissileSalvoResult> MissileResultsByActivation;
	TArray<FGuid> MissileActivationOrder;
	int32 MaximumRememberedMissileActivations = 256;
};
