#include "Gameplay/GroundMech/Rogue/GuLiGroundRogueRuntime.h"
#include "Gameplay/Cards/GuLiRogueCardSubsystem.h"
#include "Gameplay/Cards/GuLiRogueCardSettings.h"
#include "Gameplay/Skills/GuLiHybridArmy.h"
#include "Battle/Framework/GuLiBattlePlayerState.h"
#include "Battle/Framework/GuLiBattleGameState.h"
#include "Commander/Framework/GuLiCommanderNetSyncComponent.h"
#include "Network/GuLiHybridActorWorldSubsystem.h"
#include "Network/GuLiHybridAdapterRegistry.h"
#include "Network/GuLiHybridManifestRegistry.h"
#include "Network/GuLiHybridManifest.h"
#include "Network/Private/GuLiHybridObjectConversions.h"
#include "Network/Private/GuLiHybridStateArchive.h"
#include "GuLiNetGroundRogue.h"
#include "Engine/World.h"

namespace GuLiGroundRogue
{
bool ReadBuild(UWorld& W,FGuid Player,TMap<FString,int32>& Counts,uint64& Revision,GuLiNet::Effects::Set* Resolved)
{
    Counts.Reset();Revision=1;if(Resolved)Resolved->clear();auto* H=W.GetSubsystem<UGuLiHybridWorldSubsystem>();if(!H)return false;
    for(auto Team:{EGuLiTeam::Red,EGuLiTeam::Blue})
    {
        FGuLiHybridObjectState Root;if(!H->GetTeamCapabilityObject(Team,TEXT("ground_rogue"),Root))continue;
        const auto* V=Root.Capabilities.Find(TEXT("ground_rogue"));GuLiNet::GroundRogue::State S;
        if(!V||!GuLiNet::GroundRogue::DecodeState({V->Data.GetData(),SIZE_T(V->Data.Num())},S))return false;
        if(const auto B=S.Players.find(GuLiNetBridge::ToCore(Player));B!=S.Players.end())
        {
            Revision=B->second.RevisionValue;for(const auto& [Id,N]:B->second.Acquired)Counts.Add(UTF8_TO_TCHAR(Id.c_str()),int32(N));
            if(Resolved&&!GuLiNet::GroundRogue::Resolve(S.Definitions,B->second,*Resolved))return false;return true;
        }
    }return true;
}
FGuid Request(const AGuLiBattlePlayerState& PS,EGuLiHybridArmyOperation Op,FGuid Event,FGuid Session,FString Choice)
{
    auto* H=PS.GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>();FGuLiHybridObjectState Root;
    if(PS.GetBattleRole()!=EGuLiCommanderRole::Ground||!PS.IsBattleReady()||!H||!H->GetTeamCapabilityObject(PS.GetTeam(),TEXT("ground_rogue"),Root)||!H->HasControl(Root.Key)||!H->CanPublishFacts())return {};
    GuLiNet::Army::Command Input;Input.Op=GuLiNet::Army::Operation(Op);Input.Request=GuLiNetBridge::ToCore(Event);Input.Session=GuLiNetBridge::ToCore(Session);Input.Choice=TCHAR_TO_UTF8(*Choice);
    const auto Bytes=GuLiNet::Army::EncodeCommand(Input);if(Bytes.empty())return {};FGuLiHybridCommand C;C.Event=Event;C.LogicalTick=H->GetLogicalTick();C.Operation=TEXT("GroundRogue");C.Arguments.Append(Bytes.data(),int32(Bytes.size()));C.Members.Add({Root.Key,Root.ControlEpoch,Root.OwnerEpoch,0,{}});return H->RouteCommand(C)?Event:FGuid{};
}
}

class FGuLiHybridGroundRogueBridge
{
public:
static bool Apply(UWorld& W,const FGuLiHybridObjectState& O)
{
    auto* Cards=W.GetSubsystem<UGuLiRogueCardSubsystem>();const auto* B=W.GetGameState<AGuLiBattleGameState>();const auto* V=O.Capabilities.Find(TEXT("ground_rogue"));GuLiNet::GroundRogue::State S;
    if(!Cards||!B||!V||!GuLiNet::GroundRogue::DecodeState({V->Data.GetData(),SIZE_T(V->Data.Num())},S))return false;
    Cards->Epoch=B->GetMatchEpoch();
    if(Cards->GroundEpoch!=Cards->Epoch){Cards->GroundAcquisitions.Reset();Cards->GroundRevisions.Reset();Cards->GroundEpoch=Cards->Epoch;}
    for(const auto& [Id,Build]:S.Players)
    {
        const FGuid Player=GuLiNetBridge::ToUE(Id);auto& Counts=Cards->GroundAcquisitions.FindOrAdd(Player);Counts.Reset();for(const auto& [Card,N]:Build.Acquired)Counts.Add(UTF8_TO_TCHAR(Card.c_str()),int32(N));
        const bool Changed=Cards->GroundRevisions.FindRef(Player)!=Build.RevisionValue;Cards->GroundRevisions.Add(Player,Build.RevisionValue);Cards->HybridRequests.Remove(GuLiNetBridge::ToUE(Build.LastRequest));
        if(Changed)Cards->OnGroundBuildChanged.Broadcast(Player,int64(Build.RevisionValue));
    }
    for(auto It=W.GetPlayerControllerIterator();It;++It)
    {
        auto* PC=It->Get();auto* PS=PC?PC->GetPlayerState<AGuLiBattlePlayerState>():nullptr;auto* Channel=PC?PC->FindComponentByClass<UGuLiCommanderNetSyncComponent>():nullptr;
        if(!PS||!Channel||PS->GetTeam()!=O.Team||PS->GetBattleRole()!=EGuLiCommanderRole::Ground)continue;
        const auto Build=S.Players.find(GuLiNetBridge::ToCore(PS->GetPlayerGuid()));if(Build==S.Players.end())continue;const auto& Current=Build->second.Current;
        auto& Offer=Cards->Offers.FindOrAdd(Channel);const auto OldSession=Offer.Session,OldRequest=Offer.Request;const bool WasApplied=Offer.bApplied;
        const bool Context=Offer.Owner.Get()!=PS||Offer.Team!=O.Team||Offer.HybridPresentationEpoch!=Cards->Epoch;
        Offer.Owner=PS;Offer.Team=O.Team;Offer.HybridPresentationEpoch=Cards->Epoch;Offer.Request=GuLiNetBridge::ToUE(Current.Request);Offer.Session=GuLiNetBridge::ToUE(Current.Session);Offer.RerolledFrom=GuLiNetBridge::ToUE(Current.PreviousSession);Offer.Candidates.Reset();
        for(const auto& Id:Current.Candidates)Offer.Candidates.Add(UTF8_TO_TCHAR(Id.c_str()));Offer.Selected=UTF8_TO_TCHAR(Current.Selected.c_str());Offer.bApplied=!Offer.Selected.IsEmpty();Offer.bFinished=Current.Finished;Offer.bSubmitted=false;Offer.bUpgradePublished=Current.PresentationClosed;
        GuLiNet::Effects::Set Resolved;const bool Capacity=Offer.Candidates.IsEmpty()&&GuLiNet::GroundRogue::Resolve(S.Definitions,Build->second,Resolved)&&GuLiNet::Effects::GetRicochet(Resolved).Maximum>=GuLiNet::GroundRogue::MaximumHops;
        if(Offer.Session.IsValid()&&!Offer.bUpgradePublished&&(Context||OldSession!=Offer.Session||OldRequest!=Offer.Request))Channel->ClientRogueCardOffer(Offer.Request,Offer.Session,Cards->Epoch,Offer.Candidates,Capacity?TEXT("UI.RogueCards.Capacity"):TEXT(""));
        if(Offer.bApplied&&(Context||OldSession!=Offer.Session||!WasApplied))Channel->ClientRogueCardResult(Offer.Session,true,TEXT(""));
    }return true;
}
};
namespace
{
struct FPending {FGuid Event;FGuid Controller;TArray<uint8> Payload;bool Submitted=false;};
struct FPrivate {FGuLiHybridObjectKey Key;TArray<FPending> Pending;};
void Serialize(FGuLiHybridStateArchive& A,FPrivate& P)
{
    uint32 Version=2;A.Value(Version);A.Value(P.Key);A.Array(P.Pending,32,[](auto& A,auto& V){A.Value(V.Event);A.Value(V.Controller);A.Array(V.Payload,2048,[](auto& A,uint8& B){A.Value(B);});A.Value(V.Submitted);});
    if(Version!=2||!P.Key.IsValid())A.Reject();TSet<FGuid> Seen;for(const auto& V:P.Pending){GuLiNet::Army::Command C;if(!V.Controller.IsValid()||!V.Event.IsValid()||Seen.Contains(V.Event)||!GuLiNet::Army::DecodeCommand({V.Payload.GetData(),SIZE_T(V.Payload.Num())},C)||GuLiNetBridge::ToUE(C.Request)!=V.Event)A.Reject();Seen.Add(V.Event);}
}
bool Decode(TConstArrayView<uint8> B,FPrivate& P){FGuLiHybridStateArchive A(B);Serialize(A,P);return A.Done();}
class FGroundRogueAdapter final:public IGuLiHybridSimulationAdapter
{
    TWeakObjectPtr<UGuLiHybridActorWorldSubsystem> Owner;FGuLiHybridObjectState Record;FPrivate Private;bool Local=false;
    UWorld* World()const{return Owner.IsValid()?Owner->GetWorld():nullptr;}
public:
    explicit FGroundRogueAdapter(UGuLiHybridActorWorldSubsystem& O):Owner(&O){}
    UObject* LifetimeOwner()const override{return Owner.Get();}uint32 Version()const override{return 2;}
    bool Prepare(const FGuLiHybridObjectState& O)override{Record=O;Private.Key=O.Key;return O.Capabilities.Contains(TEXT("ground_rogue"));}
    void SetAuthority(const FGuLiHybridObjectState& O,bool L)override{Local=L;ApplyLedger(O,0,false);}
    bool Execute(const FGuLiHybridCommand& C,const FGuLiHybridCommandMember& M)override
    {GuLiNet::Army::Command Input;if(!Local||M.Key!=Record.Key||C.Operation!=TEXT("GroundRogue")||!GuLiNet::Army::DecodeCommand({C.Arguments.GetData(),SIZE_T(C.Arguments.Num())},Input)||GuLiNetBridge::ToUE(Input.Request)!=C.Event)return false;if(const auto* Existing=Private.Pending.FindByPredicate([&](const auto& V){return V.Event==C.Event;}))return Existing->Payload==C.Arguments;if(Private.Pending.Num()>=32)return false;Private.Pending.Add({C.Event,Record.Controller,C.Arguments,false});return true;}
    void Advance(uint64,double)override
    {
        if(!Local||Private.Pending.IsEmpty()||!World())return;auto* H=World()->GetSubsystem<UGuLiHybridWorldSubsystem>();auto& P=Private.Pending[0];FGuLiHybridTransactionResult R;
        if(P.Controller!=Record.Controller){Private.Pending.RemoveAt(0);return;}
        if(H->FindTransactionResult(P.Event,R)){if(R.Status!=EGuLiHybridTransactionStatus::Deferred&&R.Status!=EGuLiHybridTransactionStatus::Paused)Private.Pending.RemoveAt(0);return;}
        if(!P.Submitted&&H->CanPublishFacts())P.Submitted=H->SubmitCapability(Record.Key,P.Event,TEXT("capability/ground_rogue/mutate"),Record.RelationRevision,P.Payload,Record.Team).IsValid();
    }
    void ApplyLedger(const FGuLiHybridObjectState& O,uint64,bool)override{Record=O;if(World()&&!FGuLiHybridGroundRogueBridge::Apply(*World(),O))World()->GetSubsystem<UGuLiHybridWorldSubsystem>()->Suspend(TEXT("Ground rogue state could not apply"));}
    bool ApplyMotion(TConstArrayView<uint8>,uint64)override{return false;}
    bool Capture(TArray<uint8>& B)const override{auto P=Private;FGuLiHybridStateArchive A;Serialize(A,P);return A.Finish(B);}
    bool Validate(TConstArrayView<uint8> B)const override{FPrivate P;return Decode(B,P)&&P.Key==Record.Key;}
    bool Restore(TConstArrayView<uint8> B,EGuLiHybridRestoreStage S)override{FPrivate P;if(!Decode(B,P)||P.Key!=Record.Key)return false;if(S==EGuLiHybridRestoreStage::Link){Private=MoveTemp(P);for(auto& V:Private.Pending)V.Submitted=false;}if(S==EGuLiHybridRestoreStage::Activate)ApplyLedger(Record,0,false);return true;}
    bool RestoreQuery(TConstArrayView<uint8> B,EGuLiHybridRestoreStage S)override{return Restore(B,S);}
    void Release()override{Private.Pending.Reset();}
};
const FGuLiHybridAdapterRegistration Family({TEXT("GroundRogue"),EGuLiHybridObjectKind::BusinessWork,2,[](UGuLiHybridActorWorldSubsystem& O,const FGuLiHybridObjectState&)->TSharedPtr<IGuLiHybridSimulationAdapter>{return MakeShared<FGroundRogueAdapter>(O);},nullptr,nullptr,EGuLiHybridOwnershipPolicy::SharedGroup,1,false,TEXT("match/resources")});
bool Append(UWorld&,FGuLiHybridManifestData& M,FString& Error)
{
    GuLiNet::GroundRogue::Catalog C;if(!GuLiGroundRogue::LoadCatalog(C,Error))return false;
    const auto Bytes=GuLiNet::GroundRogue::EncodeCatalog(C);if(Bytes.empty()){Error=TEXT("Ground rogue graph is invalid");return false;}
    M.Numbers.Add(TEXT("ground_rogue/catalog/chunks"),(Bytes.size()+2047)/2048);constexpr TCHAR Hex[]=TEXT("0123456789abcdef");
    for(SIZE_T At=0;At<Bytes.size();At+=2048){FString V;for(SIZE_T I=At;I<FMath::Min<SIZE_T>(At+2048,Bytes.size());++I){V.AppendChar(Hex[Bytes[I]>>4]);V.AppendChar(Hex[Bytes[I]&15]);}M.Strings.Add(TEXT("ground_rogue/catalog/")+FString::FromInt(int32(At/2048)),MoveTemp(V));}
    if(!FGuLiHybridAdapterRegistry::BindDefinition(M,TEXT("team/ground_rogue"),TEXT("GroundRogue"),Error))return false;
    for(auto Team:{EGuLiTeam::Red,EGuLiTeam::Blue}){auto& O=M.Objects.AddDefaulted_GetRef();O.Definition=TEXT("team/ground_rogue");O.Team=Team;O.EstimatedCost=.01;O.Key=FGuLiHybridManifest::MakeStableKey(M.MapPackage,EGuLiHybridObjectKind::BusinessWork,FString::Printf(TEXT("ground_rogue/%u"),uint8(Team)));}
    auto& R=M.Rules.AddDefaulted_GetRef();R.Id=TEXT("capability/ground_rogue/mutate");R.SourceDefinition=TEXT("team/ground_rogue");R.Kind=EGuLiHybridRuleKind::Capability;R.Capability=TEXT("ground_rogue");R.CapabilityVersion=GuLiNet::GroundRogue::Version;R.CapabilityOperation=TEXT("mutate");R.EffectTicks=3600;
    const auto Rules=M.Rules;for(const auto& Damage:Rules)if(Damage.Kind==EGuLiHybridRuleKind::Damage&&Damage.SourceDefinition==TEXT("player/ground")){auto& Plan=M.Rules.AddDefaulted_GetRef();Plan.Id=TEXT("plan/ricochet/effect/linear/")+Damage.Id;Plan.SourceDefinition=TEXT("effect/linear/")+Damage.Id;Plan.Kind=EGuLiHybridRuleKind::Capability;Plan.Capability=TEXT("ground_ricochet");Plan.CapabilityVersion=GuLiNet::GroundRogue::Version;Plan.CapabilityOperation=TEXT("plan");Plan.EffectTicks=Damage.EffectTicks;}
    return true;
}
const FGuLiHybridManifestRegistration Provider({TEXT("GroundRogue"),EGuLiHybridManifestPhase::Derived,{TEXT("Flights")},Append});
}
