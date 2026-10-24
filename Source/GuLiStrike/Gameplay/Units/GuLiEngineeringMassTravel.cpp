#include "Gameplay/Units/GuLiEngineeringTravelComponent.h"
#include "Gameplay/Units/GuLiExternalUnitControlComponent.h"
#include "Commander/Mass/GuLiBattleAuthoritySubsystem.h"
#include "Gameplay/Navigation/Grid/GuLiGroundGridSubsystem.h"
#include "NavigationSystem.h"
#include "Network/GuLiHybridActorComponent.h"
#include "GameFramework/Character.h"
#include "Engine/World.h"

bool UGuLiEngineeringTravelComponent::EnsureMassEntity()
{
    auto* Authority=GetWorld()->GetSubsystem<UGuLiBattleAuthoritySubsystem>();
    auto* Carrier=Cast<ACharacter>(GetOwner());const auto* Binding=UGuLiHybridActorComponent::Find(Carrier);
    if (!Authority || !Carrier || !Binding) return false;
    if (!MassEntityId.IsValid()) MassEntityId=Authority->AttachEngineeringMass(*Carrier,Binding->GetState(),uint16(Vehicle().GetUnitTypeId()));
    return MassEntityId.IsValid();
}
void UGuLiEngineeringTravelComponent::RefreshMassAuthority()
{
    if (EnsureMassEntity()) GetWorld()->GetSubsystem<UGuLiBattleAuthoritySubsystem>()->SyncEngineeringMass(MassEntityId);
}
bool UGuLiEngineeringTravelComponent::ProcessQueuedPath()
{
    if (!HasQueuedPath()) return false;
    if (!EnsureMassEntity() || !UGuLiHybridActorComponent::HasSimulation(GetOwner()) || !Control || Control->AreActionsLocked() || IsRouting()) return false;
    auto& Grid=*GetWorld()->GetSubsystem<UGuLiGroundGridSubsystem>();auto& Budget=Grid.FrameBudget();
    FGuLiNavigationWorkBudget::FScope Scope(Budget);if (!Budget.TakeProjection()) return false;
    const FVector Target=ReplacementTaskId?ReplacementGoal:GroundGoal;
    const float Radius=ReplacementTaskId?ReplacementRadius:GroundAcceptance;
    FGuLiPreparedGroundMove Prepared;FNavLocation Goal;
    const auto* Navigation=FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
    const auto* NavData=Navigation?Navigation->GetNavDataForAgentName(TEXT("CommanderSoldier")):nullptr;
    FGuLiNavigationWorkBudget::FQueryScope Query(Budget);
    // Work points may be inside a completed factory. Its placement footprint is
    // not a movement obstacle; retain the requested XY on the actual walkable surface.
    const bool Valid=NavData && PrepareGroundMove(Target,Radius,Prepared)
        && Navigation->ProjectPointToNavigation(Target,Goal,FVector(.1,.1,5000),NavData)
        && Goal.NodeRef && FVector::DistSquared2D(Target,Goal.Location)<=.01;
    Prepared.bNeedsPath=false;
    if (Valid) {Goal.Location.X=Target.X;Goal.Location.Y=Target.Y;Prepared.Request.SetGoalLocation(Goal.Location);}
    LastQueueWaitMilliseconds=(FPlatformTime::Seconds()-(ReplacementTaskId?ReplacementQueuedAt:QueuedAt))*1000.;
    if (ReplacementTaskId)
    {ReplacementPath=Prepared;ReplacementStatus=Valid?EGuLiEngineeringPathPreparation::Ready:EGuLiEngineeringPathPreparation::Failed;}
    else if (!Valid || !CommitGroundMove(Prepared))
    {bPathUnreachable=true;GroundStatus=EGuLiEngineeringMoveStatus::Failed;}
    return true;
}
