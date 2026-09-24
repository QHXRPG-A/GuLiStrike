#include "Gameplay/Resources/GuLiMiningVehiclePawn.h"
#include "Gameplay/Resources/GuLiMiningVehicleManager.h"
#include "Gameplay/Resources/GuLiResourceFactoryActor.h"
#include "Gameplay/Resources/GuLiResourceWorldSubsystem.h"
#include "Gameplay/Units/GuLiExternalUnitControlComponent.h"
#include "Engine/World.h"

EGuLiCommanderWorkPhase AGuLiMiningVehiclePawn::GetBehaviorPhase() const
{
	using Phase = EGuLiCommanderWorkPhase;
	if (bWaitingForMiningSlot) return Phase::MiningWaitingPosition;
	if (BehaviorResult == EGuLiCommanderWorkResult::TargetReady) return Phase::MiningReserved;
	if (TaskState == EGuLiMiningTaskState::MovingToCluster && FindComponentByClass<UGuLiEngineeringTravelComponent>()->IsWaitingForPath()) return Phase::MiningWaitingPath;
	switch (TaskState)
	{
	case EGuLiMiningTaskState::MovingToCluster: return Phase::MiningMoving;
	case EGuLiMiningTaskState::Mining: return Phase::MiningExtracting;
	case EGuLiMiningTaskState::ReturningToFactory: return Phase::MiningReturning;
	case EGuLiMiningTaskState::Docking: return Phase::MiningUnloading;
	default: return Phase::MiningIdle;
	}
}

bool AGuLiMiningVehiclePawn::IsBehaviorRetryReady() const
{ return GetWorld()->GetTimeSeconds() >= NextAutoRetryServerTime; }

void AGuLiMiningVehiclePawn::ExecuteBehaviorAction(EGuLiMiningBehaviorAction Action, uint16 RequestedCluster)
{
	using Result = EGuLiCommanderWorkResult;
	if (!HasAuthority() || !bTaskManaged || bManagedTaskComplete
		|| UGuLiExternalUnitControlComponent::AreActorActionsLocked(this)
		|| FindComponentByClass<UGuLiEngineeringTravelComponent>()->IsRouting()) return;
	auto* Resources = GetResourceSubsystem();
	if (!Resources || !Resources->IsRuntimeReady()) return;
	switch (Action)
	{
	case EGuLiMiningBehaviorAction::SelectTarget:
	{
		if (!IsBehaviorRetryReady()) break;
		if (RequestedCluster && !Resources->CanTeamMineAt(Team,RequestedCluster))
		{ bWaitingForMiningSlot=false; BehaviorResult=Result::TargetLost; break; }
		auto* Manager = GetWorld()->GetSubsystem<UGuLiMiningVehicleManager>();
		if (IsAtReservedMiningSlot()
			&& FindComponentByClass<UGuLiEngineeringTravelComponent>()->GetMoveStatus()==EGuLiEngineeringMoveStatus::Arrived)
		{
			// Another miner may own the remaining nodes. Poll in place without requesting a path.
			bWaitingForMiningSlot=false; TaskState=EGuLiMiningTaskState::MovingToCluster;
			if (SelectMiningTarget()) BehaviorResult=Result::CanMine;
			else if (!bWaitingForMiningSlot) BehaviorResult=Result::OutOfRange;
			break;
		}
		const auto Status = Manager->TryReserveSlot(*this, RequestedCluster, MiningTaskVersion, MiningSlot, BehaviorApproach);
		if (Status==EGuLiWorkPositionAvailability::NoValidPositions && GetCargoTotal()>0)
		{ bWaitingForMiningSlot=false; BehaviorResult=Result::TargetLost; break; }
		bWaitingForMiningSlot = Status != EGuLiWorkPositionAvailability::Available;
		if (!bWaitingForMiningSlot)
		{
			TargetClusterId = MiningSlot.Cluster;
			// Reserve the work position now; select a visible node from the actual pose after arrival.
			BehaviorResult = Result::TargetReady;
		}
		else { BehaviorResult = Result::WaitingPosition; NextAutoRetryServerTime = GetWorld()->GetTimeSeconds()+.2f; }
		break;
	}
	case EGuLiMiningBehaviorAction::MoveToTarget:
		if (!GetWorld()->GetSubsystem<UGuLiMiningVehicleManager>()->ValidateReservation(*this,MiningSlot))
		{ BehaviorResult = Result::TargetLost; break; }
		if (!FindComponentByClass<UGuLiEngineeringTravelComponent>()->BeginWorkMove(BehaviorApproach,GULI_MINING_SLOT_ARRIVAL_RADIUS_CM))
		{ BehaviorResult = Result::Failed; break; }
		TaskState = EGuLiMiningTaskState::MovingToCluster; BehaviorResult = Result::Running; ForceNetUpdate(); break;
	case EGuLiMiningBehaviorAction::Reposition:
		GetWorld()->GetSubsystem<UGuLiMiningVehicleManager>()->RejectSlot(*this,MiningSlot,
			BehaviorResult==Result::OutOfRange ? EGuLiMiningSlotFailure::NoVisibleNodes : EGuLiMiningSlotFailure::Movement);
		FinishCurrentTarget(); TaskState = EGuLiMiningTaskState::Idle; BehaviorResult = Result::None;
		NextAutoRetryServerTime = GetWorld()->GetTimeSeconds()+.2f; break;
	case EGuLiMiningBehaviorAction::Extract:
		if (TaskState == EGuLiMiningTaskState::Mining) break;
		if (auto* AI = Cast<AAIController>(GetController())) AI->StopMovement();
		TaskState = EGuLiMiningTaskState::Mining; MiningAccumulator = 0; BehaviorResult = Result::Running;
		SetMiningVisual(true); ForceNetUpdate(); break;
	case EGuLiMiningBehaviorAction::SelectFactory:
	{
		FinishCurrentTarget();
		if (BehaviorResult != Result::FactoryUnreachable && BehaviorResult != Result::FactoryLost)
		{
			ReturnFactories.Reset(); NextReturnFactory = 0;
			TArray<AGuLiResourceFactoryActor*> Candidates;
			Resources->QueryFriendlyFactories(Team, GetActorLocation(), Candidates);
			for (auto* Candidate : Candidates) ReturnFactories.Add(Candidate);
		}
		AGuLiResourceFactoryActor* Destination = nullptr;
		while (NextReturnFactory < ReturnFactories.Num())
		{
			auto* Candidate = ReturnFactories[NextReturnFactory++].Get();
			if (Candidate && !Candidate->IsActorBeingDestroyed() && Candidate->GetTeam() == Team
				&& Candidate->FindComponentByClass<UGuLiBuildingLifecycleComponent>()->IsCompleted())
			{ Destination = Candidate; break; }
		}
		UnloadPointAttempts = 0;
		if (Destination != Factory)
		{
			if (IsValid(Factory)) Factory->UnregisterMiningVehicle(*this);
			Factory = Destination;
			if (Factory) Factory->RegisterMiningVehicle(*this);
		}
		bManualReturnOrder |= ControlMode == EGuLiMiningControlMode::PlayerOrder;
		TaskState = EGuLiMiningTaskState::Idle;
		BehaviorResult = IsValid(Factory) ? Result::FactoryReady : Result::NoTarget;
		break;
	}
	case EGuLiMiningBehaviorAction::ReturnToFactory:
	{
		TaskState = EGuLiMiningTaskState::ReturningToFactory;
		if (!IsValid(Factory) || Factory->GetTeam() != Team) { BehaviorResult = Result::FactoryLost; break; }
		const auto Points = Factory->GetUnloadPoints();
		if (UnloadPointAttempts >= Points.Num()) { BehaviorResult = Result::FactoryUnreachable; break; }
		// Distribute first choices without reserving a point; overlap is explicitly allowed.
		const FVector Target = Points[(StableActorId.Value + UnloadPointAttempts++) % Points.Num()];
		BehaviorResult = FindComponentByClass<UGuLiEngineeringTravelComponent>()->BeginWorkMove(Target, 50)
			? Result::Running : Result::Failed;
		ForceNetUpdate(); break;
	}
	case EGuLiMiningBehaviorAction::Unload:
		if (!IsValid(Factory) || Factory->GetTeam() != Team) { BehaviorResult = Result::FactoryLost; break; }
		FindComponentByClass<UGuLiEngineeringTravelComponent>()->StopAtSafePoint();
		TaskState = EGuLiMiningTaskState::Docking; DockingAccumulator = 0;
		BehaviorResult = Result::Running; ForceNetUpdate(); break;
	case EGuLiMiningBehaviorAction::FinishCycle: BeginGrace(GetCargoTotal() == 0); break;
	case EGuLiMiningBehaviorAction::Retry:
		if (BehaviorResult == Result::Failed) GetWorld()->GetSubsystem<UGuLiMiningVehicleManager>()->RejectSlot(*this,MiningSlot);
		FinishCurrentTarget(); TaskState = EGuLiMiningTaskState::Idle;
		BehaviorResult = Result::None;
		NextAutoRetryServerTime = GetWorld()->GetTimeSeconds() + (GetCargoTotal() > 0 ? AutoRetrySeconds : .2f); break;
	case EGuLiMiningBehaviorAction::Fail: BeginGrace(false); break;
	case EGuLiMiningBehaviorAction::Complete: BeginGrace(true); break;
	}
}
