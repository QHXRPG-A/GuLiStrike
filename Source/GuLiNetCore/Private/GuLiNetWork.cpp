#include "GuLiNetOwnerState.h"
#include "GuLiNetHash.h"
#include <algorithm>
#include <limits>

namespace GuLiNet
{
Bytes EncodeFrozenWork(const FrozenWorkRecipe& V)
{
    Bytes Original;
    if (!CanAdmitWork(V.Request.Kind) || !V.Request.Admission.empty()
        || !EncodeMessage({V.Request.Header,EncodeTransaction(V.Request)},Original)) return {};
    Writer W(4096);W.U32(FrozenWorkSchema);W.Blob(Original);
    W.U8(IsResourceTransaction(V.Request.Kind)?1:0);
    if (IsResourceTransaction(V.Request.Kind)) W.Blob(EncodeResourceOutcome(V.Resource));
    W.Identity(V.Reserved.Token);W.Identity(V.Reserved.Key.Account);W.U64(V.Reserved.Key.Request);
    W.U8(static_cast<std::uint8_t>(V.Reserved.Faction));WriteResources(W,V.Reserved.Cost);return W.Take();
}
bool DecodeFrozenWork(const ObjectRecord& Work,FrozenWorkRecipe& Out)
{
    Reader R(Work.FrozenWork);std::uint32_t Version=0;std::uint8_t HasResource=0,Faction=0;
    Bytes Original,Resource;Message M;FrozenWorkRecipe V;
    if (Work.Definition!=TransactionWorkDefinition || Work.Grant.Object.Kind!=ObjectKind::BusinessWork
        || !R.U32(Version) || Version!=FrozenWorkSchema || !R.Blob(Original,2048) || !DecodeMessage(Original,M)
        || M.Header.Kind!=MessageKind::Transaction || !M.Header.Match.Valid() || !M.Header.Epoch
        || !M.Header.Sender.Valid() || !M.Header.OwnerEpoch || !M.Header.ControlEpoch
        || !DecodeTransaction(M.Header,M.Body,V.Request) || !CanAdmitWork(V.Request.Kind) || !V.Request.Admission.empty()
        || !M.Header.Event.Valid() || V.Request.RootEvent!=M.Header.Event || V.Request.StartedAt>M.Header.At
        || Work.Parent!=M.Header.Object || !Work.Parent.Valid() || Work.BornAt!=M.Header.At
        || Work.Grant.SourceLease!=StableId("GuLiTransactionAdmission/v1",M.Header.Event.ToString())
        || Work.Grant.Object.StableId!=StableId("GuLiTransactionWork/v1",M.Header.Event.ToString())
        || Work.Grant.Object.Generation!=1 || V.Request.Faction!=Work.Grant.Faction
        || !R.U8(HasResource) || HasResource>1 || (HasResource!=0)!=IsResourceTransaction(V.Request.Kind)) return false;
    if (HasResource && (!R.Blob(Resource,1024) || !DecodeResourceOutcome(Resource,V.Resource)
        || !V.Resource.IsCommitted() || V.Resource.Event!=M.Header.Event || V.Resource.Source!=Work.Parent
        || V.Resource.Target!=V.Request.Target || V.Resource.Node!=V.Request.ResourceNode
        || static_cast<std::uint8_t>(V.Resource.Operation)!=static_cast<std::uint8_t>(V.Request.Kind)-static_cast<std::uint8_t>(TransactionKind::Extract))) return false;
    if (!R.Identity(V.Reserved.Token) || !R.Identity(V.Reserved.Key.Account) || !R.U64(V.Reserved.Key.Request)
        || !R.U8(Faction) || Faction>2 || !ReadResources(R,V.Reserved.Cost) || !R.Done()) return false;
    V.Reserved.Faction=static_cast<Team>(Faction);
    if (!HasResource && V.Request.Kind!=TransactionKind::Upgrade && (!V.Reserved.Token.Valid() || V.Reserved.Token!=V.Request.ReservationToken
        || V.Reserved.Key!=V.Request.ReservationKey || V.Reserved.Faction!=V.Request.Faction)) return false;
    if (V.Request.Kind==TransactionKind::Upgrade && (V.Reserved.Token.Valid() || V.Reserved.Key.Account.Valid()
        || V.Reserved.Key.Request || V.Reserved.Cost.Positive() || V.Reserved.Faction!=Team::None || !Work.Upgrades.empty())) return false;
    Out=std::move(V);return true;
}
bool OwnerState::CloseWork(ObjectRecord& Work,CommitResult& Out,Tick At)
{
    FrozenWorkRecipe Recipe;
    if (!DecodeFrozenWork(Work,Recipe) || Work.Grant.State!=Life::Alive || Work.RelationVersion==UINT64_MAX
        || !ActiveWorks.contains(Work.Parent) || ActiveWorks.at(Work.Parent)!=Work.Grant.Object) return false;
    if (Recipe.Request.Kind==TransactionKind::Extract)
    {
        auto I=HeldOre.find(Recipe.Request.ResourceNode);if (I==HeldOre.end() || !I->second) return false;
        if (!--I->second) HeldOre.erase(I);
    }
    if (Recipe.Request.Kind==TransactionKind::Deliver)
    {
        const auto Held=static_cast<std::uint64_t>(Recipe.Resource.RawAmount.Blue)+Recipe.Resource.RawAmount.Red;
        auto I=HeldFactoryCapacity.find(Recipe.Request.Target);
        if (I==HeldFactoryCapacity.end() || I->second<Held) return false;
        I->second-=Held;if (!I->second) HeldFactoryCapacity.erase(I);
    }
    ActiveWorks.erase(Work.Parent);ActiveWorkEvents.erase(Recipe.Request.Header.Event);
    Work.Grant.State=Life::Destroyed;Work.DiedAt=At;++Work.RelationVersion;
    Out.FinalizedWork=Work;return true;
}
CommitResult OwnerState::AdmitWork(Id Sender,const TransactionRequest& R)
{
    const auto& Previous=PreviousObjects();
    CommitResult Out;Out.Event=R.Header.Event;Out.Kind=TransactionKind::AdmitWork;Out.Target=R.Target;Out.Faction=R.Faction;Out.StateDomain=DecisionDomain;
    Message M;FrozenWorkRecipe Recipe;
    const bool Parsed=DecodeMessage(R.Admission,M) && DecodeTransaction(M.Header,M.Body,Recipe.Request);
    if (Parsed) {Out.WorkEvent=M.Header.Event;Out.WorkOperation=Recipe.Request.Kind;}
    auto Finish=[&](CommitStatus Status)
    {
        Out.Status=Status;Out.StateVersion=NextDecisionVersion();Out.Balance=EconomyLedger.Balance(Out.Faction);
        Out.MiningRevision=MiningReservationsValue.Snapshot().Version;Remember(R,Out);return Out;
    };
    const auto Source=Previous.find(R.Header.Object);
    const auto& Original=Recipe.Request;
    const bool FinalizingReservation=Original.Kind==TransactionKind::Commit || Original.Kind==TransactionKind::Refund;
    if (!Parsed || Source==Previous.end() || (Source->second.Grant.State!=Life::Alive
        && !(FinalizingReservation && Source->second.Grant.State==Life::Dead))
        || M.Header.Kind!=MessageKind::Transaction || M.Header.Object!=R.Header.Object || M.Header.Sender!=Sender
        || M.Header.Match!=Match || M.Header.Epoch!=MatchEpoch || M.Header.At!=R.Header.At
        || M.Header.OwnerEpoch!=R.Header.OwnerEpoch || M.Header.ControlEpoch!=R.Header.ControlEpoch
        || !M.Header.Event.Valid() || Original.StartedAt>M.Header.At || Original.RootEvent!=M.Header.Event
        || !CanAdmitWork(Original.Kind) || !Original.Admission.empty() || Results.contains(M.Header.Event) || ActiveWorkEvents.contains(M.Header.Event)
        || M.Header.Event!=R.RootEvent || R.RuleId!=Original.RuleId || R.Faction!=Source->second.Grant.Faction
        || Original.Faction!=R.Faction || R.Header.Event!=StableId("GuLiTransactionAdmission/v1",M.Header.Event.ToString())
        || R.Target.Kind!=ObjectKind::BusinessWork || R.Target.Generation!=1
        || R.Target.StableId!=StableId("GuLiTransactionWork/v1",M.Header.Event.ToString())
        || Previous.contains(R.Target) || ActiveWorks.contains(R.Header.Object) || Previous.size()>=100000
        || R.ReservationToken.Valid() || R.ReservationKey.Account.Valid() || R.ReservationKey.Request
        || R.Related.Valid() || R.ExpectedRelationVersion || R.ResourceNode || R.MiningCluster || R.MiningTask || R.MiningSample
        || R.MiningToken.Valid() || R.TargetX!=0 || R.TargetY!=0 || R.TargetZ!=0 || R.FacingYaw!=0
        || !R.UpgradeNode.empty() || R.ExpectedUpgradeVersion)
        return Finish(CommitStatus::BadRequest);
    const auto RuleIt=Rules.find(Original.RuleId);
    if (RuleIt==Rules.end() || RuleIt->second.Kind!=Original.Kind || RuleIt->second.SourceDefinition!=Source->second.Definition
        || Original.StartedAt<Source->second.BornAt || (!FinalizingReservation && M.Header.At-Original.StartedAt>RuleIt->second.MaxEffectLifetime)
        || M.Header.At<Source->second.BornAt || M.Header.At-Source->second.BornAt<RuleIt->second.MinimumAge)
        return Finish(CommitStatus::InvalidRule);
    if (Original.MiningCluster || Original.MiningTask || Original.MiningSample || Original.Related.Valid()
        || Original.ExpectedRelationVersion || Original.TargetX!=0 || Original.TargetY!=0 || Original.TargetZ!=0 || Original.FacingYaw!=0
        || (Original.Kind!=TransactionKind::Extract && Original.MiningToken.Valid()))
        return Finish(CommitStatus::BadRequest);
    if (!RuleCapabilityAllowed(Source->second,RuleIt->second)) return Finish(CommitStatus::Unauthorized);
    // Freeze only a valid operation. No other request may consume this root or
    // reservation while the admitted work awaits its final result.
    if (Attacks.contains(Original.RootEvent)) return Finish(CommitStatus::InvalidRule);
    if (const auto Last=LastAttack.find({R.Header.Object,Original.RuleId});Last!=LastAttack.end()
        && (Original.StartedAt<Last->second || Original.StartedAt-Last->second<RuleIt->second.Cooldown))
        return Finish(CommitStatus::InvalidRule);
    if (IsResourceTransaction(Original.Kind))
    {
        if (Original.ReservationToken.Valid() || Original.ReservationKey.Account.Valid() || Original.ReservationKey.Request
            || (Original.Kind==TransactionKind::Extract ? Original.Target.Valid() || !Original.ResourceNode : Original.ResourceNode!=0))
            return Finish(CommitStatus::BadRequest);
        if (Original.Kind==TransactionKind::Extract)
        {
            const auto Ore=ResourceLedgerValue.Snapshot().Remaining.find(Original.ResourceNode);
            const auto Held=HeldOre.find(Original.ResourceNode);
            if (!MiningReservationsValue.OwnsNode(R.Header.Object,Original.MiningToken,Original.ResourceNode)
                || Ore==ResourceLedgerValue.Snapshot().Remaining.end() || (Held!=HeldOre.end() && Ore->second<=Held->second))
                return Finish(CommitStatus::InvalidReservation);
        }
        if (Original.Kind==TransactionKind::Deliver)
        {
            const auto Target=Previous.find(Original.Target);
            if (Target==Previous.end() || Target->second.Grant.State!=Life::Alive || !Target->second.Completed
                || Target->second.Definition!=RuleIt->second.TargetDefinition) return Finish(CommitStatus::InvalidRule);
        }
        if (Original.Kind==TransactionKind::Process && (Original.Target!=R.Header.Object || !Source->second.Completed))
            return Finish(CommitStatus::InvalidRule);
        Recipe.Resource=ResourceLedgerValue.Preview(static_cast<ResourceOperation>(static_cast<std::uint8_t>(Original.Kind)-static_cast<std::uint8_t>(TransactionKind::Extract)),
            R.Header.Object,Original.Target,Original.ResourceNode,M.Header.Event,Original.StartedAt);
        if (!Recipe.Resource.IsCommitted()) return Finish(CommitStatus::InvalidRule);
        if (Original.Kind==TransactionKind::Deliver)
        {
            const auto& Factory=ResourceLedgerValue.Snapshot().Factories.at(Original.Target);
            const auto Amount=static_cast<std::uint64_t>(Recipe.Resource.RawAmount.Blue)+Recipe.Resource.RawAmount.Red;
            const auto Held=HeldFactoryCapacity.find(Original.Target);
            if (ResourceLedger::Queued(Factory)+(Held==HeldFactoryCapacity.end()?0:Held->second)+Amount>Factory.Capacity)
                return Finish(CommitStatus::InsufficientResources);
        }
    }
    else if (Original.Kind==TransactionKind::Upgrade)
    {
        const auto Catalog=UpgradeCatalogs.find(RuleIt->second.UpgradeCatalog);
        if (Original.Target!=R.Header.Object || Original.UpgradeNode.empty() || Original.ResourceNode
            || Original.ReservationToken.Valid() || Original.ReservationKey.Account.Valid() || Original.ReservationKey.Request)
            return Finish(CommitStatus::BadRequest);
        if (Catalog==UpgradeCatalogs.end()) return Finish(CommitStatus::InvalidRule);
        const auto Selected=Source->second.Upgrades.find(RuleIt->second.UpgradeCatalog);
        const UpgradeSelection Empty{Catalog->second.Data.Version,0,{}};
        const auto& Old=Selected==Source->second.Upgrades.end()?Empty:Selected->second;std::string Error;
        if (Old.CatalogVersion!=Catalog->second.Data.Version || Old.Version!=Original.ExpectedUpgradeVersion
            || !CanChooseUpgrade(Catalog->second,Old.Chosen,Original.UpgradeNode,Error)) return Finish(CommitStatus::InvalidRule);
    }
    else
    {
        const auto I=EconomyLedger.Snapshot().Pending.find(Original.ReservationKey);
        if (Original.Target.Valid() || Original.ResourceNode || I==EconomyLedger.Snapshot().Pending.end()
            || Original.ReservationKey.Account!=DeriveEconomyAccount(R.Header.Object)
            || I->second.Token!=Original.ReservationToken || I->second.Faction!=R.Faction) return Finish(CommitStatus::InvalidReservation);
        Recipe.Reserved=I->second;
    }
    ObjectRecord Work;Work.Grant.Object=R.Target;Work.Parent=R.Header.Object;Work.Definition=TransactionWorkDefinition;
    Work.Grant.Faction=R.Faction;Work.Grant.Controller=Work.Grant.SimOwner=Sender;
    Work.Grant.OwnerEpoch=Work.Grant.ControlEpoch=1;Work.Grant.SourceLease=R.Header.Event;
    Work.Grant.State=Life::Alive;Work.Grant.EstimatedCost=.01;Work.BornAt=R.Header.At;Work.FrozenWork=EncodeFrozenWork(Recipe);
    FrozenWorkRecipe Checked;if (Work.FrozenWork.empty() || !DecodeFrozenWork(Work,Checked)) return Finish(CommitStatus::BadRequest);
    Directory.emplace(R.Target,Work);ActiveWorks.emplace(R.Header.Object,R.Target);ActiveWorkEvents.emplace(M.Header.Event,R.Target);
    if (Original.Kind==TransactionKind::Extract) ++HeldOre[Original.ResourceNode];
    if (Original.Kind==TransactionKind::Deliver)
        HeldFactoryCapacity[Original.Target]+=static_cast<std::uint64_t>(Recipe.Resource.RawAmount.Blue)+Recipe.Resource.RawAmount.Red;
    Out.Births.push_back(Work);Out.Objects.push_back(std::move(Work));return Finish(CommitStatus::Committed);
}
}
