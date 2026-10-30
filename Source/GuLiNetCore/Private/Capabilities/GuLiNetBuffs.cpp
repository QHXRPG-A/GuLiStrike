#include "GuLiNetBuffs.h"
#include "GuLiNetManifest.h"
#include "GuLiNetHash.h"
#include <algorithm>
#include <cmath>

namespace GuLiNet::Buffs
{
namespace
{
std::map<std::string,Handler>& Registry(){static std::map<std::string,Handler> R;return R;}
bool Finite(double V){return std::isfinite(V);}
Id Hash(Writer& W){Id V;Id::Parse(Sha256(W.Take()).substr(0,32),V);return V;}
bool Put(ObjectRecord& O,const State& S){auto B=Encode(S);if(B.empty())return false;O.Capabilities[Name]={Version,std::move(B)};return true;}
const ObjectRecord* Read(const std::map<ObjectKey,ObjectRecord>& P,const std::map<ObjectKey,ObjectRecord>& C,const ObjectKey& K)
{if(const auto I=C.find(K);I!=C.end())return &I->second;const auto I=P.find(K);return I==P.end()?nullptr:&I->second;}
bool Living(const ObjectRecord* O){return O&&O->Grant.State==Life::Alive&&!O->Vitals.Dead&&ValidHealth(O->Vitals);}
bool ReadHosts(const ObjectRecord& O,std::map<std::uint32_t,ObjectKey>& Out)
{const auto I=O.Capabilities.find(HostName);if(I==O.Capabilities.end())return true;Reader R(I->second.Data);std::uint32_t V,N;if(I->second.Version!=Version||!R.U32(V)||V!=Version||!R.U32(N)||N>16)return false;for(std::uint32_t J=0;J<N;++J){std::uint32_t Id;ObjectKey K;if(!R.U32(Id)||!Id||!R.Key(K)||K.Kind!=ObjectKind::Field||!K.Valid()||!Out.emplace(Id,K).second)return false;}return R.Done();}
bool WriteHosts(ObjectRecord& O,const std::map<std::uint32_t,ObjectKey>& Index)
{Writer W;W.U32(Version);W.U32(std::uint32_t(Index.size()));for(const auto& [Id,K]:Index){W.U32(Id);W.Key(K);}O.Capabilities[HostName]={Version,W.Take()};return ValidateObjectCapabilities(O);}
}
bool Register(Handler H){if(H.Name.empty()||!H.Validate||!H.Damage)return false;const auto K=H.Name;return Registry().emplace(K,std::move(H)).second;}
const Handler* Find(const std::string& K){const auto I=Registry().find(K);return I==Registry().end()?nullptr:&I->second;}
bool Validate(const Definition& D)
{const auto* H=Find(D.Handler);return D.Id&&D.VersionValue==1&&H&&D.Interval&&D.Interval<=TicksPerSecond*3600&&D.Duration<=TicksPerSecond*3600&&Finite(D.Ratio)&&D.Ratio>0&&D.Ratio<=1&&Finite(D.RadiusMultiplier)&&D.Vfx<=INT32_MAX&&D.BulletVfx<=INT32_MAX&&H->Validate(D);}
Bytes EncodeDefinition(const Definition& D)
{if(!Validate(D))return {};Writer W;W.U32(D.Id);W.U32(D.VersionValue);W.U32(D.Parent);W.Text(D.Handler);W.U64(D.Duration);W.U64(D.Interval);W.F32(D.Ratio);W.F32(D.RadiusMultiplier);W.U32(D.Vfx);W.U32(D.BulletVfx);return W.Take();}
bool DecodeDefinition(std::span<const std::uint8_t> B,Definition& D)
{Reader R(B);Definition V;if(!R.U32(V.Id)||!R.U32(V.VersionValue)||!R.U32(V.Parent)||!R.Text(V.Handler,192)||!R.U64(V.Duration)||!R.U64(V.Interval)||!R.F32(V.Ratio)||!R.F32(V.RadiusMultiplier)||!R.U32(V.Vfx)||!R.U32(V.BulletVfx)||!R.Done()||!Validate(V))return false;D=std::move(V);return true;}
std::vector<Definition> FromEffects(const Effects::Set& E)
{std::vector<Definition> V;for(const auto& D:E)if(D.Handler=="buff/on_hit"){Definition B;if(!DecodeDefinition(D.Parameters,B))return {};V.push_back(std::move(B));}return V;}
ObjectKey Key(const ObjectKey& Host,std::uint32_t Root,const Id& Instance)
{Writer W;W.Text("buff/host/root/v1");W.Key(Host);W.U32(Root);W.Identity(Instance);return {ObjectKind::Field,Hash(W),1};}
Id PulseEvent(const ObjectKey& K,std::uint32_t Buff,Tick At)
{Writer W;W.Text("buff/pulse/v1");W.Key(K);W.U32(Buff);W.U64(At);return Hash(W);}
Id HitEvent(const Id& P,const ObjectKey& K)
{Writer W;W.Text("buff/hit/v1");W.Identity(P);W.Key(K);return Hash(W);}
bool HasPending(const State& S){return S.Pending.Event.Valid()&&std::any_of(S.Pending.Targets.begin(),S.Pending.Targets.end(),[](const auto& H){return !H.Settled;});}
const Slot* Due(const State& S)
{
    const Slot* Best=nullptr;for(const auto& V:S.Slots)
    {if(V.Next>S.Expires)continue;const auto* H=Find(V.Frozen.Handler);if(!H)return nullptr;if(!Best||V.Next<Best->Next||(V.Next==Best->Next&&H->Priority<Find(Best->Frozen.Handler)->Priority))Best=&V;}return Best;
}
Bytes Encode(const State& S)
{
    if(!S.Host.Valid()||!S.Source.Valid()||!S.Root||!S.Instance.Valid()||!S.Application.Valid()||S.Expires<S.Started||S.Slots.empty()||S.Slots.size()>16||S.Pending.Targets.size()>MaximumTargets)return {};
    Writer W(MaximumCapabilityBytes);W.U32(Version);W.Key(S.Host);W.Key(S.Source);W.Identity(S.Instance);W.Identity(S.Application);W.U64(S.Started);W.U64(S.Expires);W.U64(S.ApplicationOrder);W.U32(S.Root);W.U8(S.Active);W.U32(std::uint32_t(S.Slots.size()));std::set<std::uint32_t> Seen;
    bool Root=false;for(const auto& V:S.Slots){auto B=EncodeDefinition(V.Frozen);if(B.empty()||!Seen.insert(V.Frozen.Id).second||V.Next<=S.Started)return {};Root|=V.Frozen.Id==S.Root&&!V.Frozen.Parent;W.Blob(B);W.U64(V.Next);}if(!Root)return {};
    const auto& P=S.Pending;W.Identity(P.Event);W.Key(P.Source);W.U32(P.Buff);W.U64(P.At);W.F32(P.Damage);W.F32(P.Radius);W.F64(P.X);W.F64(P.Y);W.F64(P.Z);W.U32(std::uint32_t(P.Targets.size()));std::set<ObjectKey> Targets;
    for(const auto& H:P.Targets){if(!H.Target.Valid()||!Targets.insert(H.Target).second)return {};W.Key(H.Target);W.U8(H.Settled);}
    W.Identity(S.LastBurst);W.F64(S.BurstX);W.F64(S.BurstY);W.F64(S.BurstZ);W.F32(S.BurstRadius);W.U32(S.BurstVfx);
    if(!Finite(P.Damage)||P.Damage<0||!Finite(P.Radius)||P.Radius<0||!Finite(P.X)||!Finite(P.Y)||!Finite(P.Z)||!Finite(S.BurstRadius)||S.BurstRadius<0||!Finite(S.BurstX)||!Finite(S.BurstY)||!Finite(S.BurstZ)||(P.Event.Valid()&&(!P.Source.Valid()||!P.Buff||P.At>S.Expires))||(!P.Event.Valid()&&!P.Targets.empty()))return {};return W.Take();
}
bool Decode(std::span<const std::uint8_t> B,State& Out)
{
    Reader R(B);State S;std::uint32_t V,N;std::uint8_t Active;if(!R.U32(V)||V!=Version||!R.Key(S.Host)||!R.Key(S.Source)||!R.Identity(S.Instance)||!R.Identity(S.Application)||!R.U64(S.Started)||!R.U64(S.Expires)||!R.U64(S.ApplicationOrder)||!R.U32(S.Root)||!R.U8(Active)||Active>1||!R.U32(N)||N>16)return false;S.Active=Active;S.Slots.resize(N);
    for(auto& X:S.Slots){Bytes D;if(!R.Blob(D,4096)||!DecodeDefinition(D,X.Frozen)||!R.U64(X.Next))return false;}
    auto& P=S.Pending;if(!R.Identity(P.Event)||!R.Key(P.Source)||!R.U32(P.Buff)||!R.U64(P.At)||!R.F32(P.Damage)||!R.F32(P.Radius)||!R.F64(P.X)||!R.F64(P.Y)||!R.F64(P.Z)||!R.U32(N)||N>MaximumTargets)return false;P.Targets.resize(N);
    for(auto& H:P.Targets){std::uint8_t Done;if(!R.Key(H.Target)||!R.U8(Done)||Done>1)return false;H.Settled=Done;}
    if(!R.Identity(S.LastBurst)||!R.F64(S.BurstX)||!R.F64(S.BurstY)||!R.F64(S.BurstZ)||!R.F32(S.BurstRadius)||!R.U32(S.BurstVfx)||!R.Done()||Encode(S).empty())return false;Out=std::move(S);return true;
}
bool Get(const ObjectRecord& O,State& S){const auto I=O.Capabilities.find(Name);return I!=O.Capabilities.end()&&I->second.Version==Version&&Decode(I->second.Data,S)&&O.Grant.Object==Key(S.Host,S.Root,S.Instance)&&O.Definition==DefinitionName;}
Bytes EncodeCommand(const Command& C)
{if(C.Operation.empty()||C.Targets.size()>MaximumTargets)return {};Writer W(MaximumCapabilityBytes);W.Text(C.Operation);W.F64(C.X);W.F64(C.Y);W.F64(C.Z);W.F32(C.HostRadius);W.Key(C.Skip);W.U32(std::uint32_t(C.Targets.size()));for(const auto& K:C.Targets)W.Key(K);return W.Take();}
bool DecodeCommand(std::span<const std::uint8_t> B,Command& Out)
{Reader R(B);Command C;std::uint32_t N;if(!R.Text(C.Operation,32)||!R.F64(C.X)||!R.F64(C.Y)||!R.F64(C.Z)||!R.F32(C.HostRadius)||!R.Key(C.Skip)||!R.U32(N)||N>MaximumTargets||!Finite(C.X)||!Finite(C.Y)||!Finite(C.Z)||!Finite(C.HostRadius)||C.HostRadius<0)return false;C.Targets.resize(N);for(auto& K:C.Targets)if(!R.Key(K)||!K.Valid())return false;if(!R.Done())return false;Out=std::move(C);return true;}
namespace
{
bool ApplyHit(const Effects::Set& E,const TransactionRequest& R,const CommitResult& Result,const std::map<ObjectKey,ObjectRecord>& Previous,std::map<ObjectKey,ObjectRecord>& Changes)
{
    const auto* Host=Read(Previous,Changes,R.Target);if(!Living(Host))return true;
    auto Defs=FromEffects(E);if(Defs.empty())return true;std::map<std::uint32_t,ObjectKey> Index;if(!ReadHosts(*Host,Index))return false;auto HostRecord=*Host;
    for(const auto& Root:Defs)if(!Root.Parent)
    {
        const auto Existing=Index.find(Root.Id);const auto* Old=Existing==Index.end()?nullptr:Read(Previous,Changes,Existing->second);ObjectRecord Field;State S;
        const auto& Shot=Previous.at(R.Header.Object);
        if(Old){Field=*Old;if(!Get(Field,S))return false;}
        const bool Fresh=!Old||Old->Grant.State>=Life::Dead||!S.Active||R.Header.At>S.Expires;
        if(Fresh)
        {
            if(Old&&Old->Grant.State<Life::Dead){auto Closing=*Old;S.Active=false;if(!Put(Closing,S)||Closing.RelationVersion==UINT64_MAX)return false;++Closing.RelationVersion;Changes[Closing.Grant.Object]=std::move(Closing);}
            Field={};S={};S.Host=R.Target;S.Root=Root.Id;S.Started=R.Header.At;S.Instance=R.Header.Event;
        }
        const auto K=Key(S.Host,S.Root,S.Instance);
        S.Active=true;S.Source=Shot.Parent.Valid()?Shot.Parent:R.Header.Object;S.Application=R.Header.Event;S.ApplicationOrder=Result.StateVersion;
        if(R.Header.At>UINT64_MAX-Root.Duration)return false;S.Expires=std::max(S.Expires,R.Header.At+Root.Duration);
        std::vector<Slot> Slots;
        for(const auto& D:Defs)if(D.Id==Root.Id||D.Parent==Root.Id)
        {const auto I=std::find_if(S.Slots.begin(),S.Slots.end(),[&](const auto& V){return V.Frozen.Id==D.Id;});Slots.push_back({D,I==S.Slots.end()?R.Header.At+D.Interval:I->Next});}
        S.Slots=std::move(Slots);
        if(Fresh)
        {
            Field.Grant.Object=K;Field.Definition=DefinitionName;Field.Parent=S.Source;Field.Grant.SourceLease=R.Header.Event;
            Field.Grant.Faction=Shot.Grant.Faction;Field.Grant.Controller=Shot.Grant.Controller;Field.Grant.State=Life::Registered;Field.Grant.EstimatedCost=.01;
            Field.Vitals={1,1,0,0,1,false};Field.RelationVersion=1;Field.BornAt=R.Header.At;
        }
        else {if(Field.RelationVersion==UINT64_MAX)return false;++Field.RelationVersion;}
        if(Field.Grant.Controller!=Shot.Grant.Controller){if(Field.Grant.ControlEpoch==UINT64_MAX)return false;Field.Grant.Controller=Shot.Grant.Controller;++Field.Grant.ControlEpoch;}
        if(!Put(Field,S))return false;Changes[K]=std::move(Field);Index[Root.Id]=K;
    }
    if(!WriteHosts(HostRecord,Index))return false;Changes[R.Target]=std::move(HostRecord);return true;
}
CommitStatus Prepare(const CapabilityContext& C,CapabilityDelta& D)
{
    State S;Command Input;const auto& R=C.Request;
    if(R.Target!=R.Header.Object||R.ExpectedRelationVersion!=C.Source.RelationVersion||C.Source.RelationVersion==UINT64_MAX||!Get(C.Source,S)||!DecodeCommand(R.CapabilityPayload,Input))return CommitStatus::InvalidRule;
    const auto H=C.Objects.find(S.Host);const bool Alive=H!=C.Objects.end()&&Living(&H->second);
    if(Input.Operation=="pulse")
    {
        const auto* DueSlot=Due(S);if(!S.Active||!Alive||HasPending(S)||!DueSlot||DueSlot->Next>R.Header.At)return CommitStatus::InvalidRule;
        const auto Definition=DueSlot->Frozen;const auto* Handler=Find(Definition.Handler);const auto At=DueSlot->Next;
        if(!Handler||R.Header.At<At||At>S.Expires)return CommitStatus::InvalidRule;
        Pulse P;P.Event=PulseEvent(C.Source.Grant.Object,Definition.Id,At);P.Source=S.Source;P.Buff=Definition.Id;P.At=At;P.Damage=Handler->Damage(Definition,H->second.Vitals);P.X=Input.X;P.Y=Input.Y;P.Z=Input.Z;P.Radius=Handler->Area?Input.HostRadius*Definition.RadiusMultiplier:0;
        if(!Finite(P.Damage)||P.Damage<0||!Finite(P.Radius)||P.Radius<0)return CommitStatus::InvalidRule;
        std::set<ObjectKey> Seen;
        if(Handler->Area)
        {for(const auto& K:Input.Targets){const auto T=C.Objects.find(K);if(T==C.Objects.end()||!Living(&T->second)||ObjectAffiliation(T->second)==C.Source.Grant.Faction||!Seen.insert(K).second)return CommitStatus::InvalidRule;P.Targets.push_back({K,P.Damage==0});}if(!Seen.contains(S.Host))return CommitStatus::InvalidRule;
            // Space is an authenticated simulator fact, as it is for all projectile collision routes.
            S.LastBurst=P.Event;S.BurstX=P.X;S.BurstY=P.Y;S.BurstZ=P.Z;S.BurstRadius=P.Radius;S.BurstVfx=Definition.Vfx;
        }
        else {if(!Input.Targets.empty()||ObjectAffiliation(H->second)==C.Source.Grant.Faction)return CommitStatus::InvalidRule;P.Targets.push_back({S.Host,P.Damage==0});}
        for(auto& V:S.Slots)if(V.Frozen.Id==Definition.Id){if(V.Next>UINT64_MAX-V.Frozen.Interval)return CommitStatus::InvalidRule;V.Next+=V.Frozen.Interval;}
        S.Pending=std::move(P);
    }
    else if(Input.Operation=="skip")
    {
        auto I=std::find_if(S.Pending.Targets.begin(),S.Pending.Targets.end(),[&](const auto& V){return V.Target==Input.Skip&&!V.Settled;});if(I==S.Pending.Targets.end())return CommitStatus::InvalidRule;
        const auto T=C.Objects.find(Input.Skip);if(T!=C.Objects.end()&&Living(&T->second)&&!T->second.Phased&&ObjectAffiliation(T->second)!=C.Source.Grant.Faction)return CommitStatus::InvalidRule;I->Settled=true;
    }
    else if(Input.Operation=="stop")
    {
        if(HasPending(S))return CommitStatus::InvalidRule;const auto* Next=Due(S);if(Alive&&S.Active&&(R.Header.At<=S.Expires||(Next&&Next->Next<=S.Expires)))return CommitStatus::InvalidRule;S.Active=false;
    }
    else return CommitStatus::InvalidRule;
    auto Next=C.Source;if(Input.Operation=="stop"){Next.Grant.State=Life::Destroyed;Next.DiedAt=Next.LogicalAge;}
    if(!Put(Next,S))return CommitStatus::InvalidRule;++Next.RelationVersion;D.Objects.emplace(Next.Grant.Object,std::move(Next));return CommitStatus::Committed;
}
const bool Burn=Register({"burning",0,false,[](const Definition& D){return !D.Parent&&D.Duration>=D.Interval&&D.RadiusMultiplier==0;},[](const Definition& D,const Health& H){return H.Maximum*D.Ratio;}});
const bool Burst=Register({"burning_burst",1,true,[](const Definition& D){return D.Parent&&D.Duration==0&&D.RadiusMultiplier>0&&D.RadiusMultiplier<=100;},[](const Definition& D,const Health& H){return std::max(0.f,H.Maximum-H.Value)*D.Ratio;}});
const bool OnHit=Effects::Register({"buff/on_hit",1,[](const Effects::Definition& E){Definition D;return DecodeDefinition(E.Parameters,D);},
    [](const Effects::Definition& E,std::uint32_t N,Effects::Set& S){if(!N)return false;Definition D;if(!DecodeDefinition(E.Parameters,D))return false;for(const auto& V:S)if(V.Handler==E.Handler){Definition Old;if(!DecodeDefinition(V.Parameters,Old))return false;if(Old.Id==D.Id)return Old==D;}S.push_back(E);return true;},ApplyHit,
    [](const Effects::Definition& E,std::uint32_t& Bullet,std::uint32_t&,std::uint32_t&){Definition D;if(!DecodeDefinition(E.Parameters,D))return false;if(D.BulletVfx)Bullet=D.BulletVfx;return true;}});
const bool Registered=[]
{
    CapabilityContract C;C.Name=Name;C.Version=Version;C.AcceptsRetainedSource=true;
    C.ValidateState=[](const ObjectRecord& O,const CapabilityState& V){State S;return V.Version==Version&&Get(O,S);};
    C.ValidateRule=[](const Rule& R){return R.Kind==TransactionKind::Capability&&R.Capability==Name&&R.CapabilityVersion==Version&&R.CapabilityOperation=="step";};
    C.Prepare=Prepare;
    C.ValidateTransition=[](const ObjectRecord& A,const ObjectRecord& B){State X,Y;return Get(A,X)&&Get(B,Y)&&X.Host==Y.Host&&X.Root==Y.Root&&A.Grant.Object==B.Grant.Object&&B.RelationVersion==A.RelationVersion+1;};
    C.ResolveTransactionRule=[](const ObjectRecord& O,const std::map<ObjectKey,ObjectRecord>&,const TransactionRequest& R,Rule& Rule)
    {if(R.Kind!=TransactionKind::Damage)return true;State S;if(!Get(O,S)||!S.Pending.Event.Valid()||R.Header.At!=S.Pending.At||R.RootEvent!=S.Pending.Event||R.Header.Event!=HitEvent(S.Pending.Event,R.Target))return false;
        const auto T=std::find_if(S.Pending.Targets.begin(),S.Pending.Targets.end(),[&](const auto& V){return V.Target==R.Target&&!V.Settled;});if(T==S.Pending.Targets.end()||S.Pending.Damage<=0)return false;Rule.Damage=S.Pending.Damage;return true;};
    C.AcceptedDamage=[](const TransactionRequest& R,const CommitResult&,const std::map<ObjectKey,ObjectRecord>& Previous,std::map<ObjectKey,ObjectRecord>& Changes)
    {auto O=Previous.at(R.Header.Object);State S;if(!Get(O,S))return false;for(auto& H:S.Pending.Targets)if(H.Target==R.Target){H.Settled=true;break;}if(O.RelationVersion==UINT64_MAX||!Put(O,S))return false;++O.RelationVersion;Changes[O.Grant.Object]=std::move(O);return true;};
    C.InitializeManifest=[](RuntimeManifest& M,std::string&){M.Numbers[std::string("capability/")+Name+"/version"]=Version;return true;};
    return RegisterCapability(std::move(C));
}();
const bool HostRegistered=[]
{
    CapabilityContract C;C.Name=HostName;C.Version=Version;
    C.ValidateState=[](const ObjectRecord& O,const CapabilityState&){std::map<std::uint32_t,ObjectKey> V;return ReadHosts(O,V);};
    C.ValidateRule=[](const Rule&){return false;};C.Prepare=[](const CapabilityContext&,CapabilityDelta&){return CommitStatus::InvalidRule;};
    C.ValidateTransition=[](const ObjectRecord&,const ObjectRecord&){return false;};return RegisterCapability(std::move(C));
}();
}
}
