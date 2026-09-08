#include "Battle/Combat/GuLiWingmanCombatCoordinator.h"
#include "Gameplay/Ship/Abilities/GuLiShipAbilitySystemComponent.h"
#include "Gameplay/Ship/Abilities/GuLiShipAbilityDefinitions.h"
#include "Gameplay/CombatEffects/GuLiCombatEffectRuntimeSubsystem.h"
#include "Gameplay/Wingman/Combat/GuLiWingmanAttackNavigation.h"
#include "Engine/World.h"

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
		|| !ResolveTarget(Target, Snapshot) || !IsEnemyTarget(Snapshot)
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
	if (!IsReady() || !Tuning.IsWellFormed()) return;
	TargetingTuning = Tuning;
	if (Now < NextAttackTargetScan) return;
	NextAttackTargetScan = Now + FMath::Clamp(Tuning.ScanIntervalSeconds, 0.05f, 2.0f);
	FGuLiCombatTargetSnapshot Source, Selected;
	const bool bSourceAlive = ResolveTarget(Context.ShipSource, Source) && Source.bAlive;
	bool bSpecified = false;
	if (bSourceAlive && SpecifiedAttackTarget.IsValid()
		&& ResolveTarget(SpecifiedAttackTarget, Selected) && IsEnemyTarget(Selected))
	{
		if (GuLiWingmanTargeting::IsWithinReleaseRange(FVector::Distance(Source.Location, Selected.Location), Tuning))
			bSpecified = true;
		else
		{
			SpecifiedAttackTarget = {}; Selected = {}; bAutoTargetingLockedForGuard = true;
		}
	}
	if (!bSpecified)
	{
		if (SpecifiedAttackTarget.IsValid()) SpecifiedAttackTarget = {};
		Selected = {};
		FGuLiCombatTargetSnapshot Retained;
		const bool bCurrentResolved = bSourceAlive && ResolveTarget(Context.Relay->AttackState.Target.Target, Retained)
			&& IsEnemyTarget(Retained);
		const bool bRetainAuto = bCurrentResolved
			&& GuLiWingmanTargeting::IsWithinReleaseRange(FVector::Distance(Source.Location, Retained.Location), Tuning)
			&& !bAutoTargetingLockedForGuard;
		if (bCurrentResolved && !GuLiWingmanTargeting::IsWithinReleaseRange(
			FVector::Distance(Source.Location, Retained.Location), Tuning)) bAutoTargetingLockedForGuard = true;
		if (bRetainAuto) Selected = Retained;
		if (bSourceAlive && bAutoTargetingLockedForGuard)
		{
			TArray<FGuLiWingmanGuardPoseObservation> Observations;
			for (const FGuLiWingmanRosterEntry& Entry : Context.Relay->GetRoster())
			{
				const FGuLiWingmanHealthEntry* Health = Context.Relay->GetHealth().FindByPredicate(
					[&](const FGuLiWingmanHealthEntry& Item) { return Item.Wingman == Entry.Wingman; });
				auto& Observation = Observations.AddDefaulted_GetRef();
				Observation.bAlive = !Entry.bDead && (!Health || Health->CurrentHealthPermille > 0);
				if (!Observation.bAlive) continue;
				FGuLiWingmanCandidateSample Sample; double AcceptedTime = 0;
				Observation.bHasFreshAcceptedPose = Context.Relay->TryGetLatestAcceptedSample(
					Entry.Wingman, Sample, &AcceptedTime) && AcceptedTime <= Now
					&& Now - AcceptedTime <= Tuning.MaximumPoseAgeSeconds;
				if (Observation.bHasFreshAcceptedPose) Observation.Position = FVector(Sample.PositionCentimeters);
			}
			const auto& Formation = Context.Relay->GetAbilityConfig().FormationRuntime;
			bAutoTargetingLockedForGuard = !GuLiWingmanTargeting::IsGuardRejoinComplete(Observations,
				Source.Location, Formation.CatchUpDistanceCentimeters, Formation.RecoveryDistanceCentimeters,
				Tuning.GuardRejoinFraction);
		}
		if (bSourceAlive && !bRetainAuto && !bAutoTargetingLockedForGuard)
		{
			TArray<FGuLiCombatTargetSnapshot> Targets; GetTargetCatalog(Targets);
			double Best = FMath::Square(static_cast<double>(Tuning.AcquireRadiusCentimeters));
			for (const auto& Target : Targets)
			{
				const double Distance = FVector::DistSquared(Source.Location, Target.Location);
				const auto StableKey = [](const FGuLiTargetHandle& Handle)
				{
					return FString::Printf(TEXT("%03u/%s/%010u/%010u"), uint8(Handle.Kind),
						*Handle.AuthorityId.ToString(), Handle.Generation, Handle.LocalId);
				};
				if (IsEnemyTarget(Target) && Distance <= Best
					&& (Distance < Best || !Selected.Handle.IsValid() || StableKey(Target.Handle) < StableKey(Selected.Handle)))
				{ Best = Distance; Selected = Target; }
			}
		}
	}
	FGuLiWingmanAttackTarget NewTarget;
	NewTarget.Target = Selected.Handle; NewTarget.Location = Selected.Location; NewTarget.Radius = Selected.CollisionRadius;
	NewTarget.bGround = Selected.Handle.Kind == EGuLiTargetKind::CommanderSoldier;
	NewTarget.bSpecified = bSpecified; NewTarget.ServerTime = Now;
	if (NewTarget.bGround && NewTarget.Target.IsValid())
	{
		FHitResult Hit; FCollisionQueryParams Params(SCENE_QUERY_STAT(GuLiWingmanGroundTarget), false);
		if (Selected.CollisionActor.IsValid()) Params.AddIgnoredActor(Selected.CollisionActor.Get());
		if (Context.ShipASC->GetWorld()->LineTraceSingleByObjectType(Hit, Selected.Location + FVector(0,0,5000),
			Selected.Location - FVector(0,0,500000), FCollisionObjectQueryParams(ECC_WorldStatic), Params))
			NewTarget.Location = Hit.ImpactPoint;
		else NewTarget.Target = {};
	}
	auto& State = Context.Relay->AttackState;
	NewTarget.Revision = State.Target.Revision;
	if (NewTarget.Revision == 0 || NewTarget.Target != State.Target.Target || NewTarget.bSpecified != State.Target.bSpecified)
		if (++NewTarget.Revision == 0) ++NewTarget.Revision;
	State.Target = NewTarget; ++State.Revision;
	AttackTargetHistory.Add(NewTarget);
	if (AttackTargetHistory.Num() > 16) AttackTargetHistory.RemoveAt(0);
	State.Checkpoints.RemoveAll([this](const auto& Entry) { return !IsRosterMemberAlive(Entry.Emitter); });
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
			const bool bAuthorizedTarget = AttackTargetHistory.ContainsByPredicate([&](const auto& T) {
				return T.Target == Shot.Target.Target && T.Revision == Shot.Target.Revision
					&& T.bGround == bGround && T.ServerTime == Shot.Target.ServerTime && T.Location.Equals(Shot.Target.Location, 1.0); });
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
