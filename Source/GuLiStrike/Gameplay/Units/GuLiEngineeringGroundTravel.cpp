#include "Gameplay/Units/GuLiEngineeringTravelComponent.h"
#include "Gameplay/Units/GuLiExternalUnitControlComponent.h"
#include "AIController.h"
#include "Gameplay/Navigation/GuLiEngineeringPathSubsystem.h"
#include "Gameplay/Navigation/GuLiDynamicObstacleRegistry.h"
#include "NavigationSystem.h"
#include "NavigationData.h"
#include "NavFilters/NavigationQueryFilter.h"
#include "Navigation/PathFollowingComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/World.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Network/GuLiHybridActorComponent.h"
#include "Gameplay/Navigation/Grid/GuLiGroundGridSubsystem.h"
#include "Components/CapsuleComponent.h"
#include "Commander/Mass/GuLiBattleAuthoritySubsystem.h"

FString UGuLiEngineeringTravelComponent::GetTravelDebug() const
{
	return FString::Printf(TEXT("Ground=%d Transit=%d Work=%d Legs=%d Unreachable=%d Mass=%u Goal=%s"),
		int32(GroundStatus),int32(State.Phase),bWorkJourney,WorkLegs.Num(),bPathUnreachable,
		MassEntityId.Value,
		*GroundGoal.ToString());
}

void UGuLiEngineeringTravelComponent::EndPlay(const EEndPlayReason::Type Reason)
{
    if (auto* Grid=GetWorld()->GetSubsystem<UGuLiGroundGridSubsystem>();Grid && Grid->IsReady())
        Grid->Traffic().Remove(GridMotion.Key,GridMotion.Owner,GridMotion.Epoch);
	if (auto* Service = GetWorld()->GetSubsystem<UGuLiEngineeringPathSubsystem>()) Service->Cancel(*this);
    if (auto* Authority=GetWorld()->GetSubsystem<UGuLiBattleAuthoritySubsystem>()) Authority->DetachEngineeringMass(MassEntityId);
    MassEntityId={};
	Super::EndPlay(Reason);
}
bool UGuLiEngineeringTravelComponent::PrepareGroundMove(const FVector& Target, float Radius, FGuLiPreparedGroundMove& Out) const
{
	Out = {};
	if (!UGuLiHybridActorComponent::HasSimulation(GetOwner()) || Target.ContainsNaN() || Radius < 0 || !Control || Control->AreActionsLocked() || IsRouting()) return false;
	Out.Request = FAIMoveRequest(Target);
	Out.Request.SetUsePathfinding(true); Out.Request.SetAllowPartialPath(false);
	Out.Request.SetProjectGoalLocation(false); Out.Request.SetAcceptanceRadius(Radius);
	Out.Request.SetReachTestIncludesAgentRadius(false); Out.Request.SetCanStrafe(true);
	Out.Request.SetNavigationFilter(Controller().GetDefaultNavigationFilterClass());
	Out.bNeedsPath = true;
	return true;
}
bool UGuLiEngineeringTravelComponent::BeginMove(const FVector& Target, float Radius)
{
	FGuLiPreparedGroundMove Prepared;
	if (!PrepareGroundMove(Target,Radius,Prepared)) return false;
	CancelGroundMove();
	return CommitGroundMove(Prepared);
}
bool UGuLiEngineeringTravelComponent::MoveOnGround(const FVector& Target, float Radius)
{
	FGuLiPreparedGroundMove Prepared;
	return PrepareGroundMove(Target,Radius,Prepared) && CommitGroundMove(Prepared);
}
bool UGuLiEngineeringTravelComponent::CommitGroundMove(const FGuLiPreparedGroundMove& Prepared)
{
	if (!UGuLiHybridActorComponent::HasSimulation(GetOwner()) || Control->AreActionsLocked() || IsRouting()) return false;
	GroundGoal = Prepared.Request.GetGoalLocation(); GroundAcceptance = Prepared.Request.GetAcceptanceRadius();
	if (Prepared.bNeedsPath)
	{
		bPathUnreachable=false;
		GroundStatus = EGuLiEngineeringMoveStatus::WaitingForPath;
		QueuedAt = FPlatformTime::Seconds();
		GetWorld()->GetSubsystem<UGuLiEngineeringPathSubsystem>()->Enqueue(*this);
		return true;
	}
    if (!EnsureMassEntity()) return false;
    const bool Accepted=GetWorld()->GetSubsystem<UGuLiBattleAuthoritySubsystem>()->MoveEngineeringMass(
        MassEntityId,GroundGoal,GroundAcceptance,GridMotion.State.BlockedSeconds);
    if (Accepted) GridMotion.State.BlockedSeconds=0;
    bPathUnreachable=!Accepted;
    GroundStatus=Accepted?EGuLiEngineeringMoveStatus::Moving:EGuLiEngineeringMoveStatus::Failed;
    return Accepted;
}

bool UGuLiEngineeringTravelComponent::HasQueuedPath() const
{
	return ReplacementTaskId ? ReplacementStatus == EGuLiEngineeringPathPreparation::Pending : IsWaitingForPath();
}
EGuLiEngineeringPathPreparation UGuLiEngineeringTravelComponent::PrepareReplacementMove(
	uint32 TaskId, const FVector& Target, float Radius, FGuLiPreparedGroundMove& Out)
{
	if (!TaskId || !PrepareGroundMove(Target,Radius,Out)) return EGuLiEngineeringPathPreparation::Failed;
	if (ReplacementTaskId != TaskId || !ReplacementGoal.Equals(Target,1) || ReplacementRadius != Radius)
	{
		ReplacementTaskId=TaskId; ReplacementGoal=Target; ReplacementRadius=Radius; ReplacementPath={};
		ReplacementStatus=EGuLiEngineeringPathPreparation::Pending; ReplacementQueuedAt=FPlatformTime::Seconds();
		GetWorld()->GetSubsystem<UGuLiEngineeringPathSubsystem>()->Enqueue(*this);
	}
	if (ReplacementStatus == EGuLiEngineeringPathPreparation::Ready) Out=ReplacementPath;
	return ReplacementStatus;
}
void UGuLiEngineeringTravelComponent::CancelReplacementMove()
{
	ReplacementTaskId=0; ReplacementPath={};
	if (auto* Service=GetWorld()->GetSubsystem<UGuLiEngineeringPathSubsystem>())
	{
		Service->Cancel(*this);
		if (IsWaitingForPath()) Service->Enqueue(*this);
	}
}
void UGuLiEngineeringTravelComponent::TickGroundMove()
{
    RefreshMassAuthority();if (!MassEntityId.IsValid() || Control->AreActionsLocked()) return;
    if (GroundStatus==EGuLiEngineeringMoveStatus::Moving)
    {
        FGuLiSoldierNavigationDebug Debug;
        if (GetWorld()->GetSubsystem<UGuLiBattleAuthoritySubsystem>()->TryGetSoldierNavigationDebug(MassEntityId,Debug))
        {
            if (Debug.State==EGuLiSoldierNavigationState::Arrived) GroundStatus=EGuLiEngineeringMoveStatus::Arrived;
            else if (Debug.State==EGuLiSoldierNavigationState::Blocked || !Debug.ActiveOrderId)
            {GroundStatus=EGuLiEngineeringMoveStatus::Failed;bPathUnreachable=true;}
        }
    }
    if (bWorkJourney && GroundStatus==EGuLiEngineeringMoveStatus::Arrived && !WorkLegs.IsEmpty()) AdvanceWorkRoute();
}
void UGuLiEngineeringTravelComponent::CancelGroundMove()
{
    GridMotion={};
    if (MassEntityId.IsValid() && (GroundStatus==EGuLiEngineeringMoveStatus::Moving || GroundStatus==EGuLiEngineeringMoveStatus::WaitingForPath))
        if (auto* Authority=GetWorld()->GetSubsystem<UGuLiBattleAuthoritySubsystem>()) Authority->StopTaskSoldiers(MakeArrayView(&MassEntityId,1));
	if (auto* Service = GetWorld()->GetSubsystem<UGuLiEngineeringPathSubsystem>()) Service->Cancel(*this);
	ReplacementTaskId=0; ReplacementPath={};
	GroundStatus = EGuLiEngineeringMoveStatus::Idle;
	WorkLegs.Reset(); bWorkJourney = false;
}
bool UGuLiEngineeringTravelComponent::StopAtSafePoint()
{
	if (!UGuLiHybridActorComponent::HasSimulation(GetOwner()) || IsInTransit()) return false;
	CancelGroundMove();
	if (const auto* Pawn=Cast<APawn>(GetOwner())) if (auto* AI=Cast<AAIController>(Pawn->GetController())) AI->StopMovement();
	if (auto* Character = Cast<ACharacter>(GetOwner())) Character->GetCharacterMovement()->StopMovementImmediately();
	return true;
}
