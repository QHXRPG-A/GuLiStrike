#include "Battle/Combat/GuLiWingmanCombatCoordinator.h"
#include "Gameplay/Ship/Abilities/GuLiShipAbilitySystemComponent.h"
#include "Gameplay/Ship/Abilities/GuLiShipAbilityDefinitions.h"
#include "Gameplay/CombatEffects/GuLiCombatEffectRuntimeSubsystem.h"
#include "Gameplay/Wingman/Combat/GuLiWingmanAttackNavigation.h"
#include "Battle/Combat/GuLiWingmanTargetAssignment.h"
#include "Engine/World.h"
#include "GuLiFlightNavigationSubsystem.h"

namespace
{
	struct FAutomaticMemberInput
	{
		FGuLiWingmanHandle Emitter;
		FVector Position = FVector::ZeroVector;
		FGuLiTargetHandle PreviousTarget;
		bool bPreviousTargetRetainable = false;
	};

	struct FAutomaticTargetInput
	{
		FGuLiCombatTargetSnapshot Snapshot;
		FVector AttackLocation = FVector::ZeroVector;
		bool bGround = false;
		bool bInsideAcquireRange = false;
	};

	void AdvanceNonZeroRevision(uint32& Revision)
	{
		++Revision;
		if (Revision == 0u)
		{
			++Revision;
		}
	}

	bool IsGroundTargetKind(const EGuLiTargetKind Kind)
	{
		return Kind == EGuLiTargetKind::CommanderSoldier;
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
					: EGuLiWingmanAttackPattern::AirDogfight);
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

bool GuLiWingmanAttackAuthority::IsGunShotEligible(const FVector& SourceLocation, const FVector& SourceForward,
	const FGuLiCombatTargetSnapshot& LiveTarget, float RangeCentimeters, float ConeHalfAngleDegrees,
	bool bHasLineOfSight, double CaptureTimeSeconds, double NextFireTimeSeconds)
{
	return LiveTarget.Handle.IsValid() && LiveTarget.bAlive && bHasLineOfSight
		&& FMath::IsFinite(CaptureTimeSeconds) && FMath::IsFinite(NextFireTimeSeconds)
		&& CaptureTimeSeconds + 0.015 >= NextFireTimeSeconds
		&& GuLiWingmanAttack::IsInsideForwardArc(SourceLocation, SourceForward, LiveTarget.Location,
			LiveTarget.CollisionRadius, RangeCentimeters, ConeHalfAngleDegrees);
}

bool FGuLiWingmanCombatCoordinator::SetSpecifiedAttackTarget(const FGuLiTargetHandle& Target)
{
	FGuLiCombatTargetSnapshot Source, Snapshot;
	if (!IsReady() || !ResolveTarget(Context.ShipSource, Source) || !Source.bAlive
		|| !ResolveTarget(Target, Snapshot) || !Snapshot.bAlive || !IsEnemyTarget(Snapshot)
		|| !GuLiWingmanTargeting::IsWithinReleaseRange(FVector::Distance(Source.Location, Snapshot.Location),
			TargetingTuning)) return false;
	SpecifiedAttackTarget = Target; bAutoTargetingLockedForGuard = false; NextAttackTargetScan = 0;
	TickAttackTargeting(GetServerTimeSeconds(), TargetingTuning);
	return Context.Relay->AttackState.Target.Target == Target && Context.Relay->AttackState.Target.bSpecified;
}

void FGuLiWingmanCombatCoordinator::ClearSpecifiedAttackTarget()
{
	SpecifiedAttackTarget = {}; NextAttackTargetScan = 0;
}

FGuLiWingmanAttackTarget FGuLiWingmanCombatCoordinator::GetAttackTarget() const
{
	return Context.Relay ? Context.Relay->AttackState.Target : FGuLiWingmanAttackTarget{};
}

void FGuLiWingmanCombatCoordinator::TickAttackTargeting(double Now, const FGuLiWingmanTargetingTuning& Tuning)
{
	if (!IsReady() || !Tuning.IsWellFormed() || !FMath::IsFinite(Now) || Now < 0.0) return;
	TargetingTuning = Tuning;
	if (Now < NextAttackTargetScan) return;
	NextAttackTargetScan = Now + FMath::Clamp(Tuning.ScanIntervalSeconds, 0.05f, 2.0f);
	auto& State = Context.Relay->AttackState;
	const TArray<FGuLiWingmanAutoTargetAssignment> PreviousAutomaticTargets = State.AutomaticTargets;
	for (const FGuLiWingmanAutoTargetAssignment& Previous : PreviousAutomaticTargets)
	{
		FAutomaticTargetVersionState& Version = AutomaticTargetVersions.FindOrAdd(Previous.Emitter);
		if (Version.Revision == 0u)
		{
			Version.Target = Previous.Target.Target;
			Version.Revision = Previous.Target.Revision;
		}
	}

	const auto Publish = [&](FGuLiWingmanAttackTarget SharedManualTarget,
		TArray<FGuLiWingmanAutoTargetAssignment> AutomaticTargets)
	{
		if (SharedManualTarget.Target.IsValid())
		{
			SharedManualTarget.bSpecified = true;
			SharedManualTarget.Revision = State.Target.Revision;
			if (SharedManualTarget.Revision == 0u
				|| State.Target.Target != SharedManualTarget.Target
				|| !State.Target.bSpecified)
			{
				AdvanceNonZeroRevision(SharedManualTarget.Revision);
			}
		}
		else
		{
			SharedManualTarget = FGuLiWingmanAttackTarget{};
			SharedManualTarget.Revision = State.Target.Revision;
			SharedManualTarget.ServerTime = Now;
			if (State.Target.Target.IsValid() || State.Target.bSpecified)
			{
				AdvanceNonZeroRevision(SharedManualTarget.Revision);
			}
		}

		AutomaticTargets.Sort([](const FGuLiWingmanAutoTargetAssignment& Lhs,
			const FGuLiWingmanAutoTargetAssignment& Rhs)
		{
			return GuLiWingmanTargetAssignment::IsStableMemberLess(Lhs.Emitter, Rhs.Emitter);
		});
		for (TPair<FGuLiWingmanHandle, FAutomaticTargetVersionState>& Pair : AutomaticTargetVersions)
		{
			const FGuLiWingmanAutoTargetAssignment* Desired = AutomaticTargets.FindByPredicate(
				[&Pair](const FGuLiWingmanAutoTargetAssignment& Entry)
				{
					return Entry.Emitter == Pair.Key;
				});
			const FGuLiTargetHandle DesiredTarget = Desired ? Desired->Target.Target : FGuLiTargetHandle{};
			if (Pair.Value.Target != DesiredTarget)
			{
				Pair.Value.Target = DesiredTarget;
				AdvanceNonZeroRevision(Pair.Value.Revision);
			}
		}
		for (FGuLiWingmanAutoTargetAssignment& Assignment : AutomaticTargets)
		{
			FAutomaticTargetVersionState& Version = AutomaticTargetVersions.FindOrAdd(Assignment.Emitter);
			if (Version.Target != Assignment.Target.Target)
			{
				Version.Target = Assignment.Target.Target;
				AdvanceNonZeroRevision(Version.Revision);
			}
			else if (Version.Revision == 0u)
			{
				AdvanceNonZeroRevision(Version.Revision);
			}
			Assignment.Target.Revision = Version.Revision;
			Assignment.Target.bSpecified = false;
		}

		State.Target = SharedManualTarget;
		State.AutomaticTargets = MoveTemp(AutomaticTargets);
		AdvanceNonZeroRevision(State.Revision);
		State.Checkpoints.RemoveAll([this](const FGuLiWingmanAttackCheckpoint& Entry)
		{
			return !IsRosterMemberAlive(Entry.Emitter);
		});
		RecordAttackAuthorizationSnapshot();
	};

	FGuLiCombatTargetSnapshot Source;
	const bool bSourceAlive = ResolveTarget(Context.ShipSource, Source) && Source.bAlive;
	if (!bSourceAlive)
	{
		Publish(FGuLiWingmanAttackTarget{}, {});
		return;
	}

	if (SpecifiedAttackTarget.IsValid())
	{
		FGuLiCombatTargetSnapshot ManualSnapshot;
		if (ResolveTarget(SpecifiedAttackTarget, ManualSnapshot) && ManualSnapshot.bAlive
			&& IsEnemyTarget(ManualSnapshot)
			&& GuLiWingmanTargeting::IsWithinReleaseRange(
				FVector::Distance(Source.Location, ManualSnapshot.Location), Tuning))
		{
			FGuLiWingmanAttackTarget ManualTarget;
			ManualTarget.Target = ManualSnapshot.Handle;
			ManualTarget.Location = ManualSnapshot.Location;
			ManualTarget.Radius = FMath::Max(0.0f, ManualSnapshot.CollisionRadius);
			ManualTarget.bGround = IsGroundTargetKind(ManualSnapshot.Handle.Kind);
			ManualTarget.bSpecified = true;
			ManualTarget.ServerTime = Now;
			if (ManualTarget.bGround)
			{
				FHitResult Hit;
				FCollisionQueryParams Params(SCENE_QUERY_STAT(GuLiWingmanGroundTarget), false);
				if (ManualSnapshot.CollisionActor.IsValid())
				{
					Params.AddIgnoredActor(ManualSnapshot.CollisionActor.Get());
				}
				if (Context.ShipASC->GetWorld()->LineTraceSingleByObjectType(
					Hit, ManualSnapshot.Location + FVector(0, 0, 5000),
					ManualSnapshot.Location - FVector(0, 0, 500000),
					FCollisionObjectQueryParams(ECC_WorldStatic), Params))
				{
					ManualTarget.Location = Hit.ImpactPoint;
				}
				else
				{
					SpecifiedAttackTarget = {};
				}
			}
			if (SpecifiedAttackTarget.IsValid())
			{
				Publish(ManualTarget, {});
				return;
			}
		}
		else
		{
			SpecifiedAttackTarget = {};
		}
	}

	if (bAutoTargetingLockedForGuard)
	{
		TArray<FGuLiWingmanGuardPoseObservation> Observations;
		for (const FGuLiWingmanRosterEntry& Entry : Context.Relay->GetRoster())
		{
			auto& Observation = Observations.AddDefaulted_GetRef();
			Observation.bAlive = IsRosterMemberAlive(Entry.Wingman);
			if (!Observation.bAlive) continue;
			FGuLiWingmanCandidateSample Sample;
			double AcceptedTime = 0.0;
			Observation.bHasFreshAcceptedPose = Context.Relay->TryGetLatestAcceptedSample(
				Entry.Wingman, Sample, &AcceptedTime) && AcceptedTime <= Now
				&& Now - AcceptedTime <= Tuning.MaximumPoseAgeSeconds;
			if (Observation.bHasFreshAcceptedPose)
			{
				Observation.Position = FVector(Sample.PositionCentimeters);
			}
		}
		const FGuLiWingmanFormationRuntimeConfig& Formation = Context.Relay->GetAbilityConfig().FormationRuntime;
		bAutoTargetingLockedForGuard = !GuLiWingmanTargeting::IsGuardRejoinComplete(
			Observations, Source.Location, Formation.CatchUpDistanceCentimeters,
			Formation.RecoveryDistanceCentimeters, Tuning.GuardRejoinFraction);
		if (bAutoTargetingLockedForGuard)
		{
			Publish(FGuLiWingmanAttackTarget{}, {});
			return;
		}
	}

	bool bAllPreviousTargetsLostOnlyByRange = !PreviousAutomaticTargets.IsEmpty();
	for (const FGuLiWingmanAutoTargetAssignment& Previous : PreviousAutomaticTargets)
	{
		FGuLiCombatTargetSnapshot PreviousSnapshot;
		if (!IsRosterMemberAlive(Previous.Emitter)
			|| !ResolveTarget(Previous.Target.Target, PreviousSnapshot)
			|| !PreviousSnapshot.bAlive || !IsEnemyTarget(PreviousSnapshot)
			|| GuLiWingmanTargeting::IsWithinReleaseRange(
				FVector::Distance(Source.Location, PreviousSnapshot.Location), Tuning))
		{
			bAllPreviousTargetsLostOnlyByRange = false;
			break;
		}
	}
	if (bAllPreviousTargetsLostOnlyByRange)
	{
		bAutoTargetingLockedForGuard = true;
		Publish(FGuLiWingmanAttackTarget{}, {});
		return;
	}

	const FGuLiGroupAbilityConfigSnapshot& Config = Context.Relay->GetAbilityConfig();
	TArray<FAutomaticMemberInput> Members;
	for (const FGuLiWingmanRosterEntry& Entry : Context.Relay->GetRoster())
	{
		if (!IsRosterMemberAlive(Entry.Wingman)) continue;
		FGuLiWingmanCandidateSample Sample;
		double AcceptedTime = 0.0;
		if (!Context.Relay->TryGetLatestAcceptedSample(Entry.Wingman, Sample, &AcceptedTime)
			|| AcceptedTime > Now || Now - AcceptedTime > Tuning.MaximumPoseAgeSeconds
			|| Sample.FlightMode == uint8(EGuLiWingmanFlightMode::Recover)
			|| Sample.FlightMode == uint8(EGuLiWingmanFlightMode::Stale))
		{
			continue;
		}
		FAutomaticMemberInput& Member = Members.AddDefaulted_GetRef();
		Member.Emitter = Entry.Wingman;
		Member.Position = FVector(Sample.PositionCentimeters);
		if (const FGuLiWingmanAutoTargetAssignment* Previous = PreviousAutomaticTargets.FindByPredicate(
			[&Entry](const FGuLiWingmanAutoTargetAssignment& Candidate)
			{
				return Candidate.Emitter == Entry.Wingman;
			}))
		{
			Member.PreviousTarget = Previous->Target.Target;
		}
	}
	if (Members.IsEmpty())
	{
		Publish(FGuLiWingmanAttackTarget{}, {});
		return;
	}

	TArray<FGuLiCombatTargetSnapshot> TargetCatalog;
	GetTargetCatalog(TargetCatalog);
	for (const FGuLiWingmanAutoTargetAssignment& Previous : PreviousAutomaticTargets)
	{
		if (!TargetCatalog.ContainsByPredicate([&Previous](const FGuLiCombatTargetSnapshot& Snapshot)
			{ return Snapshot.Handle == Previous.Target.Target; }))
		{
			FGuLiCombatTargetSnapshot Resolved;
			if (ResolveTarget(Previous.Target.Target, Resolved))
			{
				TargetCatalog.Add(Resolved);
			}
		}
	}

	TArray<FAutomaticTargetInput> Targets;
	TSet<FGuLiTargetHandle> AddedTargets;
	for (const FGuLiCombatTargetSnapshot& Snapshot : TargetCatalog)
	{
		if (!Snapshot.Handle.IsValid() || AddedTargets.Contains(Snapshot.Handle)
			|| !Snapshot.bAlive || !IsEnemyTarget(Snapshot) || Snapshot.Location.ContainsNaN()
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
		const double ShipDistance = FVector::Distance(Source.Location, Snapshot.Location);
		const bool bInsideAcquire = GuLiWingmanTargeting::IsWithinAcquireRange(ShipDistance, Tuning);
		const bool bRetainedInsideRelease = PreviousAutomaticTargets.ContainsByPredicate(
			[&Snapshot, ShipDistance, &Tuning](const FGuLiWingmanAutoTargetAssignment& Previous)
			{
				return Previous.Target.Target == Snapshot.Handle
					&& GuLiWingmanTargeting::IsWithinReleaseRange(ShipDistance, Tuning);
			});
		if (!bInsideAcquire && !bRetainedInsideRelease) continue;

		FAutomaticTargetInput Target;
		Target.Snapshot = Snapshot;
		Target.AttackLocation = Snapshot.Location;
		Target.bGround = bGround;
		Target.bInsideAcquireRange = bInsideAcquire;
		if (bGround)
		{
			FHitResult Hit;
			FCollisionQueryParams Params(SCENE_QUERY_STAT(GuLiWingmanGroundTarget), false);
			if (Snapshot.CollisionActor.IsValid()) Params.AddIgnoredActor(Snapshot.CollisionActor.Get());
			if (!Context.ShipASC->GetWorld()->LineTraceSingleByObjectType(
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

	for (FAutomaticMemberInput& Member : Members)
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
	for (const FAutomaticMemberInput& Member : Members)
	{
		Problem.Members.Add(Member.Emitter);
	}
	for (const FAutomaticTargetInput& Target : Targets)
	{
		Problem.Targets.Add(Target.Snapshot.Handle);
	}
	for (const FAutomaticMemberInput& Member : Members)
	{
		for (const FAutomaticTargetInput& Target : Targets)
		{
			if (!Target.bInsideAcquireRange && Member.PreviousTarget != Target.Snapshot.Handle)
			{
				continue;
			}
			const double Distance = FVector::Distance(Member.Position, Target.AttackLocation);
			if (!FMath::IsFinite(Distance) || Distance < 0.0
				|| Distance > static_cast<double>(TNumericLimits<int64>::Max() / 8))
			{
				Publish(FGuLiWingmanAttackTarget{}, {});
				return;
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
	UWorld* World = Context.ShipASC->GetWorld();
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
			UE_LOG(LogTemp, Warning, TEXT("Wingman target assignment failed closed for group %s"),
				*Context.Relay->GetLeaseState().Group.ShipInstanceId.ToString());
			Publish(FGuLiWingmanAttackTarget{}, {});
			return;
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
			FGroundCorridorCacheEntry& Cached = GroundCorridorCache.FindOrAdd(Solved.Target);
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
	Publish(FGuLiWingmanAttackTarget{}, MoveTemp(AutomaticTargets));
}

void FGuLiWingmanCombatCoordinator::RecordAttackAuthorizationSnapshot()
{
	if (!Context.Relay) return;
	FAttackAuthorizationSnapshot Snapshot;
	const FGuLiWingmanAttackAuthorityState& State = Context.Relay->AttackState;
	if (State.Target.IsValid() && State.Target.bSpecified)
	{
		for (const FGuLiWingmanRosterEntry& Entry : Context.Relay->GetRoster())
		{
			if (!IsRosterMemberAlive(Entry.Wingman)) continue;
			FAttackAuthorization& Authorization = Snapshot.Entries.AddDefaulted_GetRef();
			Authorization.Emitter = Entry.Wingman;
			Authorization.Target = State.Target;
		}
	}
	else
	{
		for (const FGuLiWingmanAutoTargetAssignment& Assignment : State.AutomaticTargets)
		{
			FAttackAuthorization& Authorization = Snapshot.Entries.AddDefaulted_GetRef();
			Authorization.Emitter = Assignment.Emitter;
			Authorization.Target = Assignment.Target;
		}
	}
	Snapshot.Entries.Sort([](const FAttackAuthorization& Lhs, const FAttackAuthorization& Rhs)
	{
		return GuLiWingmanTargetAssignment::IsStableMemberLess(Lhs.Emitter, Rhs.Emitter);
	});
	AttackTargetHistory.Add(MoveTemp(Snapshot));
	// Ground preparation may legally take up to sixty seconds. Retain every
	// server-published point for longer than that window so the first shot can
	// prove the exact frozen location even while a Soldier keeps moving.
	constexpr int32 MaximumAttackAuthorizationSnapshots = 384;
	if (AttackTargetHistory.Num() > MaximumAttackAuthorizationSnapshots)
	{
		AttackTargetHistory.RemoveAt(
			0,
			AttackTargetHistory.Num() - MaximumAttackAuthorizationSnapshots,
			EAllowShrinking::No);
	}
}

void FGuLiWingmanCombatCoordinator::InvalidateMemberAfterEmergencyRebase(
	const FGuLiWingmanHandle& Emitter)
{
	if (!Emitter.IsValid())
	{
		return;
	}
	AutomaticTargetVersions.Remove(Emitter);
	for (int32 SnapshotIndex = AttackTargetHistory.Num() - 1; SnapshotIndex >= 0; --SnapshotIndex)
	{
		AttackTargetHistory[SnapshotIndex].Entries.RemoveAll(
			[&Emitter](const FAttackAuthorization& Authorization)
			{
				return Authorization.Emitter == Emitter;
			});
		if (AttackTargetHistory[SnapshotIndex].Entries.IsEmpty())
		{
			AttackTargetHistory.RemoveAt(SnapshotIndex, 1, EAllowShrinking::No);
		}
	}
	if (Context.Relay)
	{
		FGuLiWingmanAttackAuthorityState& State = Context.Relay->AttackState;
		const int32 RemovedAssignments = State.AutomaticTargets.RemoveAll(
			[&Emitter](const FGuLiWingmanAutoTargetAssignment& Assignment)
			{
				return Assignment.Emitter == Emitter;
			});
		const int32 RemovedCheckpoints = State.Checkpoints.RemoveAll(
			[&Emitter](const FGuLiWingmanAttackCheckpoint& Checkpoint)
			{
				return Checkpoint.Emitter == Emitter;
			});
		if (RemovedAssignments > 0 || RemovedCheckpoints > 0)
		{
			++State.Revision;
			if (State.Revision == 0u)
			{
				++State.Revision;
			}
		}
		RecordAttackAuthorizationSnapshot();
	}
}

bool FGuLiWingmanCombatCoordinator::WasTargetAuthorized(
	const FGuLiWingmanHandle& Emitter,
	const FGuLiTargetHandle& Target,
	const uint32 AssignmentRevision) const
{
	if (!Emitter.IsValid() || !Target.IsValid() || AssignmentRevision == 0u) return false;
	for (int32 SnapshotIndex = AttackTargetHistory.Num() - 1; SnapshotIndex >= 0; --SnapshotIndex)
	{
		if (AttackTargetHistory[SnapshotIndex].Entries.ContainsByPredicate(
			[&](const FAttackAuthorization& Authorization)
			{
				return Authorization.Emitter == Emitter
					&& Authorization.Target.Target == Target
					&& Authorization.Target.Revision == AssignmentRevision;
			}))
		{
			return true;
		}
	}
	return false;
}

bool FGuLiWingmanCombatCoordinator::WasTargetAuthorized(
	const FGuLiWingmanHandle& Emitter,
	const FGuLiWingmanAttackTarget& Target) const
{
	if (!Emitter.IsValid() || !Target.IsValid()) return false;
	for (int32 SnapshotIndex = AttackTargetHistory.Num() - 1; SnapshotIndex >= 0; --SnapshotIndex)
	{
		if (AttackTargetHistory[SnapshotIndex].Entries.ContainsByPredicate(
			[&](const FAttackAuthorization& Authorization)
			{
				return Authorization.Emitter == Emitter
					&& Authorization.Target.Target == Target.Target
					&& Authorization.Target.Revision == Target.Revision
					&& Authorization.Target.bGround == Target.bGround
					&& Authorization.Target.bSpecified == Target.bSpecified
					&& Authorization.Target.ServerTime == Target.ServerTime
					&& Authorization.Target.Location.Equals(Target.Location, 1.0);
			}))
		{
			return true;
		}
	}
	return false;
}

int32 FGuLiWingmanCombatCoordinator::CommitValidatedAttackBatch(const FGuLiWingmanCandidateBatch& Candidate, double Now)
{
	if (!IsReady() || !IsShipSourceAlive() || !Candidate.IsWellFormed()
		|| Candidate.LeaseEpoch != Context.Relay->GetLeaseState().LeaseEpoch
		|| Context.Relay->GetLeaseState().Lifecycle != EGuLiWingmanGroupLifecycle::Active) return 0;
	const auto& Config = Context.Relay->GetAbilityConfig();
	if (!Config.IsUsableByLeaseOwner() || Candidate.AbilitySetRevision != Config.AbilitySetRevision) return 0;
	UGuLiCombatEffectRuntimeSubsystem* Effects = Context.ShipASC->GetWorld()->GetSubsystem<UGuLiCombatEffectRuntimeSubsystem>();
	if (!Effects) return 0;
	int32 Count = 0;
	for (const auto& Shot : Candidate.AttackFireRecords)
	{
		const auto* Channel = Config.WeaponChannels.FindByPredicate([&](const auto& C) { return C.Binding.SlotId == Shot.SlotId; });
		if (!Channel || !Channel->bEnabled || Shot.ProfileRevision != Channel->ProfileRevision || Shot.LoadoutRevision != Config.LoadoutRevision
			|| !Context.ShipASC->IsWeaponConfigurationCurrent(Channel->Binding, Channel->SkillId, Shot.LoadoutRevision, Shot.ProfileRevision)) { continue; }
		const auto& Runtime = Channel->Runtime; const auto& Profile = Runtime.Attack;
		if (Profile.Pattern == EGuLiWingmanAttackPattern::Legacy) continue;
		const bool bGround = Profile.Pattern == EGuLiWingmanAttackPattern::GroundDive;
		if (bGround != Shot.Target.bGround) continue;
		const TArray<FGuLiWingmanCandidateSample>* Samples = &Candidate.Samples;
		double CaptureTime = Candidate.CaptureEstimatedServerTimeSeconds;
		if (Shot.ClientSimTick != Candidate.ClientSimTick)
		{
			const auto* Trail = Candidate.TrailSamples.FindByPredicate([&](const auto& T) { return T.ClientSimTick == Shot.ClientSimTick; });
			if (!Trail) continue; Samples = &Trail->Samples; CaptureTime = Trail->CaptureEstimatedServerTimeSeconds;
		}
		const auto* Sample = Samples->FindByPredicate([&](const auto& S) { return S.Wingman.MemberIndex == Shot.MemberIndex; });
		if (!Sample || !IsRosterMemberAlive(Sample->Wingman) || Now - CaptureTime > 0.5 || CaptureTime > Now + 0.05
			|| Sample->FlightMode == uint8(EGuLiWingmanFlightMode::Recover) || Sample->FlightMode == uint8(EGuLiWingmanFlightMode::Stale)) { continue; }
		const FVector Position(Sample->PositionCentimeters);
		const FRotator Rotation(Sample->RotationCentiDegrees.X / 100.0, Sample->RotationCentiDegrees.Y / 100.0, Sample->RotationCentiDegrees.Z / 100.0);
		const FVector Forward = Rotation.Vector();
		auto& Checkpoints = Context.Relay->AttackState.Checkpoints;
		auto* Previous = Checkpoints.FindByPredicate([&](const auto& C) { return C.Emitter == Sample->Wingman && C.SlotId == Shot.SlotId; });
		FGuLiWingmanAttackCheckpoint Checkpoint = Previous ? *Previous : FGuLiWingmanAttackCheckpoint{};
		const bool bSameRun = bGround && Previous && Checkpoint.RunId == Shot.RunId
			&& Checkpoint.ProfileRevision == Shot.ProfileRevision && Checkpoint.LeaseEpoch == Candidate.LeaseEpoch
			&& Checkpoint.SkillId == Channel->SkillId && Checkpoint.DefinitionChecksum == Channel->DefinitionChecksum;
		if (bSameRun && Shot.Target.Target != Checkpoint.FrozenTargetHandle) { continue; }
		FGuLiWingmanGroundRunPath Path;
		FVector Impact = Shot.Target.Location;
		if (!bSameRun)
		{
			if ((bGround && CaptureTime + 0.015 < Checkpoint.NextFireTime) || (bGround && Shot.ShotIndex != 0))
			{ continue; }
			const bool bAuthorizedTarget = WasTargetAuthorized(Sample->Wingman, Shot.Target);
			FGuLiCombatTargetSnapshot LiveTarget;
			if (!bAuthorizedTarget || !ResolveTarget(Shot.Target.Target, LiveTarget) || !IsEnemyTarget(LiveTarget)) { continue; }
			Checkpoint.Emitter = Sample->Wingman; Checkpoint.SlotId = Shot.SlotId; Checkpoint.ProfileRevision = Shot.ProfileRevision;
			Checkpoint.SkillId = Channel->SkillId; Checkpoint.DefinitionChecksum = Channel->DefinitionChecksum;
			Checkpoint.FrozenTargetHandle = Shot.Target.Target;
			Checkpoint.RunId = Shot.RunId; Checkpoint.LeaseEpoch = Candidate.LeaseEpoch; Checkpoint.StartTime = CaptureTime;
			Checkpoint.LastShotIndex = -1; Checkpoint.FrozenTarget = Shot.Target.Location;
			Checkpoint.ApproachDirection = Shot.ApproachDirection;
			if (!bGround)
			{
				Impact = LiveTarget.Location;
				if (!GuLiWingmanAttackAuthority::IsGunShotEligible(Position, Forward, LiveTarget,
					Runtime.RangeCentimeters, Runtime.TargetConeHalfAngleDegrees, HasLineOfSight(Position, LiveTarget),
					CaptureTime, Checkpoint.NextFireTime)) continue;
			}
		}
		if (bGround)
		{
			if (Shot.ShotIndex >= Profile.MissileCount || Shot.ShotIndex <= Checkpoint.LastShotIndex
				|| !GuLiWingmanAttack::BuildGroundPath(Checkpoint.FrozenTarget, Checkpoint.ApproachDirection,
					Profile, Config.FormationRuntime.MaximumTurnRateDegreesPerSecond, Path)) { continue; }
			if (!bSameRun && !GuLiWingmanAttack::GroundRunClearsTerrain(Context.ShipASC->GetWorld(), Path,
				Config.FormationRuntime.AgentRadiusCentimeters)) { continue; }
			const float Age = float(CaptureTime - Checkpoint.StartTime);
			if (FMath::Abs(Age - GuLiWingmanAttack::ShotTime(Shot.ShotIndex, Profile.MissileCount, Profile.DiveSeconds)) > 0.075f) { continue; }
			if (FVector::Dist(Position, Path.PositionAt(Age)) > 1200.0) { continue; }
			if (FVector::DotProduct(Forward, Path.DirectionAt(Age)) < FMath::Cos(FMath::DegreesToRadians(12.0f))) { continue; }
			Impact = GuLiWingmanAttack::StripPoint(Path, Profile.StripLength, Shot.ShotIndex, Profile.MissileCount);
			if (FVector::Dist(Position, Impact) > Runtime.RangeCentimeters) continue;
		}
		FGuLiCombatAttackRequest Request;
		Request.ExecutorId = Profile.ExecutorId; Request.SourceTransform = FTransform(Rotation, Position);
		Request.TargetLocation = Impact; Request.MuzzleOffset = Profile.Muzzle; Request.ShotOrdinal = Shot.ShotIndex;
		Request.Context.Source = Context.ShipSource; Request.Context.Emitter = Sample->Wingman;
		Request.Context.Target = Shot.Target.Target; Request.Context.WeaponBinding = Channel->Binding;
		Request.Context.SkillId = Channel->SkillId; Request.Context.ProfileRevision = Channel->ProfileRevision;
		Request.Context.LoadoutRevision = Config.LoadoutRevision; Request.Context.Damage = Runtime.Damage;
		Request.Context.MatchEpoch = Context.MatchEpoch;
		FGuLiWingmanFireIntent IdInput; IdInput.Group = Candidate.Group; IdInput.Emitter = Sample->Wingman;
		IdInput.MatchEpoch = Candidate.MatchEpoch; IdInput.LeaseEpoch = Candidate.LeaseEpoch; IdInput.Binding = Channel->Binding;
		IdInput.Target = Shot.Target.Target; IdInput.SkillId = Channel->SkillId; IdInput.LoadoutRevision = Config.LoadoutRevision;
		IdInput.TargetAssignmentRevision = Shot.Target.Revision;
		IdInput.ProfileRevision = Channel->ProfileRevision; IdInput.WeaponDefinitionRevision = Channel->DefinitionRevision;
		IdInput.DomainFireSequence = Shot.RunId; IdInput.ClientFireTick = Shot.ClientSimTick;
		Request.Context.ShotId = MakeStableShotId(IdInput, 0x4154544bu + Shot.ShotIndex);
		Request.Context.RootEventId = Request.Context.ShotId;
		if (bGround)
		{
			const auto* Grant = Context.ShipASC->FindConfiguredGrant(Channel->Binding);
			if (!Grant || !Grant->WeaponDefinition) continue;
			Request.Projectile = Grant->WeaponDefinition->AttackProjectile.LoadSynchronous();
			Request.FrozenField.ConfigId = TEXT("WingmanGroundMissile"); Request.FrozenField.Damage = Runtime.Damage;
			Request.FrozenField.Radius = Profile.ExplosionRadius;
			Request.Motion.Speed = Runtime.ProjectileSpeedCentimetersPerSecond; Request.Motion.MaximumLifetime = Runtime.ProjectileLifetimeSeconds;
			Request.Motion.SweepRadius = Runtime.SweepRadiusCentimeters;
		}
		if (!Effects->ExecuteWingmanAttack(Request)) { continue; }
		Checkpoint.LastShotIndex = Shot.ShotIndex;
		if (!bSameRun) Checkpoint.NextFireTime = CaptureTime + Runtime.CooldownSeconds;
		if (Previous) *Previous = Checkpoint; else Checkpoints.Add(Checkpoint);
		++Context.Relay->AttackState.Revision; ++Count;
	}
	return Count;
}
