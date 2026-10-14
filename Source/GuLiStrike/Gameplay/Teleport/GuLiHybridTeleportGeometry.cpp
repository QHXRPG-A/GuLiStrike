#include "Gameplay/Teleport/Private/GuLiHybridTeleportState.h"
#include "Gameplay/Navigation/GuLiLandingGround.h"
#include "Gameplay/Skills/GuLiSkillTargeting.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
namespace GuLiHybridTeleport
{
bool Collect(UWorld& W,const FGuLiHybridObjectState& Root,const Net::State& S,Net::Command& C)
{
    auto* H=W.GetSubsystem<UGuLiHybridWorldSubsystem>();auto* E=W.GetSubsystem<UGuLiExternalControlRegistry>();
    if (!H||!E||!S.Profiles.contains(S.ProfileId)) return false;const auto& P=S.Profiles.at(S.ProfileId);
    TArray<FGuLiHybridObjectState> All;H->GetObjects(All);TSet<FGuLiHybridObjectKey> Selected;
    const auto Add=[&](const auto& O,const FGuLiExternalBody& B)
    {C.Members.push_back({GuLiNetBridge::ToCore(O.Key),O.OwnerEpoch,O.ControlEpoch,Pose(B.Transform)});Selected.Add(O.Key);};
    for (const auto& O:All)
    {
        Net::Body B;FGuLiExternalBody Q;
        if (O.Team!=Root.Team||O.LifeState!=1||O.Health.bDead||O.bDormant||!Read(O,TEXT("relocation_body"),B)||B.Locked||B.FollowParent) continue;
        if (!E->Query(O.Key,Q)) return false;
        if (Q.bLocked||(B.Vehicle&&!P.Vehicles)||(B.Airborne&&Q.Altitude>P.MaximumHeight)
            ||FVector::DistSquared2D(Q.Transform.GetLocation(),Transform(S.Source).GetLocation())>FMath::Square(P.Radius)) continue;
        Add(O,Q);
    }
    for (const auto& O:All)
    {
        Net::Body B;FGuLiExternalBody Q;
        if (O.Team!=Root.Team||O.LifeState!=1||O.Health.bDead||!Read(O,TEXT("relocation_body"),B)||!B.FollowParent||!Selected.Contains(O.Parent)) continue;
        if (B.Locked||!E->Query(O.Key,Q)||Q.bLocked) return false;Add(O,Q);
    }
    std::sort(C.Members.begin(),C.Members.end(),[](const auto& A,const auto& B){return A.Object<B.Object;});
    return C.Members.size()<=Net::MaximumMembers;
}
bool Plan(UWorld& W,const Net::State& S,Net::Command& C,bool Returning)
{
    auto* H=W.GetSubsystem<UGuLiHybridWorldSubsystem>();auto* E=W.GetSubsystem<UGuLiExternalControlRegistry>();if (!H||!E||!S.Profiles.contains(S.ProfileId)) return false;
    const auto& P=S.Profiles.at(S.ProfileId);const FVector Center=Returning?Transform(S.Source).GetLocation():Transform(C.Point).GetLocation();
    struct FEntry {FGuLiHybridObjectState Object;FGuLiExternalBody Body;FTransform Original,Landing;};
    TArray<FEntry> Units;TSet<FGuLiHybridObjectKey> Selected;
    for (auto K:S.Members)
    {
        FGuLiHybridObjectState O;Net::Body B;FGuLiExternalBody Q;if (!H->GetObject(GuLiNetBridge::ToUE(K),O)) return false;
        if (O.LifeState!=1||O.Health.bDead||O.bDormant) continue;
        if (!Read(O,TEXT("relocation_body"),B)||!E->Query(O.Key,Q)) return false;
        if (Returning && S.Stage==Net::Phase::Locking && B.Step==1 && B.Acknowledged==1 && !B.Accepted) continue;
        Units.Add({O,Q,Transform(B.Original),Transform(B.Original)});Selected.Add(O.Key);
    }
    TArray<FGuLiExternalBody> Reserved;TArray<FGuLiHybridObjectState> All;H->GetObjects(All);
    for (const auto& O:All) if (O.LifeState==1&&!O.Health.bDead&&!O.bPhased&&!Selected.Contains(O.Key)&&O.Capabilities.Contains(TEXT("relocation_body")))
    {FGuLiExternalBody Q;if (!E->Query(O.Key,Q)) return false;Reserved.Add(Q);}
    FCollisionQueryParams Params(SCENE_QUERY_STAT(HybridRelocation),false);for (const auto& U:Units) if (U.Body.Actor.IsValid()) Params.AddIgnoredActor(U.Body.Actor.Get());
    const auto Blocked=[&](const FEntry& U)
    {
        const auto Position=U.Landing.GetLocation()+(U.Body.bGroundPivot?FVector(0,0,U.Body.HalfHeight+.6):FVector::ZeroVector);
        if (W.OverlapBlockingTestByChannel(Position,FQuat::Identity,ECC_Pawn,FCollisionShape::MakeCapsule(U.Body.Radius,FMath::Max(U.Body.HalfHeight,U.Body.Radius)),Params)) return true;
        for (const auto& B:Reserved)
        {
            const auto Q=B.Transform.GetLocation()+(B.bGroundPivot?FVector(0,0,B.HalfHeight+.6):FVector::ZeroVector);
            if (FMath::Abs(Position.Z-Q.Z)<U.Body.HalfHeight+B.HalfHeight&&FVector::DistSquared2D(Position,Q)<FMath::Square(U.Body.Radius+B.Radius+.4f)) return true;
        }
        return false;
    };
    Units.StableSort([](const auto& A,const auto& B){if (A.Body.bFollowParent!=B.Body.bFollowParent) return !A.Body.bFollowParent;return A.Body.Radius>B.Body.Radius;});
    for (auto& U:Units)
    {
        bool Found=false;int32 GroundFailures=0,OutsideRadius=0,BlockedLandings=0;
        if (U.Body.bFollowParent)
        {
            const auto* Parent=Units.FindByPredicate([&](const auto& Other){return Other.Object.Key==U.Object.Parent;});if (!Parent) return false;
            TArray<FGuLiHybridObjectKey> Siblings;for (const auto& Other:Units) if (Other.Body.bFollowParent&&Other.Object.Parent==U.Object.Parent) Siblings.Add(Other.Object.Key);
            const auto Planned=E->PlanAttached(U.Object.Key,Parent->Landing,Siblings,U.Landing);
            if (Planned.IsSet()&&!Planned.GetValue()) return false;
            if (!Planned.IsSet()) U.Landing=U.Original.GetRelativeTransform(Parent->Original)*Parent->Landing;
            Found=!Blocked(U);
        }
        else for (int32 Attempt=0;Attempt<257;++Attempt)
        {
            const FVector Offset=U.Original.GetLocation()-Transform(S.Source).GetLocation();
            const float D=Attempt?FMath::Sqrt(float(Attempt))*FMath::Max(U.Body.Radius*2.1f,20.f):0,Angle=Attempt*2.39996323f;
            const FVector Desired=Center+FVector(Offset.X+FMath::Cos(Angle)*D,Offset.Y+FMath::Sin(Angle)*D,0);FVector Ground;double Height=0;
            const bool Valid=U.Body.bAirborne?GuLiSkillTargeting::ResolveGround(W,Desired,Ground,&Height):GuLiLandingGround::Resolve(W,Desired,Ground,&Height,Cast<APawn>(U.Body.Actor.Get()));
            if (!Valid) {++GroundFailures;continue;}
            if (FVector::DistSquared2D(Ground,Center)>FMath::Square(P.Radius*(Returning?4.:1.))) {++OutsideRadius;continue;}
            if (!U.Body.bGroundPivot) Ground.Z=Height;
            U.Landing=FTransform(U.Original.GetRotation(),Ground+FVector(0,0,U.Body.Altitude),U.Original.GetScale3D());
            if (!Blocked(U)) {Found=true;break;}++BlockedLandings;
        }
        if (!Found)
        {
            UE_LOG(LogTemp,Warning,TEXT("Relocation landing unavailable object=%s center=%s radius=%.1f body=%.1f/%.1f pivot=%d return=%d ground_failures=%d outside_radius=%d blocked=%d"),
                *U.Object.Key.StableId.ToString(),*Center.ToString(),P.Radius,U.Body.Radius,U.Body.HalfHeight,
                U.Body.bGroundPivot,Returning,GroundFailures,OutsideRadius,BlockedLandings);
            return false;
        }
        auto B=U.Body;B.Transform=U.Landing;Reserved.Add(B);
    }
    C.Members.clear();for (const auto& U:Units) C.Members.push_back({GuLiNetBridge::ToCore(U.Object.Key),U.Object.OwnerEpoch,U.Object.ControlEpoch,Pose(U.Landing)});
    return true;
}
}
