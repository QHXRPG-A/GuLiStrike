#include "GuLiNetArmy.h"
#include "GuLiNetManifest.h"
#include "GuLiNetHash.h"
#include <algorithm>
#include <cmath>

namespace GuLiNet::Army
{
namespace
{
std::uint32_t Acquired(const State& S,const std::string& Id) {const auto I=S.Acquired.find(Id);return I==S.Acquired.end()?0:I->second;}
bool Eligible(const State& S,const Card& C,Id Session)
{
    if (S.Sources.size()>=512 || Acquired(S,C.Id)==UINT32_MAX || (C.Maximum && Acquired(S,C.Id)>=C.Maximum)) return false;
    for (const auto& Required:C.Required) if (!Acquired(S,Required)) return false;
    for (const auto& [Id,N]:S.Acquired) if (N)
    {
        const auto& Other=S.Definitions.Cards.at(Id);
        if (std::find(C.Excluded.begin(),C.Excluded.end(),Id)!=C.Excluded.end()
            || std::find(Other.Excluded.begin(),Other.Excluded.end(),C.Id)!=Other.Excluded.end()) return false;
    }
    auto Sources=S.Sources;auto Effect=C.Effect;Effect.Identity=Session;
    if (!Sources.emplace(Session,std::move(Effect)).second) return false;
    std::vector<Profile> P;std::string Error;return Resolve(S.Definitions,Sources,S.Equipment,P,Error);
}
std::uint64_t Random(State& S)
{auto V=S.Random;V^=V>>12;V^=V<<25;V^=V>>27;S.Random=V;return V*2685821657736338717ULL;}
void Sample(State& S,std::vector<std::string>& V,std::size_t Count)
{
    Count=std::min(Count,V.size());for (std::size_t I=0;I<Count;++I)
    {
        const auto N=std::uint64_t(V.size()-I),Threshold=(0-N)%N;std::uint64_t R;
        do {R=Random(S);} while (R<Threshold);std::swap(V[I],V[I+R%N]);
    }
    V.resize(Count);
}
Id SessionId(const ObjectRecord& O,const Command& C)
{Writer W;W.Key(O.Grant.Object);W.Identity(C.Request);W.Text("army/offer");Id Id;Id::Parse(Sha256(W.Take()).substr(0,32),Id);return Id;}
CommitStatus Prepare(const CapabilityContext& C,CapabilityDelta& D)
{
    const auto& R=C.Request;State S;Command Input;
    if (R.Target!=R.Header.Object || R.ExpectedRelationVersion!=C.Source.RelationVersion || C.Source.RelationVersion==UINT64_MAX
        || !GetState(C.Source,S) || !DecodeCommand(R.CapabilityPayload,Input) || Input.Request!=R.Header.Event
        || !Playable(C.Source.Grant.Faction) || R.Faction!=C.Source.Grant.Faction || S.RevisionValue>=UINT32_MAX)
        return CommitStatus::BadRequest;
    const auto Controller=C.Members.find(C.Source.Grant.Controller);
    if (Controller==C.Members.end() || !Controller->second.Online || !Controller->second.Ready
        || Controller->second.Seat!=Role::Commander || Controller->second.Faction!=R.Faction) return CommitStatus::Unauthorized;
    auto& O=S.Current;
    if (Input.Op==Operation::Offer || Input.Op==Operation::Reroll)
    {
        const bool Reroll=Input.Op==Operation::Reroll;
        if (Reroll && (!O.Session.Valid() || O.Session!=Input.Session || O.Finished)) return CommitStatus::InvalidRule;
        if (!Reroll && O.Session.Valid() && !O.Finished) O.Request=Input.Request;
        else
        {
            const Id Session=SessionId(C.Source,Input);std::vector<std::string> Alternatives,Retained;
            for (const auto& [Id,Card]:S.Definitions.Cards) if (Eligible(S,Card,Session))
                (Reroll && std::find(O.Candidates.begin(),O.Candidates.end(),Id)!=O.Candidates.end()?Retained:Alternatives).push_back(Id);
            if (Reroll && Alternatives.empty() && Retained.size()==O.Candidates.size()) return CommitStatus::InvalidRule;
            Sample(S,Alternatives,3);Sample(S,Retained,3-Alternatives.size());Alternatives.insert(Alternatives.end(),Retained.begin(),Retained.end());Sample(S,Alternatives,3);
            Offer Next;Next.Request=Input.Request;Next.Session=Session;Next.PreviousSession=Reroll?O.Session:Id{};
            Next.Candidates=std::move(Alternatives);Next.Finished=Next.Candidates.empty();O=std::move(Next);
        }
    }
    else if (Input.Op==Operation::Choose)
    {
        const auto Card=S.Definitions.Cards.find(Input.Choice);
        if (O.Session!=Input.Session || O.Finished || Card==S.Definitions.Cards.end()
            || std::find(O.Candidates.begin(),O.Candidates.end(),Input.Choice)==O.Candidates.end()
            || !Eligible(S,Card->second,O.Session)) return CommitStatus::InvalidRule;
        auto Effect=Card->second.Effect;Effect.Identity=O.Session;S.Sources.emplace(O.Session,std::move(Effect));++S.Acquired[Input.Choice];
        O.Selected=Input.Choice;O.Finished=true;O.CommittedAt=R.Header.At;++S.RevisionValue;
    }
    else if (Input.Op==Operation::Equip)
    {
        if (!Input.Expected || Input.Expected!=S.RevisionValue) return CommitStatus::InvalidRule;
        auto Equipment=S.Equipment;Equipment[Input.EquipmentSlot]=Input.Choice;std::vector<Profile> Profiles;std::string Error;
        if (!Resolve(S.Definitions,S.Sources,Equipment,Profiles,Error)) return CommitStatus::InvalidRule;
        const auto P=std::find_if(Profiles.begin(),Profiles.end(),[&](const auto& V){return V.Key==Input.EquipmentSlot;});
        if (P==Profiles.end() || !P->Unlocked || (!Input.Choice.empty() && P->Skill!=Input.Choice)) return CommitStatus::InvalidRule;
        S.Equipment=std::move(Equipment);++S.RevisionValue;
    }
    else
    {
        if (O.Session!=Input.Session || !O.Finished || O.Selected.empty()) return CommitStatus::InvalidRule;
        O.PresentationClosed=true;
    }
    S.LastRequest=R.Header.Event;auto Next=C.Source;if (!SetState(Next,S)) return CommitStatus::InvalidRule;++Next.RelationVersion;
    D.Objects.emplace(R.Target,std::move(Next));return CommitStatus::Committed;
}
bool Transition(const ObjectRecord& A,const ObjectRecord& B)
{
    State Before,After;
    if (!GetState(A,Before) || !GetState(B,After) || EncodeCatalog(Before.Definitions)!=EncodeCatalog(After.Definitions)
        || A.RelationVersion==UINT64_MAX || B.RelationVersion!=A.RelationVersion+1
        || After.RevisionValue<Before.RevisionValue || After.RevisionValue>Before.RevisionValue+1) return false;
    auto Same=B;Same.Capabilities[StateCapability]=A.Capabilities.at(StateCapability);Same.RelationVersion=A.RelationVersion;
    Writer W,X;WriteObject(W,A);WriteObject(X,Same);return W.Take()==X.Take();
}
bool ReadCatalog(const RuntimeManifest& M,Catalog& C,std::string& Error)
{
    const auto N=M.Numbers.find("army/catalog/chunks");
    if (N==M.Numbers.end() || N->second<1 || N->second>32 || std::floor(N->second)!=N->second) {Error="Missing army catalog";return false;}
    Bytes B;auto Hex=[](char C)->int {return C>='0' && C<='9'?C-'0':C>='a' && C<='f'?C-'a'+10:-1;};
    for (std::uint32_t I=0;I<std::uint32_t(N->second);++I)
    {
        const auto S=M.Strings.find("army/catalog/"+std::to_string(I));
        if (S==M.Strings.end() || S->second.empty() || S->second.size()>4096 || S->second.size()%2) {Error="Invalid army catalog fragment";return false;}
        for (std::size_t J=0;J<S->second.size();J+=2) {const int A=Hex(S->second[J]),D=Hex(S->second[J+1]);if (A<0 || D<0) return false;B.push_back(std::uint8_t(A*16+D));}
    }
    if (!DecodeCatalog(B,C)) {Error="Incompatible army catalog";return false;}return true;
}
bool Initialize(RuntimeManifest& M,std::string& Error)
{
    const auto Def=M.Strings.find("army/root_definition");if (Def==M.Strings.end()) return true;
    Catalog C;if (!ReadCatalog(M,C,Error)) return false;Link L;std::set<Team> Teams;
    for (auto& [K,O]:M.Objects) if (O.Definition==Def->second)
    {
        if (!Playable(O.Grant.Faction) || !Teams.insert(O.Grant.Faction).second) {Error="Army root requires one object per team";return false;}
        State S;S.Definitions=C;Writer Seed;Seed.Key(K);Seed.Text(M.MapPackage);const auto Hash=Sha256(Seed.Take());
        S.Random=std::stoull(Hash.substr(0,16),nullptr,16);if (!S.Random) S.Random=1;
        if (!SetState(O,S)) {Error="Army state exceeds bounded capability storage";return false;}
        L.Roots[O.Grant.Faction==Team::Red?0:1]=K;
    }
    if (Teams.size()!=2) {Error="Missing army root";return false;}
    const auto LinkData=EncodeLink(L);
    for (auto& [_,O]:M.Objects) if (M.Numbers.contains("army/definition/"+O.Definition)) O.Capabilities[LinkCapability]={Version,LinkData};
    for (auto& [_,R]:M.Rules) if (R.SpawnKind!=ObjectKind::None && M.Numbers.contains("army/definition/"+R.TargetDefinition)) R.SpawnCapabilities[LinkCapability]={Version,LinkData};
    for (const auto& [Id,B]:C.Rules) if (!M.Rules.contains(Id) || M.Rules.at(Id).SourceDefinition!=B.SourceDefinition || (B.Admission?M.Rules.at(Id).Kind!=TransactionKind::Spawn:M.Rules.at(Id).Kind!=TransactionKind::Damage))
    {Error="Army binding references an absent or incompatible transaction rule";return false;}
    return true;
}
bool ManifestValid(const RuntimeManifest& M,std::string& Error)
{
    if (!M.Strings.contains("army/root_definition")) return true;
    for (const auto* Name:{StateCapability,LinkCapability})
    {const auto I=M.Numbers.find(std::string("capability/")+Name+"/version");if (I==M.Numbers.end() || I->second!=Version) {Error="Missing army capability version";return false;}}
    RuntimeManifest Expected=M;if (!Initialize(Expected,Error)) return false;
    for (const auto& [K,O]:M.Objects) if (O.Capabilities!=Expected.Objects.at(K).Capabilities) {Error="Army startup does not match catalog";return false;}
    return true;
}
bool ReadRoot(const ObjectRecord& O,const std::map<ObjectKey,ObjectRecord>& Objects,Link& L,State& S)
{
    const auto I=O.Capabilities.find(LinkCapability);
    if (I==O.Capabilities.end() || I->second.Version!=Version || !DecodeLink(I->second.Data,L) || !Playable(O.Grant.Faction)) return false;
    const auto Root=Objects.find(L.Roots[O.Grant.Faction==Team::Red?0:1]);
    return Root!=Objects.end() && Root->second.Grant.Faction==O.Grant.Faction && GetState(Root->second,S);
}
bool ResolveRule(const ObjectRecord& O,const std::map<ObjectKey,ObjectRecord>& Objects,const std::string& Id,Rule& R)
{
    Link L;State S;if (!ReadRoot(O,Objects,L,S)) return false;
    const auto Binding=S.Definitions.Rules.find(Id);if (Binding==S.Definitions.Rules.end()) return true;
    Profile P;
    if (const auto F=L.Frozen.find(Id);F!=L.Frozen.end()) P=F->second;
    else
    {
        if (!L.Frozen.empty()) return false;
        std::vector<Profile> Profiles;std::string Error;if (!Resolve(S.Definitions,S.Sources,S.Equipment,Profiles,Error)) return false;
        const auto V=std::find_if(Profiles.begin(),Profiles.end(),[&](const auto& P){return P.Key==Binding->second.Key && P.Skill==Binding->second.Skill;});
        if (V==Profiles.end() || !V->Equipped || !V->Unlocked) return false;P=*V;
    }
    if (R.Kind==TransactionKind::Damage) R.Damage=P.Damage;
    // Individual projectile admissions may share one firing activation. Its
    // simulation enforces the fire cadence; per-projectile admission must not
    // apply the slot cooldown N times. Damage on an effect is also cadence-free.
    if (R.Kind==TransactionKind::Damage && L.Frozen.empty()) R.Cooldown=P.Rate>0?Tick(std::ceil(60./P.Rate)):0;
    return true;
}
bool Spawn(const ObjectRecord& Parent,const std::map<ObjectKey,ObjectRecord>& Objects,const TransactionRequest&,ObjectRecord& Child)
{
    if (Child.Grant.Policy!=OwnershipPolicy::RetainedSource) return true;
    Link L;State S;if (!ReadRoot(Parent,Objects,L,S)) return false;
    std::vector<Profile> Profiles;
    if (L.Frozen.empty())
    {
        std::string Error;if (!Resolve(S.Definitions,S.Sources,S.Equipment,Profiles,Error)) return false;
    }
    else for (const auto& [_,P]:L.Frozen) Profiles.push_back(P);
    L.Frozen.clear();
    for (const auto& [Id,Binding]:S.Definitions.Rules) if (Binding.SourceDefinition==Child.Definition)
        if (const auto P=std::find_if(Profiles.begin(),Profiles.end(),[&](const auto& V){return V.Key==Binding.Key && V.Skill==Binding.Skill && V.Equipped && V.Unlocked;});P!=Profiles.end()) L.Frozen.emplace(Id,*P);
    if (L.Frozen.empty()) return true; // Non-combat work owns a different capability contract.
    const auto Bytes=EncodeLink(L);if (Bytes.empty()) return false;Child.Capabilities[LinkCapability]={Version,Bytes};return true;
}
const bool RegisteredState=RegisterCapability({StateCapability,Version,
    [](const ObjectRecord& O,const CapabilityState&) {State S;return Playable(O.Grant.Faction) && GetState(O,S);},
    [](const Rule& R){return R.Kind==TransactionKind::Capability && R.Capability==StateCapability && R.CapabilityVersion==Version && R.CapabilityOperation=="mutate";},
    Prepare,Transition,Initialize,ManifestValid});
const bool RegisteredLinks=RegisterCapability({LinkCapability,Version,
    [](const ObjectRecord&,const CapabilityState& S){Link L;return DecodeLink(S.Data,L);},
    [](const Rule&){return false;},[](const CapabilityContext&,CapabilityDelta&){return CommitStatus::InvalidRule;},
    [](const ObjectRecord&,const ObjectRecord&){return false;},{},{},{},ResolveRule,Spawn});
}
}
