#include "Gameplay/Units/GuLiEngineeringTravelComponent.h"
#include "Gameplay/Units/GuLiExternalUnitControlComponent.h"
#include "Gameplay/Units/GuLiEngineeringAIController.h"
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

FString UGuLiEngineeringTravelComponent::GetTravelDebug() const
{
	return FString::Printf(TEXT("Ground=%d Transit=%d Work=%d Legs=%d Unreachable=%d FailedPaths=%d FailedPathBytes=%llu PathPoints=%d Goal=%s"),
		int32(GroundStatus),int32(State.Phase),bWorkJourney,WorkLegs.Num(),bPathUnreachable,
		FailedPaths.Num(),uint64(FailedPaths.GetAllocatedSize()),ActiveGroundPath.IsValid() ? ActiveGroundPath->GetPathPoints().Num() : 0,
		*GroundGoal.ToString());
}

void UGuLiEngineeringTravelComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	if (auto* Service = GetWorld()->GetSubsystem<UGuLiEngineeringPathSubsystem>()) Service->Cancel(*this);
	if (const auto* Pawn = Cast<APawn>(GetOwner()))
		if (auto* AI = Cast<AAIController>(Pawn->GetController())) AI->GetPathFollowingComponent()->OnRequestFinished.RemoveAll(this);
	Super::EndPlay(Reason);
}
bool UGuLiEngineeringTravelComponent::PrepareGroundMove(const FVector& Target, float Radius, FGuLiPreparedGroundMove& Out) const
{
	Out = {};
	if (!GetOwner()->HasAuthority() || Target.ContainsNaN() || Radius < 0 || !Control || Control->AreActionsLocked() || IsRouting()) return false;
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
	if (!GetOwner()->HasAuthority() || Control->AreActionsLocked() || IsRouting()) return false;
	GroundGoal = Prepared.Request.GetGoalLocation(); GroundAcceptance = Prepared.Request.GetAcceptanceRadius();
	if (Prepared.bNeedsPath)
	{
		bPathUnreachable=false; bGroundRepath=false;
		GroundStatus = EGuLiEngineeringMoveStatus::WaitingForPath;
		QueuedAt = FPlatformTime::Seconds(); ActiveGroundPath.Reset();
		GetWorld()->GetSubsystem<UGuLiEngineeringPathSubsystem>()->Enqueue(*this);
		return true;
	}
	if (Prepared.bAlreadyAtGoal)
	{ Controller().StopMovement(); ActiveGroundPath.Reset(); GroundStatus = EGuLiEngineeringMoveStatus::Arrived; return true; }
	if (!Prepared.Path.IsValid() || !Prepared.Path->IsValid() || Prepared.Path->IsPartial()) return false;
	ActiveGroundPath = Prepared.Path;
	// Navigation invalidation is resubmitted through the same shared budget.
	ActiveGroundPath->EnableRecalculationOnInvalidation(false);
	Controller().GetPathFollowingComponent()->OnRequestFinished.RemoveAll(this);
	Controller().GetPathFollowingComponent()->OnRequestFinished.AddUObject(this,&ThisClass::OnGroundMoveFinished);
	GroundRequestId = Controller().RequestMove(Prepared.Request,Prepared.Path);
	Controller().bAllowStrafe = true;
	GroundStatus = GroundRequestId.IsValid() ? EGuLiEngineeringMoveStatus::Moving : EGuLiEngineeringMoveStatus::Failed;
	return GroundRequestId.IsValid();
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
	if (ReplacementStatus == EGuLiEngineeringPathPreparation::Ready)
	{
		if (!ReplacementPath.bAlreadyAtGoal && (!ReplacementPath.Path.IsValid() || !ReplacementPath.Path->IsValid()))
		{
			ReplacementStatus=EGuLiEngineeringPathPreparation::Pending; ReplacementQueuedAt=FPlatformTime::Seconds();
			GetWorld()->GetSubsystem<UGuLiEngineeringPathSubsystem>()->Enqueue(*this);
		}
		else Out=ReplacementPath;
	}
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
bool UGuLiEngineeringTravelComponent::ProcessQueuedPath()
{
	if (!HasQueuedPath()) return false;
	const auto* Pawn=Cast<APawn>(GetOwner());
	if (!Pawn || !Pawn->HasAuthority() || !Pawn->GetController())
	{
		if (ReplacementTaskId) ReplacementStatus=EGuLiEngineeringPathPreparation::Failed;
		else GroundStatus=EGuLiEngineeringMoveStatus::Failed;
		return false;
	}
	if (Control->AreActionsLocked() || IsRouting()) return false;
	auto* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	if (!Nav || UNavigationSystemV1::IsNavigationBeingBuiltOrLocked(GetWorld())) return false;
	bool bSuccess=false;
	if (ReplacementTaskId)
	{
		const bool bQueried=ComputeQueuedPath(ReplacementGoal,ReplacementRadius,ReplacementQueuedAt,ReplacementPath,bSuccess);
		ReplacementStatus=bSuccess ? EGuLiEngineeringPathPreparation::Ready : EGuLiEngineeringPathPreparation::Failed;
		if (!bSuccess)
			if (auto* Crowd=Cast<UGuLiEngineeringCrowdFollowingComponent>(Controller().GetPathFollowingComponent())) Crowd->RefreshParticipation();
		return bQueried;
	}
	FGuLiPreparedGroundMove Prepared;
	const bool bQueried=ComputeQueuedPath(GroundGoal,GroundAcceptance,QueuedAt,Prepared,bSuccess);
	if (!bSuccess || !CommitGroundMove(Prepared))
	{
		bPathUnreachable=!bSuccess; GroundStatus=EGuLiEngineeringMoveStatus::Failed;
		if (auto* Crowd=Cast<UGuLiEngineeringCrowdFollowingComponent>(Controller().GetPathFollowingComponent())) Crowd->RefreshParticipation();
		if (bWorkJourney && !WorkLegs.IsEmpty()) { WorkLegs.Reset(); MoveOnGround(WorkGoal,WorkAcceptance); }
	}
	return bQueried;
}
bool UGuLiEngineeringTravelComponent::ComputeQueuedPath(const FVector& Target, float Radius, double RequestTime,
	FGuLiPreparedGroundMove& Prepared, bool& bSuccess)
{
	bSuccess=false; Prepared={};
	auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	const auto* Pawn = CastChecked<APawn>(GetOwner());
	const uint32 Profile=HashCombine(GetTypeHash(Controller().GetDefaultNavigationFilterClass().Get()),
		HashCombine(GetTypeHash(Pawn->GetNavAgentPropertiesRef().AgentRadius),GetTypeHash(Pawn->GetNavAgentPropertiesRef().AgentHeight)));
	if (FailureProfile!=Profile) { FailedPaths.Reset(); FailureProfile=Profile; }
	const FVector Start = Pawn->GetNavAgentLocation();
	const auto* Obstacles=GetWorld()->GetSubsystem<UGuLiDynamicObstacleRegistrySubsystem>();
	const uint32 Revision = Obstacles->GetStaticRevision();
	const double Now = GetWorld()->GetTimeSeconds();
	FailedPaths.RemoveAll([&](const auto& Failure) {
		FBox Bounds(ForceInit); Bounds+=Failure.Start; Bounds+=Failure.Goal;
		return Failure.RetryAt<=Now || Obstacles->HasStaticChangesSince(Failure.Revision,Bounds.ExpandBy(2000)); });
	if (FailedPaths.ContainsByPredicate([&](const auto& F) { return F.Start.Equals(Start,100) && F.Goal.Equals(Target,1); })) return false;
	LastQueueWaitMilliseconds = (FPlatformTime::Seconds()-RequestTime)*1000;
	if (!PrepareGroundMove(Target,Radius,Prepared)) return false;
	Prepared.bNeedsPath = false;
	FNavLocation Goal;
	const auto* Data = Nav->GetNavDataForProps(Pawn->GetNavAgentPropertiesRef(),Start);
	bool bReady = Data && Nav->ProjectPointToNavigation(Target,Goal,FVector(100,100,5000),Data);
	if (bReady)
	{
		Prepared.Request.SetGoalLocation(Goal.Location);
		Prepared.bAlreadyAtGoal = Controller().GetPathFollowingComponent()->HasReached(Prepared.Request);
		if (!Prepared.bAlreadyAtGoal)
		{
			FPathFindingQuery Query;
			if (Controller().BuildPathfindingQuery(Prepared.Request,Query))
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(GuLiEngineering_ActualPathQuery);
				const double Before=FPlatformTime::Seconds();
				Controller().FindPathForMoveRequest(Prepared.Request,Query,Prepared.Path);
				const auto Origin=ReplacementTaskId ? EGuLiEngineeringPathOrigin::Manual : bGroundRepath
					? EGuLiEngineeringPathOrigin::Repath : bWorkJourney ? EGuLiEngineeringPathOrigin::Work : EGuLiEngineeringPathOrigin::Manual;
				GetWorld()->GetSubsystem<UGuLiEngineeringPathSubsystem>()->RecordPathQuery(Origin,(FPlatformTime::Seconds()-Before)*1000);
			}
			bReady = Prepared.Path.IsValid() && Prepared.Path->IsValid() && !Prepared.Path->IsPartial();
			if (bReady) Prepared.Path->EnableRecalculationOnInvalidation(false);
		}
	}
	bSuccess=bReady;
	if (!bReady)
	{
		FailedPaths.Add({Start,Target,TNumericLimits<double>::Max(),Revision});
		if (FailedPaths.Num()>32) FailedPaths.RemoveAt(0);
	}
	return true;
}
void UGuLiEngineeringTravelComponent::OnGroundMoveFinished(FAIRequestID Id, const FPathFollowingResult& Result)
{
	if (Id != GroundRequestId || GroundStatus != EGuLiEngineeringMoveStatus::Moving) return;
	bPathUnreachable=false;
	if (ActiveGroundPath.IsValid() && !ActiveGroundPath->IsValid())
	{ MoveOnGround(GroundGoal,GroundAcceptance); bGroundRepath=true; return; }
	GroundStatus = Result.IsSuccess() ? EGuLiEngineeringMoveStatus::Arrived : EGuLiEngineeringMoveStatus::Failed;
}
void UGuLiEngineeringTravelComponent::TickGroundMove()
{
	if (Control->AreActionsLocked()) return;
	if (GroundStatus == EGuLiEngineeringMoveStatus::Moving && ActiveGroundPath.IsValid() && !ActiveGroundPath->IsValid())
	{ Controller().StopMovement(); MoveOnGround(GroundGoal,GroundAcceptance); bGroundRepath=true; }
	if (bWorkJourney && GroundStatus == EGuLiEngineeringMoveStatus::Arrived && !WorkLegs.IsEmpty()) AdvanceWorkRoute();
}
void UGuLiEngineeringTravelComponent::CancelGroundMove()
{
	if (auto* Service = GetWorld()->GetSubsystem<UGuLiEngineeringPathSubsystem>()) Service->Cancel(*this);
	ReplacementTaskId=0; ReplacementPath={};
	GroundStatus = EGuLiEngineeringMoveStatus::Idle; ActiveGroundPath.Reset();
	WorkLegs.Reset(); bWorkJourney = false;
}
bool UGuLiEngineeringTravelComponent::StopAtSafePoint()
{
	if (!GetOwner()->HasAuthority() || IsInTransit()) return false;
	CancelGroundMove();
	if (const auto* Pawn=Cast<APawn>(GetOwner())) if (auto* AI=Cast<AAIController>(Pawn->GetController())) AI->StopMovement();
	if (auto* Character = Cast<ACharacter>(GetOwner())) Character->GetCharacterMovement()->StopMovementImmediately();
	return true;
}
bool UGuLiEngineeringTravelComponent::FindGroundPath(const FVector& Target, float& Length) const
{
	// Compatibility callers may inspect the current prepared path, never initiate a hidden query.
	if (!Target.Equals(GroundGoal,1) || !ActiveGroundPath.IsValid() || !ActiveGroundPath->IsValid()) return false;
	Length = ActiveGroundPath->GetLength(); return true;
}
