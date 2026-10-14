#include "Gameplay/Wingman/Combat/GuLiWingmanTargeting.h"
#include "Battle/Combat/GuLiCombatDamageLedger.h"
#include "Battle/Combat/GuLiWingmanTargetAssignment.h"
#include "Gameplay/Wingman/Combat/GuLiWingmanAttackNavigation.h"
#include "GuLiFlightNavigationSubsystem.h"
#include "Engine/World.h"

namespace
{
	struct FAutomaticTargetInput
	{
		FGuLiCombatTargetSnapshot Snapshot;
		FVector AttackLocation = FVector::ZeroVector;
		bool bGround = false;
		bool bCanAssignNewMember = true;
	};
	bool IsGroundTargetKind(const EGuLiTargetKind Kind)
	{
		return Kind == EGuLiTargetKind::CommanderSoldier || Kind == EGuLiTargetKind::GroundActor;
	}

	bool IsAirTargetKind(const EGuLiTargetKind Kind)
	{
		return Kind == EGuLiTargetKind::Ship || Kind == EGuLiTargetKind::Wingman;
	}

	bool HasAutomaticAttackCapability(
		const FGuLiGroupAbilityConfigSnapshot& Config,
		const bool bGround)
	{
		return Config.WeaponChannels.ContainsByPredicate([bGround](
			const FGuLiWingmanWeaponChannelConfig& Channel)
		{
			if (!Channel.bEnabled || Channel.Kind != EGuLiWingmanWeaponKind::BasicAutomatic)
			{
				return false;
			}
			return Channel.Runtime.Attack.Pattern == EGuLiWingmanAttackPattern::Legacy
				|| Channel.Runtime.Attack.Pattern == (bGround
					? EGuLiWingmanAttackPattern::GroundDive
					: EGuLiWingmanAttackPattern::AirBurstOrbit);
		});
	}

	bool HasClearGroundBombingCorridor(
		UWorld* World,
		const FVector& GroundTarget,
		const FVector& PreferredApproach,
		const FGuLiWingmanAttackProfile& Profile,
		const FGuLiWingmanFormationRuntimeConfig& Formation)
	{
		if (!World || GroundTarget.ContainsNaN() || PreferredApproach.ContainsNaN())
		{
			return false;
		}
		for (int32 CandidateIndex = 0;
			CandidateIndex < GuLiWingmanAttack::MaximumGroundApproachCandidates;
			++CandidateIndex)
		{
			const FVector Approach = GuLiWingmanAttack::BuildGroundApproachCandidate(
				PreferredApproach, 0u, CandidateIndex);
			FGuLiWingmanGroundRunPath Path;
			if (GuLiWingmanAttack::BuildGroundPath(
					GroundTarget,
					Approach,
					Profile,
					Formation.MaximumTurnRateDegreesPerSecond,
					Path)
				&& GuLiWingmanAttack::GroundRunClearsTerrain(
					World, Path, Formation.AgentRadiusCentimeters))
			{
				return true;
			}
		}
		return false;
	}
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

bool GuLiWingmanTargeting::SelectAutomaticTargets(UWorld* World,
    const FGuLiCombatTargetSnapshot& Source, const FGuLiGroupAbilityConfigSnapshot& Config,
    const FGuLiWingmanTargetingTuning& Tuning, double Now,
    TConstArrayView<FGuLiWingmanTargetMember> InputMembers,
    TConstArrayView<FGuLiCombatTargetSnapshot> TargetCatalog,
    TConstArrayView<FGuLiWingmanAutoTargetAssignment> PreviousAutomaticTargets,
    TMap<FGuLiTargetHandle,FGuLiWingmanGroundCorridorCache>& GroundCorridorCache,
    TArray<FGuLiWingmanAutoTargetAssignment>& OutAssignments)
{
    OutAssignments.Reset();
    if (!Source.bAlive || Source.Team==EGuLiTeam::Unassigned || Source.Location.ContainsNaN()
        || !Tuning.IsWellFormed() || !FMath::IsFinite(Now) || Now<0) return false;
    TArray<FGuLiWingmanTargetMember> Members;
    Members.Append(InputMembers.GetData(),InputMembers.Num());
	TArray<FAutomaticTargetInput> Targets;
	TSet<FGuLiTargetHandle> AddedTargets;
	for (const FGuLiCombatTargetSnapshot& Snapshot : TargetCatalog)
	{
		if (!Snapshot.Handle.IsValid() || AddedTargets.Contains(Snapshot.Handle)
			|| !Snapshot.bAlive || (Snapshot.Team == EGuLiTeam::Unassigned || Snapshot.Team == Source.Team) || Snapshot.Location.ContainsNaN()
			|| !FMath::IsFinite(Snapshot.CollisionRadius))
		{
			continue;
		}
		const bool bGround = IsGroundTargetKind(Snapshot.Handle.Kind);
		if ((!bGround && !IsAirTargetKind(Snapshot.Handle.Kind))
			|| !HasAutomaticAttackCapability(Config, bGround))
		{
			continue;
		}
		FAutomaticTargetInput Target;
		Target.Snapshot = Snapshot;
		Target.AttackLocation = Snapshot.Location;
		Target.bGround = bGround;
		if (bGround)
		{
			const double ShipDistance = FVector::Distance(Source.Location, Snapshot.Location);
			const bool bInsideAcquire =
				GuLiWingmanTargeting::IsWithinAcquireRange(ShipDistance, Tuning);
			const bool bRetainedInsideRelease = PreviousAutomaticTargets.ContainsByPredicate(
				[&Snapshot, ShipDistance, &Tuning](
					const FGuLiWingmanAutoTargetAssignment& Previous)
				{
					return Previous.Target.Target == Snapshot.Handle
						&& GuLiWingmanTargeting::IsWithinReleaseRange(ShipDistance, Tuning);
				});
			if (!bInsideAcquire && !bRetainedInsideRelease) continue;
			Target.bCanAssignNewMember = bInsideAcquire;
			FHitResult Hit;
			FCollisionQueryParams Params(SCENE_QUERY_STAT(GuLiWingmanGroundTarget), false);
			if (Snapshot.CollisionActor.IsValid()) Params.AddIgnoredActor(Snapshot.CollisionActor.Get());
			if (!World || !World->LineTraceSingleByObjectType(
				Hit, Snapshot.Location + FVector(0, 0, 5000),
				Snapshot.Location - FVector(0, 0, 500000),
				FCollisionObjectQueryParams(ECC_WorldStatic), Params))
			{
				continue;
			}
			Target.AttackLocation = Hit.ImpactPoint;
		}
		Targets.Add(MoveTemp(Target));
		AddedTargets.Add(Snapshot.Handle);
	}
	Targets.Sort([](const FAutomaticTargetInput& Lhs, const FAutomaticTargetInput& Rhs)
	{
		return GuLiWingmanTargetAssignment::IsStableTargetLess(Lhs.Snapshot.Handle, Rhs.Snapshot.Handle);
	});

	for (FGuLiWingmanTargetMember& Member : Members)
	{
		const FAutomaticTargetInput* Previous = Targets.FindByPredicate(
			[&Member](const FAutomaticTargetInput& Target)
			{
				return Target.Snapshot.Handle == Member.PreviousTarget;
			});
		Member.bPreviousTargetRetainable = Previous != nullptr;
	}

	FGuLiWingmanTargetAssignmentProblem Problem;
	Problem.SwitchPenaltyCentimeters =
		GuLiWingmanTargetAssignment::DefaultSwitchPenaltyCentimeters;
	for (const FGuLiWingmanTargetMember& Member : Members)
	{
		Problem.Members.Add(Member.Emitter);
	}
	for (const FAutomaticTargetInput& Target : Targets)
	{
		Problem.Targets.Add(Target.Snapshot.Handle);
	}
	for (const FGuLiWingmanTargetMember& Member : Members)
	{
		for (const FAutomaticTargetInput& Target : Targets)
		{
			if (!Target.bCanAssignNewMember && Member.PreviousTarget != Target.Snapshot.Handle)
			{
				continue;
			}
			const double Distance = FVector::Distance(Member.Position, Target.AttackLocation);
			if (!FMath::IsFinite(Distance) || Distance < 0.0
				|| Distance > static_cast<double>(TNumericLimits<int64>::Max() / 8))
			{
				return false;
			}
			FGuLiWingmanTargetAssignmentEdge& Edge = Problem.LegalEdges.AddDefaulted_GetRef();
			Edge.Emitter = Member.Emitter;
			Edge.Target = Target.Snapshot.Handle;
			Edge.DistanceCentimeters = FMath::RoundToInt64(Distance);
			Edge.bSwitchesFromRetainableTarget = Member.bPreviousTargetRetainable
				&& Member.PreviousTarget != Target.Snapshot.Handle;
		}
	}

	const FGuLiWingmanWeaponChannelConfig* GroundChannel =
		Config.WeaponChannels.FindByPredicate([](const FGuLiWingmanWeaponChannelConfig& Channel)
		{
			return Channel.bEnabled
				&& Channel.Runtime.Attack.Pattern == EGuLiWingmanAttackPattern::GroundDive;
		});
	FString NavigationError;
	const UGuLiFlightNavigationSubsystem* Navigation =
		World ? World->GetSubsystem<UGuLiFlightNavigationSubsystem>() : nullptr;
	const bool bCanPreflightGroundCorridors = GroundChannel && Navigation
		&& Navigation->HasUsableNavigationAt(Source.Location, NavigationError);
	TSet<FGuLiTargetHandle> ClearGroundTargets;
	TSet<FGuLiTargetHandle> BlockedGroundTargets;
	TArray<FGuLiWingmanTargetAssignmentPair> SolvedAssignments;
	for (;;)
	{
		SolvedAssignments.Reset();
		if (!GuLiWingmanTargetAssignment::Solve(Problem, SolvedAssignments))
		{
			return false;
		}

		bool bRemovedBlockedTarget = false;
		for (const FGuLiWingmanTargetAssignmentPair& Solved : SolvedAssignments)
		{
			const FAutomaticTargetInput* Target = Targets.FindByPredicate(
				[&Solved](const FAutomaticTargetInput& Candidate)
				{
					return Candidate.Snapshot.Handle == Solved.Target;
				});
			if (!bCanPreflightGroundCorridors || !Target || !Target->bGround
				|| ClearGroundTargets.Contains(Solved.Target)
				|| BlockedGroundTargets.Contains(Solved.Target))
			{
				continue;
			}
			FGuLiWingmanGroundCorridorCache& Cached = GroundCorridorCache.FindOrAdd(Solved.Target);
			const bool bCacheCurrent = Cached.ValidUntilSeconds >= Now
				&& Cached.TargetLocation.Equals(Target->AttackLocation, 100.0);
			const bool bClear = bCacheCurrent
				? Cached.bClear
				: GroundChannel
					&& HasClearGroundBombingCorridor(
						World,
						Target->AttackLocation,
						Target->AttackLocation - Source.Location,
						GroundChannel->Runtime.Attack,
						Config.FormationRuntime);
			if (!bCacheCurrent)
			{
				Cached.TargetLocation = Target->AttackLocation;
				Cached.ValidUntilSeconds = Now + 1.0;
				Cached.bClear = bClear;
			}
			if (bClear)
			{
				ClearGroundTargets.Add(Solved.Target);
			}
			else
			{
				BlockedGroundTargets.Add(Solved.Target);
			}
		}
		if (!BlockedGroundTargets.IsEmpty())
		{
			const int32 Removed = Problem.LegalEdges.RemoveAll(
				[&BlockedGroundTargets](const FGuLiWingmanTargetAssignmentEdge& Edge)
				{
					return BlockedGroundTargets.Contains(Edge.Target);
				});
			bRemovedBlockedTarget = Removed > 0;
		}
		if (!bRemovedBlockedTarget)
		{
			break;
		}
	}
	TArray<FGuLiWingmanAutoTargetAssignment> AutomaticTargets;
	for (const FGuLiWingmanTargetAssignmentPair& Solved : SolvedAssignments)
	{
		const FAutomaticTargetInput* Target = Targets.FindByPredicate(
			[&Solved](const FAutomaticTargetInput& Candidate)
			{
				return Candidate.Snapshot.Handle == Solved.Target;
			});
		if (!Target) continue;
		FGuLiWingmanAutoTargetAssignment& Assignment = AutomaticTargets.AddDefaulted_GetRef();
		Assignment.Emitter = Solved.Emitter;
		Assignment.Target.Target = Target->Snapshot.Handle;
		Assignment.Target.Location = Target->AttackLocation;
		Assignment.Target.Radius = FMath::Max(0.0f, Target->Snapshot.CollisionRadius);
		Assignment.Target.bGround = Target->bGround;
		Assignment.Target.bSpecified = false;
		Assignment.Target.ServerTime = Now;
	}
    OutAssignments=MoveTemp(AutomaticTargets);
    return true;
}
