#include "Gameplay/Units/GuLiEngineeringTravelComponent.h"
#include "Gameplay/Units/GuLiExternalUnitControlComponent.h"
#include "Gameplay/Navigation/GuLiEngineeringPathSubsystem.h"
#include "Gameplay/Navigation/GuLiDynamicObstacleRegistry.h"
#include "Gameplay/Units/Private/GuLiHybridEngineeringState.h"
#include "Network/GuLiNetworkSettings.h"
#include "Network/GuLiHybridActorComponent.h"
#include "AIController.h"
#include "Navigation/PathFollowingComponent.h"
#include "NavFilters/NavigationQueryFilter.h"
#include "Engine/World.h"
#include "Gameplay/Navigation/Grid/GuLiGridMotionState.h"
#include "Commander/Mass/GuLiBattleAuthoritySubsystem.h"

namespace GuLiEngineeringCheckpoint
{
constexpr uint32 Version=2;
struct FLeg {int32 Source=INDEX_NONE,Target=INDEX_NONE; FVector Entry=FVector::ZeroVector;};
struct FFailure {FVector Start=FVector::ZeroVector,Goal=FVector::ZeroVector; double RetryAt=0; uint32 Revision=0;};
struct FState
{
    FGuLiStrongholdTransitState Transit;
    TArray<int32> Passed;
    TArray<FLeg> Legs;
    TArray<FFailure> Failures;
    float ExitAccumulator=0,GroundRadius=100,WorkRadius=100,ReplacementRadius=100;
    int32 ExitCursor=0;
    EGuLiEngineeringMoveStatus Ground=EGuLiEngineeringMoveStatus::Idle;
    FVector GroundGoal=FVector::ZeroVector,WorkGoal=FVector::ZeroVector,ReplacementGoal=FVector::ZeroVector;
    double QueueAge=0,ReplacementAge=0;
    bool Work=false,Unreachable=false,Repath=false;
    uint32 Replacement=0;
    FGuLiGridMotion GridMotion;
    GuLiGrid::FTrafficPriority GridPriority;
    bool GridQueued=false,GridWorkPaused=false;
};
void Serialize(FGuLiHybridStateArchive& A,FState& V)
{
    uint32 Schema=Version; A.Value(Schema); if (Schema!=Version) A.Reject();
    GuLiHybridEngineeringState::Transit(A,V.Transit);
    A.Array(V.Passed,512,[](auto& Ar,int32& N){Ar.Value(N); if (N<0) Ar.Reject();});
    A.Array(V.Legs,512,[](auto& Ar,FLeg& L)
    {Ar.Value(L.Source); Ar.Value(L.Target); Ar.Value(L.Entry); if (L.Source<0 || L.Target<0 || L.Source==L.Target) Ar.Reject();});
    A.Array(V.Failures,32,[](auto& Ar,FFailure& F)
    {Ar.Value(F.Start); Ar.Value(F.Goal); Ar.Value(F.RetryAt); Ar.Value(F.Revision); if (F.RetryAt<0) Ar.Reject();});
    A.Value(V.ExitAccumulator); A.Value(V.ExitCursor); A.Enum(V.Ground,uint8(EGuLiEngineeringMoveStatus::Failed));
    A.Value(V.GroundGoal); A.Value(V.GroundRadius); A.Value(V.WorkGoal); A.Value(V.WorkRadius);
    A.Value(V.Work); A.Value(V.Unreachable); A.Value(V.Repath); A.Value(V.QueueAge);
    A.Value(V.Replacement); A.Value(V.ReplacementGoal); A.Value(V.ReplacementRadius); A.Value(V.ReplacementAge);
    GuLiHybridState::GridMotion(A,V.GridMotion);A.Value(V.GridQueued);A.Value(V.GridWorkPaused);
    A.Enum(V.GridPriority.Class,uint8(GuLiGrid::ETrafficClass::DirectPlayer));
    A.Value(V.GridPriority.AcceptedSequence);A.Value(V.GridPriority.Command);
    if (V.ExitAccumulator<0 || V.ExitAccumulator>.101 || V.ExitCursor<0 || V.GroundRadius<0 || V.WorkRadius<0
        || V.ReplacementRadius<0 || V.QueueAge<0 || V.ReplacementAge<0 || (!V.Work && !V.Legs.IsEmpty())) A.Reject();
}
bool Decode(TConstArrayView<uint8> Bytes,FState& V)
{FGuLiHybridStateArchive A(Bytes); Serialize(A,V); return A.Done();}
}

bool UGuLiEngineeringTravelComponent::CaptureHybridTravel(TArray<uint8>& Bytes) const
{
    using namespace GuLiEngineeringCheckpoint;
    if (!UGuLiNetworkSettings::IsHybrid(GetWorld())) return false;
    FState V; V.Transit=State; V.Passed=PassedNodes; V.ExitAccumulator=ExitQueryAccumulator; V.ExitCursor=ExitCandidateCursor;
    V.Ground=GroundStatus; V.GroundGoal=GroundGoal; V.GroundRadius=GroundAcceptance;
    V.Work=bWorkJourney; V.WorkGoal=WorkGoal; V.WorkRadius=WorkAcceptance; V.Unreachable=bPathUnreachable;
    const double Now=FPlatformTime::Seconds(); V.QueueAge=QueuedAt>0?FMath::Max(0.,Now-QueuedAt):0;
    V.Replacement=ReplacementTaskId; V.ReplacementGoal=ReplacementGoal; V.ReplacementRadius=ReplacementRadius;
    V.ReplacementAge=ReplacementQueuedAt>0?FMath::Max(0.,Now-ReplacementQueuedAt):0;
    V.GridMotion=GridMotion;
    if (const auto* Authority=GetWorld()->GetSubsystem<UGuLiBattleAuthoritySubsystem>())
        Authority->CaptureEngineeringMass(MassEntityId,V.GridMotion);
    V.GridMotion.State.RetryAt=FMath::Max(0.,V.GridMotion.State.RetryAt-UGuLiHybridActorComponent::BattleSeconds(GetOwner()));
    for (const auto& L:WorkLegs) V.Legs.Add({L.Source,L.Target,L.Entry});
    FGuLiHybridStateArchive A; GuLiEngineeringCheckpoint::Serialize(A,V); return A.Finish(Bytes);
}
bool UGuLiEngineeringTravelComponent::ValidateHybridTravel(TConstArrayView<uint8> Bytes) const
{GuLiEngineeringCheckpoint::FState V; return GuLiEngineeringCheckpoint::Decode(Bytes,V);}
bool UGuLiEngineeringTravelComponent::ActivateRestoredTravel()
{
    if (!bRestoredTravelPending || !UGuLiHybridActorComponent::HasSimulation(GetOwner())) return true;
    if (IsWaitingForPath() || ReplacementTaskId)
    {
        auto* Service=GetWorld()->GetSubsystem<UGuLiEngineeringPathSubsystem>();
        if (!Service) return false;
        Service->Enqueue(*this);
    }
    bRestoredTravelPending=false;OnRep_State();return true;
}
bool UGuLiEngineeringTravelComponent::RestoreHybridTravel(TConstArrayView<uint8> Bytes,bool bActivate)
{
    using namespace GuLiEngineeringCheckpoint;
    FState V; if (!Decode(Bytes,V) || !UGuLiNetworkSettings::IsHybrid(GetWorld())) return false;
    auto* Service=GetWorld()->GetSubsystem<UGuLiEngineeringPathSubsystem>();
    if (bActivate) return ActivateRestoredTravel();
    if (Service) Service->Cancel(*this);
    State=MoveTemp(V.Transit); PassedNodes=MoveTemp(V.Passed); ExitQueryAccumulator=V.ExitAccumulator; ExitCandidateCursor=V.ExitCursor;
    GroundStatus=(V.Ground==EGuLiEngineeringMoveStatus::Moving)?EGuLiEngineeringMoveStatus::WaitingForPath:V.Ground;
    GroundGoal=V.GroundGoal; GroundAcceptance=V.GroundRadius;
    // Keep the v2 wire layout; rebuild active ground work through the ordinary path queue.
    GridMotion=V.GridMotion;GridMotion.State.bStepCommitted=false;
    if (!GridMotion.State.bUserStopped && (V.GridQueued || V.GridWorkPaused)) GroundStatus=EGuLiEngineeringMoveStatus::WaitingForPath;
    bWorkJourney=V.Work; WorkGoal=V.WorkGoal; WorkAcceptance=V.WorkRadius; bPathUnreachable=V.Unreachable;
    WorkLegs.Reset(); for (const auto& L:V.Legs) WorkLegs.Add({L.Source,L.Target,L.Entry});
    RouteCacheRevision=0; RouteCacheStart=FVector::ZeroVector; RouteCosts.Reset(); RouteParents.Reset(); RouteSources.Reset(); RouteCacheTeam=EGuLiTeam::Unassigned;
    const double Now=FPlatformTime::Seconds(); QueuedAt=Now-V.QueueAge;
    ReplacementTaskId=V.Replacement; ReplacementGoal=V.ReplacementGoal; ReplacementRadius=V.ReplacementRadius;
    ReplacementQueuedAt=Now-V.ReplacementAge; ReplacementStatus=EGuLiEngineeringPathPreparation::Pending; ReplacementPath={};
    bRestoredTravelPending=true;SetComponentTickEnabled(false);return true;
}
