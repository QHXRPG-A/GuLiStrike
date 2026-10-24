#include "Gameplay/Teleport/Private/GuLiGridLandingPlan.h"
#include "Gameplay/Navigation/Grid/GuLiGridQueries.h"
#include "Gameplay/Navigation/Grid/GuLiGridSlots.h"
#include "Gameplay/Skills/GuLiSkillTargeting.h"
#include "Engine/World.h"

namespace GuLiHybridTeleport
{
struct FGridLandingPlan::FImpl
{
    struct FEntry
    {
        FGuLiHybridObjectKey Key,Parent;
        uint64 OwnerEpoch=0,ControlEpoch=0;
        FGuLiExternalBody Body;
        FTransform Original,Landing;
        bool Placed=false;
    };
    GuLiNet::Id Cast;
    uint32 Step=0;
    bool Returning=false;
    FVector Center=FVector::ZeroVector;
    double Radius=0;
    int32 Gather=0,AirCursor=0,AirPass=0,Attempt=0,FinishCursor=0,GroundCount=0,GroundPlaced=0;
    enum class EStage : uint8 { Gather,Ground,Air,Finish,Done,Failed } Stage=EStage::Gather;
    TArray<FEntry> Units;
    TMap<FGuLiHybridObjectKey,int32> Indices;
    TMap<FGuLiHybridObjectKey,TArray<FGuLiHybridObjectKey>> Siblings;
    TSharedPtr<FGuLiGridSlots> Slots;
    FCollisionQueryParams Collision{SCENE_QUERY_STAT(GridRelocation),false};
    std::vector<Net::Member> Output;

    bool Blocked(UWorld& W,UGuLiGroundGridSubsystem& G,UGuLiExternalControlRegistry& E,const FEntry& U,const FTransform& Landing)
    {
        const auto Position=Landing.GetLocation()+(U.Body.bGroundPivot?FVector(0,0,U.Body.HalfHeight+.6):FVector::ZeroVector);
        if (W.OverlapBlockingTestByChannel(Position,FQuat::Identity,ECC_Pawn,
            FCollisionShape::MakeCapsule(U.Body.Radius,FMath::Max(U.Body.Radius,U.Body.HalfHeight)),Collision)) return true;
        const auto Overlap=[&](const FGuLiExternalBody& B,const FTransform& T)
        {
            const auto Q=T.GetLocation()+(B.bGroundPivot?FVector(0,0,B.HalfHeight+.6):FVector::ZeroVector);
            return FMath::Abs(Position.Z-Q.Z)<U.Body.HalfHeight+B.HalfHeight
                && FVector::DistSquared2D(Position,Q)<FMath::Square(U.Body.Radius+B.Radius+.4f);
        };
        TArray<FGuLiHybridObjectKey> Hits;
        const int32 Size=GuLiGrid::FootprintFromDiameter(U.Body.Radius*2.);
        G.Traffic().Query(GuLiGrid::FRect::At(G.Snapshot()->AnchorAt(Landing.GetLocation(),Size),Size),U.Key,Hits);
        for (const auto& K:Hits)
        {
            if (Indices.Contains(K)) continue;
            if (!U.Body.bAirborne && !U.Body.bFollowParent) return true;
            FGuLiExternalBody Body;if (!E.Query(K,Body) || Overlap(Body,Body.Transform)) return true;
        }
        // Ground members were already packed using bitmaps. Only airborne/attached members need 3D checks.
        if (U.Body.bAirborne || U.Body.bFollowParent)
            for (const auto& Other:Units) if (Other.Placed && Overlap(Other.Body,Other.Landing)) return true;
        return false;
    }
};
FGridLandingPlan::FGridLandingPlan()=default;
FGridLandingPlan::~FGridLandingPlan()=default;
ELandingPlan FGridLandingPlan::Advance(UWorld& W,const Net::State& S,Net::Command& C,bool Returning)
{
    auto* G=W.GetSubsystem<UGuLiGroundGridSubsystem>();auto* H=W.GetSubsystem<UGuLiHybridWorldSubsystem>();
    auto* E=W.GetSubsystem<UGuLiExternalControlRegistry>();
    if (!G || !G->IsReady() || !H || !E || !S.Profiles.contains(S.ProfileId) || S.Members.size()>Net::MaximumMembers) return ELandingPlan::Failed;
    const auto Center=Returning?Transform(S.Source).GetLocation():Transform(C.Point).GetLocation();
    if (!Impl || Impl->Cast!=S.Cast || Impl->Step!=S.Step || Impl->Returning!=Returning || Impl->Center!=Center)
    {
        Impl=MakeUnique<FImpl>();Impl->Cast=S.Cast;Impl->Step=S.Step;Impl->Returning=Returning;Impl->Center=Center;
        Impl->Radius=S.Profiles.at(S.ProfileId).Radius*(Returning?4.:1.);
    }
    auto& I=*Impl;auto& Work=G->FrameBudget();FGuLiNavigationWorkBudget::FScope Scope(Work);
    TGuardValue<double> Slice(Work.LimitSeconds,FMath::Min(Work.LimitSeconds,Work.Elapsed()+.00025));
    if (!I.Slots && I.Stage==FImpl::EStage::Gather)
    {I.Slots=G->CreateSlotWorkspace();if (!I.Slots) return ELandingPlan::Pending;}
    while (Work.CanWork())
    {
        if (I.Stage==FImpl::EStage::Gather)
        {
            if (I.Gather==int32(S.Members.size()))
            {
                I.Slots->Freeze(*G->Snapshot(),I.Center,FMath::CeilToInt32(I.Radius/300.)+1);
                I.Stage=FImpl::EStage::Ground;continue;
            }
            const auto K=GuLiNetBridge::ToUE(S.Members[I.Gather++]);FGuLiHybridObjectState O;Net::Body B;FGuLiExternalBody Body;
            if (!H->GetObject(K,O)) {I.Stage=FImpl::EStage::Failed;continue;}
            if (O.LifeState!=1 || O.Health.bDead || O.bDormant) continue;
            if (!Read(O,TEXT("relocation_body"),B) || !E->Query(K,Body)) {I.Stage=FImpl::EStage::Failed;continue;}
            if (Returning && S.Stage==Net::Phase::Locking && B.Step==1 && B.Acknowledged==1 && !B.Accepted) continue;
            const int32 Index=I.Units.Add({K,O.Parent,O.OwnerEpoch,O.ControlEpoch,Body,Transform(B.Original),Transform(B.Original),false});
            I.Indices.Add(K,Index);if (Body.Actor.IsValid()) I.Collision.AddIgnoredActor(Body.Actor.Get());
            if (Body.bFollowParent) I.Siblings.FindOrAdd(O.Parent).Add(K);
            else if (!Body.bAirborne)
            {
                if (!I.Slots->Add(Index+1,GuLiGrid::FootprintFromDiameter(Body.Radius*2.))) I.Stage=FImpl::EStage::Failed;
                ++I.GroundCount;
            }
        }
        else if (I.Stage==FImpl::EStage::Ground)
        {
            uint32 Id=0;FIntPoint Anchor;int32 Size=1;
            if (I.Slots->Next(*G->Snapshot(),Work,Id,Anchor,Size,[&](uint32 N,FIntPoint P,int32 Footprint)
            {
                auto& U=I.Units[N-1];auto Ground=G->Snapshot()->Center(P,Footprint);
                if (FVector::DistSquared2D(Ground,I.Center)>FMath::Square(I.Radius)) return false;
                Ground.Z+=U.Body.Altitude;
                const FTransform T(U.Original.GetRotation(),Ground,U.Original.GetScale3D());
                if (I.Blocked(W,*G,*E,U,T)) return false;U.Landing=T;return true;
            }))
            {I.Units[Id-1].Placed=true;++I.GroundPlaced;continue;}
            if (!I.Slots->IsFinished()) return ELandingPlan::Pending;
            I.Slots.Reset();I.Stage=I.GroundPlaced==I.GroundCount?FImpl::EStage::Air:FImpl::EStage::Failed;
        }
        else if (I.Stage==FImpl::EStage::Air)
        {
            if (I.AirCursor==I.Units.Num())
            {if (!I.AirPass) {I.AirPass=1;I.AirCursor=0;} else I.Stage=FImpl::EStage::Finish;continue;}
            auto& U=I.Units[I.AirCursor];if (U.Placed) {++I.AirCursor;continue;}
            if (!I.AirPass && U.Body.bFollowParent) {++I.AirCursor;continue;}
            if (!Work.TakeProjection()) return ELandingPlan::Pending;
            if (U.Body.bFollowParent)
            {
                const int32* Parent=I.Indices.Find(U.Parent);
                if (!Parent || !I.Units[*Parent].Placed) {I.Stage=FImpl::EStage::Failed;continue;}
                const auto& P=I.Units[*Parent];const auto Planned=E->PlanAttached(U.Key,P.Landing,I.Siblings.FindChecked(U.Parent),U.Landing);
                if (Planned.IsSet() && !Planned.GetValue()) {I.Stage=FImpl::EStage::Failed;continue;}
                if (!Planned.IsSet()) U.Landing=U.Original.GetRelativeTransform(P.Original)*P.Landing;
            }
            else
            {
                if (I.Attempt>=257) {I.Stage=FImpl::EStage::Failed;continue;}
                const int32 Attempt=I.Attempt++;const FVector Offset=U.Original.GetLocation()-Transform(S.Source).GetLocation();
                const double Angle=Attempt*2.39996323,Distance=I.Radius*FMath::Sqrt(double(Attempt)/256.);
                const FVector Desired=I.Center+(Attempt?FVector::ZeroVector:FVector(Offset.X,Offset.Y,0))
                    +FVector(FMath::Cos(Angle)*Distance,FMath::Sin(Angle)*Distance,0);
                FVector Ground;double Height=0;
                if (!GuLiSkillTargeting::ResolveGround(W,Desired,Ground,&Height)
                    || FVector::DistSquared2D(Ground,I.Center)>FMath::Square(I.Radius)) continue;
                if (!U.Body.bGroundPivot) Ground.Z=Height;
                U.Landing=FTransform(U.Original.GetRotation(),Ground+FVector(0,0,U.Body.Altitude),U.Original.GetScale3D());
            }
            if (I.Blocked(W,*G,*E,U,U.Landing))
            {if (U.Body.bFollowParent) I.Stage=FImpl::EStage::Failed;continue;}
            U.Placed=true;++I.AirCursor;I.Attempt=0;
        }
        else if (I.Stage==FImpl::EStage::Finish)
        {
            if (I.FinishCursor==I.Units.Num()) {I.Stage=FImpl::EStage::Done;continue;}
            const auto& U=I.Units[I.FinishCursor++];FGuLiHybridObjectState O;
            if (!H->GetObjectView(U.Key,O) || O.OwnerEpoch!=U.OwnerEpoch || O.ControlEpoch!=U.ControlEpoch)
            {I.Stage=FImpl::EStage::Failed;continue;}
            I.Output.push_back({GuLiNetBridge::ToCore(U.Key),U.OwnerEpoch,U.ControlEpoch,Pose(U.Landing)});
        }
        else if (I.Stage==FImpl::EStage::Done) {C.Members=std::move(I.Output);return ELandingPlan::Ready;}
        else {I.Slots.Reset();return ELandingPlan::Failed;}
    }
    return ELandingPlan::Pending;
}
}
