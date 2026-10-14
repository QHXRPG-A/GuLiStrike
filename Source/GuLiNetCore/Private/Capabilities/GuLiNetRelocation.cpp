#include "GuLiNetRelocation.h"
#include "GuLiNetManifest.h"
#include <algorithm>
#include <cmath>
namespace GuLiNet::Relocation
{
namespace
{
void Write(Writer& W,const Pose& P) {W.F64(P.X);W.F64(P.Y);W.F64(P.Z);W.F32(P.Pitch);W.F32(P.Yaw);W.F32(P.Roll);W.F32(P.ScaleX);W.F32(P.ScaleY);W.F32(P.ScaleZ);}
bool ReadPose(Reader& R,Pose& P)
{return R.F64(P.X)&&R.F64(P.Y)&&R.F64(P.Z)&&R.F32(P.Pitch)&&R.F32(P.Yaw)&&R.F32(P.Roll)&&R.F32(P.ScaleX)&&R.F32(P.ScaleY)&&R.F32(P.ScaleZ)
    && std::isfinite(P.X)&&std::isfinite(P.Y)&&std::isfinite(P.Z)&&std::isfinite(P.Pitch)&&std::isfinite(P.Yaw)&&std::isfinite(P.Roll)
    && std::abs(P.X)<1.e9 && std::abs(P.Y)<1.e9 && std::abs(P.Z)<1.e9
    && std::isfinite(P.ScaleX)&&std::isfinite(P.ScaleY)&&std::isfinite(P.ScaleZ)
    && std::abs(P.ScaleX)>1.e-6&&std::abs(P.ScaleY)>1.e-6&&std::abs(P.ScaleZ)>1.e-6
    && std::abs(P.ScaleX)<1.e4&&std::abs(P.ScaleY)<1.e4&&std::abs(P.ScaleZ)<1.e4;}
bool Boolean(Reader& R,bool& Out) {std::uint8_t B=0;if (!R.U8(B)||B>1) return false;Out=B!=0;return true;}
bool Count(Reader& R,std::uint32_t& N,std::uint32_t Maximum) {return R.U32(N)&&N<=Maximum;}
template<class T> bool ReadState(const ObjectRecord& O,const char* Name,T& Out)
{const auto I=O.Capabilities.find(Name);return I!=O.Capabilities.end() && I->second.Version==Version && Decode(I->second.Data,Out);}
void Put(ObjectRecord& O,const State& S) {O.Capabilities[CasterCapability]={Version,Encode(S)};}
void Put(ObjectRecord& O,const Body& S) {O.Capabilities[BodyCapability]={Version,Encode(S)};O.Phased=S.Phased;}
bool Active(const ObjectRecord& O) {return O.Grant.State==Life::Alive && !O.Grant.Dormant && !O.Vitals.Dead;}
}
Bytes Encode(const Body& B)
{
    Writer W(MaximumCapabilityBytes);W.U8(B.Vehicle);W.U8(B.Airborne);W.U8(B.FollowParent);
    W.Key(B.Root);W.Identity(B.Cast);W.U32(B.Step);W.U32(B.Acknowledged);W.U64(B.ChangedAt);W.U64(B.AppliedAt);
    W.U8(B.Accepted);W.U8(B.Locked);W.U8(B.Phased);W.U8(B.Displace);Write(W,B.Original);Write(W,B.Destination);return W.Take();
}
bool Decode(std::span<const std::uint8_t> Bytes,Body& Out)
{
    Reader R(Bytes);Body B;
    if (!Boolean(R,B.Vehicle)||!Boolean(R,B.Airborne)||!Boolean(R,B.FollowParent)||!R.Key(B.Root)||!R.Identity(B.Cast)
        ||!R.U32(B.Step)||!R.U32(B.Acknowledged)||!R.U64(B.ChangedAt)||!R.U64(B.AppliedAt)||!Boolean(R,B.Accepted)||!Boolean(R,B.Locked)||!Boolean(R,B.Phased)||!Boolean(R,B.Displace)
        ||!ReadPose(R,B.Original)||!ReadPose(R,B.Destination)||!R.Done()||B.Acknowledged>B.Step
        ||(B.Step?(!B.Root.Valid()||!B.Cast.Valid()):(B.Root.Valid()||B.Cast.Valid()||B.Locked||B.Phased||B.Displace))) return false;
    Out=B;return true;
}
Bytes Encode(const State& S)
{
    Writer W(MaximumCapabilityBytes);W.U32(std::uint32_t(S.Profiles.size()));
    for (const auto& [ID,P]:S.Profiles)
    {W.Text(ID);W.Text(P.Skill);W.U32(P.Level);W.F64(P.Radius);W.F64(P.MaximumHeight);W.F64(P.BeamHeight);W.U64(P.Windup);W.U64(P.Wait);W.U64(P.Recovery);W.U64(P.Cooldown);W.U8(P.Vehicles);}
    W.U32(std::uint32_t(S.ReadyAt.size()));for (const auto& [ID,T]:S.ReadyAt) {W.Text(ID);W.U64(T);}
    W.U32(std::uint32_t(S.Levels.size()));for (const auto& [ID,L]:S.Levels) {W.Text(ID);W.U32(L);}
    W.Identity(S.Cast);W.Identity(S.LastRequest);W.Text(S.ProfileId);W.U8(std::uint8_t(S.Stage));W.U32(S.Step);W.U64(S.ChangedAt);W.U64(S.Deadline);
    W.U8(S.CancelPending);Write(W,S.Source);Write(W,S.Destination);W.U32(std::uint32_t(S.Members.size()));for (auto K:S.Members) W.Key(K);return W.Take();
}
bool Decode(std::span<const std::uint8_t> Bytes,State& Out)
{
    Reader R(Bytes);State S;std::uint32_t N=0;std::uint8_t Stage=0;
    if (!Count(R,N,64)) return false;
    for (std::uint32_t I=0;I<N;++I)
    {
        std::string ID;Profile P;
        if (!R.Text(ID,192)||ID.empty()||!R.Text(P.Skill,192)||P.Skill.empty()||!R.U32(P.Level)||!P.Level
            ||!R.F64(P.Radius)||!R.F64(P.MaximumHeight)||!R.F64(P.BeamHeight)||!R.U64(P.Windup)||!R.U64(P.Wait)||!R.U64(P.Recovery)||!R.U64(P.Cooldown)||!Boolean(R,P.Vehicles)
            ||!std::isfinite(P.Radius)||P.Radius<=0||P.Radius>1.e7||!std::isfinite(P.MaximumHeight)||P.MaximumHeight<0
            ||!std::isfinite(P.BeamHeight)||P.BeamHeight<=0||P.BeamHeight>1.e7
            ||P.Windup>36000||!P.Wait||P.Wait>36000||P.Recovery>36000||P.Cooldown>5184000||!S.Profiles.emplace(ID,P).second) return false;
    }
    if (!Count(R,N,64)) return false;
    for (std::uint32_t I=0;I<N;++I) {std::string ID;Tick T=0;if (!R.Text(ID,192)||!R.U64(T)||!S.ReadyAt.emplace(ID,T).second) return false;}
    if (!Count(R,N,64)) return false;
    for (std::uint32_t I=0;I<N;++I) {std::string ID;std::uint32_t L=0;if (!R.Text(ID,192)||!R.U32(L)||!L||!S.Levels.emplace(ID,L).second) return false;}
    if (!R.Identity(S.Cast)||!R.Identity(S.LastRequest)||!R.Text(S.ProfileId,192)||!R.U8(Stage)||Stage>std::uint8_t(Phase::Finished)
        ||!R.U32(S.Step)||!R.U64(S.ChangedAt)||!R.U64(S.Deadline)||!Boolean(R,S.CancelPending)||!ReadPose(R,S.Source)||!ReadPose(R,S.Destination)||!Count(R,N,MaximumMembers)) return false;
    std::set<ObjectKey> Seen;for (std::uint32_t I=0;I<N;++I) {ObjectKey K;if (!R.Key(K)||!K.Valid()||!Seen.insert(K).second) return false;S.Members.push_back(K);}
    S.Stage=Phase(Stage);if (!R.Done() || (S.Stage!=Phase::Idle && (!S.Cast.Valid()||!S.Profiles.contains(S.ProfileId)))) return false;
    Out=std::move(S);return true;
}
Bytes Encode(const Command& C)
{
    Writer W(MaximumCapabilityBytes);W.U8(std::uint8_t(C.Op));W.Identity(C.Cast);W.Text(C.ProfileId);Write(W,C.Point);W.U32(C.Step);W.U8(C.Accepted);
    W.U32(std::uint32_t(C.Members.size()));for (const auto& M:C.Members) {W.Key(M.Object);W.U64(M.OwnerEpoch);W.U64(M.ControlEpoch);Write(W,M.Transform);}return W.Take();
}
bool Decode(std::span<const std::uint8_t> Bytes,Command& Out)
{
    Reader R(Bytes);Command C;std::uint8_t Op=0;std::uint32_t N=0;
    if (!R.U8(Op)||Op>std::uint8_t(Operation::Acknowledge)||!R.Identity(C.Cast)||!C.Cast.Valid()||!R.Text(C.ProfileId,192)||!ReadPose(R,C.Point)
        ||!R.U32(C.Step)||!Boolean(R,C.Accepted)||!Count(R,N,MaximumMembers)) return false;
    std::set<ObjectKey> Seen;for (std::uint32_t I=0;I<N;++I)
    {Member M;if (!R.Key(M.Object)||!M.Object.Valid()||!R.U64(M.OwnerEpoch)||!R.U64(M.ControlEpoch)||!ReadPose(R,M.Transform)||!Seen.insert(M.Object).second) return false;C.Members.push_back(M);}
    if (!R.Done()) return false;C.Op=Operation(Op);Out=std::move(C);return true;
}
bool Read(const ObjectRecord& O,State& S) {return ReadState(O,CasterCapability,S);}
bool Read(const ObjectRecord& O,Body& S) {return ReadState(O,BodyCapability,S);}
namespace
{
bool Transition(const ObjectRecord& A,const ObjectRecord& B)
{
    if (A.RelationVersion==UINT64_MAX || B.RelationVersion!=A.RelationVersion+1) return false;
    auto Copy=B;Copy.RelationVersion=A.RelationVersion;Copy.Phased=A.Phased;
    for (const auto* Name:{CasterCapability,BodyCapability})
    {
        const auto Before=A.Capabilities.find(Name),After=B.Capabilities.find(Name);
        if ((Before==A.Capabilities.end())!=(After==B.Capabilities.end())) return false;
        if (Before!=A.Capabilities.end()) Copy.Capabilities[Name]=Before->second;
    }
    State X,Y;if (Read(A,X)) {if (!Read(B,Y)) return false;auto C=Y;C.Profiles=X.Profiles; if (Encode(C)!=Encode(Y)) return false;}
    Body P,Q;if (Read(A,P) && (!Read(B,Q)||P.Vehicle!=Q.Vehicle||P.Airborne!=Q.Airborne||P.FollowParent!=Q.FollowParent)) return false;
    Writer W,V;WriteObject(W,A);WriteObject(V,Copy);return W.Take()==V.Take();
}
CommitStatus Prepare(const CapabilityContext& C,CapabilityDelta& D)
{
    const auto& R=C.Request;const auto& Source=C.Source;Command Input;
    if (!Decode(R.CapabilityPayload,Input)||R.Target!=Source.Grant.Object||R.Faction!=Source.Grant.Faction
        ||R.ExpectedRelationVersion!=Source.RelationVersion||Source.RelationVersion==UINT64_MAX) return CommitStatus::InvalidRule;
    if (C.Operation.Capability==BodyCapability)
    {
        Body B;if (!Read(Source,B)) return CommitStatus::InvalidRule;const auto Root=C.Objects.find(B.Root);
        if (Input.Op!=Operation::Acknowledge || B.Cast!=Input.Cast||B.Step!=Input.Step
            ||B.Acknowledged==B.Step||Root==C.Objects.end()||!Input.Members.empty()) return CommitStatus::InvalidRule;
        State S;if (!Read(Root->second,S)||S.Cast!=B.Cast||S.Step!=B.Step
            ||std::find(S.Members.begin(),S.Members.end(),Source.Grant.Object)==S.Members.end()) return CommitStatus::Unauthorized;
        B.Acknowledged=B.Step;B.Accepted=Input.Accepted;B.AppliedAt=R.Header.At;if (S.Stage==Phase::Locking) B.Original=Input.Point;
        auto O=Source;Put(O,B);++O.RelationVersion;D.Objects.emplace(O.Grant.Object,std::move(O));return CommitStatus::Committed;
    }
    State S;if (!Read(Source,S)||!Active(Source)) return CommitStatus::InvalidRule;
    const auto Now=R.Header.At;Tick PhaseAt=Now;
    const auto SetPhase=[&](Phase P,Tick Delay){S.Stage=P;S.ChangedAt=PhaseAt;S.Deadline=PhaseAt+Delay;};
    if (Input.Op==Operation::SetLevel)
    {
        const auto P=S.Profiles.find(Input.ProfileId);
        if (P==S.Profiles.end() || (S.Stage!=Phase::Idle && S.Stage!=Phase::Finished)) return CommitStatus::InvalidRule;
        S.Levels[P->second.Skill]=P->second.Level;
    }
    else if (Input.Op==Operation::Begin)
    {
        const auto Profile=S.Profiles.find(Input.ProfileId);
        if ((S.Stage!=Phase::Idle&&S.Stage!=Phase::Finished)||Profile==S.Profiles.end()||S.ReadyAt[Profile->second.Skill]>Now||S.Levels[Profile->second.Skill]!=Profile->second.Level
            ||Input.Cast!=R.Header.Event||!Input.Members.empty()) return CommitStatus::InvalidRule;
        S.Cast=Input.Cast;S.ProfileId=Input.ProfileId;S.Step=0;S.Members.clear();S.Source=S.Destination=Input.Point;S.CancelPending=false;
        S.ReadyAt[Profile->second.Skill]=Now+Profile->second.Cooldown;SetPhase(Phase::Windup,Profile->second.Windup);
    }
    else
    {
        if (Input.Cast!=S.Cast||!S.Profiles.contains(S.ProfileId)||Input.Step!=S.Step) return CommitStatus::InvalidRule;
        const auto& P=S.Profiles.at(S.ProfileId);
        const auto All=[&](bool& Rejected)
        {
            Rejected=false;for (auto K:S.Members)
            {
                const auto I=C.Objects.find(K);if (I==C.Objects.end()) return false;if (!Active(I->second)) continue;
                Body B;if (!Read(I->second,B)||B.Root!=Source.Grant.Object||B.Cast!=S.Cast||B.Step!=S.Step||B.Acknowledged!=S.Step) return false;
                Rejected|=!B.Accepted;
            }
            return true;
        };
        const auto SetBodies=[&](bool Locked,bool Phased,bool Displace)
        {
            if (S.Step==UINT32_MAX) return false;++S.Step;
            for (auto K:S.Members)
            {
                const auto I=C.Objects.find(K);if (I==C.Objects.end()) return false;
                if (I->second.Grant.State!=Life::Alive || I->second.Vitals.Dead) continue;
                Body B;if (!Read(I->second,B)||B.Cast!=S.Cast||B.Root!=Source.Grant.Object||I->second.RelationVersion==UINT64_MAX) return false;
                const bool NeverLocked=S.Stage==Phase::Locking && B.Step==1 && B.Acknowledged==1 && !B.Accepted;
                B.Step=S.Step;B.ChangedAt=Now;B.Locked=Locked;B.Phased=Phased;B.Displace=Displace;B.Accepted=true;
                if (I->second.Grant.Dormant || NeverLocked)
                {
                    // A vacant controllable body stays at its last committed
                    // position. It has no simulator to acknowledge another move.
                    B.Locked=B.Phased=B.Displace=false;B.Acknowledged=B.Step;B.AppliedAt=Now;
                }
                else if (Displace)
                {
                    const auto M=std::find_if(Input.Members.begin(),Input.Members.end(),[&](const auto& M){return M.Object==K;});
                    if (M==Input.Members.end() || M->OwnerEpoch!=I->second.Grant.OwnerEpoch || M->ControlEpoch!=I->second.Grant.ControlEpoch) return false;
                    B.Destination=M->Transform;
                }
                auto O=I->second;Put(O,B);++O.RelationVersion;D.Objects.emplace(K,std::move(O));
            }
            return true;
        };
        if (Input.Op==Operation::Collect)
        {
            if (S.Stage!=Phase::Windup||Now<S.Deadline) return CommitStatus::InvalidRule;
            S.Step=1;
            std::set<ObjectKey> Selected;for (const auto& M:Input.Members) Selected.insert(M.Object);
            for (const auto& M:Input.Members)
            {
                const auto I=C.Objects.find(M.Object);Body B;
                if (I==C.Objects.end()||!Active(I->second)||!Read(I->second,B)||B.Locked||B.Phased||I->second.RelationVersion==UINT64_MAX
                    ||I->second.Grant.Faction!=Source.Grant.Faction||M.OwnerEpoch!=I->second.Grant.OwnerEpoch||M.ControlEpoch!=I->second.Grant.ControlEpoch
                    ||(B.Vehicle&&!P.Vehicles)|| (B.FollowParent?!Selected.contains(I->second.Parent):
                        std::hypot(M.Transform.X-S.Source.X,M.Transform.Y-S.Source.Y)>P.Radius)) return CommitStatus::InvalidRule;
                B.Root=Source.Grant.Object;B.Cast=S.Cast;B.Step=1;B.ChangedAt=Now;B.AppliedAt=0;B.Acknowledged=0;B.Accepted=true;B.Locked=B.Phased=true;B.Displace=false;
                B.Original=B.Destination=M.Transform;auto O=I->second;Put(O,B);++O.RelationVersion;D.Objects.emplace(M.Object,std::move(O));S.Members.push_back(M.Object);
            }
            SetPhase(S.Members.empty()?Phase::Finished:Phase::Locking,P.Wait);
        }
        else if (Input.Op==Operation::Destination)
        {
            if (S.Stage!=Phase::Choosing||Now>=S.Deadline||Input.Members.empty()||!SetBodies(true,false,true)) return CommitStatus::InvalidRule;
            S.Destination=Input.Point;SetPhase(Phase::Landing,P.Wait);
        }
        else if (Input.Op==Operation::Return)
        {
            if (S.Stage==Phase::Idle||S.Stage==Phase::Finished||S.Stage==Phase::Releasing) return CommitStatus::InvalidRule;
            const bool HasActive=std::any_of(S.Members.begin(),S.Members.end(),[&](ObjectKey K)
            {
                const auto I=C.Objects.find(K);Body B;
                return I!=C.Objects.end() && Active(I->second)
                    && !(S.Stage==Phase::Locking && Read(I->second,B) && B.Step==1 && B.Acknowledged==1 && !B.Accepted);
            });
            if (!HasActive)
            {if (!SetBodies(false,false,false)) return CommitStatus::InvalidRule;S.CancelPending=false;SetPhase(Phase::Finished,0);}
            else
            {
                bool Rejected=false;
                if ((S.Stage==Phase::Locking && !All(Rejected)) || Input.Members.empty()) S.CancelPending=true;
                else if (!SetBodies(true,false,true)) return CommitStatus::InvalidRule;
                else {S.CancelPending=false;S.Destination=S.Source;SetPhase(Phase::Landing,P.Wait);}
            }
        }
        else if (Input.Op==Operation::Release)
        {
            if (S.Stage!=Phase::Recovery||Now<S.Deadline||!SetBodies(false,false,false)) return CommitStatus::InvalidRule;
            SetPhase(Phase::Releasing,P.Wait);
        }
        else if (Input.Op==Operation::Advance)
        {
            bool Rejected=false;if (!All(Rejected)) return CommitStatus::Deferred;
            if (Rejected) return CommitStatus::InvalidRule;
            if (S.Stage==Phase::Locking) SetPhase(Phase::Choosing,P.Wait);
            else if (S.Stage==Phase::Landing) SetPhase(Phase::Recovery,P.Recovery);
            else if (S.Stage==Phase::Releasing) SetPhase(Phase::Finished,0);
            else return CommitStatus::InvalidRule;
        }
        else return CommitStatus::InvalidRule;
    }
    S.LastRequest=R.Header.Event;auto O=Source;Put(O,S);++O.RelationVersion;D.Objects.emplace(O.Grant.Object,std::move(O));return CommitStatus::Committed;
}
bool Initialize(RuntimeManifest& M,std::string& Error)
{
    State Initial;
    for (const auto& [Key,Skill]:M.Strings) if (Key.starts_with("relocation/profile/"))
    {
        const auto ID=Key.substr(19);const auto P="relocation/profile/"+ID+"/";
        const auto N=[&](const char* Field)->double {const auto I=M.Numbers.find(P+Field);return I==M.Numbers.end()?-1:I->second;};
        Profile V;V.Skill=Skill;
        for (const char* F:{"level","radius","height","beam","windup","wait","recovery","cooldown","vehicles"}) if (!std::isfinite(N(F))||N(F)<0) {Error="Invalid relocation profile: "+ID;return false;}
        for (const char* F:{"level","windup","wait","recovery","cooldown","vehicles"})
            if (N(F)>5184000 || std::floor(N(F))!=N(F)) {Error="Out-of-range relocation integer: "+ID;return false;}
        if (N("level")<1 || N("vehicles")>1 || N("height")>1.e7) {Error="Invalid relocation profile limits: "+ID;return false;}
        V.Level=std::uint32_t(N("level"));V.Radius=N("radius");V.MaximumHeight=N("height");V.BeamHeight=N("beam");V.Windup=Tick(N("windup"));V.Wait=Tick(N("wait"));V.Recovery=Tick(N("recovery"));V.Cooldown=Tick(N("cooldown"));V.Vehicles=N("vehicles")!=0;
        Initial.Profiles.emplace(ID,V);Initial.Levels.try_emplace(V.Skill,1);
    }
    for (const auto& [Key,Flags]:M.Numbers) if (Key.starts_with("relocation/target/")
        && (!std::isfinite(Flags)||Flags<0||Flags>7||std::floor(Flags)!=Flags)) {Error="Invalid relocation body flags: "+Key;return false;}
    State Checked;if (!Decode(Encode(Initial),Checked)) {Error="Invalid relocation profile schema";return false;}
    const auto Add=[&](const std::string& Def,CapabilityStates& Caps)
    {
        if (M.Strings.contains("relocation/caster/"+Def)) Caps[CasterCapability]={Version,Encode(Initial)};
        const auto I=M.Numbers.find("relocation/target/"+Def);
        if (I!=M.Numbers.end()) {const auto Flags=std::uint32_t(I->second);Body B;B.Vehicle=(Flags&1)!=0;B.Airborne=(Flags&2)!=0;B.FollowParent=(Flags&4)!=0;Caps[BodyCapability]={Version,Encode(B)};}
    };
    for (auto& [_,O]:M.Objects) Add(O.Definition,O.Capabilities);
    for (auto& [_,R]:M.Rules) if (R.SpawnKind!=ObjectKind::None) Add(R.TargetDefinition,R.SpawnCapabilities);
    M.Numbers[std::string("capability/")+CasterCapability+"/version"]=Version;M.Numbers[std::string("capability/")+BodyCapability+"/version"]=Version;return true;
}
const bool CasterRegistered=RegisterCapability({CasterCapability,Version,
    [](const ObjectRecord& O,const CapabilityState&){State S;return Read(O,S);},
    [](const Rule& R){return R.Kind==TransactionKind::Capability&&R.Capability==CasterCapability&&R.CapabilityVersion==Version&&R.CapabilityOperation=="mutate"&&!R.Amount.Positive();},
    Prepare,Transition,Initialize});
const bool BodyRegistered=RegisterCapability({BodyCapability,Version,
    [](const ObjectRecord& O,const CapabilityState&){Body B;return Read(O,B)&&B.Phased==O.Phased;},
    [](const Rule& R){return R.Kind==TransactionKind::Capability&&R.Capability==BodyCapability&&R.CapabilityVersion==Version&&R.CapabilityOperation=="ack"&&!R.Amount.Positive();},
    Prepare,Transition});
}
}
