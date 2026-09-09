// Copyright Epic Games, Inc. All Rights Reserved.

#include "Battle/Combat/GuLiWingmanCombatCoordinator.h"

#include "Battle/Combat/GuLiLogicalMissileSubsystem.h"
#include "CollisionQueryParams.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Gameplay/Ship/Abilities/GuLiShipAbilityDefinitions.h"
#include "Gameplay/Ship/Abilities/GuLiShipAbilitySystemComponent.h"

namespace
{
	bool SameGroup(const FGuLiWingmanGroupHandle& Group, const FGuLiGroupAbilityConfigSnapshot& Config)
	{
		return Group.ShipInstanceId == Config.ShipInstanceId
			&& Group.ShipGeneration == Config.ShipGeneration
			&& Group.GroupGeneration == Config.GroupGeneration;
	}

	FVector SamplePosition(const FGuLiWingmanCandidateSample& Sample)
	{
		return FVector(
			static_cast<double>(Sample.PositionCentimeters.X),
			static_cast<double>(Sample.PositionCentimeters.Y),
			static_cast<double>(Sample.PositionCentimeters.Z));
	}

	bool IsInsideRange(const FVector& Source, const FGuLiCombatTargetSnapshot& Target, const float Range)
	{
		const double EffectiveRange = static_cast<double>(Range) + FMath::Max(0.0f, Target.CollisionRadius);
		return FVector::DistSquared(Source, Target.Location) <= FMath::Square(EffectiveRange);
	}

	bool IsInsideAimCone(const FVector& Origin, const FVector& Forward,
		const FGuLiCombatTargetSnapshot& Target, const float HalfAngleDegrees)
	{
		const FVector SafeForward = Forward.GetSafeNormal();
		const FVector ToTarget = (Target.Location - Origin).GetSafeNormal();
		return !SafeForward.IsNearlyZero() && !ToTarget.IsNearlyZero()
			&& FVector::DotProduct(SafeForward, ToTarget)
				>= FMath::Cos(FMath::DegreesToRadians(HalfAngleDegrees));
	}

	bool IsFiniteTime(const double Value)
	{
		return FMath::IsFinite(Value) && Value >= 0.0;
	}

	bool IsStableWingmanTargetLess(const FGuLiTargetHandle& Lhs, const FGuLiTargetHandle& Rhs)
	{
		if (Lhs.Kind != Rhs.Kind)
		{
			return static_cast<uint8>(Lhs.Kind) < static_cast<uint8>(Rhs.Kind);
		}
		if (Lhs.AuthorityId != Rhs.AuthorityId)
		{
			return Lhs.AuthorityId < Rhs.AuthorityId;
		}
		if (Lhs.Generation != Rhs.Generation)
		{
			return Lhs.Generation < Rhs.Generation;
		}
		return Lhs.LocalId < Rhs.LocalId;
	}
}

bool GuLiWingmanMissileAim::Quantize(
	const FVector& Direction,
	FIntVector& OutDirectionMilli)
{
	OutDirectionMilli = FIntVector::ZeroValue;
	if (Direction.ContainsNaN())
	{
		return false;
	}
	const FVector Unit = Direction.GetSafeNormal();
	if (Unit.IsNearlyZero())
	{
		return false;
	}
	OutDirectionMilli = FIntVector(
		FMath::RoundToInt(Unit.X * QuantizedUnit),
		FMath::RoundToInt(Unit.Y * QuantizedUnit),
		FMath::RoundToInt(Unit.Z * QuantizedUnit));
	return OutDirectionMilli != FIntVector::ZeroValue;
}

bool GuLiWingmanMissileAim::Decode(
	const FIntVector& DirectionMilli,
	FVector& OutDirection)
{
	OutDirection = FVector::ZeroVector;
	if (FMath::Abs(DirectionMilli.X) > QuantizedUnit
		|| FMath::Abs(DirectionMilli.Y) > QuantizedUnit
		|| FMath::Abs(DirectionMilli.Z) > QuantizedUnit)
	{
		return false;
	}
	const int64 LengthSquared = static_cast<int64>(DirectionMilli.X) * DirectionMilli.X
		+ static_cast<int64>(DirectionMilli.Y) * DirectionMilli.Y
		+ static_cast<int64>(DirectionMilli.Z) * DirectionMilli.Z;
	constexpr int64 MinimumLengthSquared = 990ll * 990ll;
	constexpr int64 MaximumLengthSquared = 1010ll * 1010ll;
	if (LengthSquared < MinimumLengthSquared || LengthSquared > MaximumLengthSquared)
	{
		return false;
	}
	OutDirection = FVector(
		static_cast<double>(DirectionMilli.X),
		static_cast<double>(DirectionMilli.Y),
		static_cast<double>(DirectionMilli.Z)).GetSafeNormal();
	return !OutDirection.IsNearlyZero() && OutDirection.IsNormalized();
}

bool FGuLiWingmanCombatContext::IsWellFormed() const
{
	return MatchEpoch != 0u && ShipSource.IsValid() && ShipTeam != EGuLiTeam::Unassigned
		&& ShipASC.IsValid() && Relay && DamageLedger.IsValid() && LogicalMissiles.IsValid()
		&& FMath::IsFinite(MaximumAcceptedAgeSeconds) && MaximumAcceptedAgeSeconds > 0.0;
}

bool FGuLiWingmanTargetingTuning::IsWellFormed() const
{
	return FMath::IsFinite(AcquireRadiusCentimeters) && AcquireRadiusCentimeters > 0
		&& FMath::IsFinite(ReleaseRadiusCentimeters) && ReleaseRadiusCentimeters >= AcquireRadiusCentimeters
		&& FMath::IsFinite(GuardRejoinFraction) && GuardRejoinFraction > 0 && GuardRejoinFraction <= 1
		&& FMath::IsFinite(ScanIntervalSeconds) && ScanIntervalSeconds > 0
		&& FMath::IsFinite(MaximumPoseAgeSeconds) && MaximumPoseAgeSeconds > 0;
}

bool GuLiWingmanTargeting::IsWithinAcquireRange(double DistanceCentimeters,
	const FGuLiWingmanTargetingTuning& Tuning)
{
	return Tuning.IsWellFormed() && FMath::IsFinite(DistanceCentimeters) && DistanceCentimeters >= 0
		&& DistanceCentimeters <= Tuning.AcquireRadiusCentimeters;
}

bool GuLiWingmanTargeting::IsWithinReleaseRange(double DistanceCentimeters,
	const FGuLiWingmanTargetingTuning& Tuning)
{
	return Tuning.IsWellFormed() && FMath::IsFinite(DistanceCentimeters) && DistanceCentimeters >= 0
		&& DistanceCentimeters <= Tuning.ReleaseRadiusCentimeters;
}

const FGuLiWingmanAttackTarget* GuLiWingmanTargeting::ResolveTargetForEmitter(
	const FGuLiWingmanAttackAuthorityState& State,
	const FGuLiWingmanHandle& Emitter)
{
	if (!Emitter.IsValid())
	{
		return nullptr;
	}
	if (State.Target.IsValid() && State.Target.bSpecified)
	{
		return &State.Target;
	}
	const FGuLiWingmanAutoTargetAssignment* Assignment =
		State.AutomaticTargets.FindByPredicate([&Emitter](const FGuLiWingmanAutoTargetAssignment& Entry)
		{
			return Entry.Emitter == Emitter;
		});
	return Assignment && Assignment->Target.IsValid() && !Assignment->Target.bSpecified
		? &Assignment->Target : nullptr;
}

bool GuLiWingmanTargeting::IsGuardRejoinComplete(
	TConstArrayView<FGuLiWingmanGuardPoseObservation> Observations, const FVector& ShipLocation,
	float CatchUpDistanceCentimeters, float RecoveryDistanceCentimeters, float RequiredFraction)
{
	if (Observations.IsEmpty() || ShipLocation.ContainsNaN() || !FMath::IsFinite(CatchUpDistanceCentimeters)
		|| !FMath::IsFinite(RecoveryDistanceCentimeters) || !FMath::IsFinite(RequiredFraction)
		|| CatchUpDistanceCentimeters <= 0 || RecoveryDistanceCentimeters < CatchUpDistanceCentimeters
		|| RequiredFraction <= 0 || RequiredFraction > 1) return false;
	int32 Alive = 0, InsideCatchUp = 0;
	for (const FGuLiWingmanGuardPoseObservation& Observation : Observations)
	{
		if (!Observation.bAlive) continue;
		++Alive;
		if (!Observation.bHasFreshAcceptedPose || Observation.Position.ContainsNaN()) return false;
		const double Distance = FVector::Distance(ShipLocation, Observation.Position);
		if (Distance > RecoveryDistanceCentimeters) return false;
		if (Distance <= CatchUpDistanceCentimeters) ++InsideCatchUp;
	}
	return Alive > 0 && InsideCatchUp >= FMath::CeilToInt(Alive * RequiredFraction);
}

bool FGuLiWingmanMissileSalvoRequest::IsWellFormed() const
{
	FVector DecodedAim;
	return ActivationId.IsValid() && Binding.IsWellFormed()
		&& Binding.Domain == EGuLiWeaponDomain::Wingman
		&& !SkillId.IsNone() && MissileAbilityId.IsValid()
		&& AbilitySetRevision != 0u && LoadoutRevision != 0u
		&& ProfileRevision != 0u && MissileDefinitionRevision != 0u
		&& GuLiWingmanMissileAim::Decode(AimDirectionMilli, DecodedAim);
}

bool FGuLiWingmanCombatCoordinator::Initialize(
	const FGuLiWingmanCombatContext& InContext, FString* OutError)
{
	Reset();
	if (!InContext.IsWellFormed())
	{
		if (OutError)
		{
			*OutError = TEXT("Wingman combat context is incomplete or malformed.");
		}
		return false;
	}
	if (InContext.DamageLedger->GetMatchEpoch() != InContext.MatchEpoch)
	{
		if (OutError)
		{
			*OutError = TEXT("Damage Ledger epoch does not match the Wingman combat context.");
		}
		return false;
	}
	Context = InContext;
	if (!IsShipSourceAlive())
	{
		if (OutError)
		{
			*OutError = TEXT("Ship source is missing, dead, or registered to a different team.");
		}
		Reset();
		return false;
	}
	LastWeaponConfig = Context.Relay->GetAbilityConfig();
	// Preserve the existing initial-spawn readiness while explicitly registering
	// every generation. SynchronizeRosterState can then distinguish a replenished
	// handle from an original member that simply has not fired yet.
	for (const FGuLiWingmanRosterEntry& Entry : Context.Relay->GetRoster())
	{
		if (Entry.bDead)
		{
			continue;
		}
		TMap<FName, double>& EmitterCooldowns =
			NextFireTimeByEmitterAndSlot.FindOrAdd(Entry.Wingman);
		for (const FGuLiWingmanWeaponChannelConfig& Channel : LastWeaponConfig.WeaponChannels)
		{
			if (Channel.bEnabled && Channel.Kind == EGuLiWingmanWeaponKind::BasicAutomatic)
			{
				EmitterCooldowns.FindOrAdd(Channel.Binding.SlotId, 0.0);
			}
		}
	}
	for (const FGuLiWingmanAutoTargetAssignment& Assignment : Context.Relay->AttackState.AutomaticTargets)
	{
		FAutomaticTargetVersionState& Version = AutomaticTargetVersions.FindOrAdd(Assignment.Emitter);
		Version.Target = Assignment.Target.Target;
		Version.Revision = Assignment.Target.Revision;
	}
	RecordAttackAuthorizationSnapshot();
	return true;
}

void FGuLiWingmanCombatCoordinator::Reset()
{
	Context = FGuLiWingmanCombatContext{};
	SpecifiedAttackTarget = {};
	AutomaticTargetVersions.Reset();
	GroundCorridorCache.Reset();
	AttackTargetHistory.Reset();
	NextAttackTargetScan = 0.0;
	TargetingTuning = FGuLiWingmanTargetingTuning{};
	bAutoTargetingLockedForGuard = false;
	LastWeaponConfig = FGuLiGroupAbilityConfigSnapshot{};
	NextFireTimeByEmitterAndSlot.Reset();
	MissileResultsByActivation.Reset();
	MissileActivationOrder.Reset();
}

bool FGuLiWingmanCombatCoordinator::IsReady() const
{
	return Context.IsWellFormed()
		&& Context.DamageLedger->GetMatchEpoch() == Context.MatchEpoch
		&& IsShipSourceAlive();
}

bool FGuLiWingmanCombatCoordinator::ApplyCommittedAbilityConfig(
	const FGuLiGroupAbilityConfigSnapshot& NewConfig,
	const double NowSeconds)
{
	if (!IsReady() || !NewConfig.IsUsableByLeaseOwner() || !IsFiniteTime(NowSeconds)
		|| !SameGroup(Context.Relay->GetLeaseState().Group, NewConfig)
		|| Context.Relay->GetAbilityConfig().SnapshotHash != NewConfig.SnapshotHash)
	{
		return false;
	}
	if (LastWeaponConfig.HasSameVersion(NewConfig))
	{
		return true;
	}
	for (const FGuLiWingmanWeaponChannelConfig& Channel : NewConfig.WeaponChannels)
	{
		if (!Channel.bEnabled || Channel.Kind != EGuLiWingmanWeaponKind::BasicAutomatic)
		{
			continue;
		}
		const FGuLiWingmanWeaponChannelConfig* Previous =
			LastWeaponConfig.FindWeaponChannel(Channel.Binding);
		if (!Previous || !Previous->bEnabled || Previous->SkillId != Channel.SkillId)
		{
			for (const FGuLiWingmanRosterEntry& Entry : Context.Relay->GetRoster())
			{
				if (Entry.bDead) continue;
				TMap<FName, double>& EmitterCooldowns =
					NextFireTimeByEmitterAndSlot.FindOrAdd(Entry.Wingman);
				double& Next = EmitterCooldowns.FindOrAdd(Channel.Binding.SlotId);
				Next = FMath::Max(Next, NowSeconds + Channel.Runtime.CooldownSeconds);
			}
		}
		else if (!FMath::IsNearlyEqual(
			Previous->Runtime.CooldownSeconds, Channel.Runtime.CooldownSeconds))
		{
			for (TPair<FGuLiWingmanHandle, TMap<FName, double>>& EmitterPair :
				NextFireTimeByEmitterAndSlot)
			{
				if (double* ExistingNext = EmitterPair.Value.Find(Channel.Binding.SlotId))
				{
					const double RemainingRatio = FMath::Clamp(
						(*ExistingNext - NowSeconds) / Previous->Runtime.CooldownSeconds, 0.0, 1.0);
					*ExistingNext = NowSeconds + RemainingRatio * Channel.Runtime.CooldownSeconds;
				}
			}
		}
	}
	LastWeaponConfig = NewConfig;
	return true;
}

FGuLiFireIntentServerValidator FGuLiWingmanCombatCoordinator::MakeBasicFireIntentValidator()
{
	return [this](const FGuLiWingmanFireIntent& Intent, const FGuLiWingmanAcceptedBatch& SourceBatch)
	{
		return ValidateBasicFireIntent(Intent, SourceBatch, GetServerTimeSeconds());
	};
}

EGuLiWingmanRejectReason FGuLiWingmanCombatCoordinator::ValidateBasicFireIntent(
	const FGuLiWingmanFireIntent& Intent,
	const FGuLiWingmanAcceptedBatch& SourceBatch,
	const double NowSeconds) const
{
	if (!IsReady() || !IsFiniteTime(NowSeconds)
		|| Context.Relay->GetLeaseState().Lifecycle != EGuLiWingmanGroupLifecycle::Active)
	{
		return EGuLiWingmanRejectReason::InactiveGroup;
	}
	EGuLiWingmanRejectReason RejectReason = ValidateCurrentBasicDefinition(Intent);
	if (RejectReason != EGuLiWingmanRejectReason::None)
	{
		return RejectReason;
	}
	if (!IsFreshAcceptedBatch(SourceBatch, NowSeconds)
		|| SourceBatch.StateRef.MatchEpoch != Intent.SourceAcceptedState.MatchEpoch
		|| SourceBatch.StateRef.GroupGeneration != Intent.SourceAcceptedState.GroupGeneration
		|| SourceBatch.StateRef.AcceptedSequence != Intent.SourceAcceptedState.AcceptedSequence
		|| SourceBatch.StateRef.ClientSimTick != Intent.SourceAcceptedState.ClientSimTick)
	{
		return EGuLiWingmanRejectReason::StaleSourceState;
	}
	const FGuLiWingmanCandidateSample* EmitterSample = SourceBatch.FindSample(Intent.Emitter);
	if (!EmitterSample || !IsRosterMemberAlive(Intent.Emitter))
	{
		return EGuLiWingmanRejectReason::EmitterDead;
	}

	FGuLiCombatTargetSnapshot Target;
	if (!ResolveTarget(Intent.Target, Target) || !Target.bAlive)
	{
		return EGuLiWingmanRejectReason::InvalidTarget;
	}
	if (!IsEnemyTarget(Target))
	{
		return EGuLiWingmanRejectReason::FriendlyTarget;
	}
	if (!WasTargetAuthorized(
		Intent.Emitter, Intent.Target, Intent.TargetAssignmentRevision))
	{
		return EGuLiWingmanRejectReason::InvalidTarget;
	}
	const FGuLiWingmanWeaponChannelConfig* Channel =
		Context.Relay->GetAbilityConfig().FindWeaponChannel(Intent.Binding);
	if (!Channel)
	{
		return EGuLiWingmanRejectReason::UnknownWeaponChannel;
	}
	const FVector EmitterPosition = SamplePosition(*EmitterSample);
	if (!IsInsideRange(EmitterPosition, Target, Channel->Runtime.RangeCentimeters))
	{
		return EGuLiWingmanRejectReason::OutOfRange;
	}
	const FVector IntentAim(
		static_cast<double>(Intent.AimDirectionMilli.X),
		static_cast<double>(Intent.AimDirectionMilli.Y),
		static_cast<double>(Intent.AimDirectionMilli.Z));
	if (!IsInsideAimCone(EmitterPosition, IntentAim, Target, Channel->Runtime.TargetConeHalfAngleDegrees))
	{
		return EGuLiWingmanRejectReason::InvalidTarget;
	}
	if (Channel->Runtime.bRequiresLineOfSight && !HasLineOfSight(EmitterPosition, Target))
	{
		return EGuLiWingmanRejectReason::NoLineOfSight;
	}
	if (const TMap<FName, double>* EmitterCooldowns =
		NextFireTimeByEmitterAndSlot.Find(Intent.Emitter))
	{
		if (const double* NextFireTime = EmitterCooldowns->Find(Intent.Binding.SlotId);
			NextFireTime && NowSeconds + UE_DOUBLE_SMALL_NUMBER < *NextFireTime)
		{
			return EGuLiWingmanRejectReason::CooldownActive;
		}
	}
	return EGuLiWingmanRejectReason::None;
}

FGuLiWingmanBasicFireResult FGuLiWingmanCombatCoordinator::SubmitBasicFireIntent(
	const FGuid& SenderPlayerGuid,
	const FGuLiWingmanFireIntent& Intent,
	const double NowSeconds)
{
	FGuLiWingmanBasicFireResult Result;
	if (!IsReady())
	{
		Result.RelayResult = FGuLiWingmanSubmissionResult::Rejected(EGuLiWingmanRejectReason::InactiveGroup,
			Intent.DomainFireSequence);
		return Result;
	}
	const FGuLiFireIntentServerValidator Validator = [this, NowSeconds](
		const FGuLiWingmanFireIntent& Candidate, const FGuLiWingmanAcceptedBatch& Batch)
	{
		return ValidateBasicFireIntent(Candidate, Batch, NowSeconds);
	};
	Result.RelayResult = Context.Relay->SubmitFireIntent(SenderPlayerGuid, Intent, NowSeconds, Validator);
	if (Result.RelayResult.Disposition != EGuLiWingmanSubmissionDisposition::Accepted)
	{
		return Result;
	}
	return CommitAcceptedBasicFireIntent(Intent, NowSeconds);
}

FGuLiWingmanBasicFireResult FGuLiWingmanCombatCoordinator::CommitAcceptedBasicFireIntent(
	const FGuLiWingmanFireIntent& Intent, const double NowSeconds)
{
	FGuLiWingmanBasicFireResult Result;
	Result.RelayResult = FGuLiWingmanSubmissionResult::AcceptedSequence(Intent.DomainFireSequence);
	if (!IsReady() || !IsFiniteTime(NowSeconds))
	{
		Result.RelayResult = FGuLiWingmanSubmissionResult::Rejected(
			EGuLiWingmanRejectReason::InactiveGroup, Intent.DomainFireSequence);
		return Result;
	}
	const FGuLiWingmanAcceptedBatch* SourceBatch = Context.Relay->FindAcceptedBatch(Intent.SourceAcceptedState);
	const FGuLiWingmanCandidateSample* SourceSample = SourceBatch ? SourceBatch->FindSample(Intent.Emitter) : nullptr;
	const FGuLiWingmanWeaponChannelConfig* Channel =
		Context.Relay->GetAbilityConfig().FindWeaponChannel(Intent.Binding);
	if (!SourceBatch || !SourceSample || !Channel || !Channel->bEnabled)
	{
		Result.RelayResult = FGuLiWingmanSubmissionResult::Rejected(
			EGuLiWingmanRejectReason::StaleSourceState, Intent.DomainFireSequence);
		return Result;
	}
	FGuLiCombatTargetSnapshot Target;
	if (!ResolveTarget(Intent.Target, Target))
	{
		Result.RelayResult = FGuLiWingmanSubmissionResult::Rejected(
			EGuLiWingmanRejectReason::InvalidTarget, Intent.DomainFireSequence);
		return Result;
	}

	Result.ShotId = MakeStableShotId(Intent, 0x53484f54u);
	Result.DamageEventId = MakeStableShotId(Intent, 0x44414d47u);
	FGuLiDamageRequest Damage;
	Damage.MatchEpoch = Context.MatchEpoch;
	Damage.DamageEventId = Result.DamageEventId;
	Damage.ShotId = Result.ShotId;
	Damage.Source = Context.ShipSource;
	Damage.Emitter = Intent.Emitter;
	Damage.Target = Intent.Target;
	Damage.WeaponBinding = Channel->Binding;
	Damage.SkillId = Channel->SkillId;
	Damage.LoadoutRevision = Intent.LoadoutRevision;
	Damage.ProfileRevision = Channel->ProfileRevision;
	Damage.RootEventId = Result.ShotId;
	Damage.Damage = Channel->Runtime.Damage;
	Damage.HitLocation = Target.Location;
	Result.DamageResult = Context.DamageLedger->CommitDamage(Damage);
	if (Result.DamageResult.Status == EGuLiDamageCommitStatus::Committed)
	{
		TMap<FName, double>& EmitterCooldowns =
			NextFireTimeByEmitterAndSlot.FindOrAdd(Intent.Emitter);
		double& NextFireTime = EmitterCooldowns.FindOrAdd(Intent.Binding.SlotId);
		NextFireTime = FMath::Max(NextFireTime, NowSeconds + Channel->Runtime.CooldownSeconds);
	}
	return Result;
}

FGuLiWingmanMissileSalvoResult FGuLiWingmanCombatCoordinator::ActivateMissileSalvo(
	const FGuLiWingmanMissileSalvoRequest& Request, const double NowSeconds)
{
	FGuLiWingmanMissileSalvoResult Result;
	if (const FGuLiWingmanMissileSalvoResult* Previous = MissileResultsByActivation.Find(Request.ActivationId))
	{
		Result = *Previous;
		Result.RejectReason = EGuLiWingmanRejectReason::Duplicate;
		Result.LaunchedCount = 0;
		Result.bSharedCooldownStarted = false;
		return Result;
	}
	auto Finish = [this, &Request](const FGuLiWingmanMissileSalvoResult& FinalResult)
	{
		return FinishMissileRequest(Request.ActivationId, FinalResult);
	};
	if (!Request.IsWellFormed())
	{
		Result.RejectReason = EGuLiWingmanRejectReason::InvalidIdentity;
		return Finish(Result);
	}
	if (!IsReady() || !IsFiniteTime(NowSeconds)
		|| Context.Relay->GetLeaseState().Lifecycle != EGuLiWingmanGroupLifecycle::Active
		|| !Context.ShipASC->IsActiveAbilityInputEnabled())
	{
		Result.RejectReason = EGuLiWingmanRejectReason::InactiveGroup;
		return Finish(Result);
	}
	Result.RejectReason = ValidateCurrentMissileDefinition(Request);
	if (Result.RejectReason != EGuLiWingmanRejectReason::None)
	{
		return Finish(Result);
	}
	const FGuLiWingmanWeaponChannelConfig* Channel =
		Context.Relay->GetAbilityConfig().FindWeaponChannel(Request.Binding);
	if (!Channel)
	{
		Result.RejectReason = EGuLiWingmanRejectReason::WeaponDefinitionMismatch;
		return Finish(Result);
	}
	if (Context.ShipASC->IsWeaponCooldownActive(Channel->CooldownGroupId))
	{
		Result.RejectReason = EGuLiWingmanRejectReason::CooldownActive;
		return Finish(Result);
	}

	FVector AimForward;
	FGuLiCombatTargetSnapshot ShipSource;
	if (!GuLiWingmanMissileAim::Decode(Request.AimDirectionMilli, AimForward)
		|| !ResolveTarget(Context.ShipSource, ShipSource) || !ShipSource.bAlive)
	{
		Result.RejectReason = EGuLiWingmanRejectReason::InvalidTarget;
		return Finish(Result);
	}
	FGuLiCombatTargetSnapshot Target;
	Result.RejectReason = SelectMissileTarget(
		ShipSource.Location, AimForward, Channel->Runtime, Target);
	if (Result.RejectReason != EGuLiWingmanRejectReason::None)
	{
		return Finish(Result);
	}
	Result.SelectedTarget = Target.Handle;
	const TArray<FGuLiWingmanAcceptedBatch>& History = Context.Relay->GetAcceptedHistory();
	const FGuLiWingmanAcceptedBatch* LatestBatch = History.IsEmpty() ? nullptr : &History.Last();
	if (!LatestBatch || !IsFreshAcceptedBatch(*LatestBatch, NowSeconds))
	{
		Result.RejectReason = EGuLiWingmanRejectReason::StaleSourceState;
		return Finish(Result);
	}

	TArray<FGuLiLogicalMissileLaunchRequest> Launches;
	bool bFoundLivingEmitter = false;
	bool bFoundInRangeEmitter = false;
	bool bFoundEmitterWithLos = false;
	for (uint8 FlightIndex = 0u; FlightIndex < GULI_WINGMAN_FLIGHT_COUNT; ++FlightIndex)
	{
		Launches.Reset();
		for (uint8 MemberIndex = 0u; MemberIndex < GULI_WINGMAN_MEMBERS_PER_FLIGHT; ++MemberIndex)
		{
			const FGuLiWingmanRosterEntry* RosterEntry = Context.Relay->GetRoster().FindByPredicate(
				[FlightIndex, MemberIndex](const FGuLiWingmanRosterEntry& Entry)
				{
					return Entry.Wingman.Flight.FlightIndex == FlightIndex
						&& Entry.Wingman.MemberIndex == MemberIndex;
				});
			if (!RosterEntry || !IsRosterMemberAlive(RosterEntry->Wingman))
			{
				continue;
			}
			const FGuLiWingmanCandidateSample* Sample = LatestBatch->FindSample(RosterEntry->Wingman);
			if (!Sample)
			{
				continue;
			}
			bFoundLivingEmitter = true;
			const FVector LaunchPosition = SamplePosition(*Sample);
			if (!IsInsideRange(LaunchPosition, Target, Channel->Runtime.RangeCentimeters))
			{
				continue;
			}
			bFoundInRangeEmitter = true;
			if (Channel->Runtime.bRequiresLineOfSight && !HasLineOfSight(LaunchPosition, Target))
			{
				continue;
			}
			bFoundEmitterWithLos = true;
			FGuLiLogicalMissileLaunchRequest& Launch = Launches.AddDefaulted_GetRef();
			Launch.MatchEpoch = Context.MatchEpoch;
			Launch.MissileId = MakeStableMissileId(Request.ActivationId, RosterEntry->Wingman, 0x4d49534cu);
			Launch.ShotId = MakeStableMissileId(Request.ActivationId, RosterEntry->Wingman, 0x53484f54u);
			Launch.RootEventId = Request.ActivationId;
			Launch.WeaponBinding = Request.Binding;
			Launch.SkillId = Request.SkillId;
			Launch.LoadoutRevision = Request.LoadoutRevision;
			Launch.ProfileRevision = Request.ProfileRevision;
			Launch.Source = Context.ShipSource;
			Launch.Emitter = RosterEntry->Wingman;
			Launch.Target = Target.Handle;
			Launch.LaunchPosition = LaunchPosition;
			Launch.LaunchDirection = (Target.Location - LaunchPosition).GetSafeNormal();
			Launch.SpeedCentimetersPerSecond = Channel->Runtime.ProjectileSpeedCentimetersPerSecond;
			Launch.TurnRateDegreesPerSecond = Channel->Runtime.MaximumHomingTurnRateDegreesPerSecond;
			Launch.SweepRadiusCentimeters = Channel->Runtime.SweepRadiusCentimeters;
			Launch.MaximumLifetimeSeconds = Channel->Runtime.ProjectileLifetimeSeconds;
			Launch.Damage = Channel->Runtime.Damage;
		}
		if (!Launches.IsEmpty())
		{
			Result.FlightIndex = FlightIndex;
			break;
		}
	}
	if (Launches.IsEmpty())
	{
		Result.RejectReason = !bFoundLivingEmitter
			? EGuLiWingmanRejectReason::EmitterDead
			: (!bFoundInRangeEmitter ? EGuLiWingmanRejectReason::OutOfRange
				: (!bFoundEmitterWithLos ? EGuLiWingmanRejectReason::NoLineOfSight
					: EGuLiWingmanRejectReason::InvalidTarget));
		return Finish(Result);
	}

	// Freeze every fallible gameplay gate before reserving the shared cooldown.
	// LaunchFlightSalvo performs the same preflight and then mutates all records in
	// one no-fail block, so neither side of this transaction can be left partial.
	if (!Context.LogicalMissiles->CanLaunchFlightSalvo(Launches))
	{
		Result.RejectReason = EGuLiWingmanRejectReason::InvalidTarget;
		return Finish(Result);
	}
	if (!Context.ShipASC->ServerTryReserveWeaponCooldown(
		Channel->CooldownGroupId, Channel->Runtime.CooldownSeconds, Request.ActivationId))
	{
		Result.RejectReason = EGuLiWingmanRejectReason::CooldownActive;
		return Finish(Result);
	}

	int32 LaunchedCount = 0;
	if (!Context.LogicalMissiles->LaunchFlightSalvo(Launches, LaunchedCount)
		|| LaunchedCount != Launches.Num())
	{
		Context.ShipASC->ServerRollbackWeaponCooldown(
			Channel->CooldownGroupId, Request.ActivationId);
		Result.RejectReason = EGuLiWingmanRejectReason::InvalidTarget;
		return Finish(Result);
	}
	Result.LaunchedCount = FMath::Min(LaunchedCount, static_cast<int32>(GULI_WINGMAN_MEMBERS_PER_FLIGHT));
	Result.bSharedCooldownStarted = true;
	Result.RejectReason = EGuLiWingmanRejectReason::None;
	return Finish(Result);
}

int32 FGuLiWingmanCombatCoordinator::SynchronizeRosterState()
{
	int32 RemovedCount = 0;
	for (auto Iterator = NextFireTimeByEmitterAndSlot.CreateIterator(); Iterator; ++Iterator)
	{
		if (!IsRosterMemberAlive(Iterator.Key()))
		{
			Iterator.RemoveCurrent();
			++RemovedCount;
		}
	}
	for (auto Iterator = AutomaticTargetVersions.CreateIterator(); Iterator; ++Iterator)
	{
		if (!IsRosterMemberAlive(Iterator.Key()))
		{
			Iterator.RemoveCurrent();
			++RemovedCount;
		}
	}
	if (Context.Relay)
	{
		FGuLiWingmanAttackAuthorityState& AttackState = Context.Relay->AttackState;
		const int32 RemovedAssignments = AttackState.AutomaticTargets.RemoveAll(
			[this](const FGuLiWingmanAutoTargetAssignment& Assignment)
			{
				return !IsRosterMemberAlive(Assignment.Emitter);
			});
		const int32 RemovedCheckpoints = AttackState.Checkpoints.RemoveAll(
			[this](const FGuLiWingmanAttackCheckpoint& Checkpoint)
			{
				return !IsRosterMemberAlive(Checkpoint.Emitter);
			});
		if (RemovedAssignments > 0 || RemovedCheckpoints > 0)
		{
			++RemovedCount;
			++AttackState.Revision;
			if (AttackState.Revision == 0u) ++AttackState.Revision;
			RecordAttackAuthorizationSnapshot();
		}
	}
	const double NowSeconds = GetServerTimeSeconds();
	if (IsFiniteTime(NowSeconds))
	{
		for (const FGuLiWingmanRosterEntry& Entry : Context.Relay->GetRoster())
		{
			if (Entry.bDead || NextFireTimeByEmitterAndSlot.Contains(Entry.Wingman))
			{
				continue;
			}
			TMap<FName, double>& EmitterCooldowns =
				NextFireTimeByEmitterAndSlot.Add(Entry.Wingman);
			for (const FGuLiWingmanWeaponChannelConfig& Channel : LastWeaponConfig.WeaponChannels)
			{
				if (Channel.bEnabled && Channel.Kind == EGuLiWingmanWeaponKind::BasicAutomatic)
				{
					EmitterCooldowns.Add(
						Channel.Binding.SlotId, NowSeconds + Channel.Runtime.CooldownSeconds);
				}
			}
		}
	}
	return RemovedCount;
}

bool FGuLiWingmanCombatCoordinator::ResolveTarget(
	const FGuLiTargetHandle& Handle, FGuLiCombatTargetSnapshot& OutSnapshot) const
{
	OutSnapshot = FGuLiCombatTargetSnapshot{};
	const bool bResolved = Context.TargetResolver
		? Context.TargetResolver(Handle, OutSnapshot)
		: (Context.DamageLedger.IsValid() && Context.DamageLedger->TryGetTargetSnapshot(Handle, OutSnapshot));
	return bResolved && OutSnapshot.Handle == Handle && !OutSnapshot.Location.ContainsNaN();
}

void FGuLiWingmanCombatCoordinator::GetTargetCatalog(
	TArray<FGuLiCombatTargetSnapshot>& OutSnapshots) const
{
	OutSnapshots.Reset();
	if (Context.TargetCatalogResolver)
	{
		Context.TargetCatalogResolver(OutSnapshots);
	}
	else if (Context.DamageLedger.IsValid())
	{
		Context.DamageLedger->GetTargetSnapshots(OutSnapshots);
	}
}

EGuLiWingmanRejectReason FGuLiWingmanCombatCoordinator::SelectMissileTarget(
	const FVector& AuthorityOrigin,
	const FVector& AimForward,
	const FGuLiWingmanWeaponRuntimeConfig& Runtime,
	FGuLiCombatTargetSnapshot& OutTarget) const
{
	OutTarget = FGuLiCombatTargetSnapshot{};
	const FVector SafeForward = AimForward.GetSafeNormal();
	if (AuthorityOrigin.ContainsNaN() || SafeForward.IsNearlyZero())
	{
		return EGuLiWingmanRejectReason::InvalidTarget;
	}

	TArray<FGuLiCombatTargetSnapshot> Targets;
	GetTargetCatalog(Targets);
	const double MinimumDot = FMath::Cos(FMath::DegreesToRadians(
		FMath::Clamp(static_cast<double>(Runtime.TargetConeHalfAngleDegrees), 0.0, 180.0)));
	double BestDistanceSquared = TNumericLimits<double>::Max();
	bool bFriendlyInReticle = false;
	bool bEnemyInReticleOutOfRange = false;
	bool bEnemyInReticleBlocked = false;
	for (const FGuLiCombatTargetSnapshot& Candidate : Targets)
	{
		if (!Candidate.Handle.IsValid() || Candidate.Handle == Context.ShipSource
			|| !Candidate.bAlive || Candidate.Location.ContainsNaN()
			|| Candidate.Team == EGuLiTeam::Unassigned)
		{
			continue;
		}
		const FVector ToTarget = Candidate.Location - AuthorityOrigin;
		const double DistanceSquared = ToTarget.SizeSquared();
		if (!FMath::IsFinite(DistanceSquared) || DistanceSquared <= UE_DOUBLE_SMALL_NUMBER)
		{
			continue;
		}
		const bool bInsideCone = FVector::DotProduct(
			SafeForward, ToTarget / FMath::Sqrt(DistanceSquared)) >= MinimumDot;
		if (!bInsideCone)
		{
			continue;
		}
		const bool bInsideRange = IsInsideRange(AuthorityOrigin, Candidate, Runtime.RangeCentimeters);
		if (!IsEnemyTarget(Candidate))
		{
			bFriendlyInReticle |= bInsideRange;
			continue;
		}
		if (!bInsideRange)
		{
			bEnemyInReticleOutOfRange = true;
			continue;
		}
		if (Runtime.bRequiresLineOfSight && !HasLineOfSight(AuthorityOrigin, Candidate))
		{
			bEnemyInReticleBlocked = true;
			continue;
		}

		const bool bDistanceTie = FMath::IsNearlyEqual(
			DistanceSquared, BestDistanceSquared, 1.0);
		if (!OutTarget.Handle.IsValid()
			|| (DistanceSquared < BestDistanceSquared && !bDistanceTie)
			|| (bDistanceTie && IsStableWingmanTargetLess(Candidate.Handle, OutTarget.Handle)))
		{
			OutTarget = Candidate;
			BestDistanceSquared = DistanceSquared;
		}
	}

	if (OutTarget.Handle.IsValid())
	{
		return EGuLiWingmanRejectReason::None;
	}
	if (bEnemyInReticleBlocked)
	{
		return EGuLiWingmanRejectReason::NoLineOfSight;
	}
	if (bEnemyInReticleOutOfRange)
	{
		return EGuLiWingmanRejectReason::OutOfRange;
	}
	return bFriendlyInReticle
		? EGuLiWingmanRejectReason::FriendlyTarget
		: EGuLiWingmanRejectReason::InvalidTarget;
}

bool FGuLiWingmanCombatCoordinator::IsShipSourceAlive() const
{
	FGuLiCombatTargetSnapshot Source;
	return Context.ShipSource.IsValid() && ResolveTarget(Context.ShipSource, Source)
		&& Source.bAlive && Source.Team == Context.ShipTeam;
}

bool FGuLiWingmanCombatCoordinator::HasLineOfSight(
	const FVector& SourceLocation, const FGuLiCombatTargetSnapshot& Target) const
{
	if (Context.LineOfSightResolver)
	{
		return Context.LineOfSightResolver(SourceLocation, Target);
	}
	const UGuLiShipAbilitySystemComponent* ASC = Context.ShipASC.Get();
	const AActor* Ship = ASC ? ASC->GetOwnerActor() : nullptr;
	UWorld* World = Ship ? Ship->GetWorld() : nullptr;
	if (!World)
	{
		return false;
	}
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(GuLiWingmanWeaponLos), false, Ship);
	FHitResult Hit;
	if (!World->LineTraceSingleByChannel(Hit, SourceLocation, Target.Location, ECC_Visibility, QueryParams))
	{
		return true;
	}
	return Target.CollisionActor.IsValid() && Hit.GetActor() == Target.CollisionActor.Get();
}

bool FGuLiWingmanCombatCoordinator::IsEnemyTarget(const FGuLiCombatTargetSnapshot& Target) const
{
	return Target.Team != EGuLiTeam::Unassigned && Target.Team != Context.ShipTeam;
}

bool FGuLiWingmanCombatCoordinator::IsFreshAcceptedBatch(
	const FGuLiWingmanAcceptedBatch& Batch, const double NowSeconds) const
{
	const FGuLiGroupAbilityConfigSnapshot& Config = Context.Relay->GetAbilityConfig();
	return Batch.IsWellFormed() && Batch.Group == Context.Relay->GetLeaseState().Group
		&& Batch.StateRef.MatchEpoch == Context.MatchEpoch
		&& Batch.AbilitySetRevision == Config.AbilitySetRevision
		&& Batch.FormationCommandRevision == Config.FormationCommandRevision
		&& Batch.FormationDefinitionChecksum == Config.FormationDefinitionChecksum
		&& IsFiniteTime(NowSeconds) && NowSeconds >= Batch.ServerAcceptedTimeSeconds
		&& NowSeconds - Batch.ServerAcceptedTimeSeconds <= Context.MaximumAcceptedAgeSeconds;
}

bool FGuLiWingmanCombatCoordinator::IsRosterMemberAlive(const FGuLiWingmanHandle& Emitter) const
{
	const FGuLiWingmanRosterEntry* Entry = Context.Relay->GetRoster().FindByPredicate(
		[&Emitter](const FGuLiWingmanRosterEntry& Candidate)
		{
			return Candidate.Wingman == Emitter;
		});
	const FGuLiWingmanHealthEntry* Health = Context.Relay->GetHealth().FindByPredicate(
		[&Emitter](const FGuLiWingmanHealthEntry& Candidate)
		{
			return Candidate.Wingman == Emitter;
		});
	return Entry && Health && !Entry->bDead && Health->CurrentHealthPermille > 0u;
}

EGuLiWingmanRejectReason FGuLiWingmanCombatCoordinator::ValidateCurrentBasicDefinition(
	const FGuLiWingmanFireIntent& Intent) const
{
	const FGuLiGroupAbilityConfigSnapshot& Config = Context.Relay->GetAbilityConfig();
	if (!Config.IsUsableByLeaseOwner())
	{
		return EGuLiWingmanRejectReason::MissingAbilityConfig;
	}
	if (!SameGroup(Intent.Group, Config))
	{
		return EGuLiWingmanRejectReason::WrongGeneration;
	}
	if (Intent.AbilitySetRevision != Config.AbilitySetRevision)
	{
		return EGuLiWingmanRejectReason::StaleAbilitySetRevision;
	}
	if (Intent.LoadoutRevision != Config.LoadoutRevision)
	{
		return EGuLiWingmanRejectReason::StaleLoadoutRevision;
	}
	const FGuLiWingmanWeaponChannelConfig* Channel = Config.FindWeaponChannel(Intent.Binding);
	if (!Channel || !Channel->bEnabled || Channel->Kind != EGuLiWingmanWeaponKind::BasicAutomatic
		|| Channel->Runtime.Attack.Pattern != EGuLiWingmanAttackPattern::Legacy)
	{
		return EGuLiWingmanRejectReason::UnknownWeaponChannel;
	}
	if (Intent.WeaponAbilityId != Channel->AbilityId)
	{
		return EGuLiWingmanRejectReason::UnknownWeaponAbility;
	}
	if (Intent.SkillId != Channel->SkillId)
	{
		return EGuLiWingmanRejectReason::WeaponSkillMismatch;
	}
	if (Intent.ProfileRevision != Channel->ProfileRevision)
	{
		return EGuLiWingmanRejectReason::StaleProfileRevision;
	}
	if (Intent.WeaponDefinitionRevision != Channel->DefinitionRevision)
	{
		return EGuLiWingmanRejectReason::WeaponDefinitionMismatch;
	}
	UGuLiShipAbilitySystemComponent* ASC = Context.ShipASC.Get();
	const FGuLiShipAbilityGrant* Grant = ASC ? ASC->FindConfiguredGrant(Intent.Binding) : nullptr;
	return ASC && ASC->IsAbilityConfigurationCurrent(Intent.WeaponAbilityId, Intent.AbilitySetRevision)
		&& ASC->IsWeaponConfigurationCurrent(Intent.Binding, Intent.SkillId,
			Intent.LoadoutRevision, Intent.ProfileRevision)
		&& Grant && Grant->WeaponDefinition
		&& Grant->WeaponDefinition->Kind == EGuLiWingmanWeaponKind::BasicAutomatic
		&& Grant->GetDefinitionRevision() == Channel->DefinitionRevision
		&& Grant->GetDefinitionChecksum() == Channel->DefinitionChecksum
		? EGuLiWingmanRejectReason::None : EGuLiWingmanRejectReason::WeaponDefinitionMismatch;
}

EGuLiWingmanRejectReason FGuLiWingmanCombatCoordinator::ValidateCurrentMissileDefinition(
	const FGuLiWingmanMissileSalvoRequest& Request) const
{
	const FGuLiGroupAbilityConfigSnapshot& Config = Context.Relay->GetAbilityConfig();
	if (!Config.IsUsableByLeaseOwner())
	{
		return EGuLiWingmanRejectReason::MissingAbilityConfig;
	}
	if (Request.AbilitySetRevision != Config.AbilitySetRevision)
	{
		return EGuLiWingmanRejectReason::StaleAbilitySetRevision;
	}
	if (Request.LoadoutRevision != Config.LoadoutRevision)
	{
		return EGuLiWingmanRejectReason::StaleLoadoutRevision;
	}
	const FGuLiWingmanWeaponChannelConfig* Channel = Config.FindWeaponChannel(Request.Binding);
	if (!Channel || !Channel->bEnabled || Channel->Kind != EGuLiWingmanWeaponKind::Missile)
	{
		return EGuLiWingmanRejectReason::UnknownWeaponChannel;
	}
	if (Request.MissileAbilityId != Channel->AbilityId)
	{
		return EGuLiWingmanRejectReason::UnknownWeaponAbility;
	}
	if (Request.SkillId != Channel->SkillId)
	{
		return EGuLiWingmanRejectReason::WeaponSkillMismatch;
	}
	if (Request.ProfileRevision != Channel->ProfileRevision)
	{
		return EGuLiWingmanRejectReason::StaleProfileRevision;
	}
	if (Request.MissileDefinitionRevision != Channel->DefinitionRevision)
	{
		return EGuLiWingmanRejectReason::WeaponDefinitionMismatch;
	}
	UGuLiShipAbilitySystemComponent* ASC = Context.ShipASC.Get();
	const FGuLiShipAbilityGrant* Grant = ASC ? ASC->FindConfiguredGrant(Request.Binding) : nullptr;
	return ASC && ASC->IsAbilityConfigurationCurrent(Request.MissileAbilityId, Request.AbilitySetRevision)
		&& ASC->IsWeaponConfigurationCurrent(Request.Binding, Request.SkillId,
			Request.LoadoutRevision, Request.ProfileRevision)
		&& Grant && Grant->WeaponDefinition
		&& Grant->WeaponDefinition->Kind == EGuLiWingmanWeaponKind::Missile
		&& Grant->GetDefinitionRevision() == Channel->DefinitionRevision
		&& Grant->GetDefinitionChecksum() == Channel->DefinitionChecksum
		? EGuLiWingmanRejectReason::None : EGuLiWingmanRejectReason::WeaponDefinitionMismatch;
}

double FGuLiWingmanCombatCoordinator::GetServerTimeSeconds() const
{
	if (Context.ServerTimeProvider)
	{
		return Context.ServerTimeProvider();
	}
	const UGuLiShipAbilitySystemComponent* ASC = Context.ShipASC.Get();
	return ASC && ASC->GetWorld() ? static_cast<double>(ASC->GetWorld()->GetTimeSeconds()) : -1.0;
}

void FGuLiWingmanCombatCoordinator::RememberMissileResult(
	const FGuid& ActivationId, const FGuLiWingmanMissileSalvoResult& Result)
{
	if (!ActivationId.IsValid() || MissileResultsByActivation.Contains(ActivationId))
	{
		return;
	}
	MissileResultsByActivation.Add(ActivationId, Result);
	MissileActivationOrder.Add(ActivationId);
	const int32 Overflow = MissileActivationOrder.Num() - FMath::Max(1, MaximumRememberedMissileActivations);
	if (Overflow > 0)
	{
		for (int32 Index = 0; Index < Overflow; ++Index)
		{
			MissileResultsByActivation.Remove(MissileActivationOrder[Index]);
		}
		MissileActivationOrder.RemoveAt(0, Overflow, EAllowShrinking::No);
	}
}

FGuLiWingmanMissileSalvoResult FGuLiWingmanCombatCoordinator::FinishMissileRequest(
	const FGuid& ActivationId,
	const FGuLiWingmanMissileSalvoResult& Result)
{
	RememberMissileResult(ActivationId, Result);
	return Result;
}

FGuid FGuLiWingmanCombatCoordinator::MakeStableShotId(
	const FGuLiWingmanFireIntent& Intent, const uint32 Salt)
{
	const uint32 GroupHash = GetTypeHash(Intent.Group);
	const uint32 EmitterHash = GetTypeHash(Intent.Emitter);
	const uint32 TargetHash = GetTypeHash(Intent.Target);
	const uint32 BindingHash = GetTypeHash(Intent.Binding);
	const uint32 SkillHash = GetTypeHash(Intent.SkillId);
	return FGuid(
		HashCombine(HashCombine(GroupHash, BindingHash), Salt),
		HashCombine(EmitterHash, Intent.DomainFireSequence),
		HashCombine(HashCombine(TargetHash, SkillHash), Intent.MatchEpoch),
		HashCombine(HashCombine(Intent.LoadoutRevision, Intent.ProfileRevision),
			HashCombine(Intent.WeaponDefinitionRevision,
				HashCombine(Intent.TargetAssignmentRevision, Salt ^ 0x9e3779b9u))));
}

FGuid FGuLiWingmanCombatCoordinator::MakeStableMissileId(
	const FGuid& ActivationId, const FGuLiWingmanHandle& Emitter, const uint32 Salt)
{
	const uint32 ActivationHash = GetTypeHash(ActivationId);
	const uint32 EmitterHash = GetTypeHash(Emitter);
	return FGuid(
		HashCombine(ActivationId.A, Salt),
		HashCombine(ActivationId.B, EmitterHash),
		HashCombine(ActivationId.C, Emitter.EntityGeneration),
		HashCombine(HashCombine(ActivationId.D, ActivationHash), Salt ^ EmitterHash));
}
