#include "Gameplay/Buffs/GuLiBuffPresentation.h"
#include "Network/GuLiHybridActorWorldSubsystem.h"
#include "Network/GuLiHybridAdapterRegistry.h"
#include "Network/GuLiHybridManifestRegistry.h"
#include "Network/GuLiHybridManifest.h"
#include "Network/Private/GuLiHybridObjectConversions.h"
#include "Network/Private/GuLiHybridStateArchive.h"
#include "Battle/Framework/GuLiBattleGameState.h"
#include "Battle/Combat/GuLiCombatDamageLedger.h"
#include "GuLiNetHash.h"
#include "Engine/World.h"
#include <algorithm>

namespace
{
bool Read(const FGuLiHybridObjectState& O,GuLiNet::Buffs::State& S)
{const auto* C=O.Capabilities.Find(TEXT("buff_status"));return C&&C->Version==GuLiNet::Buffs::Version&&GuLiNet::Buffs::Decode({C->Data.GetData(),SIZE_T(C->Data.Num())},S)&&GuLiNetBridge::ToCore(O.Key)==GuLiNet::Buffs::Key(S.Host,S.Root,S.Instance);}
struct FPrivate{FGuLiHybridObjectKey Key;FGuid Event;uint64 At=0,Revision=0,Attempt=0;TArray<uint8> Payload;bool Submitted=false;};
void Archive(FGuLiHybridStateArchive& A,FPrivate& P)
{uint32 V=1;A.Value(V);A.Value(P.Key);A.Value(P.Event);A.Value(P.At);A.Value(P.Revision);A.Value(P.Attempt);A.Bytes(P.Payload,GuLiNet::MaximumCapabilityBytes);A.Value(P.Submitted);if(V!=1||!P.Key.IsValid()||P.Event.IsValid()!=!P.Payload.IsEmpty())A.Reject();if(!P.Payload.IsEmpty()){GuLiNet::Buffs::Command C;if(!GuLiNet::Buffs::DecodeCommand({P.Payload.GetData(),SIZE_T(P.Payload.Num())},C))A.Reject();}}
bool Decode(TConstArrayView<uint8> B,FPrivate& P){FGuLiHybridStateArchive A(B);Archive(A,P);return A.Done();}
class FBuffAdapter final : public IGuLiHybridSimulationAdapter
{
    TWeakObjectPtr<UGuLiHybridActorWorldSubsystem> Owner;FGuLiHybridObjectState Record;GuLiNet::Buffs::State State;FPrivate Private;bool Local=false;
    UWorld* World()const{return Owner.IsValid()?Owner->GetWorld():nullptr;}
    void Visual(bool Bootstrap){if(World())if(auto* V=World()->GetSubsystem<UGuLiBuffPresentation>()){if(Record.LifeState>=2)V->Remove(Record.Key);else V->Apply(Record.Key,State,Bootstrap);}}
    void Queue(const GuLiNet::Buffs::Command& C,UGuLiHybridWorldSubsystem& H)
    {
        const auto B=GuLiNet::Buffs::EncodeCommand(C);if(B.empty()){H.Suspend(TEXT("Buff pulse exceeds the persistent state encoding capacity"));return;}
        if(Private.Attempt==MAX_uint64){H.Suspend(TEXT("Buff request identity capacity exhausted"));return;}++Private.Attempt;
        GuLiNet::Writer W;W.Key(GuLiNetBridge::ToCore(Record.Key));W.U64(Record.RelationRevision);W.U64(Private.Attempt);W.Text(C.Operation);W.Key(C.Skip);GuLiNet::Id Event;GuLiNet::Id::Parse(GuLiNet::Sha256(W.Take()).substr(0,32),Event);
        Private.Event=GuLiNetBridge::ToUE(Event);Private.At=H.GetObjectLogicalTick(Record.Key);Private.Revision=Record.RelationRevision;Private.Payload.Reset();Private.Payload.Append(B.data(),int32(B.size()));Private.Submitted=false;
    }
public:
    explicit FBuffAdapter(UGuLiHybridActorWorldSubsystem& O):Owner(&O){}
    UObject* LifetimeOwner()const override{return Owner.Get();}uint32 Version()const override{return 1;}
    bool Prepare(const FGuLiHybridObjectState& O)override{Record=O;Private.Key=O.Key;return Read(O,State);}
    void SetAuthority(const FGuLiHybridObjectState& O,bool Value)override{Local=Value;Record=O;Read(O,State);Visual(true);}
    bool Execute(const FGuLiHybridCommand&,const FGuLiHybridCommandMember&)override{return false;}
    void Advance(uint64,double)override
    {
        if(!Local||!World()||Record.LifeState!=1)return;auto* H=World()->GetSubsystem<UGuLiHybridWorldSubsystem>();auto* D=World()->GetSubsystem<UGuLiDamageLedgerSubsystem>();if(!H||!D||!H->CanPublishFacts()||H->IsBattlePaused())return;
        if(Private.Event.IsValid())
        {
            FGuLiHybridTransactionResult R;
            if(H->FindTransactionResult(Private.Event,R)&&R.Status!=EGuLiHybridTransactionStatus::Deferred&&R.Status!=EGuLiHybridTransactionStatus::Paused)
            {Private.Event={};Private.Payload.Reset();Private.Submitted=false;}
            else {if(!Private.Submitted)Private.Submitted=H->SubmitCapability(Record.Key,Private.Event,TEXT("capability/buff/step"),Private.Revision,Private.Payload,Record.Team,Private.At).IsValid();return;}
        }
        if(GuLiNet::Buffs::HasPending(State))
        {
            for(const auto& Hit:State.Pending.Targets)if(!Hit.Settled)
            {
                const auto Key=GuLiNetBridge::ToUE(Hit.Target);FGuLiHybridObjectState Target;FGuLiTargetHandle TargetHandle;
                if(!H->GetObjectView(Key,Target)||Target.LifeState!=1||Target.Health.bDead||Target.bPhased||Target.Affiliation==Record.Team||!H->ResolveObjectTarget(Key,TargetHandle))
                {GuLiNet::Buffs::Command C;C.Operation="skip";C.Skip=Hit.Target;Queue(C,*H);return;}
                const FGuid Event=GuLiNetBridge::ToUE(GuLiNet::Buffs::HitEvent(State.Pending.Event,Hit.Target));FGuLiDamageCommitResult Result;
                if(H->FindDamageResult(Event,Result))continue;
                FGuLiDamageRequest R;R.MatchEpoch=D->GetMatchEpoch();R.DamageEventId=Event;R.RootEventId=GuLiNetBridge::ToUE(State.Pending.Event);R.ShotId=R.RootEventId;R.SkillId=TEXT("BuffDamage");R.Target=TargetHandle;R.Damage=State.Pending.Damage;R.HitLocation=FVector(State.Pending.X,State.Pending.Y,State.Pending.Z);
                H->ResolveObjectTarget(GuLiNetBridge::ToUE(State.Pending.Source),R.Source);H->SubmitDamage(R,State.Pending.At,{},&Record.Key,State.Pending.At);
            }return;
        }
        FGuLiHybridObjectState Host;const auto HostKey=GuLiNetBridge::ToUE(State.Host);const bool Alive=H->GetObjectView(HostKey,Host)&&Host.LifeState==1&&!Host.Health.bDead;
        const auto* Due=GuLiNet::Buffs::Due(State);const uint64 Now=H->GetObjectLogicalTick(Record.Key);
        if(!State.Active||!Alive||(!Due&&Now>State.Expires)){GuLiNet::Buffs::Command C;C.Operation="stop";Queue(C,*H);return;}
        if(!Due||Due->Next>Now)return;
        FGuLiTargetHandle Handle;FGuLiCombatTargetSnapshot Snapshot;if(!H->ResolveObjectTarget(HostKey,Handle)||!D->TryGetTargetSnapshot(Handle,Snapshot))return;
        GuLiNet::Buffs::Command C;C.Operation="pulse";const FVector Center=Snapshot.GetHitCenter();C.X=Center.X;C.Y=Center.Y;C.Z=Center.Z;C.HostRadius=Snapshot.CollisionRadius;
        const auto* Handler=GuLiNet::Buffs::Find(Due->Frozen.Handler);
        if(Handler&&Handler->Area)
        {
            TArray<FGuLiCombatTargetSnapshot> Targets;D->GetTargetSnapshots(Targets);const double Radius=double(C.HostRadius)*Due->Frozen.RadiusMultiplier;
            for(const auto& T:Targets)if(T.bAlive&&T.Team!=Record.Team&&T.Team!=EGuLiTeam::Unassigned&&FVector::DistSquared(Center,T.GetHitCenter())<=Radius*Radius)
            {FGuLiHybridObjectKey K;if(H->ResolveTarget(T.Handle,K))C.Targets.push_back(GuLiNetBridge::ToCore(K));}
            if(Host.Affiliation!=Record.Team)C.Targets.push_back(State.Host);
            std::sort(C.Targets.begin(),C.Targets.end());C.Targets.erase(std::unique(C.Targets.begin(),C.Targets.end()),C.Targets.end());
        }
        Queue(C,*H);
    }
    void ApplyLedger(const FGuLiHybridObjectState& O,uint64,bool)override{Record=O;if(!Read(O,State)){if(World())World()->GetSubsystem<UGuLiHybridWorldSubsystem>()->Suspend(TEXT("Invalid committed Buff state"));return;}Visual(false);}
    bool ApplyMotion(TConstArrayView<uint8>,uint64)override{return false;}
    bool Capture(TArray<uint8>& B)const override{auto P=Private;FGuLiHybridStateArchive A;Archive(A,P);return A.Finish(B);}
    bool Validate(TConstArrayView<uint8> B)const override{FPrivate P;return Decode(B,P)&&P.Key==Record.Key;}
    bool Restore(TConstArrayView<uint8> B,EGuLiHybridRestoreStage Stage)override{FPrivate P;if(!Decode(B,P)||P.Key!=Record.Key)return false;if(Stage==EGuLiHybridRestoreStage::Link){Private=MoveTemp(P);Private.Submitted=false;}if(Stage==EGuLiHybridRestoreStage::Activate)Visual(true);return true;}
    bool RestoreQuery(TConstArrayView<uint8> B,EGuLiHybridRestoreStage S)override{return Restore(B,S);}
    void Release()override{if(World())if(auto* V=World()->GetSubsystem<UGuLiBuffPresentation>())V->Remove(Record.Key);}
};
const FGuLiHybridAdapterRegistration Family({TEXT("BuffStatus"),EGuLiHybridObjectKind::Field,1,[](UGuLiHybridActorWorldSubsystem& O,const FGuLiHybridObjectState&)->TSharedPtr<IGuLiHybridSimulationAdapter>{return MakeShared<FBuffAdapter>(O);},nullptr,nullptr,EGuLiHybridOwnershipPolicy::RetainedSource,3,false,{}});
bool Append(UWorld&,FGuLiHybridManifestData& M,FString& Error)
{
    if(!FGuLiHybridAdapterRegistry::BindDefinition(M,TEXT("buff/status"),TEXT("BuffStatus"),Error))return false;
    auto& Step=M.Rules.AddDefaulted_GetRef();Step.Id=TEXT("capability/buff/step");Step.SourceDefinition=TEXT("buff/status");Step.Kind=EGuLiHybridRuleKind::Capability;Step.Capability=TEXT("buff_status");Step.CapabilityVersion=GuLiNet::Buffs::Version;Step.CapabilityOperation=TEXT("step");Step.EffectTicks=GuLiNet::TicksPerSecond*3600;
    auto& Damage=M.Rules.AddDefaulted_GetRef();Damage.Id=TEXT("settle/buff/status");Damage.SourceDefinition=TEXT("buff/status");Damage.Kind=EGuLiHybridRuleKind::Damage;Damage.Damage=1;Damage.MaximumTargets=GuLiNet::Buffs::MaximumTargets;Damage.EffectTicks=GuLiNet::TicksPerSecond*3600;
    M.Numbers.Add(TEXT("buffs/state/version"),GuLiNet::Buffs::Version);return true;
}
const FGuLiHybridManifestRegistration Provider({TEXT("Buffs"),EGuLiHybridManifestPhase::Derived,{TEXT("GroundRogue")},Append});
}
