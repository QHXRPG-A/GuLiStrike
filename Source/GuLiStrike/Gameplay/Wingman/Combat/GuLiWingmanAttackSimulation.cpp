#include "Gameplay/Wingman/GuLiWingmanSimulationSubsystem.h"
#include "Gameplay/Wingman/GuLiWingmanPawn.h"
#include "Gameplay/Wingman/GuLiWingmanRuntimeTypes.h"
#include "Engine/World.h"
#include "Gameplay/Wingman/Combat/GuLiWingmanAttackNavigation.h"
#include "Battle/Combat/GuLiWingmanCombatCoordinator.h"
#include "GuLiFlightNavigationQuery.h"

void UGuLiWingmanSimulationSubsystem::TickAttackRuns(const FGuLiWingmanGroupHandle& Group,
	const FGuLiWingmanAttackAuthorityState& State, uint32 LeaseEpoch, uint32 ClientTick, double Now, bool bActive)
{
	auto* Runtime = OwnedGroups.Find(Group);
	if (!Runtime || !CanOwnSimulation() || !FMath::IsFinite(Now)) return;
	if (bActive && Runtime->LastAttackTick == ClientTick) return;
	Runtime->LastAttackTick = ClientTick;
	const auto& Config = Runtime->AbilityConfig;
	for (const TWeakObjectPtr<AGuLiWingmanPawn>& Entry : Runtime->Pawns)
	{
		AGuLiWingmanPawn* Pawn = Entry.Get();
		if (!Pawn) continue;
		FGuLiWingmanRuntimeState& PawnState = Pawn->GetMutableRuntimeState();
		auto& Attack = PawnState.Attack;
		auto& Weapon = PawnState.Weapon;
		const auto& Identity = PawnState.Identity;
		const auto& SwarmAgent = PawnState.SwarmAgent;
		const auto& Carrier = PawnState.Carrier;
		const auto& Dynamics = PawnState.Dynamics;
		const auto& Avoidance = PawnState.Avoidance;
		const FTransform Transform = Pawn->GetActorTransform();
		if (Attack.EntityGeneration != Identity.Handle.EntityGeneration)
		{
			Attack = FGuLiWingmanAttackRunState{};
			Attack.EntityGeneration = Identity.Handle.EntityGeneration;
			Runtime->PendingAttackShots.RemoveAll([&](const auto& P) { return P.Key == Identity.Handle.Flight.FlightIndex && P.Value.MemberIndex == Identity.Handle.MemberIndex; });
		}
		for (const auto& Checkpoint : State.Checkpoints)
			if (Checkpoint.Emitter == Identity.Handle)
				Weapon.SetNextFireSeconds(Checkpoint.SlotId, FMath::Max(Weapon.GetNextFireSeconds(Checkpoint.SlotId), Checkpoint.NextFireTime));
		const auto Cancel = [&](uint8 Reason=1)
		{
			if(Attack.bGuiding) Attack.LastCancelReason=Reason;
			Attack.Phase = EGuLiWingmanAttackPhase::Idle; Attack.bGuiding = false; Attack.RetryAfter = Now + 1.0;
			Attack.AirTurn = {}; Attack.RetreatPoint = FVector::ZeroVector; Attack.RetreatOrigin = FVector::ZeroVector;
			Attack.bAirTurnUsingDirectGuidance = false; Attack.PreferredVelocity = FVector::ZeroVector;
			Runtime->PendingAttackShots.RemoveAll([&](const auto& P) { return P.Key == Identity.Handle.Flight.FlightIndex && P.Value.MemberIndex == Identity.Handle.MemberIndex; });
		};
		const bool bExecutingGround =
			GuLiWingmanAttack::IsFrozenGroundExecutionPhase(Attack.Phase);
		const bool bPreparingGround =
			GuLiWingmanAttack::IsGroundPreparationPhase(Attack.Phase);
		if (!bActive || !Runtime->bCombatAuthorizationValid
			|| !Dynamics.bAlive || !Config.IsUsableByLeaseOwner()
			// Dive/PullUp/Climb already follow a complete FlightNav + terrain checked
			// path. Abandoning that path after the aircraft has descended is what left
			// later sorties parked near the ground. The integration gate still stops an
			// actually invalid next step.
			|| (!bExecutingGround
				&& (Avoidance.bControlledRecovery || Avoidance.ConsecutiveBlockedSeconds > 0.1f)))
		{ Cancel(); continue; }
		const FGuLiWingmanAttackTarget* DesiredTarget =
			GuLiWingmanTargeting::ResolveTargetForEmitter(State, Identity.Handle);
		const auto* Channel = Config.WeaponChannels.FindByPredicate([&](const auto& C) {
			return C.bEnabled && (bExecutingGround ? C.Binding.SlotId == Attack.SlotId
				: DesiredTarget && C.Runtime.Attack.Pattern == (DesiredTarget->bGround
					? EGuLiWingmanAttackPattern::GroundDive
					: EGuLiWingmanAttackPattern::AirDogfight)); });
		if (!Channel || (Attack.bGuiding && (Attack.LeaseEpoch != LeaseEpoch
			|| (Attack.SlotId == Channel->Binding.SlotId && (Attack.ProfileRevision != Channel->ProfileRevision
				|| Attack.SkillId != Channel->SkillId || Attack.DefinitionChecksum != Channel->DefinitionChecksum)))))
		{ Cancel(); continue; }
		if (!bExecutingGround && (!DesiredTarget || !DesiredTarget->IsValid()
			|| Now < DesiredTarget->ServerTime || Now - DesiredTarget->ServerTime > 1.0))
		{ Cancel(); continue; }
		const auto& Profile = Channel->Runtime.Attack;
		const FVector Position = Transform.GetLocation(), Forward = Transform.GetUnitAxis(EAxis::X);
		const bool bTargetRelationChanged = !bExecutingGround
			&& (Attack.Target.Target != DesiredTarget->Target
				|| Attack.Target.Revision != DesiredTarget->Revision
				|| Attack.SlotId != Channel->Binding.SlotId);
		if (bTargetRelationChanged)
		{
			Attack.Phase = EGuLiWingmanAttackPhase::Idle; Attack.bGuiding = false;
			Attack.AirTurn = {}; Attack.RetreatPoint = FVector::ZeroVector; Attack.RetreatOrigin = FVector::ZeroVector;
		}
		// Ground preparation freezes one server-published bombing point. Continuing
		// to overwrite it every 200 ms made a moving Soldier invalidate the entry
		// just as the aircraft finished lining up. A changed assignment revision still
		// cancels and starts a fresh run; ordinary position refreshes do not.
		if (!bExecutingGround && (!bPreparingGround || bTargetRelationChanged))
		{
			Attack.Target = *DesiredTarget;
		}
		Attack.SlotId = Channel->Binding.SlotId; Attack.ProfileRevision = Channel->ProfileRevision; Attack.LeaseEpoch = LeaseEpoch;
		Attack.SkillId = Channel->SkillId; Attack.DefinitionChecksum = Channel->DefinitionChecksum;
		const auto QueueShot = [&](int32 Index)
		{
			FGuLiWingmanAttackFireRecord Shot;
			Shot.MemberIndex = Identity.Handle.MemberIndex; Shot.ClientSimTick = ClientTick;
			Shot.SlotId = Channel->Binding.SlotId; Shot.ProfileRevision = Channel->ProfileRevision;
			Shot.LoadoutRevision = Config.LoadoutRevision; Shot.RunId = Attack.RunId; Shot.ShotIndex = Index; Shot.Target = Attack.Target;
			Shot.ApproachDirection = Profile.Pattern == EGuLiWingmanAttackPattern::GroundDive ? Attack.Path.Direction : FVector::ForwardVector;
			Runtime->PendingAttackShots.Emplace(Identity.Handle.Flight.FlightIndex, MoveTemp(Shot));
		};
		if (Profile.Pattern == EGuLiWingmanAttackPattern::AirDogfight)
		{
			const float AgentRadius = Config.FormationRuntime.AgentRadiusCentimeters;
			const float Bounds = FMath::Max(0.0f, Attack.Target.Radius) + AgentRadius;
			const float BreakawayBoundary = Bounds + Profile.BreakawayDistance;
			const float Distance = FVector::Distance(Position, Attack.Target.Location);
			const float TurnRate = Config.FormationRuntime.MaximumTurnRateDegreesPerSecond;
			const auto EnterTurn = [&](EGuLiWingmanAttackPhase Phase, const FVector& Destination,
				uint32 PhaseSalt, uint32 CandidateBase)->bool
			{
				if (++Attack.AirStateEntrySerial == 0u) ++Attack.AirStateEntrySerial;
				Attack.StartTime = Now; Attack.bAirTurnUsingDirectGuidance = false;
				for (uint32 Attempt = 0; Attempt < GuLiWingmanAttack::MaximumAirManeuverCandidates; ++Attempt)
				{
					const uint32 Seed = GuLiWingmanAttack::MakeAirManeuverSeed(SwarmAgent.AgentSeed,
						Attack.Target.Revision, Attack.AirStateEntrySerial, ClientTick, PhaseSalt, CandidateBase + Attempt);
					FGuLiWingmanAirTurnPlan Plan;
					if (GuLiWingmanAttack::BuildAirTurnPlan(Position, Destination, Profile.FlightSpeed, TurnRate,
						Profile, Seed, Plan)
						&& GuLiWingmanAttack::AirSegmentClearsWorld(GetWorld(), Position, Plan.ControlPoint, AgentRadius)
						&& GuLiWingmanAttack::AirSegmentClearsWorld(GetWorld(), Plan.ControlPoint, Destination, AgentRadius))
					{
						Attack.Phase = Phase; Attack.AirTurn = Plan;
						Attack.PreferredVelocity = (Plan.ControlPoint - Position).GetSafeNormal() * Profile.FlightSpeed;
						return true;
					}
				}
				return false;
			};
			const auto EnterBreakaway = [&]()->bool
			{
				// One entry serial covers all eight deterministic safety candidates. The accepted point is frozen for this run.
				if (++Attack.AirStateEntrySerial == 0u) ++Attack.AirStateEntrySerial;
				Attack.StartTime = Now; Attack.bAirTurnUsingDirectGuidance = false;
				for (uint32 Attempt = 0; Attempt < GuLiWingmanAttack::MaximumAirManeuverCandidates; ++Attempt)
				{
					const uint32 Seed = GuLiWingmanAttack::MakeAirManeuverSeed(SwarmAgent.AgentSeed,
						Attack.Target.Revision, Attack.AirStateEntrySerial, ClientTick,
						GuLiWingmanAttack::BreakawayTurnSalt, Attempt);
					const FVector Retreat = GuLiWingmanAttack::BuildRetreatCandidate(Position, Attack.Target.Location,
						Carrier.Transform.GetLocation(), BreakawayBoundary, Profile, Seed);
					FGuLiWingmanAirTurnPlan Plan;
					if (!Retreat.IsNearlyZero() && GuLiWingmanAttack::BuildAirTurnPlan(Position, Retreat,
						Profile.FlightSpeed, TurnRate, Profile, Seed ^ 0x68bc21ebu, Plan)
						&& GuLiWingmanAttack::AirSegmentClearsWorld(GetWorld(), Position, Plan.ControlPoint, AgentRadius)
						&& GuLiWingmanAttack::AirSegmentClearsWorld(GetWorld(), Plan.ControlPoint, Retreat, AgentRadius))
					{
						Attack.Phase = EGuLiWingmanAttackPhase::AirBreakawayTurn;
						Attack.RetreatOrigin = Position; Attack.RetreatPoint = Retreat; Attack.AirTurn = Plan;
						Attack.PreferredVelocity = (Plan.ControlPoint - Position).GetSafeNormal() * Profile.FlightSpeed;
						return true;
					}
				}
				return false;
			};

			if (Attack.Phase == EGuLiWingmanAttackPhase::Idle)
			{
				if (Now < Attack.RetryAfter) continue;
				Attack.Phase = EGuLiWingmanAttackPhase::AirApproachFire;
			}
			Attack.bGuiding = true;
			if (Attack.Phase == EGuLiWingmanAttackPhase::AirApproachFire)
			{
				Attack.PreferredVelocity = (Attack.Target.Location - Position).GetSafeNormal() * Profile.FlightSpeed;
				if (Distance <= BreakawayBoundary)
				{
					if (!EnterBreakaway()) Cancel(5);
					continue;
				}
				if (GuLiWingmanAttack::CanQueueAirGun(Attack.Phase)
					&& Now >= Weapon.GetNextFireSeconds(Attack.SlotId)
					&& GuLiWingmanAttack::IsInsideForwardArc(Position, Forward, Attack.Target.Location,
						Attack.Target.Radius, Channel->Runtime.RangeCentimeters,
						Channel->Runtime.TargetConeHalfAngleDegrees))
				{
					Attack.RunId = ClientTick; QueueShot(0);
					Weapon.SetNextFireSeconds(Attack.SlotId, Now + Channel->Runtime.CooldownSeconds);
				}
				continue;
			}
			if (Attack.Phase == EGuLiWingmanAttackPhase::AirBreakawayTurn)
			{
				const bool bTurnFinished = GuLiWingmanAttack::ShouldFinishAirTurn(Position, Attack.AirTurn,
					Profile.ManeuverArrivalRadius, Now - Attack.StartTime);
				const bool bTimedOut = Now - Attack.StartTime >= GuLiWingmanAttack::AirTurnTimeoutSeconds;
				if (bTurnFinished)
				{
					Attack.bAirTurnUsingDirectGuidance = bTimedOut;
					Attack.Phase = GuLiWingmanAttack::NextAirDogfightPhase(Attack.Phase);
					Attack.PreferredVelocity = (Attack.RetreatPoint - Position).GetSafeNormal() * Profile.FlightSpeed;
				}
				else Attack.PreferredVelocity = (Attack.AirTurn.ControlPoint - Position).GetSafeNormal() * Profile.FlightSpeed;
				continue;
			}
			if (Attack.Phase == EGuLiWingmanAttackPhase::AirRetreat)
			{
				if (GuLiWingmanAttack::HasReachedOrPassed(Position, Attack.RetreatOrigin,
					Attack.RetreatPoint, Profile.ManeuverArrivalRadius))
				{
					if (!EnterTurn(GuLiWingmanAttack::NextAirDogfightPhase(Attack.Phase), Attack.Target.Location,
						GuLiWingmanAttack::ReturnTurnSalt, 0u)) Cancel(6);
				}
				else Attack.PreferredVelocity = (Attack.RetreatPoint - Position).GetSafeNormal() * Profile.FlightSpeed;
				continue;
			}
			if (Attack.Phase == EGuLiWingmanAttackPhase::AirReturnTurn)
			{
				const bool bTurnFinished = GuLiWingmanAttack::ShouldFinishAirTurn(Position, Attack.AirTurn,
					Profile.ManeuverArrivalRadius, Now - Attack.StartTime);
				const bool bTimedOut = Now - Attack.StartTime >= GuLiWingmanAttack::AirTurnTimeoutSeconds;
				if (bTurnFinished)
				{
					Attack.bAirTurnUsingDirectGuidance = bTimedOut;
					Attack.Phase = GuLiWingmanAttack::NextAirDogfightPhase(Attack.Phase);
					Attack.PreferredVelocity = (Attack.Target.Location - Position).GetSafeNormal() * Profile.FlightSpeed;
				}
				else Attack.PreferredVelocity = (Attack.AirTurn.ControlPoint - Position).GetSafeNormal() * Profile.FlightSpeed;
				continue;
			}
			Cancel(7);
			continue;
		}
		if (Profile.Pattern != EGuLiWingmanAttackPattern::GroundDive) { Cancel(); continue; }
		if (Attack.Phase == EGuLiWingmanAttackPhase::Idle)
		{
			if (Now < FMath::Max(Attack.RetryAfter, Weapon.GetNextFireSeconds(Attack.SlotId))) continue;
			bool bFoundClearApproach = false;
			Attack.GroundPathFailureMask = 0;
			Attack.GroundNavigationFailureMask = 0;
			for (int32 CandidateIndex = 0;
				CandidateIndex < GuLiWingmanAttack::MaximumGroundApproachCandidates; ++CandidateIndex)
			{
				const FVector Approach = GuLiWingmanAttack::BuildGroundApproachCandidate(
					Attack.Target.Location - Position, SwarmAgent.AgentSeed, CandidateIndex);
				FGuLiWingmanGroundRunPath CandidatePath;
				GuLiWingmanAttack::EGroundPathRejectReason RejectReason =
					GuLiWingmanAttack::EGroundPathRejectReason::InvalidInput;
				EGuLiFlightNavSegmentStatus NavigationStatus = EGuLiFlightNavSegmentStatus::Valid;
				const bool bBuiltPath = GuLiWingmanAttack::BuildGroundPath(Attack.Target.Location, Approach, Profile,
					Config.FormationRuntime.MaximumTurnRateDegreesPerSecond, CandidatePath);
				const FVector CandidateSetup = bBuiltPath
					? GuLiWingmanAttack::GroundRunSetupPoint(CandidatePath)
					: FVector::ZeroVector;
				const bool bIngressAlreadyReached = bBuiltPath
					&& FVector::Distance(Position, CandidateSetup)
						<= Config.FormationRuntime.AgentRadiusCentimeters;
				const bool bClearPath = bBuiltPath
					&& GuLiWingmanAttack::GroundRunClearsTerrain(GetWorld(), CandidatePath,
						Config.FormationRuntime.AgentRadiusCentimeters, &RejectReason, &NavigationStatus)
					&& (bIngressAlreadyReached
						|| GuLiWingmanAttack::AirSegmentClearsWorld(GetWorld(), Position,
							CandidateSetup, Config.FormationRuntime.AgentRadiusCentimeters));
				if (!bClearPath)
				{
					const uint8 ReasonBit = 1u << static_cast<uint8>(RejectReason);
					Attack.GroundPathFailureMask |= ReasonBit;
					if (RejectReason == GuLiWingmanAttack::EGroundPathRejectReason::Navigation)
						Attack.GroundNavigationFailureMask |= 1u << static_cast<uint8>(NavigationStatus);
					continue;
				}
				Attack.Path = CandidatePath;
				bFoundClearApproach = true;
				break;
			}
			if (!bFoundClearApproach)
			{
				Attack.LastCancelReason = 8;
				Cancel(8);
				continue;
			}
			Attack.Phase = EGuLiWingmanAttackPhase::Ingress;
			Attack.StartTime = Now;
			Attack.PhaseStartTime = Now;
			Attack.LastCancelReason = 0;
		}
		Attack.bGuiding = true;
		const FVector DiveDirection = Attack.Path.DirectionAt(0);
		const FVector Setup = GuLiWingmanAttack::GroundRunSetupPoint(Attack.Path);
		if (Attack.Phase == EGuLiWingmanAttackPhase::Ingress)
		{
			Attack.PreferredVelocity = (Setup - Position).GetSafeNormal() * Profile.FlightSpeed;
			if (FVector::Distance(Position, Setup) < Profile.FlightSpeed * 2.0f)
			{
				Attack.Phase = EGuLiWingmanAttackPhase::Lineup;
				Attack.PhaseStartTime = Now;
			}
			if (Now - Attack.StartTime > GuLiWingmanAttack::MaximumGroundIngressSeconds) Cancel();
			continue;
		}
		if (Attack.Phase == EGuLiWingmanAttackPhase::Lineup)
		{
			const float Along = FVector::DotProduct(Position - Attack.Path.Entry, DiveDirection);
			const FVector Closest = Attack.Path.Entry + DiveDirection * Along;
			Attack.PreferredVelocity = (DiveDirection * (Profile.FlightSpeed * 1.0f) + (Closest - Position)).GetSafeNormal() * Profile.FlightSpeed;
			if (Along >= -80.0f && Along <= 600.0f && FVector::Distance(Position, Closest) < 500.0f
				&& FVector::DotProduct(Forward, DiveDirection) > FMath::Cos(FMath::DegreesToRadians(6.0f))
				&& FMath::Abs(Dynamics.Velocity.Size() - Profile.FlightSpeed) < Profile.FlightSpeed * 0.05f)
			{
				Attack.Phase = EGuLiWingmanAttackPhase::Dive;
				Attack.StartTime = Now;
				Attack.PhaseStartTime = Now;
				Attack.RunId = ClientTick;
				Attack.NextShotIndex = 1;
				QueueShot(0); Weapon.SetNextFireSeconds(Attack.SlotId, Now + Channel->Runtime.CooldownSeconds);
			}
			else if (Along > 600.0f
				|| Now - Attack.PhaseStartTime > GuLiWingmanAttack::MaximumGroundLineupSeconds) Cancel();
			continue;
		}
		const float Age = float(Now - Attack.StartTime);
		if (Age > Profile.DiveSeconds + 0.05f && Attack.NextShotIndex < Profile.MissileCount) { Cancel(2); continue; }
		const FVector Desired = Attack.Path.DirectionAt(Age + 1.0f / 30.0f);
		Attack.PreferredVelocity = Desired * Profile.FlightSpeed;
		if (Age <= Profile.DiveSeconds + 0.05f && Attack.NextShotIndex < Profile.MissileCount)
		{
			const float Due = GuLiWingmanAttack::ShotTime(Attack.NextShotIndex, Profile.MissileCount, Profile.DiveSeconds);
			if (Age >= Due - 0.001f)
			{
				if (Age - Due > 0.075f) { Cancel(2); continue; }
				if (FVector::Dist(Position, Attack.Path.PositionAt(Age)) > 1200.0) { Cancel(3); continue; }
				if (FVector::DotProduct(Forward, DiveDirection) < FMath::Cos(FMath::DegreesToRadians(12.0f))) { Cancel(4); continue; }
				QueueShot(Attack.NextShotIndex++);
			}
		}
		if (Age > Profile.DiveSeconds) Attack.Phase = EGuLiWingmanAttackPhase::PullUp;
		if (Age > Profile.DiveSeconds + Attack.Path.TurnSeconds) Attack.Phase = EGuLiWingmanAttackPhase::Climb;
		if (Age >= Attack.Path.TotalSeconds())
		{
			Attack.Phase = EGuLiWingmanAttackPhase::Idle;
			Attack.bGuiding = false;
			Attack.LastCancelReason = 0;
			++Attack.CompletedGroundRuns;
		}
	}
	if (!bActive) Runtime->PendingAttackShots.Reset();
}

void UGuLiWingmanSimulationSubsystem::AppendAttackFireRecords(FGuLiWingmanCandidateBatch& Candidate)
{
	auto* Runtime = OwnedGroups.Find(Candidate.Group);
	if (!Runtime) return;
	for (int32 Index = 0; Index < Runtime->PendingAttackShots.Num();)
	{
		const auto& Pending = Runtime->PendingAttackShots[Index];
		if (Pending.Key != Candidate.FlightIndex) { ++Index; continue; }
		const auto& Shot = Pending.Value;
		const bool bPoseIncluded = Shot.ClientSimTick == Candidate.ClientSimTick || Candidate.TrailSamples.ContainsByPredicate(
			[&](const auto& T) { return T.ClientSimTick == Shot.ClientSimTick; });
		if (bPoseIncluded && Candidate.AttackFireRecords.Num() < GuLiWingmanAttack::MaximumFireRecordsPerFlight)
		{
			Candidate.AttackFireRecords.Add(Shot);
		}
		// Missing/late trajectory ticks never become a future catch-up volley.
		Runtime->PendingAttackShots.RemoveAt(Index, 1, EAllowShrinking::No);
	}
}

void UGuLiWingmanSimulationSubsystem::GetAttackDiagnostics(TArray<FGuLiWingmanAttackDiagnostic>& Out) const
{
	Out.Reset();
	if (OwnedGroups.IsEmpty()) return;
	for (const auto& Pair : OwnedGroups)
	{
		for (const TWeakObjectPtr<AGuLiWingmanPawn>& Entry : Pair.Value.Pawns)
		{
			const AGuLiWingmanPawn* Pawn = Entry.Get();
			if (!Pawn) continue;
			const FGuLiWingmanRuntimeState& State = Pawn->GetRuntimeState();
			const auto& Attack = State.Attack;
			const FTransform Transform = Pawn->GetActorTransform();
			auto& D = Out.AddDefaulted_GetRef();
			D.Wingman = State.Identity.Handle;
			D.Target = Attack.Target;
			D.SlotId = Attack.SlotId;
			D.Phase = uint8(Attack.Phase);
			const auto& Dynamics = State.Dynamics;
			const auto& Avoidance = State.Avoidance;
			D.FlightMode = uint8(Dynamics.Mode);
			D.ConsecutiveBlockedSeconds = Avoidance.ConsecutiveBlockedSeconds;
			D.bControlledRecovery = Avoidance.bControlledRecovery;
			D.GroundPathFailureMask = Attack.GroundPathFailureMask;
			D.GroundNavigationFailureMask = Attack.GroundNavigationFailureMask;
			D.Position = Transform.GetLocation();
			D.Forward = Transform.GetUnitAxis(EAxis::X);
			D.Entry = Attack.Path.Entry; D.PreferredVelocity = Attack.PreferredVelocity; D.RunId = Attack.RunId;
			D.CompletedGroundRuns = Attack.CompletedGroundRuns;
			D.RetreatPoint = Attack.RetreatPoint; D.TurnControlPoint = Attack.AirTurn.ControlPoint;
			D.TurnYawDegrees = Attack.AirTurn.SignedYawDegrees; D.TurnPitchDegrees = Attack.AirTurn.PitchDegrees;
			D.StateEntrySerial = Attack.AirStateEntrySerial;
			D.NextShot = Attack.NextShotIndex; D.StartTime = Attack.StartTime;
			D.PhaseStartTime = Attack.PhaseStartTime; D.bGuiding = Attack.bGuiding;
			D.CancelReason = Attack.LastCancelReason;
		}
	}
}

#if WITH_DEV_AUTOMATION_TESTS
bool UGuLiWingmanSimulationSubsystem::PrimeGroundDiveForTests(
	const FGuLiWingmanGroupHandle& Group,
	const FGuLiWingmanHandle& Wingman,
	const double StartTime)
{
	FGuLiWingmanLocalGroupRuntime* Runtime = OwnedGroups.Find(Group);
	if (!Runtime || !FMath::IsFinite(StartTime))
	{
		return false;
	}
	AGuLiWingmanPawn* Pawn = FindOwnedPawn(*Runtime, Wingman);
	if (!Pawn)
	{
		return false;
	}
	FGuLiWingmanAttackRunState& Attack = Pawn->GetMutableRuntimeState().Attack;
	if ((Attack.Phase != EGuLiWingmanAttackPhase::Ingress
			&& Attack.Phase != EGuLiWingmanAttackPhase::Lineup)
		|| !Attack.Target.IsValid() || !Attack.Target.bGround
		|| Attack.Path.TotalSeconds() <= 0.0f)
	{
		return false;
	}
	Attack.Phase = EGuLiWingmanAttackPhase::Dive;
	Attack.StartTime = StartTime;
	Attack.NextShotIndex = FMath::Max(Attack.NextShotIndex, 1);
	Attack.bGuiding = true;
	return true;
}
#endif

void UGuLiWingmanSimulationSubsystem::GetPendingAttackCaptureTicks(const FGuLiWingmanGroupHandle& Group,
	uint8 FlightIndex, TArray<uint32>& Out) const
{
	Out.Reset();
	if (const auto* Runtime = OwnedGroups.Find(Group))
		for (const auto& Pending : Runtime->PendingAttackShots)
			if (Pending.Key == FlightIndex) Out.AddUnique(Pending.Value.ClientSimTick);
}

bool UGuLiWingmanSimulationSubsystem::GetCompletedSimulationTick(const FGuLiWingmanGroupHandle& Group, uint32& OutTick) const
{
	const auto* Runtime=OwnedGroups.Find(Group);
	if(!Runtime || Runtime->Pawns.IsEmpty()) return false;
	OutTick = 0;
	// A replenished member starts with a fresh component clock. The oldest surviving
	// group members retain the shared integration timeline; member zero is not a clock owner.
	for (const TWeakObjectPtr<AGuLiWingmanPawn>& Entry : Runtime->Pawns)
		if (const AGuLiWingmanPawn* Pawn = Entry.Get())
			OutTick = FMath::Max(OutTick,
				Pawn->GetRuntimeState().Dynamics.CaptureSimulationTick);
	return OutTick != 0;
}
