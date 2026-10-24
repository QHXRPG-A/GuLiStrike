#include "Gameplay/Navigation/Grid/GuLiGridQueries.h"
#include "Gameplay/Navigation/Grid/GuLiGridMotion.h"
#include "Gameplay/Navigation/Grid/GuLiGridNetwork.h"
#include "GuLiNetRelocation.h"

namespace
{
bool ReadDisplacement(const FGuLiHybridObjectState& O,GuLiNet::Relocation::Body& B)
{
    const auto* C=O.Capabilities.Find(TEXT("relocation_body"));
    return C && C->Version==GuLiNet::Relocation::Version
        && GuLiNet::Relocation::Decode({C->Data.GetData(),SIZE_T(C->Data.Num())},B)
        && !B.Airborne && !B.FollowParent && B.Displace && B.Locked && B.Step;
}
}
bool GuLiGrid::HoldsDisplacement(const FGuLiHybridObjectState& O)
{
    GuLiNet::Relocation::Body B;
    return ReadDisplacement(O,B) && (B.Step!=B.Acknowledged || !B.Accepted);
}
bool GuLiGrid::PendingDisplacementAt(const FGuLiHybridObjectState& O,const FGuLiGridSnapshot& G,const FRect& R,int32 Size)
{
    GuLiNet::Relocation::Body B;if (!ReadDisplacement(O,B) || B.Step==B.Acknowledged) return false;
    const auto Dest=FRect::At(G.AnchorAt(FVector(B.Destination.X,B.Destination.Y,B.Destination.Z),Size),Size);
    return Dest.Min==R.Min && Dest.Max==R.Max;
}

void GuLiGrid::ObserveRemoteBody(UWorld& World,const FGuLiHybridObjectState& O,const FVector& Position,int32 Size,bool bGround)
{
    auto* G=World.GetSubsystem<UGuLiGroundGridSubsystem>();if (!G || !G->IsReady()) return;
    if (!bGround || O.LifeState!=1 || O.bPhased)
    {G->Traffic().Remove(O.Key,O.SimulationOwner,O.OwnerEpoch);return;}
    const auto Actual=FGuLiGridMotion::Coverage(*G->Snapshot(),Position,Size);
    const auto* R=G->Traffic().Find(O.Key);
    if (!R || R->Owner!=O.SimulationOwner || R->OwnerEpoch!=O.OwnerEpoch)
    {
        FTrafficPriority Priority;
        if (O.Key.Kind==EGuLiHybridObjectKind::GroundPlayer) Priority.Class=ETrafficClass::DirectPlayer;
        G->Traffic().Admit(O.Key,O.SimulationOwner,O.OwnerEpoch,uint8(O.Team),Actual,Priority,Size);
    }
    else if (R->Occupied.Min!=Actual.Min || R->Occupied.Max!=Actual.Max) G->Traffic().Commit(O.Key,O.SimulationOwner,O.OwnerEpoch,Actual);
    if (auto N=G->Network()) N->PublishedPose(O.Key,O.SimulationOwner,O.OwnerEpoch,Actual);
}

void GuLiGrid::ObserveLocalBody(UWorld& World,const FGuLiHybridObjectKey& Key,const FVector& Position,int32 Size)
{
    auto* G=World.GetSubsystem<UGuLiGroundGridSubsystem>();if (!G || !G->IsReady()) return;
    auto N=G->Network();const auto* R=G->Traffic().Find(Key);
    // Ownership changes update this record before simulation can resume.
    if (!R || !N || !N->IsLocal(R->Owner) || N->HoldsBody(Key)) return;
    const auto Actual=FGuLiGridMotion::Coverage(*G->Snapshot(),Position,Size);
    if (R->Occupied.Min==Actual.Min && R->Occupied.Max==Actual.Max) return;
    const FGuid Owner=R->Owner;const uint64 Epoch=R->OwnerEpoch;
    if (G->Traffic().Commit(Key,Owner,Epoch,Actual)) N->PublishedPose(Key,Owner,Epoch,Actual);
}

TSharedPtr<const FGuLiGridSnapshot,ESPMode::ThreadSafe> FGuLiGridSnapshot::FromData(const UGuLiGroundGridData& D)
{
    FString Error;if (!D.Validate(Error)) return {};
    auto S=MakeShared<FGuLiGridSnapshot,ESPMode::ThreadSafe>();S->Cells=D.Cells;S->Origin=D.Origin;S->SourceHash=D.SourceHash;
    S->Chunks={FMath::DivideAndRoundUp(S->Cells.X,64),FMath::DivideAndRoundUp(S->Cells.Y,64)};
    auto T=MakeShared<FGuLiGridTerrain,ESPMode::ThreadSafe>();T->Heights=D.VertexHeights;T->Clearance=D.Clearance;
    S->Terrain=T;S->Heights=T->Heights;S->Clearance=T->Clearance;S->Obstacles.SetNum(S->Chunks.X*S->Chunks.Y);return S;
}
TSharedPtr<const FGuLiGridSnapshot,ESPMode::ThreadSafe> GuLiGrid::AuthoredSnapshot(UWorld& World)
{
    if (const auto* G=World.GetSubsystem<UGuLiGroundGridSubsystem>();G && G->IsReady()) return G->Snapshot();
#if WITH_EDITOR
    if (!World.IsGameWorld() && World.GetMapName().EndsWith(TEXT("LVL_CommanderMassPrototype")))
    {
        static TWeakObjectPtr<UGuLiGroundGridData> Data;
        static TSharedPtr<const FGuLiGridSnapshot,ESPMode::ThreadSafe> Cached;
        if (!Data.IsValid()) Data=LoadObject<UGuLiGroundGridData>(nullptr,TEXT("/Game/GuLiStrike/Data/Navigation/DA_CommanderGroundGrid.DA_CommanderGroundGrid"));
        if (Data.IsValid())
        {if (!Cached || Cached->SourceHash!=Data->SourceHash) Cached=FGuLiGridSnapshot::FromData(*Data);return Cached;}
    }
#endif
    return {};
}
