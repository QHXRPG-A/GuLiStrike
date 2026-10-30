#include "Gameplay/Navigation/Grid/GuLiGridMotion.h"
#include "Gameplay/Navigation/Grid/GuLiGroundGridSubsystem.h"
#include "Gameplay/Navigation/Grid/GuLiGridNetwork.h"
#include "Gameplay/Navigation/Grid/GuLiGridQueries.h"

GuLiGrid::FRect FGuLiGridMotion::Coverage(const FGuLiGridSnapshot& G,const FVector& P,int32 N)
{
    const double X=(P.X-G.Origin.X)/300.,Y=(P.Y-G.Origin.Y)/300.,Half=N*.5;
    return {{FMath::FloorToInt32(X-Half+1.e-7),FMath::FloorToInt32(Y-Half+1.e-7)},
        {FMath::CeilToInt32(X+Half-1.e-7),FMath::CeilToInt32(Y+Half-1.e-7)}};
}
bool FGuLiGridMotion::Bind(UGuLiGroundGridSubsystem& G,const FGuLiHybridObjectKey& InKey,const FGuid& InOwner,uint64 InEpoch,
    uint8 InTeam,const FVector& P,int32 Size,TOptional<EGuLiUnitMovementLayer> Layer)
{
    const auto S=G.Snapshot();if (!S || !InKey.IsValid() || P.ContainsNaN() || Size<1 || Size>64) return false;
    if (!G.Traffic().Admit(InKey,InOwner,InEpoch,InTeam,Coverage(*S,P,Size),State.Priority,Size,Layer.IsSet()?Layer.GetValue():GuLiGrid::MovementLayerFor(*G.GetWorld(),InKey))) return false;
    Key=InKey;Owner=InOwner;Epoch=InEpoch;Team=InTeam;State.Footprint=Size;
    State.Anchor=S->AnchorAt(P,Size);State.bStepCommitted=false;
    if (auto Net=G.Network()) Net->Observe(*G.Traffic().Find(Key));
    return true;
}
void FGuLiGridMotion::Stop(UGuLiGroundGridSubsystem& G,const FVector& P,double Now,EGuLiGridMoveEnd Reason)
{
    const auto S=G.Snapshot();if (!S) return;
    State.bUserStopped=Reason==EGuLiGridMoveEnd::Stopped;
    State.State=GuLiGrid::EMoveState::Parked;State.bStepCommitted=false;
    State.Anchor=S->AnchorAt(P,State.Footprint);End=Reason;
    G.Traffic().Cancel(Key,Owner,Epoch);
    G.Traffic().Commit(Key,Owner,Epoch,Coverage(*S,P,State.Footprint));
    if (auto Net=G.Network()) Net->Stop(Key,Coverage(*S,P,State.Footprint));
}
