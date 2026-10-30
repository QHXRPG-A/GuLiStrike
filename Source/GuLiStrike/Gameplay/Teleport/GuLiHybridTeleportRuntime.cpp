#include "Gameplay/Teleport/GuLiHybridTeleport.h"
#include "Gameplay/Teleport/Private/GuLiHybridTeleportState.h"
#include "Gameplay/Teleport/Private/GuLiGridLandingPlan.h"
#include "Gameplay/Navigation/Grid/GuLiGridQueries.h"
#include "Gameplay/Navigation/Grid/GuLiGridNetwork.h"
#include "Gameplay/Teleport/GuLiTeleportFieldActor.h"
#include "Network/Private/GuLiHybridEntityState.h"
#include "Network/Private/GuLiHybridStateArchive.h"
#include "Network/GuLiHybridActorWorldSubsystem.h"
#include "Network/GuLiNetworkSettings.h"
#include "Gameplay/Skills/GuLiHybridArmy.h"
#include "Battle/Framework/GuLiBattleGameState.h"
#include "Engine/World.h"
#include "Gameplay/Performance/GuLiHotspotOptions.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
namespace GuLiHybridTeleport
{
namespace
{
FGuid Identity(UWorld& W,const FGuLiHybridObjectKey& Key,GuLiNet::Id Cast,const FString& Step)
{return FGuLiHybridManifest::MakeStableKey(W.GetOutermost()->GetName(),EGuLiHybridObjectKind::BusinessWork,
    TEXT("relocation/")+Key.StableId.ToString(EGuidFormats::Digits)+TEXT("/")+GuLiNetBridge::ToUE(Cast).ToString(EGuidFormats::Digits)+TEXT("/")+Step).StableId;}
TArray<uint8> Bytes(const Net::Command& C) {const auto B=Net::Encode(C);TArray<uint8> Out;Out.Append(B.data(),int32(B.size()));return Out;}
FName Rule(const FGuLiHybridObjectState& O,bool Ack) {return FName(*((Ack?TEXT("relocation/ack/"):TEXT("relocation/cast/"))+O.Definition.ToString()));}
bool Root(UWorld& W,EGuLiTeam Team,FGuLiHybridObjectState& O,Net::State& S)
{return GuLiHybridArmy::FindRoot(W,Team,O)&&Read(O,TEXT("relocation_cast"),S);}
FGuid Route(UWorld& W,EGuLiTeam Team,FGuid Event,const Net::Command& Input)
{
    auto* H=W.GetSubsystem<UGuLiHybridWorldSubsystem>();FGuLiHybridObjectState O;Net::State S;
    if (!H||!H->CanPublishFacts()||!Root(W,Team,O,S)||!H->HasControl(O.Key)||!Event.IsValid()) return {};
    FGuLiHybridCommand C;C.Event=Event;C.LogicalTick=H->GetLogicalTick();C.Operation=TEXT("RelocationInput");C.Arguments=Bytes(Input);
    C.Members.Add({O.Key,O.ControlEpoch,O.OwnerEpoch,0,{}});return !C.Arguments.IsEmpty()&&H->RouteCommand(C)?Event:FGuid();
}
struct FBodyPrivate
{
    FGuLiHybridObjectKey Key;FGuid Cast,Request;uint32 Step=0;FTransform Original;
    uint64 Expected=0,OwnerEpoch=0;
    bool Accepted=true,Submitted=false;
    FGuid LandingCast;uint32 LandingStep=0;uint64 LandingAt=0;double LandingBlocked=0;
    void Archive(FGuLiHybridStateArchive& A)
    {uint32 V=2;A.Value(V);A.Value(Key);A.Value(Cast);A.Value(Request);A.Value(Step);A.Value(Original);A.Value(Expected);A.Value(OwnerEpoch);A.Value(Accepted);A.Value(Submitted);
        A.Value(LandingCast);A.Value(LandingStep);A.Value(LandingAt);A.Value(LandingBlocked);
        if (V!=2||!Key.IsValid()||(Step&&!Cast.IsValid())||(Submitted&&!Request.IsValid())
            || !FMath::IsFinite(LandingBlocked) || LandingBlocked<0 || LandingBlocked>60) A.Reject();}
};
class FBody final:public IGuLiHybridEntityCapability
{
    TWeakObjectPtr<UWorld> World;FGuLiHybridObjectState Record;FBodyPrivate Private;FDelegateHandle Post;
    uint64 RecoveredLandingEpoch=0;
    bool bOptimize=false,bRestoreDetached=false,bPeriodicWork=true;
    mutable bool bDecoded=false,bDecodeValid=false;
    mutable uint32 CachedVersion=0;
    mutable TArray<uint8> CachedBytes;
    mutable Net::Body CachedBody;
    TWeakObjectPtr<UGuLiHotspotOptions> Metrics;
    bool ReadBody(Net::Body& Out) const
    {
        const auto* C=Record.Capabilities.Find(TEXT("relocation_body"));
        if (bOptimize && bDecoded && CachedVersion==(C?C->Version:0) && (!C?CachedBytes.IsEmpty():CachedBytes==C->Data))
        {if (Metrics.IsValid()) ++Metrics->BodyDecodeHits;Out=CachedBody;return bDecodeValid;}
        TRACE_CPUPROFILER_EVENT_SCOPE(GuLiSim_TeleportBodyDecode);
        if (Metrics.IsValid()) ++Metrics->BodyDecodes;
        bDecodeValid=Read(Record,TEXT("relocation_body"),Out);
        if (bOptimize) {bDecoded=true;CachedVersion=C?C->Version:0;CachedBytes=C?C->Data:TArray<uint8>();CachedBody=Out;}
        return bDecodeValid;
    }
    void RefreshSubscription()
    {
        Net::Body B;
        // An outstanding acknowledgement must keep polling final results and
        // version-conflict retries. Actual application remains PostActor work,
        // including on remote objects and while a root has not arrived yet.
        const bool bValidBody=ReadBody(B);bPeriodicWork=!bValidBody || (B.Step && B.Acknowledged!=B.Step);
        bool Needed=!bOptimize;
        if (bOptimize && !bRestoreDetached && World.IsValid() && Record.LifeState<2)
        {
            // Invalid data must still reach the original failure path. A missing root
            // never completes an application and therefore keeps this subscription.
            Needed=!bValidBody || (B.Step && (Private.Cast!=GuLiNetBridge::ToUE(B.Cast) || Private.Step!=B.Step));
        }
        if (Needed && !Post.IsValid()) Post=FWorldDelegates::OnWorldPostActorTick.AddRaw(this,&FBody::ApplyAfterActors);
        else if (!Needed && Post.IsValid()) {FWorldDelegates::OnWorldPostActorTick.Remove(Post);Post.Reset();}
    }
    UGuLiHybridWorldSubsystem* Hybrid() const {return World.IsValid()?World->GetSubsystem<UGuLiHybridWorldSubsystem>():nullptr;}
    void RecoverLanding(const Net::Body& B)
    {
        if (RecoveredLandingEpoch==Record.OwnerEpoch || B.Phased || !B.Displace || B.Airborne || B.FollowParent
            || B.Acknowledged==B.Step || !Private.Accepted || Private.Step!=B.Step || Private.Cast!=GuLiNetBridge::ToUE(B.Cast)) return;
        auto* H=Hybrid();auto* G=World->GetSubsystem<UGuLiGroundGridSubsystem>();
        if (!G || !G->IsReady() || !G->Network() || !H->HasSimulation(Record.Key)) return;
        FGuLiExternalBody Body;if (!World->GetSubsystem<UGuLiExternalControlRegistry>()->Query(Record.Key,Body)) return;
        const int32 Size=GuLiGrid::FootprintFromDiameter(Body.Radius*2.);
        const auto R=GuLiGrid::FRect::At(G->Snapshot()->AnchorAt(Transform(B.Destination).GetLocation(),Size),Size);
        G->Traffic().Admit(Record.Key,Record.SimulationOwner,Record.OwnerEpoch,uint8(Record.Team),R,{},Size,Body.MovementLayer);
        G->Network()->RecoverPlacement(Record,Record.Key,R,Size,true,Body.MovementLayer);RecoveredLandingEpoch=Record.OwnerEpoch;
    }
    bool Decode(TConstArrayView<uint8> B,FBodyPrivate& P) const
    {FGuLiHybridStateArchive A(B,1024);P.Archive(A);return A.Done()&&P.Key==Record.Key;}
    void ApplyAfterActors(UWorld* W,ELevelTick,float)
    {
        if (W!=World.Get()||Record.LifeState>=2) return;
        TRACE_CPUPROFILER_EVENT_SCOPE(GuLiSim_TeleportPostActor);
        if (Metrics.IsValid()) ++Metrics->BodyCallbacks;
        Net::Body B;
        if (!ReadBody(B)) {Hybrid()->Suspend(TEXT("Invalid external body state"));return;}
        if (!B.Step||(Private.Cast==GuLiNetBridge::ToUE(B.Cast)&&Private.Step==B.Step)) return;
        auto* Endpoints=W->GetSubsystem<UGuLiExternalControlRegistry>();FGuLiExternalBody Q;
        if (!Endpoints||!Endpoints->Query(Record.Key,Q)) {Hybrid()->Suspend(TEXT("A required external-control endpoint is missing"));return;}
        bool Accepted=true;
        if (B.Phased)
        {
            FGuLiHybridObjectState RootObject;Net::State S;
            if (!Hybrid()->GetObject(GuLiNetBridge::ToUE(B.Root),RootObject)||!Read(RootObject,TEXT("relocation_cast"),S)||!S.Profiles.contains(S.ProfileId)||S.Cast!=B.Cast) return;
            const auto& P=S.Profiles.at(S.ProfileId);
            Accepted=!Q.bLocked && (!B.Vehicle||P.Vehicles) && (!B.Airborne||Q.Altitude<=P.MaximumHeight)
                && (B.FollowParent||FVector::DistSquared2D(Q.Transform.GetLocation(),Transform(S.Source).GetLocation())<=FMath::Square(P.Radius));
        }
        auto* Grid=W->GetSubsystem<UGuLiGroundGridSubsystem>();
        const bool GridGround=Grid && Grid->IsReady() && !Q.bAirborne && !Q.bFollowParent;
        if (GridGround && B.Displace && !B.Phased)
        {
            if (!Hybrid()->HasSimulation(Record.Key))
            {
                // Remote endpoints follow the owner's accepted placement, never a speculative landing.
                if (B.Acknowledged!=B.Step) return;Accepted=B.Accepted;
            }
            else
            {
                const auto Cast=GuLiNetBridge::ToUE(B.Cast);const uint64 Now=Hybrid()->GetObjectLogicalTick(Record.Key);
                if (Private.LandingCast!=Cast || Private.LandingStep!=B.Step)
                {Private.LandingCast=Cast;Private.LandingStep=B.Step;Private.LandingBlocked=0;Private.LandingAt=Now;}
                const double Delta=Now>=Private.LandingAt?FMath::Min(1.,double(Now-Private.LandingAt)/60.):0.;
                Private.LandingAt=Now;
                auto& Work=Grid->FrameBudget();FGuLiNavigationWorkBudget::FScope Scope(Work);
                if (!Work.TakeProjection()) return;
                const int32 Size=GuLiGrid::FootprintFromDiameter(Q.Radius*2.);const auto Point=Transform(B.Destination).GetLocation();
                const auto Rect=GuLiGrid::FRect::At(Grid->Snapshot()->AnchorAt(Point,Size),Size);
                const auto Permit=GuLiGrid::Unoccupied(*Grid,Point,Size,Record.Key,Q.MovementLayer) && Grid->Network()
                    ?Grid->Network()->PermitPlacement(Record.Key,Record.Key,Rect,Size,uint8(Record.Team),Q.MovementLayer):EGuLiGridPermit::Blocked;
                if (Permit==EGuLiGridPermit::Pending) return;
                if (Permit==EGuLiGridPermit::Blocked)
                {
                    Private.LandingBlocked+=Delta;if (Private.LandingBlocked<GuLiGrid::BlockedTimeout) return;
                    Accepted=false;if (Grid->Network()) Grid->Network()->Withdraw(Record.Key);
                }
            }
        }
        FGuLiExternalControl Control;Control.Token=GuLiNetBridge::ToUE(B.Cast);Control.Step=B.Step;Control.LogicalTick=Hybrid()->GetObjectLogicalTick(Record.Key);Control.bPhased=B.Phased;
        Control.bLocked=B.Locked;Control.bDisplace=B.Displace;Control.Transform=Transform(B.Destination);
        if (Accepted) Accepted=Endpoints->Apply(Record.Key,Control);
        if (GridGround && Accepted)
        {
            if (B.Phased)
            {
                Grid->Traffic().Remove(Record.Key,Record.SimulationOwner,Record.OwnerEpoch);
                if (Grid->Network() && Hybrid()->HasSimulation(Record.Key)) Grid->Network()->Withdraw(Record.Key);
            }
            else if (B.Displace)
            {
                const int32 Size=GuLiGrid::FootprintFromDiameter(Q.Radius*2.);
                Grid->Traffic().Admit(Record.Key,Record.SimulationOwner,Record.OwnerEpoch,uint8(Record.Team),
                    GuLiGrid::FRect::At(Grid->Snapshot()->AnchorAt(Control.Transform.GetLocation(),Size),Size),{},Size,Q.MovementLayer);
            }
        }
        Private.Cast=Control.Token;Private.Step=B.Step;Private.Accepted=Accepted;Private.Submitted=false;Private.Request.Invalidate();
        if (B.Phased) Private.Original=Q.Transform;
        if (Metrics.IsValid()) ++Metrics->BodyApplications;
        RefreshSubscription();
    }
public:
    FBody(UWorld& W,const FGuLiHybridObjectState& O):World(&W),Record(O)
    {
        Private.Key=O.Key;Metrics=W.GetSubsystem<UGuLiHotspotOptions>();
        bOptimize=Metrics.IsValid() && Metrics->Has(EGuLiHotspot::TeleportIdle);RefreshSubscription();
    }
    ~FBody() override {FWorldDelegates::OnWorldPostActorTick.Remove(Post);}
    bool HasPeriodicWork() const override {return bPeriodicWork;}
    void ApplyLedger(const FGuLiHybridObjectState& O) override {Record=O;RefreshSubscription();}
    bool AcceptMotion(uint64 At) const override
    {Net::Body B;return ReadBody(B) && (!B.Step || (B.Acknowledged==B.Step && At>B.AppliedAt));}
    void Advance(uint64,double) override
    {
        auto* H=Hybrid();Net::Body B;if (!H||!ReadBody(B)||!B.Step||B.Acknowledged==B.Step
            ||Private.Cast!=GuLiNetBridge::ToUE(B.Cast)||Private.Step!=B.Step||!H->CanPublishFacts()) return;
        RecoverLanding(B);
        // The caster transaction publishes its root and member changes to
        // independently owned objects. A body can arrive before the root on
        // this simulator. Wait for that exact root step before acknowledging;
        // the authoritative capability contract must still reject mismatches.
        FGuLiHybridObjectState RootObject;Net::State RootState;
        if (!H->GetObject(GuLiNetBridge::ToUE(B.Root),RootObject) || !Read(RootObject,TEXT("relocation_cast"),RootState)
            || RootState.Cast!=B.Cast || RootState.Step!=B.Step
            || std::find(RootState.Members.begin(),RootState.Members.end(),GuLiNetBridge::ToCore(Record.Key))==RootState.Members.end())
            return;
        FGuLiHybridTransactionResult R;
        if (Private.Request.IsValid() && H->FindTransactionResult(Private.Request,R))
        {
            if (R.Status==EGuLiHybridTransactionStatus::Deferred||R.Status==EGuLiHybridTransactionStatus::Paused||R.IsCommitted()) return;
            // A different capability may have committed while this acknowledgement
            // was in flight. Keep its final decision and retry with a fresh identity.
            if (Private.Expected!=Record.RelationRevision || Private.OwnerEpoch!=Record.OwnerEpoch)
            {Private.Request.Invalidate();Private.Submitted=false;}
            else
            {
                UE_LOG(LogTemp,Error,TEXT("[GuLiTeleport] acknowledgement rejected object=%s event=%s status=%u body_step=%u root_step=%u relation=%llu owner=%llu"),
                    *Record.Key.StableId.ToString(),*Private.Request.ToString(),uint32(R.Status),B.Step,RootState.Step,Private.Expected,Private.OwnerEpoch);
                H->Suspend(TEXT("External-control acknowledgement was rejected"));return;
            }
        }
        if (Private.Submitted) return;
        if (!Private.Request.IsValid())
        {
            Private.Expected=Record.RelationRevision;Private.OwnerEpoch=Record.OwnerEpoch;
            Private.Request=Identity(*World.Get(),Record.Key,B.Cast,FString::Printf(TEXT("ack/%u/%llu/%llu"),B.Step,Private.OwnerEpoch,Private.Expected));
        }
        Net::Command C;C.Op=Net::Operation::Acknowledge;C.Cast=B.Cast;C.Step=B.Step;C.Accepted=Private.Accepted;C.Point=Pose(Private.Original);
        Private.Submitted=H->SubmitCapability(Record.Key,Private.Request,Rule(Record,true),Private.Expected,Bytes(C),Record.Team).IsValid();
    }
    bool Capture(TArray<uint8>& Out) const override {auto P=Private;FGuLiHybridStateArchive A(1024);P.Archive(A);return A.Finish(Out);}
    bool Validate(TConstArrayView<uint8> B) const override {FBodyPrivate P;return Decode(B,P);}
    bool Restore(TConstArrayView<uint8> B,EGuLiHybridRestoreStage Stage) override
    {
        FBodyPrivate P;if (!Decode(B,P)) return false;
        if (Stage==EGuLiHybridRestoreStage::Detach) bRestoreDetached=true;
        if (Stage==EGuLiHybridRestoreStage::Link) {Private=P;RecoveredLandingEpoch=0;}
        if (Stage==EGuLiHybridRestoreStage::Activate) bRestoreDetached=false;
        RefreshSubscription();return true;
    }
};
struct FPending {FGuid Event;TArray<uint8> Payload;bool Prepared=false,Submitted=false;uint64 Expected=0;bool Automatic=false;};
struct FCastPrivate
{
    FGuLiHybridObjectKey Key;TArray<FPending> Pending;uint64 RetryAt=0;
    void Archive(FGuLiHybridStateArchive& A)
    {
        uint32 V=1;A.Value(V);A.Value(Key);A.Value(RetryAt);A.Array(Pending,32,[](auto& A,auto& P)
        {A.Value(P.Event);A.Bytes(P.Payload,GuLiNet::MaximumCapabilityBytes);A.Value(P.Prepared);A.Value(P.Submitted);A.Value(P.Expected);A.Value(P.Automatic);
            Net::Command C;if (!P.Event.IsValid()||!Net::Decode({P.Payload.GetData(),SIZE_T(P.Payload.Num())},C)||(P.Submitted&&!P.Prepared)) A.Reject();});
        if (V!=1||!Key.IsValid()) A.Reject();TSet<FGuid> Seen;for (const auto& P:Pending) {if (Seen.Contains(P.Event)) A.Reject();Seen.Add(P.Event);}
    }
};
class FCaster final:public IGuLiHybridEntityCapability
{
    TWeakObjectPtr<UWorld> World;FGuLiHybridObjectState Record;FCastPrivate Private;
    TUniquePtr<FGridLandingPlan> LandingPlan;
    ELandingPlan PrepareLanding(const Net::State& S,Net::Command& C,bool Returning)
    {
        if (auto* G=World->GetSubsystem<UGuLiGroundGridSubsystem>();G && G->IsReady())
        {
            if (!LandingPlan) LandingPlan=MakeUnique<FGridLandingPlan>();
            const auto R=LandingPlan->Advance(*World.Get(),S,C,Returning);
            if (R!=ELandingPlan::Pending) LandingPlan.Reset();return R;
        }
        return Plan(*World.Get(),S,C,Returning)?ELandingPlan::Ready:ELandingPlan::Failed;
    }
    TWeakObjectPtr<AGuLiTeleportFieldActor> Visual;
    UGuLiHybridWorldSubsystem* Hybrid() const {return World.IsValid()?World->GetSubsystem<UGuLiHybridWorldSubsystem>():nullptr;}
    bool Decode(TConstArrayView<uint8> B,FCastPrivate& P) const {FGuLiHybridStateArchive A(B,2*1024*1024);P.Archive(A);return A.Done()&&P.Key==Record.Key;}
    bool Acknowledged(const Net::State& S,bool& Rejected) const
    {
        Rejected=false;for (const auto K:S.Members)
        {
            FGuLiHybridObjectState O;Net::Body B;if (!Hybrid()->GetObject(GuLiNetBridge::ToUE(K),O)) return false;
            if (O.LifeState>=2) continue;
            if (O.bDormant)
            {if (Read(O,TEXT("relocation_body"),B)) Rejected|=B.Locked||B.Phased;continue;}
            if (!Read(O,TEXT("relocation_body"),B)||B.Cast!=S.Cast||B.Step!=S.Step||B.Acknowledged!=S.Step) return false;
            Rejected|=!B.Accepted;
        }
        return true;
    }
    void UpdateVisual()
    {
        if (!World.IsValid()||!UGuLiNetworkSettings::CanPresent(World.Get())) return;
        const auto V=Query(*World.Get(),Record.Team);
        if (!V.CastId.IsValid()) {if (Visual.IsValid()) Visual->Destroy();Visual.Reset();return;}
        if (!Visual.IsValid()) Visual=World->SpawnActor<AGuLiTeleportFieldActor>();
        if (Visual.IsValid()) Visual->ApplyHybridView(V);
    }
public:
    FCaster(UWorld& W,const FGuLiHybridObjectState& O):World(&W),Record(O) {Private.Key=O.Key;}
    ~FCaster() override {if (Visual.IsValid()) Visual->Destroy();}
    void ApplyLedger(const FGuLiHybridObjectState& O) override {Record=O;UpdateVisual();}
    TOptional<bool> Command(const FGuLiHybridCommand& C,const FGuLiHybridCommandMember& M) override
    {
        if (C.Operation!=TEXT("RelocationInput")) return {};Net::Command Input;
        if (M.Key!=Record.Key||Private.Pending.Num()>=32||!Net::Decode({C.Arguments.GetData(),SIZE_T(C.Arguments.Num())},Input)
            ||(Input.Op!=Net::Operation::Begin&&Input.Op!=Net::Operation::Destination&&Input.Op!=Net::Operation::Return&&Input.Op!=Net::Operation::SetLevel)) return false;
        if (const auto* P=Private.Pending.FindByPredicate([&](const auto& P){return P.Event==C.Event;})) return P->Payload==C.Arguments;
        Private.Pending.Add({C.Event,C.Arguments,false,false,0});return true;
    }
    void Advance(uint64 At,double) override
    {
        auto* H=Hybrid();Net::State S;if (!H||!Read(Record,TEXT("relocation_cast"),S)||!H->CanPublishFacts()) return;
        if (!Private.Pending.IsEmpty())
        {
            auto& P=Private.Pending[0];FGuLiHybridTransactionResult R;
            if (H->FindTransactionResult(P.Event,R))
            {
                if (R.Status==EGuLiHybridTransactionStatus::Deferred||R.Status==EGuLiHybridTransactionStatus::Paused) return;
                if (!R.IsCommitted()) UE_LOG(LogTemp,Warning,TEXT("Relocation request %s rejected status=%d stage=%d step=%u expected=%llu current=%llu age=%llu deadline=%llu payload=%d"),
                    *P.Event.ToString(),uint8(R.Status),uint8(S.Stage),S.Step,P.Expected,Record.RelationRevision,At,S.Deadline,P.Payload.Num());
                if (!R.IsCommitted() && P.Automatic && P.Expected==Record.RelationRevision)
                {
                    Net::Command Attempt;bool Rejected=false;
                    if (Net::Decode({P.Payload.GetData(),SIZE_T(P.Payload.Num())},Attempt)
                        && Attempt.Op==Net::Operation::Advance && S.Stage==Net::Phase::Locking)
                    {
                        // Member acknowledgements advance independently of the
                        // caster revision. A late rejection invalidates an
                        // already queued Advance; the next operation is Return.
                        if (!Acknowledged(S,Rejected)) return;
                        if (Rejected) {Private.Pending.RemoveAt(0);return;}
                    }
                    H->Suspend(TEXT("An automatic relocation transition was finally rejected"));return;
                }
                Private.Pending.RemoveAt(0);return;
            }
            if (P.Submitted) return;
            if (!P.Prepared)
            {
                Net::Command C;if (!Net::Decode({P.Payload.GetData(),SIZE_T(P.Payload.Num())},C)) {H->Suspend(TEXT("Invalid relocation input fragment"));return;}
                C.Step=S.Step;
                if (C.Op==Net::Operation::Destination||C.Op==Net::Operation::Return)
                {
                    bool Rejected=false;
                    if (C.Op==Net::Operation::Destination || S.Stage!=Net::Phase::Locking || Acknowledged(S,Rejected))
                    {
                        const auto Result=PrepareLanding(S,C,C.Op==Net::Operation::Return);
                        if (Result==ELandingPlan::Pending) return;
                        if (Result==ELandingPlan::Failed) C.Members.clear();
                    }
                }
                P.Payload=Bytes(C);P.Expected=Record.RelationRevision;P.Prepared=true;
            }
            P.Submitted=H->SubmitCapability(Record.Key,P.Event,Rule(Record,false),P.Expected,P.Payload,Record.Team).IsValid();return;
        }
        Net::Command C;C.Cast=S.Cast;C.Step=S.Step;bool Needed=false,Rejected=false;
        const bool All=Acknowledged(S,Rejected);
        if (All && Rejected && S.Stage==Net::Phase::Releasing)
        {H->Suspend(TEXT("Relocation control could not be released"));return;}
        if (S.Stage==Net::Phase::Windup&&At>=S.Deadline)
        {C.Op=Net::Operation::Collect;Needed=true;if (!Collect(*World.Get(),Record,S,C)) {H->Suspend(TEXT("Relocation collection exceeds its capability contract"));return;}}
        else if ((S.CancelPending || (S.Stage==Net::Phase::Choosing&&At>=S.Deadline) || Rejected)
            && (S.Stage!=Net::Phase::Locking || All) && At>=Private.RetryAt)
        {
            C.Op=Net::Operation::Return;C.Point=S.Source;Needed=true;
            const auto Result=PrepareLanding(S,C,true);
            if (Result==ELandingPlan::Pending) return;
            if (Result==ELandingPlan::Failed)
            {
                Private.RetryAt=At+300+GetTypeHash(Record.Key)%60;
                return;
            }
        }
        else if (All && (S.Stage==Net::Phase::Locking||S.Stage==Net::Phase::Landing||S.Stage==Net::Phase::Releasing)) {C.Op=Net::Operation::Advance;Needed=true;}
        else if (S.Stage==Net::Phase::Recovery&&At>=S.Deadline) {C.Op=Net::Operation::Release;Needed=true;}
        // Receipt latency affects this cast only. Object owners keep retrying
        // their acknowledgements; unrelated objects continue advancing.
        else if (!All) return;
        if (Needed)
        {
            const auto Event=Identity(*World.Get(),Record.Key,S.Cast,FString::Printf(TEXT("%u/%u/%llu"),uint8(C.Op),S.Step,Record.RelationRevision));
            Private.Pending.Add({Event,Bytes(C),true,false,Record.RelationRevision,true});
        }
    }
    bool Capture(TArray<uint8>& Out) const override {auto P=Private;FGuLiHybridStateArchive A(2*1024*1024);P.Archive(A);return A.Finish(Out);}
    bool Validate(TConstArrayView<uint8> B) const override {FCastPrivate P;return Decode(B,P);}
    bool Restore(TConstArrayView<uint8> B,EGuLiHybridRestoreStage Stage) override
    {FCastPrivate P;if (!Decode(B,P)) return false;if (Stage==EGuLiHybridRestoreStage::Detach) LandingPlan.Reset();
        if (Stage==EGuLiHybridRestoreStage::Link) Private=MoveTemp(P);return true;}
};
const FGuLiHybridEntityCapabilityRegistration BodyContract({TEXT("RelocationBody"),2,
    [](UWorld& W,const FGuLiHybridObjectState& O,const FGuLiHybridManifestData&)->TSharedPtr<IGuLiHybridEntityCapability>{return MakeShared<FBody>(W,O);}});
const FGuLiHybridEntityCapabilityRegistration CastContract({TEXT("RelocationCast"),1,
    [](UWorld& W,const FGuLiHybridObjectState& O,const FGuLiHybridManifestData&)->TSharedPtr<IGuLiHybridEntityCapability>{return MakeShared<FCaster>(W,O);}});
}
FGuid Request(const FGuLiActiveSkillExecutionContext& Context)
{
    auto* W=Context.GetExecutionWorld();if (!W) return {};Net::Command C;
    C.Cast=GuLiNetBridge::ToCore(Context.Command==EGuLiActiveSkillCommand::Activate?Context.RequestId:Context.CastId);
    C.Op=Context.Command==EGuLiActiveSkillCommand::Activate?Net::Operation::Begin:Context.Command==EGuLiActiveSkillCommand::Continue?Net::Operation::Destination:Net::Operation::Return;
    C.ProfileId=TCHAR_TO_UTF8(*FString::Printf(TEXT("%s/%d"),*Context.SkillId.ToString(),Context.Level));C.Point=Pose(FTransform(Context.GroundPoint));
    return Route(*W,Context.GetExecutionTeam(),Context.RequestId,C);
}
FGuid SetLevel(UWorld& W,EGuLiTeam Team,FName Skill,int32 Level)
{
    const auto Event=FGuid::NewGuid();Net::Command C;C.Op=Net::Operation::SetLevel;C.Cast=GuLiNetBridge::ToCore(Event);
    C.ProfileId=TCHAR_TO_UTF8(*FString::Printf(TEXT("%s/%d"),*Skill.ToString(),Level));return Route(W,Team,Event,C);
}
bool ReadRuntime(UWorld& W,EGuLiTeam Team,FName Skill,FGuLiActiveSkillRuntime& Out)
{
    FGuLiHybridObjectState O;Net::State S;if (!Root(W,Team,O,S)) return false;const std::string ID=TCHAR_TO_UTF8(*Skill.ToString());
    const auto Level=S.Levels.find(ID);if (Level==S.Levels.end()) return false;
    Out.SkillId=Skill;Out.Level=int32(Level->second);const auto Ready=S.ReadyAt.find(ID);Out.ReadyAt=Ready==S.ReadyAt.end()?0:double(Ready->second)/60.;
    Out.ActiveCastId=S.Stage!=Net::Phase::Idle&&S.Stage!=Net::Phase::Finished&&S.Profiles.at(S.ProfileId).Skill==ID?GuLiNetBridge::ToUE(S.Cast):FGuid();return true;
}
FGuLiTeleportCastState Query(UWorld& W,EGuLiTeam Team)
{int32 Level=1;return Query(W,Team,Level);}
FGuLiTeleportCastState Query(UWorld& W,EGuLiTeam Team,int32& Level)
{
    Level=1;FGuLiTeleportCastState V;FGuLiHybridObjectState O;Net::State S;if (!Root(W,Team,O,S)) return V;
    for (const auto& [Skill,Value]:S.Levels) if (FName(UTF8_TO_TCHAR(Skill.c_str()))==TEXT("Teleport")) {Level=int32(Value);break;}
    if (!S.Profiles.contains(S.ProfileId)) return V;
    const auto& P=S.Profiles.at(S.ProfileId);V.CastId=GuLiNetBridge::ToUE(S.Cast);V.CommanderId=O.Controller;V.Team=Team;
    V.MatchEpoch=W.GetGameState<AGuLiBattleGameState>()?W.GetGameState<AGuLiBattleGameState>()->GetMatchEpoch():0;
    V.Config.Level=P.Level;V.Config.RadiusCentimeters=P.Radius;V.Config.WindupSeconds=float(P.Windup)/60;V.Config.MaxTargetWaitSeconds=float(P.Wait)/60;
    V.Config.RecoverySeconds=float(P.Recovery)/60;V.Config.bAllowPlayerVehicles=P.Vehicles;V.Config.MaxShipHeightCentimeters=P.MaximumHeight;V.Config.BeamHeightCentimeters=P.BeamHeight;
    V.Source=Transform(S.Source).GetLocation();V.Destination=Transform(S.Destination).GetLocation();V.PhaseStartTime=double(S.ChangedAt)/60;V.Deadline=double(S.Deadline)/60;
    V.ParticipantCount=int32(S.Members.size());V.Revision=int32(FMath::Min<uint64>(O.RelationRevision,MAX_int32));V.bLanded=S.Stage>=Net::Phase::Landing;
    V.Phase=S.Stage==Net::Phase::Idle?EGuLiTeleportPhase::Idle:S.Stage==Net::Phase::Finished?EGuLiTeleportPhase::Finished:
        S.CancelPending?EGuLiTeleportPhase::Returning:S.Stage==Net::Phase::Choosing?EGuLiTeleportPhase::AwaitingDestination:
        S.Stage==Net::Phase::Windup||S.Stage==Net::Phase::Locking?EGuLiTeleportPhase::Windup:EGuLiTeleportPhase::Recovery;
    return V;
}
}
