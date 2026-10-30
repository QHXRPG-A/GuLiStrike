#include "GuLiNetGroundRogue.h"
#include "GuLiNetManifest.h"
#include "GuLiNetHash.h"
#include <algorithm>
#include <cmath>

namespace GuLiNet::GroundRogue
{
namespace
{
bool Text(const std::string& S) {return !S.empty() && S.size()<=192 && S.find('\0')==std::string::npos;}
bool Count(Reader& R,std::uint32_t& N,std::uint32_t Max=512) {return R.U32(N) && N<=Max;}
void Strings(Writer& W,const std::vector<std::string>& V) {W.U32(std::uint32_t(V.size()));for (const auto& S:V) W.Text(S);}
bool Strings(Reader& R,std::vector<std::string>& V) {std::uint32_t N;if (!Count(R,N)) return false;V.resize(N);for (auto& S:V) if (!R.Text(S,192)||!Text(S)) return false;return true;}
bool Valid(const Ricochet& V,bool Empty=false)
{return (Empty&&!V.Minimum&&!V.Maximum&&!V.PerStack&&!V.Range&&!V.Retention)||!Effects::EncodeRicochet(V).empty();}
Bytes RawCatalog(const Catalog& C)
{
    Writer W(MaximumCapabilityBytes);W.U32(Version);W.U32(std::uint32_t(C.Cards.size()));
    for(const auto& [Id,V]:C.Cards){W.Text(Id);W.U32(V.MaximumAcquisitions);Strings(W,V.Required);Strings(W,V.Excluded);W.U32(std::uint32_t(V.EffectIds.size()));for(auto E:V.EffectIds)W.U32(E);}
    Effects::Set E;for(const auto& [Id,D]:C.Effects){if(Id!=D.Id)return {};E.push_back(D);}auto B=Effects::Encode(E);if(B.empty())return {};W.Blob(B);return W.Take();
}
std::uint32_t Acquired(const Build& B,const std::string& Id) {const auto I=B.Acquired.find(Id);return I==B.Acquired.end()?0:I->second;}
Id Hash(Writer& W) {Id V;Id::Parse(Sha256(W.Take()).substr(0,32),V);return V;}
std::uint64_t Random(Build& B) {auto V=B.Random;V^=V>>12;V^=V<<25;V^=V>>27;B.Random=V;return V*2685821657736338717ULL;}
void Sample(Build& B,std::vector<std::string>& V,std::size_t N)
{N=std::min(N,V.size());for(std::size_t I=0;I<N;++I){const auto Size=std::uint64_t(V.size()-I),Threshold=(0-Size)%Size;std::uint64_t R;do{R=Random(B);}while(R<Threshold);std::swap(V[I],V[I+R%Size]);}V.resize(N);}
bool Put(ObjectRecord& O,const State& S) {auto B=EncodeState(S);if(B.empty())return false;O.Capabilities[Name]={Version,std::move(B)};return true;}
}
bool Compile(const Catalog& C,CompiledUpgradeCatalog& Out,std::string& Error)
{
    const auto Bytes=RawCatalog(C);if(Bytes.empty()){Error="Invalid effect directory";return false;}const auto Hash=Sha256(Bytes);
    if(C.Compiled&&C.CompiledHash==Hash){Out=*C.Compiled;return true;}
    UpgradeCatalog G;std::set<std::pair<std::string,std::string>> Pairs;
    for(const auto& [Id,V]:C.Cards)
    {
        if(!Text(Id)||Id!=V.Id||V.EffectIds.empty()||V.EffectIds.size()>64){Error="Invalid Ground rogue card: "+Id;return false;}
        std::set<std::uint32_t> EffectIds;for(auto E:V.EffectIds)if(!C.Effects.contains(E)||!EffectIds.insert(E).second){Error="Unknown or duplicate effect reference: "+Id;return false;}
        UpgradeNode N;N.Id=N.Route=N.Group=Id;
        if(!V.Required.empty())
        {
            N.Requires.Root=0;N.Requires.Nodes.push_back({UpgradeCondition::All});
            std::set<std::string> Seen;
            for(const auto& Required:V.Required)
            {if(Required==Id||!Seen.insert(Required).second){Error="Self or duplicate prerequisite";return false;}const auto Index=std::uint32_t(N.Requires.Nodes.size());N.Requires.Nodes[0].Children.push_back(Index);N.Requires.Nodes.push_back({UpgradeCondition::ChosenNode,{},Required,1});}
        }
        G.Nodes.push_back(std::move(N));UpgradeGroup Group;Group.Configuration=Group.Identity=Id;Group.Executable=true;Group.Capabilities.push_back({"effect/"+Id});G.Groups.push_back(std::move(Group));
        std::set<std::string> Seen;
        for(const auto& Other:V.Excluded)
        {if(Other==Id||!C.Cards.contains(Other)||!Seen.insert(Other).second){Error="Invalid Ground exclusion";return false;}Pairs.emplace(std::min(Id,Other),std::max(Id,Other));}
    }
    for(const auto& [A,B]:Pairs)G.Exclusions.push_back({A+"/"+B,{A,B}});
    if(!CompileUpgradeCatalog(G,Out,Error))return false;C.Compiled=std::make_shared<const CompiledUpgradeCatalog>(Out);C.CompiledHash=Hash;return true;
}
bool Resolve(const Catalog& C,const Build& B,Effects::Set& Out)
{
    CompiledUpgradeCatalog Graph;std::string Error;if(!Compile(C,Graph,Error))return false;
    UpgradeContext Context;for(const auto& [Id,N]:B.Acquired)if(N)Context.Chosen.insert(Id);
    for(const auto& [Id,N]:B.Acquired)
    {
        const auto Index=Graph.NodeIndices.find(Id);if(Index==Graph.NodeIndices.end()||!EvaluateUpgradeExpression(Graph.Data.Nodes[Index->second].Requires,Context))return false;
        if(const auto Conflicts=Graph.Conflicts.find(Id);Conflicts!=Graph.Conflicts.end())for(const auto& Other:Conflicts->second)if(Context.Chosen.contains(Other))return false;
    }
    std::map<std::uint32_t,std::uint32_t> Counts;
    for(const auto& [Id,N]:B.Acquired)
    {
        const auto I=C.Cards.find(Id);if(I==C.Cards.end()||!N||(I->second.MaximumAcquisitions&&N>I->second.MaximumAcquisitions))return false;
        for(auto E:I->second.EffectIds){if(std::uint64_t(Counts[E])+N>UINT32_MAX)return false;Counts[E]+=N;}
    }
    Out.clear();for(const auto& [Id,N]:Counts){const auto& D=C.Effects.at(Id);const auto* H=Effects::Find(D.Handler);if(!H||!H->Accumulate(D,N,Out))return false;}
    return Effects::Validate(Out);
}
bool Eligible(const Catalog& C,const Build& B,const std::string& Id)
{
    const auto I=C.Cards.find(Id);if(I==C.Cards.end()||Acquired(B,Id)==UINT32_MAX||(I->second.MaximumAcquisitions&&Acquired(B,Id)>=I->second.MaximumAcquisitions))return false;
    CompiledUpgradeCatalog G;std::string Error;if(!Compile(C,G,Error))return false;UpgradeContext Context;
    for(const auto& [Chosen,N]:B.Acquired)if(N){Context.Chosen.insert(Chosen);Context.RouteLevels[Chosen]=1;}
    if(!EvaluateUpgradeExpression(G.Data.Nodes[G.NodeIndices.at(Id)].Requires,Context))return false;
    if(const auto Conflicts=G.Conflicts.find(Id);Conflicts!=G.Conflicts.end())for(const auto& Other:Conflicts->second)if(Acquired(B,Other))return false;
    auto Next=B;++Next.Acquired[Id];Effects::Set E;return Resolve(C,Next,E);
}
Bytes EncodeCatalog(const Catalog& C)
{
    CompiledUpgradeCatalog G;std::string Error;if(!Compile(C,G,Error))return {};return RawCatalog(C);
}
bool DecodeCatalog(std::span<const std::uint8_t> Bytes,Catalog& Out)
{
    Reader R(Bytes);Catalog C;std::uint32_t Ver,N;if(!R.U32(Ver)||Ver!=Version||!Count(R,N))return false;
    for(std::uint32_t I=0;I<N;++I){Card V;std::uint32_t K;if(!R.Text(V.Id,192)||!R.U32(V.MaximumAcquisitions)||!Strings(R,V.Required)||!Strings(R,V.Excluded)||!Count(R,K,64))return false;V.EffectIds.resize(K);for(auto& E:V.EffectIds)if(!R.U32(E))return false;if(!C.Cards.emplace(V.Id,V).second)return false;}
    GuLiNet::Bytes B;Effects::Set E;if(!R.Blob(B,MaximumCapabilityBytes)||!Effects::Decode(B,E))return false;for(auto& D:E)if(!C.Effects.emplace(D.Id,D).second)return false;
    CompiledUpgradeCatalog G;std::string Error;if(!R.Done()||!Compile(C,G,Error))return false;Out=std::move(C);return true;
}
Bytes EncodeState(const State& S)
{
    const auto Catalog=EncodeCatalog(S.Definitions);if(Catalog.empty()||S.Players.size()>128)return {};Writer W(MaximumCapabilityBytes);W.U32(Version);W.Blob(Catalog);W.U32(std::uint32_t(S.Players.size()));
    for(const auto& [Player,B]:S.Players)
    {
        Effects::Set E;if(!Player.Valid()||!B.RevisionValue||!B.Random||B.Acquired.size()>512||!Resolve(S.Definitions,B,E))return {};
        W.Identity(Player);W.U64(B.RevisionValue);W.Identity(B.LastRequest);W.U64(B.Random);W.U32(std::uint32_t(B.Acquired.size()));for(const auto& [Id,N]:B.Acquired){W.Text(Id);W.U32(N);}
        const auto& O=B.Current;W.Identity(O.Request);W.Identity(O.Session);W.Identity(O.PreviousSession);Strings(W,O.Candidates);W.Text(O.Selected);W.U64(O.CommittedAt);W.U8(O.Finished);W.U8(O.PresentationClosed);
    }return W.Take();
}
bool DecodeState(std::span<const std::uint8_t> Bytes,State& Out)
{
    Reader R(Bytes);State S;std::uint32_t Ver,N;GuLiNet::Bytes Catalog;if(!R.U32(Ver)||Ver!=Version||!R.Blob(Catalog,MaximumCapabilityBytes)||!DecodeCatalog(Catalog,S.Definitions)||!Count(R,N,128))return false;
    for(std::uint32_t I=0;I<N;++I)
    {
        Id Player;Build B;std::uint32_t K;if(!R.Identity(Player)||!Player.Valid()||!R.U64(B.RevisionValue)||!B.RevisionValue||!R.Identity(B.LastRequest)||!R.U64(B.Random)||!B.Random||!Count(R,K))return false;
        for(std::uint32_t J=0;J<K;++J){std::string Id;std::uint32_t Count;if(!R.Text(Id,192)||!R.U32(Count)||!Count||!B.Acquired.emplace(Id,Count).second)return false;}
        auto& O=B.Current;std::uint8_t Finished,Closed;if(!R.Identity(O.Request)||!R.Identity(O.Session)||!R.Identity(O.PreviousSession)||!Strings(R,O.Candidates)||O.Candidates.size()>3||!R.Text(O.Selected,192)||!R.U64(O.CommittedAt)||!R.U8(Finished)||Finished>1||!R.U8(Closed)||Closed>1)return false;
        O.Finished=Finished;O.PresentationClosed=Closed;std::set<std::string> Seen;
        for(const auto& Id:O.Candidates)if(!S.Definitions.Cards.contains(Id)||!Seen.insert(Id).second)return false;
        if(!O.Selected.empty()&&(!O.Finished||!Seen.contains(O.Selected)))return false;
        std::uint64_t Total=1;for(const auto& [_,Count]:B.Acquired)Total+=Count;
        Effects::Set E;if(B.RevisionValue!=Total||!Resolve(S.Definitions,B,E)||!S.Players.emplace(Player,std::move(B)).second)return false;
    }if(!R.Done())return false;Out=std::move(S);return true;
}
bool GetState(const ObjectRecord& O,State& S){const auto I=O.Capabilities.find(Name);return I!=O.Capabilities.end()&&I->second.Version==Version&&DecodeState(I->second.Data,S);}
Bytes EncodeAttack(const Attack& A)
{auto E=Effects::Encode(A.Effects);if(E.empty()||!A.Source.Valid()||A.Source.Kind!=ObjectKind::GroundPlayer||!A.Root.Valid()||A.Root.Kind!=ObjectKind::BusinessWork||!A.Player.Valid()||!A.RevisionValue||!std::isfinite(A.Damage)||A.Damage<=0||A.BulletVfx>INT32_MAX||A.ImpactVfx>INT32_MAX||A.MuzzleVfx>INT32_MAX)return {};Writer W(MaximumCapabilityBytes);W.U32(Version);W.Key(A.Source);W.Key(A.Root);W.Identity(A.Player);W.U64(A.RevisionValue);W.Blob(E);W.F32(A.Damage);W.U32(A.BulletVfx);W.U32(A.ImpactVfx);W.U32(A.MuzzleVfx);return W.Take();}
bool DecodeAttack(std::span<const std::uint8_t> Bytes,Attack& Out)
{Reader R(Bytes);Attack A;GuLiNet::Bytes E;std::uint32_t V;if(!R.U32(V)||V!=Version||!R.Key(A.Source)||!R.Key(A.Root)||!R.Identity(A.Player)||!R.U64(A.RevisionValue)||!R.Blob(E,MaximumCapabilityBytes)||!Effects::Decode(E,A.Effects)||!R.F32(A.Damage)||!R.U32(A.BulletVfx)||!R.U32(A.ImpactVfx)||!R.U32(A.MuzzleVfx)||!R.Done()||EncodeAttack(A).empty())return false;Out=std::move(A);return true;}
Id HopEvent(const Id& Shot,std::uint32_t Index,const ObjectKey& Target){Writer W;W.Identity(Shot);W.U32(Index);W.Key(Target);W.Text("ground/ricochet/hit");return Hash(W);}
std::uint32_t HopBudget(const Id& Shot,const Ricochet& E)
{if(!Valid(E))return 0;Writer W;W.Identity(Shot);W.Text("ground/ricochet/budget");const auto H=Sha256(W.Take());return E.Minimum+std::uint32_t(std::stoull(H.substr(0,16),nullptr,16)%(E.Maximum-E.Minimum+1));}
Bytes EncodeChain(const Chain& C)
{
    auto A=EncodeAttack(C.Frozen);if(A.empty()||!C.First.Valid()||!C.Shot.Valid()||!std::isfinite(C.X)||!std::isfinite(C.Y)||!std::isfinite(C.Z)||C.Budget!=HopBudget(C.Shot,Effects::GetRicochet(C.Frozen.Effects))||C.Hops.size()>C.Budget)return {};
    Writer W(MaximumCapabilityBytes);W.U32(Version);W.Blob(A);W.Key(C.First);W.Identity(C.Shot);W.U64(C.At);W.F64(C.X);W.F64(C.Y);W.F64(C.Z);W.U32(C.Budget);W.U32(std::uint32_t(C.Hops.size()));
    std::set<ObjectKey> Seen{C.First};double X=C.X,Y=C.Y,Z=C.Z;
    for(const auto& H:C.Hops){const auto D=(H.X-X)*(H.X-X)+(H.Y-Y)*(H.Y-Y)+(H.Z-Z)*(H.Z-Z);if(!H.Target.Valid()||!Seen.insert(H.Target).second||!std::isfinite(D)||D>double(Effects::GetRicochet(C.Frozen.Effects).Range)*Effects::GetRicochet(C.Frozen.Effects).Range+.01)return {};W.Key(H.Target);W.F64(H.X);W.F64(H.Y);W.F64(H.Z);X=H.X;Y=H.Y;Z=H.Z;}return W.Take();
}
bool DecodeChain(std::span<const std::uint8_t> Bytes,Chain& Out)
{Reader R(Bytes);Chain C;GuLiNet::Bytes A;std::uint32_t V,N;if(!R.U32(V)||V!=Version||!R.Blob(A,MaximumCapabilityBytes)||!DecodeAttack(A,C.Frozen)||!R.Key(C.First)||!R.Identity(C.Shot)||!R.U64(C.At)||!R.F64(C.X)||!R.F64(C.Y)||!R.F64(C.Z)||!R.U32(C.Budget)||!Count(R,N,MaximumHops))return false;C.Hops.resize(N);for(auto& H:C.Hops)if(!R.Key(H.Target)||!R.F64(H.X)||!R.F64(H.Y)||!R.F64(H.Z))return false;if(!R.Done()||EncodeChain(C).empty())return false;Out=std::move(C);return true;}
namespace
{
CommitStatus Prepare(const CapabilityContext& C,CapabilityDelta& D)
{
    State S;Army::Command Input;const auto& R=C.Request;const auto Member=C.Members.find(C.Source.Grant.Controller);
    if(R.Target!=R.Header.Object||R.ExpectedRelationVersion!=C.Source.RelationVersion||C.Source.RelationVersion==UINT64_MAX||!GetState(C.Source,S)||!Army::DecodeCommand(R.CapabilityPayload,Input)||Input.Request!=R.Header.Event)return CommitStatus::BadRequest;
    if(Member==C.Members.end()||!Member->second.Online||!Member->second.Ready||Member->second.Seat!=Role::Ground||Member->second.Faction!=C.Source.Grant.Faction||R.Faction!=C.Source.Grant.Faction)return CommitStatus::Unauthorized;
    auto& B=S.Players[C.Source.Grant.Controller];auto& O=B.Current;
    if(Input.Op==Army::Operation::Offer||Input.Op==Army::Operation::Reroll)
    {
        const bool Reroll=Input.Op==Army::Operation::Reroll;
        if(Reroll&&(!O.Session.Valid()||O.Session!=Input.Session||O.Finished))return CommitStatus::InvalidRule;
        if(!Reroll&&O.Session.Valid()&&!O.Finished)O.Request=Input.Request;
        else
        {
            std::vector<std::string> New,Old;for(const auto& [Id,_]:S.Definitions.Cards)if(Eligible(S.Definitions,B,Id))(Reroll&&std::find(O.Candidates.begin(),O.Candidates.end(),Id)!=O.Candidates.end()?Old:New).push_back(Id);
            if(Reroll&&New.empty()&&Old.size()==O.Candidates.size())return CommitStatus::InvalidRule;
            Sample(B,New,3);Sample(B,Old,3-New.size());New.insert(New.end(),Old.begin(),Old.end());Sample(B,New,3);
            Writer W;W.Key(C.Source.Grant.Object);W.Identity(C.Source.Grant.Controller);W.Identity(Input.Request);W.Text("ground/offer");Army::Offer Next;Next.Request=Input.Request;Next.Session=Hash(W);Next.PreviousSession=Reroll?O.Session:Id{};Next.Candidates=std::move(New);Next.Finished=Next.Candidates.empty();O=std::move(Next);
        }
    }
    else if(Input.Op==Army::Operation::Choose)
    {
        if(O.Session!=Input.Session||O.Finished||std::find(O.Candidates.begin(),O.Candidates.end(),Input.Choice)==O.Candidates.end()||!Eligible(S.Definitions,B,Input.Choice)||B.RevisionValue==UINT64_MAX)return CommitStatus::InvalidRule;
        ++B.Acquired[Input.Choice];++B.RevisionValue;O.Selected=Input.Choice;O.Finished=true;O.CommittedAt=R.Header.At;
    }
    else if(Input.Op==Army::Operation::ClosePresentation)
    {if(O.Session!=Input.Session||!O.Finished||O.Selected.empty())return CommitStatus::InvalidRule;O.PresentationClosed=true;}
    else return CommitStatus::InvalidRule;
    B.LastRequest=R.Header.Event;auto Next=C.Source;if(!Put(Next,S))return CommitStatus::InvalidRule;++Next.RelationVersion;D.Objects.emplace(R.Target,std::move(Next));return CommitStatus::Committed;
}
bool Transition(const ObjectRecord& A,const ObjectRecord& B)
{State X,Y;if(!GetState(A,X)||!GetState(B,Y)||EncodeCatalog(X.Definitions)!=EncodeCatalog(Y.Definitions)||A.RelationVersion==UINT64_MAX||B.RelationVersion!=A.RelationVersion+1)return false;auto Same=B;Same.Capabilities[Name]=A.Capabilities.at(Name);Same.RelationVersion=A.RelationVersion;Writer W,V;WriteObject(W,A);WriteObject(V,Same);return W.Take()==V.Take();}
bool Init(RuntimeManifest& M,std::string& Error)
{
    const auto N=M.Numbers.find("ground_rogue/catalog/chunks");if(N==M.Numbers.end())return true;if(N->second<1||N->second>32)return false;Bytes Bytes;
    auto Hex=[](char V){return V>='0'&&V<='9'?V-'0':V>='a'&&V<='f'?V-'a'+10:-1;};
    for(std::uint32_t I=0;I<std::uint32_t(N->second);++I){const auto S=M.Strings.find("ground_rogue/catalog/"+std::to_string(I));if(S==M.Strings.end()||S->second.size()%2)return false;for(std::size_t J=0;J<S->second.size();J+=2){int A=Hex(S->second[J]),B=Hex(S->second[J+1]);if(A<0||B<0)return false;Bytes.push_back(std::uint8_t(A*16+B));}}
    Catalog C;if(!DecodeCatalog(Bytes,C)){Error="Invalid Ground rogue catalog";return false;}for(auto& [_,O]:M.Objects)if(O.Definition=="team/ground_rogue"){State S;S.Definitions=C;if(!Put(O,S))return false;}
    Writer Binding(MaximumCapabilityBytes);Binding.U32(Version);
    for(auto TeamValue:{Team::Red,Team::Blue})
    {ObjectKey Root;for(const auto& [K,O]:M.Objects)if(O.Definition=="team/ground_rogue"&&O.Grant.Faction==TeamValue)Root=K;if(!Root.Valid())return false;Binding.Key(Root);}
    std::vector<std::string> Rules;
    for(const auto& [Id,R]:M.Rules)if(R.Kind==TransactionKind::Damage&&R.SourceDefinition=="player/ground")Rules.push_back(Id);
    Binding.U32(std::uint32_t(Rules.size()));for(const auto& Id:Rules)
    {Binding.Text(Id);Binding.F32(M.Rules.at(Id).Damage);for(const char* Field:{"bullet","impact","muzzle"}){const auto I=M.Numbers.find("ground_attack/"+Id+"/"+Field);if(I==M.Numbers.end()||I->second<0||I->second>INT32_MAX)return false;Binding.U32(std::uint32_t(I->second));}}
    auto Data=Binding.Take();if(Data.empty())return false;
    for(auto& [_,O]:M.Objects)if(O.Definition=="player/ground")O.Capabilities["ground_binding"]={Version,Data};
    for(auto& [_,R]:M.Rules)if(R.TargetDefinition=="player/ground"&&R.SpawnKind==ObjectKind::GroundPlayer)R.SpawnCapabilities["ground_binding"]={Version,Data};
    for(const char* Capability:{Name,AttackName,ChainName,"ground_binding"})M.Numbers[std::string("capability/")+Capability+"/version"]=Version;return true;
}
const bool Registered=RegisterCapability({Name,Version,[](const ObjectRecord& O,const CapabilityState&){State S;return GetState(O,S);},[](const Rule& R){return R.Kind==TransactionKind::Capability&&R.Capability==Name&&R.CapabilityVersion==Version&&R.CapabilityOperation=="mutate";},Prepare,Transition,Init});
const bool AttackRegistered=[]
{
    CapabilityContract C;C.Name=AttackName;C.Version=Version;
    C.ValidateState=[](const ObjectRecord& O,const CapabilityState& S){Attack A;return S.Version==Version&&DecodeAttack(S.Data,A)&&A.Source==O.Parent&&O.Grant.Object.Kind==ObjectKind::Flight;};
    C.ValidateRule=[](const Rule&){return false;};C.Prepare=[](const CapabilityContext&,CapabilityDelta&){return CommitStatus::InvalidRule;};
    C.ValidateTransition=[](const ObjectRecord& A,const ObjectRecord& B){return A.Capabilities.at(AttackName).Data==B.Capabilities.at(AttackName).Data;};
    C.ResolveRule=[](const ObjectRecord& O,const std::map<ObjectKey,ObjectRecord>&,const std::string&,Rule& R)
    {if(R.Kind!=TransactionKind::Damage)return true;Attack A;if(!DecodeAttack(O.Capabilities.at(AttackName).Data,A))return false;R.Damage=A.Damage;return true;};
    C.AcceptedDamage=[](const TransactionRequest& R,const CommitResult& Result,const std::map<ObjectKey,ObjectRecord>& Objects,std::map<ObjectKey,ObjectRecord>& Changes)
    {Attack A;const auto Source=Objects.find(R.Header.Object);return Source!=Objects.end()&&DecodeAttack(Source->second.Capabilities.at(AttackName).Data,A)&&Effects::AcceptedHit(A.Effects,R,Result,Objects,Changes);};
    return RegisterCapability(std::move(C));
}();
struct Binding {ObjectKey Roots[2];std::map<std::string,Attack> Rules;};
bool ReadBinding(std::span<const std::uint8_t> Bytes,Binding& B)
{
    Reader R(Bytes);std::uint32_t V,N;if(!R.U32(V)||V!=Version||!R.Key(B.Roots[0])||!R.Key(B.Roots[1])||!Count(R,N))return false;
    for(std::uint32_t I=0;I<N;++I){std::string Id;Attack A;if(!R.Text(Id,192)||!R.F32(A.Damage)||!R.U32(A.BulletVfx)||!R.U32(A.ImpactVfx)||!R.U32(A.MuzzleVfx)||!B.Rules.emplace(Id,A).second)return false;}return R.Done();
}
const bool BindingRegistered=[]
{
    CapabilityContract C;C.Name="ground_binding";C.Version=Version;
    C.ValidateState=[](const ObjectRecord& O,const CapabilityState& S){Binding B;return O.Grant.Object.Kind==ObjectKind::GroundPlayer&&S.Version==Version&&ReadBinding(S.Data,B);};
    C.ValidateRule=[](const Rule&){return false;};C.Prepare=[](const CapabilityContext&,CapabilityDelta&){return CommitStatus::InvalidRule;};
    C.ValidateTransition=[](const ObjectRecord& A,const ObjectRecord& B){return A.Capabilities.at("ground_binding").Data==B.Capabilities.at("ground_binding").Data;};
    C.DeriveSpawn=[](const ObjectRecord& Parent,const std::map<ObjectKey,ObjectRecord>& Objects,const TransactionRequest&,ObjectRecord& Child)
    {
        if(Child.Grant.Object.Kind!=ObjectKind::Flight||!Child.Definition.starts_with("effect/linear/"))return true;
        Binding B;if(!ReadBinding(Parent.Capabilities.at("ground_binding").Data,B))return false;
        const auto Profile=B.Rules.find(Child.Definition.substr(std::string("effect/linear/").size()));if(Profile==B.Rules.end())return false;
        Attack A=Profile->second;A.Source=Parent.Grant.Object;A.Root=B.Roots[Parent.Grant.Faction==Team::Red?0:1];A.Player=Parent.Grant.Controller;A.RevisionValue=1;
        const auto Root=Objects.find(A.Root);State S;if(Root==Objects.end()||!GetState(Root->second,S))return false;
        if(const auto Build=S.Players.find(A.Player);Build!=S.Players.end()){A.RevisionValue=Build->second.RevisionValue;if(!Resolve(S.Definitions,Build->second,A.Effects))return false;}
        if(!Effects::FreezePresentation(A.Effects,A.BulletVfx,A.ImpactVfx,A.MuzzleVfx))return false;
        auto Bytes=EncodeAttack(A);if(Bytes.empty())return false;Child.Capabilities[AttackName]={Version,std::move(Bytes)};
        Child.Capabilities[ChainName]={Version,{}};return true;
    };return RegisterCapability(std::move(C));
}();
const bool ChainRegistered=[]
{
    CapabilityContract C;C.Name=ChainName;C.Version=Version;
    C.ValidateState=[](const ObjectRecord& O,const CapabilityState& S){Chain P;return S.Version==Version&&(S.Data.empty()?O.Capabilities.contains(AttackName):(DecodeChain(S.Data,P)&&O.Grant.Object.StableId==P.Shot&&O.Parent==P.Frozen.Source));};
    C.ValidateRule=[](const Rule& R){return R.Kind==TransactionKind::Capability&&R.Capability==ChainName&&R.CapabilityVersion==Version&&R.CapabilityOperation=="plan";};
    C.Prepare=[](const CapabilityContext& C,CapabilityDelta& D)
    {
        Chain P;Attack A;const auto F=C.Source.Capabilities.find(AttackName);const auto& R=C.Request;
        if(R.Target!=R.Header.Object||R.ExpectedRelationVersion!=C.Source.RelationVersion||!C.Source.Capabilities.at(ChainName).Data.empty()||F==C.Source.Capabilities.end()||!DecodeAttack(F->second.Data,A)||!DecodeChain(R.CapabilityPayload,P)||EncodeAttack(A)!=EncodeAttack(P.Frozen)||P.Shot!=C.Source.Grant.Object.StableId||P.At>R.Header.At||C.Source.RelationVersion==UINT64_MAX)return CommitStatus::InvalidRule;
        const auto First=C.Objects.find(P.First);if(First==C.Objects.end()||ObjectAffiliation(First->second)==C.Source.Grant.Faction)return CommitStatus::InvalidRule;
        for(const auto& H:P.Hops){const auto T=C.Objects.find(H.Target);if(T==C.Objects.end()||ObjectAffiliation(T->second)==C.Source.Grant.Faction)return CommitStatus::InvalidRule;}
        auto Next=C.Source;Next.Capabilities[ChainName]={Version,R.CapabilityPayload};++Next.RelationVersion;D.Objects.emplace(R.Target,std::move(Next));return CommitStatus::Committed;
    };
    C.ValidateTransition=[](const ObjectRecord& A,const ObjectRecord& B)
    {if(!A.Capabilities.contains(ChainName)||!A.Capabilities.at(ChainName).Data.empty()||!B.Capabilities.contains(ChainName)||B.Capabilities.at(ChainName).Data.empty()||B.RelationVersion!=A.RelationVersion+1)return false;auto Same=B;Same.Capabilities[ChainName]=A.Capabilities.at(ChainName);Same.RelationVersion=A.RelationVersion;Writer W,V;WriteObject(W,A);WriteObject(V,Same);return W.Take()==V.Take();};
    C.ResolveTransactionRule=[](const ObjectRecord& O,const std::map<ObjectKey,ObjectRecord>&,const TransactionRequest& R,Rule& Rule)
    {
        if(R.Kind!=TransactionKind::Damage)return true;const auto& Data=O.Capabilities.at(ChainName).Data;if(Data.empty())return true;Chain P;if(!DecodeChain(Data,P))return false;
        if(R.Target==P.First)return true;
        for(std::size_t I=0;I<P.Hops.size();++I)if(P.Hops[I].Target==R.Target)
        {if(R.RootEvent!=HopEvent(P.Shot,std::uint32_t(I+1),R.Target)||R.Header.Event!=R.RootEvent||R.Header.At!=P.At)return false;Rule.Damage=float(double(P.Frozen.Damage)*std::pow(double(Effects::GetRicochet(P.Frozen.Effects).Retention),double(I+1)));return std::isfinite(Rule.Damage)&&Rule.Damage>0;}
        return false;
    };
    return RegisterCapability(std::move(C));
}();
}
}
