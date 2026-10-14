#include "Gameplay/Skills/GuLiHybridArmy.h"
#include "Gameplay/Skills/Private/GuLiHybridArmyConversions.h"
#include "Gameplay/Skills/GuLiArmySkillSubsystem.h"
#include "Gameplay/Cards/GuLiRogueCardSubsystem.h"
#include "Commander/Mass/GuLiBattleAuthoritySubsystem.h"
#include "Commander/Framework/GuLiCommanderNetSyncComponent.h"
#include "Battle/Framework/GuLiBattlePlayerState.h"
#include "Battle/Framework/GuLiBattleGameState.h"
#include "Network/GuLiHybridActorWorldSubsystem.h"
#include "Network/GuLiHybridAdapterRegistry.h"
#include "Network/Private/GuLiHybridStateArchive.h"
#include "Network/Private/GuLiHybridObjectConversions.h"
#include "Engine/World.h"
#include "Gameplay/CombatEffects/GuLiCombatEffectPresentationSubsystem.h"
#include "NiagaraSystem.h"

class FGuLiHybridArmyBridge
{
public:
    static bool Apply(UWorld& W,const FGuLiHybridObjectState& O,bool Effects)
    {
        const auto* V=O.Capabilities.Find(TEXT("army_state"));GuLiNet::Army::State S;
        if (!V || V->Version!=GuLiNet::Army::Version || !GuLiNet::Army::DecodeState({V->Data.GetData(),SIZE_T(V->Data.Num())},S)) return false;
        auto* Skills=W.GetSubsystem<UGuLiArmySkillSubsystem>();auto* Cards=W.GetSubsystem<UGuLiRogueCardSubsystem>();
        const auto* Battle=W.GetGameState<AGuLiBattleGameState>();if (!Skills || !Cards || !Battle) return false;
        const uint32 Epoch=Battle->GetMatchEpoch();std::vector<GuLiNet::Army::Profile> Profiles;std::string Error;
        if (!GuLiNet::Army::Resolve(S.Definitions,S.Sources,S.Equipment,Profiles,Error)) return false;
        // Applying the committed capability after a handoff also completes its UI request.
        Cards->HybridRequests.Remove(GuLiNetBridge::ToUE(S.LastRequest));
        Skills->CommittedProfiles.RemoveAll([&](const auto& P){return P.Team==O.Team;});
        for (const auto& P:Profiles) Skills->CommittedProfiles.Add(GuLiArmyBridge::Profile(P,O.Team,uint32(S.RevisionValue)));
        Skills->CommittedMatchEpoch=Epoch;Skills->HybridTeamRevisions.Add(O.Team,uint32(S.RevisionValue));Skills->RebuildLookup();
        Skills->WeaponLoadoutCommitted.Broadcast(Skills->GetLoadoutRevision());
        TMap<uint16,float> Movement;
        for (const auto& [Id,Source]:S.Sources) for (const auto& [Unit,Bonus]:Source.MovementPercent)
        {auto* Multiplier=Movement.Find(Unit);if (Multiplier) *Multiplier*=1.f+Bonus;else Movement.Add(Unit,1.f+Bonus);}
        if (auto* Authority=W.GetSubsystem<UGuLiBattleAuthoritySubsystem>()) Authority->SetHybridMovementState(O.Team,Movement);
        auto& Counts=Cards->Acquisitions.FindOrAdd(uint32(O.Team));Counts.Reset();
        for (const auto& [Id,N]:S.Acquired) Counts.Add(UTF8_TO_TCHAR(Id.c_str()),int32(N));Cards->Epoch=Epoch;
        const FGuid Closed=S.Current.PresentationClosed?GuLiNetBridge::ToUE(S.Current.Session):FGuid{};
        const bool NewClosed=Closed.IsValid() && Cards->HybridClosedSessions.FindRef(uint32(O.Team))!=Closed;
        Cards->HybridClosedSessions.Add(uint32(O.Team),Closed);
        if (Effects && NewClosed && !S.Current.Selected.empty())
        {
            const auto* Card=Cards->FindCard(UTF8_TO_TCHAR(S.Current.Selected.c_str()));FGuLiRogueUpgradeCue Cue;
            if (Card && !Card->UpgradeVfx.IsNull() && GuLiRogueUpgrade::ParseColor(Card->UpgradeVfxColor,Cue.Color))
            {
                Cue.Session=GuLiNetBridge::ToUE(S.Current.Session);Cue.MatchEpoch=Epoch;Cue.Team=O.Team;Cue.UnitTypeId=uint16(Card->UnitTypeId);
                Cue.System=TSoftObjectPtr<UNiagaraSystem>(Card->UpgradeVfx.ToSoftObjectPath());Cue.Scale=Card->UpgradeVfxScale;
                Cue.StartTime=WorldTime(W);
                TArray<FGuLiHybridObjectState> Objects;W.GetSubsystem<UGuLiHybridWorldSubsystem>()->GetObjects(Objects);
                const FName Definition(*FString::Printf(TEXT("unit/%d"),Card->UnitTypeId));
                for (const auto& Object:Objects) if (Object.Key.Kind==EGuLiHybridObjectKind::Soldier && Object.Definition==Definition && Object.Team==O.Team && !Object.Health.bDead) Cue.Soldiers.Add(FGuLiSoldierId{Object.Ordinal});
                if (auto* Visual=W.GetSubsystem<UGuLiCombatEffectPresentationSubsystem>()) Visual->ApplyRogueUpgrade(Cue);
            }
        }
        for (auto It=W.GetPlayerControllerIterator();It;++It)
        {
            auto* PC=It->Get();auto* PS=PC?PC->GetPlayerState<AGuLiBattlePlayerState>():nullptr;
            auto* Channel=PC?PC->FindComponentByClass<UGuLiCommanderNetSyncComponent>():nullptr;
            if (!Channel || !PS || PS->GetTeam()!=O.Team || !PS->IsCommander()) continue;
            auto& Offer=Cards->Offers.FindOrAdd(Channel);const auto OldSession=Offer.Session;const bool WasApplied=Offer.bApplied;
            const auto OldRequest=Offer.Request;const auto OldCandidates=Offer.Candidates;
            const bool ContextChanged=Offer.HybridPresentationEpoch!=Epoch || Offer.Owner.Get()!=PS || Offer.Team!=O.Team;
            Offer.Request=GuLiNetBridge::ToUE(S.Current.Request);Offer.Session=GuLiNetBridge::ToUE(S.Current.Session);Offer.RerolledFrom=GuLiNetBridge::ToUE(S.Current.PreviousSession);
            Offer.Owner=PS;Offer.Team=O.Team;Offer.Candidates.Reset();for (const auto& Id:S.Current.Candidates) Offer.Candidates.Add(UTF8_TO_TCHAR(Id.c_str()));
            Offer.Selected=UTF8_TO_TCHAR(S.Current.Selected.c_str());Offer.bFinished=S.Current.Finished;Offer.bSubmitted=false;Offer.bApplied=!Offer.Selected.IsEmpty();
            Offer.bUpgradePublished=S.Current.PresentationClosed;Offer.CommittedTime=float(S.Current.CommittedAt/60.);
            Offer.HybridPresentationEpoch=Epoch;
            const bool OfferChanged=ContextChanged || OldSession!=Offer.Session || OldRequest!=Offer.Request || OldCandidates!=Offer.Candidates;
            if (!Offer.bUpgradePublished && Offer.Session.IsValid() && OfferChanged)
                Channel->ClientRogueCardOffer(Offer.Request,Offer.Session,Epoch,Offer.Candidates,TEXT(""));
            // UI reconciliation also runs on restore; business effects above
            // remain controlled separately by their committed event identity.
            if (Offer.bApplied && (ContextChanged || OldSession!=Offer.Session || !WasApplied))
                Channel->ClientRogueCardResult(Offer.Session,true,TEXT(""));
        }
        return true;
    }
private:
    static float WorldTime(UWorld& W) {const auto* B=W.GetGameState<AGuLiBattleGameState>();return B?B->GetServerWorldTimeSeconds():W.GetTimeSeconds();}
};
namespace GuLiHybridArmy
{
bool FindRoot(UWorld& W,EGuLiTeam Team,FGuLiHybridObjectState& Out)
{
    auto* H=W.GetSubsystem<UGuLiHybridWorldSubsystem>();if (!H) return false;TArray<FGuLiHybridObjectState> Objects;H->GetObjects(Objects);
    const auto* O=Objects.FindByPredicate([&](const auto& O){return O.Team==Team && O.Capabilities.Contains(TEXT("army_state"));});if (!O) return false;Out=*O;return true;
}
FGuid Request(UWorld& W,EGuLiTeam Team,EGuLiHybridArmyOperation Op,FGuid Event,FGuid Session,FString Choice,uint32 Expected,uint16 Unit,FName Slot)
{
    auto* H=W.GetSubsystem<UGuLiHybridWorldSubsystem>();FGuLiHybridObjectState O;
    if (!Event.IsValid() || !H || !FindRoot(W,Team,O) || !H->HasControl(O.Key) || !H->CanPublishFacts()) return {};
    GuLiNet::Army::Command Input;Input.Op=GuLiNet::Army::Operation(Op);Input.Request=GuLiNetBridge::ToCore(Event);Input.Session=GuLiNetBridge::ToCore(Session);
    Input.Choice=TCHAR_TO_UTF8(*Choice);Input.Expected=Expected;Input.EquipmentSlot={Unit,GuLiArmyBridge::Text(Slot)};const auto Bytes=GuLiNet::Army::EncodeCommand(Input);
    if (Bytes.empty()) return {};FGuLiHybridCommand C;C.Event=Event;C.LogicalTick=H->GetLogicalTick();C.Operation=TEXT("ArmyCapability");C.Arguments.Append(Bytes.data(),int32(Bytes.size()));
    C.Members.Add({O.Key,O.ControlEpoch,O.OwnerEpoch,0,{}});return H->RouteCommand(C)?Event:FGuid{};
}
bool Equip(const AGuLiBattlePlayerState& Player,uint16 Unit,FName Slot,FName Skill,uint32 Expected,FString& Error)
{
    const auto Event=Request(*Player.GetWorld(),Player.GetTeam(),EGuLiHybridArmyOperation::Equip,FGuid::NewGuid(),{},Skill.IsNone()?FString{}:Skill.ToString().ToLower(),Expected,Unit,Slot);
    if (!Event.IsValid()) {Error=TEXT("Army equipment request cannot be queued before the controller is ready");return false;}
    Error.Reset();return true;
}
}
namespace
{
constexpr uint32 Version=1;
struct FPending {FGuid Event;TArray<uint8> Payload;bool Submitted=false;};
struct FPrivate {FGuLiHybridObjectKey Key;TArray<FPending> Pending;};
void Serialize(FGuLiHybridStateArchive& A,FPrivate& P)
{
    uint32 SerializedVersion=Version;A.Value(SerializedVersion);A.Value(P.Key);A.Array(P.Pending,32,[](auto& A,auto& V){A.Value(V.Event);A.Array(V.Payload,2048,[](auto& A,uint8& Byte){A.Value(Byte);});A.Value(V.Submitted);});
    if (SerializedVersion!=Version || !P.Key.IsValid()) A.Reject();TSet<FGuid> Seen;
    for (const auto& V:P.Pending)
    {GuLiNet::Army::Command C;if (!V.Event.IsValid() || Seen.Contains(V.Event) || !GuLiNet::Army::DecodeCommand({V.Payload.GetData(),SIZE_T(V.Payload.Num())},C) || GuLiNetBridge::ToUE(C.Request)!=V.Event) A.Reject();Seen.Add(V.Event);}
}
bool Decode(TConstArrayView<uint8> B,FPrivate& P) {FGuLiHybridStateArchive A(B);Serialize(A,P);return A.Done();}
class FArmyAdapter final:public IGuLiHybridSimulationAdapter
{
    TWeakObjectPtr<UGuLiHybridActorWorldSubsystem> Owner;FGuLiHybridObjectState Record;FPrivate Private;bool Local=false;
    UWorld* World() const {return Owner.IsValid()?Owner->GetWorld():nullptr;}
public:
    explicit FArmyAdapter(UGuLiHybridActorWorldSubsystem& O):Owner(&O) {}
    UObject* LifetimeOwner() const override {return Owner.Get();}
    uint32 Version() const override {return ::Version;}
    bool Prepare(const FGuLiHybridObjectState& O) override
    {Record=O;Private.Key=O.Key;return O.Capabilities.Contains(TEXT("army_state"));}
    void SetAuthority(const FGuLiHybridObjectState& O,bool L) override {Record=O;Local=L;ApplyLedger(O,0,false);}
    bool Execute(const FGuLiHybridCommand& C,const FGuLiHybridCommandMember& M) override
    {
        GuLiNet::Army::Command Input;
        if (!Local || M.Key!=Record.Key || C.Operation!=TEXT("ArmyCapability") || !GuLiNet::Army::DecodeCommand({C.Arguments.GetData(),SIZE_T(C.Arguments.Num())},Input)
            || GuLiNetBridge::ToUE(Input.Request)!=C.Event) return false;
        if (const auto* P=Private.Pending.FindByPredicate([&](const auto& V){return V.Event==C.Event;})) return P->Payload==C.Arguments;
        if (Private.Pending.Num()>=32) return false;Private.Pending.Add({C.Event,C.Arguments,false});return true;
    }
    void Advance(uint64,double) override
    {
        auto* H=World()->GetSubsystem<UGuLiHybridWorldSubsystem>();if (!Local || Private.Pending.IsEmpty()) return;
        auto& P=Private.Pending[0];FGuLiHybridTransactionResult R;
        if (H->FindTransactionResult(P.Event,R))
        {
            if (R.Status==EGuLiHybridTransactionStatus::Deferred || R.Status==EGuLiHybridTransactionStatus::Paused) return;
            Private.Pending.RemoveAt(0);return;
        }
        if (!P.Submitted && H->CanPublishFacts()) P.Submitted=H->SubmitCapability(Record.Key,P.Event,TEXT("capability/army/mutate"),Record.RelationRevision,P.Payload,Record.Team).IsValid();
    }
    void ApplyLedger(const FGuLiHybridObjectState& O,uint64,bool Effects) override
    {
        Record=O;const auto* V=O.Capabilities.Find(TEXT("army_state"));GuLiNet::Army::State S;
        if (V && GuLiNet::Army::DecodeState({V->Data.GetData(),SIZE_T(V->Data.Num())},S))
            Private.Pending.RemoveAll([&](const auto& P){return P.Event==GuLiNetBridge::ToUE(S.LastRequest);});
        if (World() && !FGuLiHybridArmyBridge::Apply(*World(),O,Effects)) World()->GetSubsystem<UGuLiHybridWorldSubsystem>()->Suspend(TEXT("Army capability could not apply its committed state"));
    }
    bool ApplyMotion(TConstArrayView<uint8>,uint64) override {return false;}
    bool Capture(TArray<uint8>& B) const override {auto P=Private;FGuLiHybridStateArchive A;Serialize(A,P);return A.Finish(B);}
    bool Validate(TConstArrayView<uint8> B) const override {FPrivate P;return Decode(B,P) && P.Key==Record.Key;}
    bool Restore(TConstArrayView<uint8> B,EGuLiHybridRestoreStage S) override
    {FPrivate P;if (!Decode(B,P) || P.Key!=Record.Key) return false;if (S==EGuLiHybridRestoreStage::Link) Private=MoveTemp(P);if (S==EGuLiHybridRestoreStage::Activate) ApplyLedger(Record,0,false);return true;}
    bool RestoreQuery(TConstArrayView<uint8> B,EGuLiHybridRestoreStage S) override {return Restore(B,S);}
    void Release() override {Private.Pending.Reset();}
};
const FGuLiHybridAdapterRegistration Family({TEXT("ArmyState"),EGuLiHybridObjectKind::BusinessWork,Version,
    [](UGuLiHybridActorWorldSubsystem& O,const FGuLiHybridObjectState&)->TSharedPtr<IGuLiHybridSimulationAdapter>{return MakeShared<FArmyAdapter>(O);},nullptr,nullptr,EGuLiHybridOwnershipPolicy::SharedGroup,0,false,TEXT("match/resources")});
}
