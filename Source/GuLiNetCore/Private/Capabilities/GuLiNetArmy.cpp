#include "GuLiNetArmy.h"
#include "GuLiNetManifest.h"
#include "GuLiNetHash.h"
#include <algorithm>
#include <cmath>
#include <functional>

namespace GuLiNet::Army
{
namespace
{
constexpr std::uint32_t Limit=512;
bool Name(const std::string& S) {return !S.empty() && S.size()<=192 && S.find('\0')==std::string::npos;}
bool Count(Reader& R,std::uint32_t& N,std::uint32_t Maximum=Limit) {return R.U32(N) && N<=Maximum;}
bool Bool(Reader& R,bool& V) {std::uint8_t N;if (!R.U8(N) || N>1) return false;V=N!=0;return true;}
void Texts(Writer& W,const std::vector<std::string>& V) {W.U32(std::uint32_t(V.size()));for (const auto& S:V) W.Text(S);}
bool Texts(Reader& R,std::vector<std::string>& V)
{std::uint32_t N;if (!Count(R,N)) return false;V.resize(N);for (auto& S:V) if (!R.Text(S,192) || !Name(S)) return false;return true;}
void WriteSlot(Writer& W,const Slot& S) {W.U16(S.Unit);W.Text(S.Name);}
bool ReadSlot(Reader& R,Slot& S) {return R.U16(S.Unit) && S.Unit && R.Text(S.Name,192) && Name(S.Name);}
void WriteSelector(Writer& W,const Selector& S)
{W.U32(std::uint32_t(S.Units.size()));for (auto U:S.Units) W.U16(U);W.Text(S.SlotName);W.Text(S.Skill);Texts(W,S.Tags);}
bool ReadSelector(Reader& R,Selector& S)
{
    std::uint32_t N;if (!Count(R,N)) return false;S.Units.resize(N);
    for (auto& U:S.Units) if (!R.U16(U) || !U) return false;
    return R.Text(S.SlotName,192) && Name(S.SlotName) && R.Text(S.Skill,192) && Texts(R,S.Tags);
}
void WriteSource(Writer& W,const Source& S)
{
    W.Identity(S.Identity);W.U32(std::uint32_t(S.Modifiers.size()));
    for (const auto& M:S.Modifiers) {WriteSelector(W,M.Target);W.U8(M.Attribute);W.U8(M.Operation);W.F32(M.Magnitude);W.U32(std::uint32_t(M.Integer));}
    W.U32(std::uint32_t(S.Replacements.size()));for (const auto& V:S.Replacements) {WriteSelector(W,V.Target);W.Text(V.Skill);W.U32(std::uint32_t(V.Priority));}
    W.U32(std::uint32_t(S.Unlocks.size()));for (const auto& V:S.Unlocks) WriteSelector(W,V);
    W.U32(std::uint32_t(S.MovementPercent.size()));for (const auto& [U,V]:S.MovementPercent) {W.U16(U);W.F32(V);}
}
bool ReadSource(Reader& R,Source& S)
{
    std::uint32_t N;if (!R.Identity(S.Identity) || !Count(R,N)) return false;S.Modifiers.resize(N);
    for (auto& M:S.Modifiers) {std::uint32_t I;if (!ReadSelector(R,M.Target) || !R.U8(M.Attribute) || !R.U8(M.Operation) || !R.F32(M.Magnitude) || !R.U32(I)) return false;M.Integer=std::int32_t(I);}
    if (!Count(R,N)) return false;S.Replacements.resize(N);
    for (auto& V:S.Replacements) {std::uint32_t P;if (!ReadSelector(R,V.Target) || !R.Text(V.Skill,192) || !R.U32(P)) return false;V.Priority=std::int32_t(P);}
    if (!Count(R,N)) return false;S.Unlocks.resize(N);for (auto& V:S.Unlocks) if (!ReadSelector(R,V)) return false;
    if (!Count(R,N)) return false;for (std::uint32_t I=0;I<N;++I) {std::uint16_t U;float V;if (!R.U16(U) || !R.F32(V) || !S.MovementPercent.emplace(U,V).second) return false;}
    return true;
}
void WriteProfile(Writer& W,const Profile& P)
{
    WriteSlot(W,P.Key);W.Text(P.Skill);W.Text(P.Executor);Texts(W,P.Tags);
    for (float V:{P.Damage,P.Rate,P.Range,P.Speed,P.Lifetime,P.Sweep,P.Spread}) W.F32(V);
    W.U32(std::uint32_t(P.Count));W.U32(std::uint32_t(P.MaximumCount));W.U8(P.Default);W.U8(P.Unlocked);W.U8(P.Equipped);W.U8(P.Active);
}
bool ReadProfile(Reader& R,Profile& P)
{
    std::uint32_t C,M;
    if (!ReadSlot(R,P.Key) || !R.Text(P.Skill,192) || !R.Text(P.Executor,192) || !Texts(R,P.Tags)) return false;
    for (auto* V:{&P.Damage,&P.Rate,&P.Range,&P.Speed,&P.Lifetime,&P.Sweep,&P.Spread}) if (!R.F32(*V)) return false;
    if (!R.U32(C) || !R.U32(M) || C>INT32_MAX || M>INT32_MAX || !C || C>M) return false;P.Count=std::int32_t(C);P.MaximumCount=std::int32_t(M);
    return Bool(R,P.Default) && Bool(R,P.Unlocked) && Bool(R,P.Equipped) && Bool(R,P.Active);
}
bool Matches(const Selector& S,const Slot& K)
{return S.SlotName==K.Name && (S.Units.empty() || std::find(S.Units.begin(),S.Units.end(),K.Unit)!=S.Units.end());}
bool MatchesSkill(const Selector& S,const Profile& P)
{
    if (!S.Skill.empty() && S.Skill!=P.Skill) return false;
    for (const auto& Tag:S.Tags)
        if (std::none_of(P.Tags.begin(),P.Tags.end(),[&](const auto& T){return T==Tag || T.starts_with(Tag+".");})) return false;
    return true;
}
bool ValidNumber(double V,int I) {return std::isfinite(V) && V>=0 && V<=(I==1?30.:I==2?1000000.:1000000000.);}
bool ValidTarget(const Selector& S,const Catalog& C,bool Filter)
{
    if (!Name(S.SlotName) || (!Filter && (!S.Skill.empty() || !S.Tags.empty()))) return false;
    if (!S.Skill.empty() && std::none_of(C.Profiles.begin(),C.Profiles.end(),[&](const auto& P){return P.Skill==S.Skill;})) return false;
    for (auto U:S.Units) if (std::none_of(C.Profiles.begin(),C.Profiles.end(),[&](const auto& P){return P.Default && P.Key.Unit==U && P.Key.Name==S.SlotName;})) return false;
    return std::any_of(C.Profiles.begin(),C.Profiles.end(),[&](const auto& P){return P.Default && Matches(S,P.Key);});
}
bool ValidSource(const Source& S,const Catalog& C)
{
    if (S.Modifiers.size()>Limit || S.Replacements.size()>Limit || S.Unlocks.size()>Limit) return false;
    for (const auto& M:S.Modifiers)
        if (!ValidTarget(M.Target,C,true) || M.Attribute>3 || M.Operation>1 || !std::isfinite(M.Magnitude)
            || (M.Operation==1 && M.Magnitude < -1.f)
            || (M.Attribute==3?(M.Operation!=0 || M.Magnitude!=0):M.Integer!=0)) return false;
    for (const auto& V:S.Replacements)
        if (!ValidTarget(V.Target,C,false) || !Name(V.Skill)) return false;
    for (const auto& V:S.Unlocks) if (!ValidTarget(V,C,false)) return false;
    for (const auto& [U,V]:S.MovementPercent)
        if (!U || !std::isfinite(V) || V<0 || std::none_of(C.Profiles.begin(),C.Profiles.end(),[&](const auto& P){return P.Key.Unit==U;})) return false;
    return true;
}
bool ResolveImpl(const Catalog& C,const std::map<Id,Source>& Sources,const std::map<Slot,std::string>& Equipment,std::vector<Profile>& Out)
{
    if (Sources.size()>Limit || Equipment.size()>Limit) return false;
    for (const auto& [Id,S]:Sources) if (!Id.Valid() || Id!=S.Identity || !ValidSource(S,C)) return false;
    for (const auto& [K,V]:Equipment)
        if (std::none_of(C.Profiles.begin(),C.Profiles.end(),[&](const auto& P){return P.Key==K && (V.empty()?P.Default:P.Skill==V);})) return false;
    std::map<std::uint16_t,float> Movement;
    for (const auto& [_,S]:Sources) for (const auto& [U,V]:S.MovementPercent)
    {auto [I,Added]=Movement.try_emplace(U,1.f);I->second*=1.f+V;const auto Base=C.MovementBase.find(U);if (Base==C.MovementBase.end() || !std::isfinite(I->second) || Base->second*I->second>INT16_MAX) return false;}
    std::vector<Profile> Result;
    for (const auto& Default:C.Profiles) if (Default.Default)
    {
        const auto Selected=Equipment.find(Default.Key);std::string Skill=Selected!=Equipment.end() && !Selected->second.empty()?Selected->second:Default.Skill;
        bool Unlocked=Default.Unlocked;std::map<std::int32_t,std::string> Replacements;
        for (const auto& [_,S]:Sources)
        {
            for (const auto& U:S.Unlocks) Unlocked|=Matches(U,Default.Key);
            for (const auto& R:S.Replacements) if (Matches(R.Target,Default.Key))
            {
                const auto [It,Added]=Replacements.emplace(R.Priority,R.Skill);
                if ((!Added && It->second!=R.Skill) || std::none_of(C.Profiles.begin(),C.Profiles.end(),[&](const auto& P){return P.Key==Default.Key && P.Skill==R.Skill;})) return false;
            }
        }
        if (!Replacements.empty()) Skill=Replacements.rbegin()->second;
        const auto Config=std::find_if(C.Profiles.begin(),C.Profiles.end(),[&](const auto& P){return P.Key==Default.Key && P.Skill==Skill;});
        if (Config==C.Profiles.end()) return false;auto P=*Config;
        double Flat[3]{},Factor[3]{1,1,1};std::int64_t CountValue=P.Count;
        // Id is four big-endian GUID words: its ordering is the same as the
        // existing UE GUID-string ordering used for floating point reduction.
        for (const auto& [_,S]:Sources)
        {
            double Percent[3]{};
            for (const auto& M:S.Modifiers) if (Matches(M.Target,P.Key) && MatchesSkill(M.Target,P))
            {if (M.Attribute==3) CountValue+=M.Integer;else if (!M.Operation) Flat[M.Attribute]+=M.Magnitude;else Percent[M.Attribute]+=M.Magnitude;}
            for (int I=0;I<3;++I) {if (Percent[I]<-1.) return false;Factor[I]*=1.+Percent[I];}
        }
        const double Values[3]{(P.Damage+Flat[0])*Factor[0],(P.Rate+Flat[1])*Factor[1],(P.Range+Flat[2])*Factor[2]};
        for (int I=0;I<3;++I) if (!ValidNumber(Values[I],I)) return false;
        if (CountValue<1 || CountValue>P.MaximumCount) return false;
        P.Damage=float(Values[0]);P.Rate=float(Values[1]);P.Range=float(Values[2]);P.Count=std::int32_t(CountValue);
        P.Unlocked=Unlocked;P.Equipped=Unlocked && (Selected==Equipment.end() || !Selected->second.empty());Result.push_back(std::move(P));
    }
    std::sort(Result.begin(),Result.end(),[](const auto& A,const auto& B){return A.Key<B.Key;});Out=std::move(Result);return true;
}
}
bool ValidateCatalog(const Catalog& C,std::string& Error)
{
    auto Fail=[&](){Error="Invalid army numeric/card catalog";return false;};
    if (C.Profiles.empty() || C.Profiles.size()>Limit || C.Cards.size()>Limit || C.Rules.size()>4096) return Fail();
    std::set<std::pair<Slot,std::string>> Keys;std::map<Slot,int> Defaults;
    for (const auto& P:C.Profiles)
    {
        if (!P.Key.Unit || !Name(P.Key.Name) || !Name(P.Skill) || !Name(P.Executor) || !Keys.emplace(P.Key,P.Skill).second
            || !ValidNumber(P.Damage,0) || !ValidNumber(P.Rate,1) || !ValidNumber(P.Range,2) || P.Count<1 || P.Count>P.MaximumCount
            || !ValidNumber(P.Speed,0) || !ValidNumber(P.Lifetime,0) || !ValidNumber(P.Sweep,0) || !ValidNumber(P.Spread,0) || P.Spread>180) return Fail();
        Defaults[P.Key]+=P.Default;
    }
    for (const auto& [_,N]:Defaults) if (N!=1) return Fail();
    for (const auto& [U,V]:C.MovementBase) if (!U || !std::isfinite(V) || V<=0 || V>INT16_MAX) return Fail();
    for (const auto& [Name,Crd]:C.Cards)
    {
        if (!Army::Name(Name) || Crd.Id!=Name || !ValidSource(Crd.Effect,C)) return Fail();
        for (const auto* V:{&Crd.Required,&Crd.Excluded})
        {std::set<std::string> Seen;for (const auto& Id:*V) if (Id==Name || !C.Cards.contains(Id) || !Seen.insert(Id).second) return Fail();}
    }
    std::map<std::string,std::set<std::string>> Closures;std::set<std::string> Visiting;
    std::function<bool(const std::string&)> Visit=[&](const std::string& Id)
    {
        if (Closures.contains(Id)) return true;if (!Visiting.insert(Id).second) return false;
        std::set<std::string> All{Id};for (const auto& Ref:C.Cards.at(Id).Required)
        {if (!Visit(Ref)) return false;All.insert(Closures.at(Ref).begin(),Closures.at(Ref).end());}
        for (const auto& Ref:All) for (const auto& Other:C.Cards.at(Ref).Excluded) if (All.contains(Other)) return false;
        Visiting.erase(Id);Closures.emplace(Id,std::move(All));return true;
    };
    for (const auto& [Id,_]:C.Cards) if (!Visit(Id)) return Fail();
    for (const auto& [Id,R]:C.Rules) if (Id.empty() || Id.size()>256 || !Keys.contains({R.Key,R.Skill})) return Fail();
    std::vector<Profile> V;if (!ResolveImpl(C,{},{},V)) return Fail();Error.clear();return true;
}
bool Resolve(const Catalog& C,const std::map<Id,Source>& S,const std::map<Slot,std::string>& E,std::vector<Profile>& P,std::string& Error)
{if (!ValidateCatalog(C,Error) || !ResolveImpl(C,S,E,P)) {Error="Army source/equipment numeric resolution rejected";return false;}Error.clear();return true;}
Bytes EncodeCatalog(const Catalog& C)
{
    Writer W(MaximumCapabilityBytes);W.U32(Version);W.U32(std::uint32_t(C.Profiles.size()));for (const auto& P:C.Profiles) WriteProfile(W,P);
    W.U32(std::uint32_t(C.Cards.size()));for (const auto& [_,V]:C.Cards) {W.Text(V.Id);W.U32(V.Maximum);Texts(W,V.Required);Texts(W,V.Excluded);WriteSource(W,V.Effect);}
    W.U32(std::uint32_t(C.Rules.size()));for (const auto& [Id,V]:C.Rules) {W.Text(Id);WriteSlot(W,V.Key);W.Text(V.Skill);W.U8(V.Admission);W.Text(V.SourceDefinition);}
    W.U32(std::uint32_t(C.MovementBase.size()));for (const auto& [U,V]:C.MovementBase) {W.U16(U);W.F32(V);}return W.Take();
}
bool DecodeCatalog(std::span<const std::uint8_t> B,Catalog& Out)
{
    Reader R(B);Catalog C;std::uint32_t SerializedVersion,N;if (!R.U32(SerializedVersion) || SerializedVersion!=Version || !Count(R,N)) return false;C.Profiles.resize(N);
    for (auto& P:C.Profiles) if (!ReadProfile(R,P)) return false;
    if (!Count(R,N)) return false;for (std::uint32_t I=0;I<N;++I) {Card V;if (!R.Text(V.Id,192) || !R.U32(V.Maximum) || !Texts(R,V.Required) || !Texts(R,V.Excluded) || !ReadSource(R,V.Effect)) return false;auto Id=V.Id;if (!C.Cards.emplace(Id,std::move(V)).second) return false;}
    if (!Count(R,N,4096)) return false;for (std::uint32_t I=0;I<N;++I) {std::string Id;RuleBinding V;if (!R.Text(Id,256) || !ReadSlot(R,V.Key) || !R.Text(V.Skill,192) || !Bool(R,V.Admission) || !R.Text(V.SourceDefinition,192) || !Name(V.SourceDefinition) || !C.Rules.emplace(Id,std::move(V)).second) return false;}
    if (!Count(R,N)) return false;for (std::uint32_t I=0;I<N;++I) {std::uint16_t U;float V;if (!R.U16(U) || !R.F32(V) || !C.MovementBase.emplace(U,V).second) return false;}
    std::string Error;if (!R.Done() || !ValidateCatalog(C,Error)) return false;Out=std::move(C);return true;
}
Bytes EncodeState(const State& S)
{
    Writer W(MaximumCapabilityBytes);W.U32(Version);W.Blob(EncodeCatalog(S.Definitions));W.U64(S.RevisionValue);W.Identity(S.LastRequest);W.U64(S.Random);
    const auto& O=S.Current;W.Identity(O.Request);W.Identity(O.Session);W.Identity(O.PreviousSession);Texts(W,O.Candidates);W.Text(O.Selected);W.U64(O.CommittedAt);W.U8(O.Finished);W.U8(O.PresentationClosed);
    W.U32(std::uint32_t(S.Sources.size()));for (const auto& [_,V]:S.Sources) WriteSource(W,V);
    W.U32(std::uint32_t(S.Acquired.size()));for (const auto& [Id,N]:S.Acquired) {W.Text(Id);W.U32(N);}
    W.U32(std::uint32_t(S.Equipment.size()));for (const auto& [K,V]:S.Equipment) {WriteSlot(W,K);W.Text(V);}return W.Take();
}
bool DecodeState(std::span<const std::uint8_t> B,State& Out)
{
    if (B.size()>MaximumCapabilityBytes) return false;Reader R(B);State S;Bytes CatalogBytes;std::uint32_t SerializedVersion,N;
    if (!R.U32(SerializedVersion) || SerializedVersion!=Version || !R.Blob(CatalogBytes,MaximumCapabilityBytes) || !DecodeCatalog(CatalogBytes,S.Definitions) || !R.U64(S.RevisionValue) || !S.RevisionValue || !R.Identity(S.LastRequest) || !R.U64(S.Random) || !S.Random) return false;
    auto& O=S.Current;if (!R.Identity(O.Request) || !R.Identity(O.Session) || !R.Identity(O.PreviousSession) || !Texts(R,O.Candidates) || O.Candidates.size()>3
        || !R.Text(O.Selected,192) || !R.U64(O.CommittedAt) || !Bool(R,O.Finished) || !Bool(R,O.PresentationClosed)) return false;
    std::set<std::string> Seen;for (const auto& Id:O.Candidates) if (!S.Definitions.Cards.contains(Id) || !Seen.insert(Id).second) return false;
    if ((!O.Selected.empty() && (!O.Finished || !Seen.contains(O.Selected))) || (O.Session.Valid()!=O.Request.Valid()) || (!O.Session.Valid() && (!O.Candidates.empty() || O.Finished))) return false;
    if (!Count(R,N)) return false;for (std::uint32_t I=0;I<N;++I) {Source V;if (!ReadSource(R,V)) return false;const auto Id=V.Identity;if (!S.Sources.emplace(Id,std::move(V)).second) return false;}
    if (!Count(R,N)) return false;for (std::uint32_t I=0;I<N;++I) {std::string Id;std::uint32_t C;if (!R.Text(Id,192) || !S.Definitions.Cards.contains(Id) || !R.U32(C) || !C || (S.Definitions.Cards.at(Id).Maximum && C>S.Definitions.Cards.at(Id).Maximum) || !S.Acquired.emplace(Id,C).second) return false;}
    if (!Count(R,N)) return false;for (std::uint32_t I=0;I<N;++I) {Slot K;std::string V;if (!ReadSlot(R,K) || !R.Text(V,192) || !S.Equipment.emplace(K,V).second) return false;}
    std::vector<Profile> P;std::string Error;if (!R.Done() || !Resolve(S.Definitions,S.Sources,S.Equipment,P,Error)) return false;Out=std::move(S);return true;
}
bool GetState(const ObjectRecord& O,State& S) {const auto I=O.Capabilities.find(StateCapability);return I!=O.Capabilities.end() && I->second.Version==Version && DecodeState(I->second.Data,S);}
bool SetState(ObjectRecord& O,const State& S) {auto B=EncodeState(S);if (B.empty()) return false;O.Capabilities[StateCapability]={Version,std::move(B)};return true;}
Bytes EncodeCommand(const Command& C)
{Writer W(2048);W.U32(Version);W.U8(std::uint8_t(C.Op));W.Identity(C.Request);W.Identity(C.Session);W.U64(C.Expected);W.Text(C.Choice);W.U16(C.EquipmentSlot.Unit);W.Text(C.EquipmentSlot.Name);return W.Take();}
bool DecodeCommand(std::span<const std::uint8_t> B,Command& C)
{Reader R(B);std::uint32_t V;std::uint8_t O;if (!R.U32(V) || V!=Version || !R.U8(O) || O>std::uint8_t(Operation::ClosePresentation)) return false;C.Op=Operation(O);return R.Identity(C.Request) && C.Request.Valid() && R.Identity(C.Session) && R.U64(C.Expected) && R.Text(C.Choice,192) && R.U16(C.EquipmentSlot.Unit) && R.Text(C.EquipmentSlot.Name,192) && R.Done();}
Bytes EncodeLink(const Link& L)
{Writer W(MaximumCapabilityBytes);W.U32(Version);for (const auto& K:L.Roots) W.Key(K);W.U32(std::uint32_t(L.Frozen.size()));for (const auto& [Id,P]:L.Frozen) {W.Text(Id);WriteProfile(W,P);}return W.Take();}
bool DecodeLink(std::span<const std::uint8_t> B,Link& L)
{Reader R(B);std::uint32_t V,N;if (!R.U32(V) || V!=Version) return false;for (auto& K:L.Roots) if (!R.Key(K) || !K.Valid()) return false;if (!Count(R,N,4096)) return false;for (std::uint32_t I=0;I<N;++I) {std::string Id;Profile P;if (!R.Text(Id,256) || !ReadProfile(R,P) || !L.Frozen.emplace(Id,std::move(P)).second) return false;}return R.Done();}
}
