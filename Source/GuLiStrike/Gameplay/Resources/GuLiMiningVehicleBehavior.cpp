#include "Gameplay/Resources/GuLiMiningVehiclePawn.h"
#include "Gameplay/Resources/GuLiMiningVehicleManager.h"
#include "Gameplay/Building/GuLiShieldGeneratorActor.h"
#include "Gameplay/Resources/GuLiResourceWorldSubsystem.h"
#include "Gameplay/Units/GuLiExternalUnitControlComponent.h"
#include "Engine/World.h"
#include "Gameplay/Data/GuLiUnitDataSubsystem.h"
#include "Network/GuLiHybridActorComponent.h"
#include "Gameplay/Navigation/Grid/GuLiGridQueries.h"

TArray<FVector> AGuLiMiningVehiclePawn::GetDeliveryPoints() const
{
    const auto& Config=GetWorld()->GetSubsystem<UGuLiUnitDataSubsystem>()->GetAvoidanceCatalog().Units.FindChecked(UnitTypeId).Solver;
    return Factory->GetUnloadPoints(GetActorLocation(),Config.Radius,Config.ContactMargin);
}

bool AGuLiMiningVehiclePawn::ReplanGridWork()
{
    if (!bTaskManaged || bManagedTaskComplete || !UGuLiHybridActorComponent::HasSimulation(this)) return false;
    auto* Travel=FindComponentByClass<UGuLiEngineeringTravelComponent>();
    auto* Grid=GetWorld()->GetSubsystem<UGuLiGroundGridSubsystem>();if (!Grid || !Grid->IsReady()) return false;
    if (TaskState==EGuLiMiningTaskState::ReturningToFactory || TaskState==EGuLiMiningTaskState::Docking)
    {
        if (!IsValid(Factory) || Factory->GetTeam()!=Team) {BeginGrace(false);Travel->StopAtSafePoint();return false;}
        const auto Points=GetDeliveryPoints();int32 Best=INDEX_NONE;
        for (int32 I=0;I<Points.Num();++I)
            if (Best==INDEX_NONE || FVector::DistSquared2D(GetActorLocation(),Points[I])<FVector::DistSquared2D(GetActorLocation(),Points[Best])) Best=I;
        if (Best==INDEX_NONE) return false;
        TaskState=EGuLiMiningTaskState::ReturningToFactory;BehaviorResult=EGuLiCommanderWorkResult::Running;
        return Travel->BeginWorkMove(Points[Best],50);
    }
    auto* Manager=GetWorld()->GetSubsystem<UGuLiMiningVehicleManager>();
    if (MiningSlot.IsValid()) {Manager->ReleaseSlot(*this,MiningSlot);MiningSlot={};TargetNodeId=0;SetMiningVisual(false);}
    FGuLiMiningSlotReservation Next;FVector Position;
    if (Manager->TryReserveSlot(*this,TargetClusterId,MiningTaskVersion,Next,Position)!=EGuLiWorkPositionAvailability::Available) return false;
    MiningSlot=Next;BehaviorApproach=Position;TargetClusterId=Next.Cluster;bWaitingForMiningSlot=false;
    TaskState=EGuLiMiningTaskState::MovingToCluster;BehaviorResult=EGuLiCommanderWorkResult::Running;
    return Travel->BeginWorkMove(Position,GULI_MINING_SLOT_ARRIVAL_RADIUS_CM);
}

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
{ return UGuLiHybridActorComponent::BattleSeconds(this) >= NextAutoRetryServerTime; }

void AGuLiMiningVehiclePawn::ExecuteBehaviorAction(EGuLiMiningBehaviorAction Action, uint16 RequestedCluster)
{
	using Result = EGuLiCommanderWorkResult;
	if (!UGuLiHybridActorComponent::HasSimulation(this) || !bTaskManaged || bManagedTaskComplete
		|| UGuLiExternalUnitControlComponent::AreActorActionsLocked(this)
		|| FindComponentByClass<UGuLiEngineeringTravelComponent>()->IsRouting()) return;
	auto* Resources = GetResourceSubsystem();
	if (!Resources || !Resources->IsMiningRuntimeReady()) return;
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
		else { BehaviorResult = Result::WaitingPosition; NextAutoRetryServerTime = UGuLiHybridActorComponent::BattleSeconds(this)+.2f; }
		break;
	}
	case EGuLiMiningBehaviorAction::MoveToTarget:
		if (!GetWorld()->GetSubsystem<UGuLiMiningVehicleManager>()->ValidateReservation(*this,MiningSlot))
		{ BehaviorResult = Result::TargetLost; break; }
		if (!FindComponentByClass<UGuLiEngineeringTravelComponent>()->BeginWorkMove(BehaviorApproach,GULI_MINING_SLOT_ARRIVAL_RADIUS_CM))
		{ BehaviorResult = Result::Failed; break; }
		TaskState = EGuLiMiningTaskState::MovingToCluster; BehaviorResult = Result::Running;  break;
	case EGuLiMiningBehaviorAction::Reposition:
		GetWorld()->GetSubsystem<UGuLiMiningVehicleManager>()->RejectSlot(*this,MiningSlot,
			BehaviorResult==Result::OutOfRange ? EGuLiMiningSlotFailure::NoVisibleNodes : EGuLiMiningSlotFailure::Movement);
		FinishCurrentTarget(); TaskState = EGuLiMiningTaskState::Idle; BehaviorResult = Result::None;
		NextAutoRetryServerTime = UGuLiHybridActorComponent::BattleSeconds(this)+.2f; break;
	case EGuLiMiningBehaviorAction::Extract:
		if (TaskState == EGuLiMiningTaskState::Mining) break;
		if (auto* AI = Cast<AAIController>(GetController())) AI->StopMovement();
		TaskState = EGuLiMiningTaskState::Mining; MiningAccumulator = 0; BehaviorResult = Result::Running;
		SetMiningVisual(true);  break;
	case EGuLiMiningBehaviorAction::SelectFactory:
	{
		FinishCurrentTarget();
		if (BehaviorResult != Result::FactoryUnreachable && BehaviorResult != Result::FactoryLost)
		{
			ReturnDropoffs.Reset(); NextReturnDropoff = 0;
			TArray<AGuLiShieldGeneratorActor*> Candidates;
			Resources->QueryFriendlyDropoffs(Team, GetActorLocation(), Candidates);
			for (auto* Candidate : Candidates) ReturnDropoffs.Add(Candidate);
		}
		AGuLiShieldGeneratorActor* Destination = nullptr;
		while (NextReturnDropoff < ReturnDropoffs.Num())
		{
			auto* Candidate = ReturnDropoffs[NextReturnDropoff++].Get();
			if (Candidate && !Candidate->IsActorBeingDestroyed() && Candidate->GetTeam() == Team
				&& Candidate->FindComponentByClass<UGuLiBuildingLifecycleComponent>()->IsCompleted())
			{ Destination = Candidate; break; }
		}
		UnloadPointAttempts = 0;
		Factory = Destination;
		bManualReturnOrder |= ControlMode == EGuLiMiningControlMode::PlayerOrder;
		TaskState = EGuLiMiningTaskState::Idle;
		BehaviorResult = IsValid(Factory) ? Result::FactoryReady : Result::NoTarget;
		break;
	}
	case EGuLiMiningBehaviorAction::ReturnToFactory:
	{
		TaskState = EGuLiMiningTaskState::ReturningToFactory;
		if (!IsValid(Factory) || Factory->GetTeam() != Team) { BehaviorResult = Result::FactoryLost; break; }
		const auto Points = GetDeliveryPoints();
		if (Points.IsEmpty() || UnloadPointAttempts >= Points.Num()) { BehaviorResult = Result::FactoryUnreachable; break; }
        const FVector Target=Points[UnloadPointAttempts];
        ++UnloadPointAttempts;
		BehaviorResult = FindComponentByClass<UGuLiEngineeringTravelComponent>()->BeginWorkMove(Target, 50)
			? Result::Running : Result::Failed;
		 break;
	}
	case EGuLiMiningBehaviorAction::Unload:
		if (!IsValid(Factory) || Factory->GetTeam() != Team) { BehaviorResult = Result::FactoryLost; break; }
		FindComponentByClass<UGuLiEngineeringTravelComponent>()->StopAtSafePoint();
		TaskState = EGuLiMiningTaskState::Docking; DockingAccumulator = 0;
		BehaviorResult = Result::Running;  break;
	case EGuLiMiningBehaviorAction::FinishCycle: BeginGrace(GetCargoTotal() == 0); break;
	case EGuLiMiningBehaviorAction::Retry:
		if (BehaviorResult == Result::Failed) GetWorld()->GetSubsystem<UGuLiMiningVehicleManager>()->RejectSlot(*this,MiningSlot);
		FinishCurrentTarget(); TaskState = EGuLiMiningTaskState::Idle;
		BehaviorResult = Result::None;
		NextAutoRetryServerTime = UGuLiHybridActorComponent::BattleSeconds(this) + (GetCargoTotal() > 0 ? AutoRetrySeconds : .2f); break;
	case EGuLiMiningBehaviorAction::Fail: BeginGrace(false); break;
	case EGuLiMiningBehaviorAction::Complete: BeginGrace(true); break;
	}
}
