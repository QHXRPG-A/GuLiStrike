#include "GuLiNetEffects.h"
#include "GuLiNetWeaponAuthoring.h"
#include <algorithm>
#include <cmath>

namespace GuLiNet::Effects
{
namespace
{
std::map<std::string,Handler>& Registry(){static std::map<std::string,Handler> R;return R;}
bool Name(const std::string& S){return !S.empty()&&S.size()<=192&&S.find('\0')==std::string::npos;}
bool ValidRicochet(const Ricochet& V)
{return V.Minimum&&V.Minimum<=V.Maximum&&V.Maximum<=MaximumRicochetHops&&V.PerStack&&std::isfinite(V.Range)&&V.Range>0&&V.Range<=1000000&&std::isfinite(V.Retention)&&V.Retention>0&&V.Retention<=1;}
}
bool Register(Handler H)
{if(!Name(H.Name)||!H.VersionValue||!H.Validate||!H.Accumulate)return false;const auto K=H.Name;return Registry().emplace(K,std::move(H)).second;}
const Handler* Find(const std::string& K){const auto I=Registry().find(K);return I==Registry().end()?nullptr:&I->second;}
bool Validate(const Definition& D)
{const auto* H=Find(D.Handler);return D.Id&&H&&D.VersionValue==H->VersionValue&&D.Parameters.size()<=4096&&H->Validate(D);}
bool Validate(const Set& S)
{if(S.size()>64)return false;std::set<std::uint32_t> Seen;for(const auto& D:S)if(!Seen.insert(D.Id).second||!Validate(D))return false;return true;}
Bytes Encode(const Set& S)
{if(!Validate(S))return {};Writer W(MaximumCapabilityBytes);W.U32(Version);W.U32(std::uint32_t(S.size()));for(const auto& D:S){W.U32(D.Id);W.U32(D.VersionValue);W.Text(D.Handler);W.Blob(D.Parameters);}return W.Take();}
bool Decode(std::span<const std::uint8_t> B,Set& Out)
{Reader R(B);Set S;std::uint32_t V,N;if(!R.U32(V)||V!=Version||!R.U32(N)||N>64)return false;S.resize(N);for(auto& D:S)if(!R.U32(D.Id)||!R.U32(D.VersionValue)||!R.Text(D.Handler,192)||!R.Blob(D.Parameters,4096))return false;if(!R.Done()||!Validate(S))return false;Out=std::move(S);return true;}
Bytes EncodeRicochet(const Ricochet& V)
{if(!ValidRicochet(V))return {};Writer W;W.U32(V.Minimum);W.U32(V.Maximum);W.U32(V.PerStack);W.F32(V.Range);W.F32(V.Retention);return W.Take();}
bool DecodeRicochet(std::span<const std::uint8_t> B,Ricochet& Out)
{Reader R(B);Ricochet V;if(!R.U32(V.Minimum)||!R.U32(V.Maximum)||!R.U32(V.PerStack)||!R.F32(V.Range)||!R.F32(V.Retention)||!R.Done()||!ValidRicochet(V))return false;Out=V;return true;}
Ricochet GetRicochet(const Set& S)
{Ricochet V;for(const auto& D:S)if(D.Handler=="ricochet"&&!DecodeRicochet(D.Parameters,V))return {};return V;}
bool AcceptedHit(const Set& S,const TransactionRequest& R,const CommitResult& Result,const std::map<ObjectKey,ObjectRecord>& Previous,std::map<ObjectKey,ObjectRecord>& Changes)
{std::set<std::string> Seen;for(const auto& D:S)if(Seen.insert(D.Handler).second){const auto* H=Find(D.Handler);if(!H||(H->AcceptedHit&&!H->AcceptedHit(S,R,Result,Previous,Changes)))return false;}return true;}
bool FreezePresentation(const Set& S,std::uint32_t& Bullet,std::uint32_t& Impact,std::uint32_t& Muzzle)
{for(const auto& D:S){const auto* H=Find(D.Handler);if(!H||(H->Presentation&&!H->Presentation(D,Bullet,Impact,Muzzle)))return false;}return true;}
bool ResolveHitProperties(const Set& effects,GroundHitProperties& out)
{
    GroundHitProperties hit;
    for(const auto& effect:effects)
    {
        const auto* handler=Find(effect.Handler);
        if(!handler || (handler->ModifyHit && !handler->ModifyHit(effect,hit)))return false;
    }
    out=hit;
    return true;
}
bool ResolveGroundShot(const GroundWeaponBase& base,const Set& effects,GroundShotStats& out)
{
    const bool enhanced=HasEnhancedBullet(effects);
    GroundShotStats shot;
    shot.FireRate=base.FireRate*FireRateMultiplier(effects);
    shot.Damage=base.Damage*(enhanced?GetBulletRank(effects).DamageMultiplier:base.InitialDamageRatio);
    shot.BulletVfx=enhanced?base.BulletVfx:base.InitialBulletVfx;
    shot.ImpactVfx=base.ImpactVfx;
    shot.MuzzleVfx=base.MuzzleVfx;
    if(!std::isfinite(shot.Damage) || shot.Damage<=0 || !std::isfinite(shot.FireRate)
        || shot.FireRate<=0 || shot.FireRate>MaximumGroundFireRate
        || !FreezePresentation(effects,shot.BulletVfx,shot.ImpactVfx,shot.MuzzleVfx))return false;
    out=shot;
    return true;
}
Bytes EncodeFireRate(float Multiplier)
{if(!std::isfinite(Multiplier)||Multiplier<=1||Multiplier>MaximumGroundFireRate)return {};Writer W;W.F32(Multiplier);return W.Take();}
bool DecodeFireRate(std::span<const std::uint8_t> B,float& Out)
{Reader R(B);float V;if(!R.F32(V)||!R.Done()||EncodeFireRate(V).empty())return false;Out=V;return true;}
float FireRateMultiplier(const Set& S)
{float Value=GetBulletRank(S).RateMultiplier;for(const auto& D:S)if(D.Handler=="ground/fire_rate"){float V;if(!DecodeFireRate(D.Parameters,V))return 0;Value*=V;}return Value;}
bool HasEnhancedBullet(const Set& S)
{return std::any_of(S.begin(),S.end(),[](const auto& D){return D.Handler=="ground/enhanced_bullet";});}
Bytes EncodeExplosive(const ExplosivePackage& V)
{
    if(!std::isfinite(V.SpeedMultiplier)||V.SpeedMultiplier<=0||V.SpeedMultiplier>1||!std::isfinite(V.OutwardOffset)||V.OutwardOffset<0||V.OutwardOffset>1000||V.Intervals.empty()||V.Intervals.size()>16||V.Level>V.Intervals.size()||!std::isfinite(V.DamageMultiplier)||V.DamageMultiplier<=0||!std::isfinite(V.Radius)||V.Radius<=0||V.Radius>100000||!V.BulletVfx||V.BulletVfx>INT32_MAX||!V.ImpactVfx||V.ImpactVfx>INT32_MAX)return {};
    Writer W;W.U32(std::uint32_t(V.Intervals.size()));std::uint32_t Previous=UINT32_MAX;
    for(auto N:V.Intervals){if(!N||N>=Previous)return {};W.U32(N);Previous=N;}
    W.U32(V.Level);W.F32(V.DamageMultiplier);W.F32(V.Radius);W.U32(V.BulletVfx);W.U32(V.ImpactVfx);W.F32(V.SpeedMultiplier);W.F32(V.OutwardOffset);return W.Take();
}
bool DecodeExplosive(std::span<const std::uint8_t> B,ExplosivePackage& Out)
{
    Reader R(B);ExplosivePackage V;std::uint32_t N;if(!R.U32(N)||!N||N>16)return false;V.Intervals.resize(N);
    for(auto& I:V.Intervals)if(!R.U32(I))return false;
    if(!R.U32(V.Level)||!R.F32(V.DamageMultiplier)||!R.F32(V.Radius)||!R.U32(V.BulletVfx)||!R.U32(V.ImpactVfx)||!R.F32(V.SpeedMultiplier)||!R.F32(V.OutwardOffset)||!R.Done()||EncodeExplosive(V).empty())return false;Out=std::move(V);return true;
}
Bytes EncodeEnhancedBullet(const EnhancedBullet& Bullet)
{
    if(Bullet.Ranks.empty() || Bullet.Ranks.size()>16 || Bullet.Level>Bullet.Ranks.size())return {};
    Writer W; W.U32(Bullet.Level); W.U32(std::uint32_t(Bullet.Ranks.size()));
    for(const auto& R:Bullet.Ranks)
    {
        if(!std::isfinite(R.DamageMultiplier) || R.DamageMultiplier<=0 || !std::isfinite(R.RateMultiplier) || R.RateMultiplier<=0 || !R.Vfx)return {};
        W.F32(R.DamageMultiplier); W.F32(R.RateMultiplier); W.U32(R.Vfx);
    }
    return W.Take();
}
bool DecodeEnhancedBullet(std::span<const std::uint8_t> Data,EnhancedBullet& Out)
{
    Reader R(Data); EnhancedBullet Bullet; std::uint32_t Count;
    if(!R.U32(Bullet.Level) || !R.U32(Count) || !Count || Count>16 || Bullet.Level>Count)return false;
    Bullet.Ranks.resize(Count);
    for(auto& Rank:Bullet.Ranks)if(!R.F32(Rank.DamageMultiplier) || !R.F32(Rank.RateMultiplier) || !R.U32(Rank.Vfx))return false;
    if(!R.Done() || EncodeEnhancedBullet(Bullet).empty())return false;
    Out=std::move(Bullet); return true;
}
BulletRank GetBulletRank(const Set& Effects)
{
    for(const auto& D:Effects)if(D.Handler=="ground/enhanced_bullet" && !D.Parameters.empty())
    {
        EnhancedBullet Bullet;
        if(DecodeEnhancedBullet(D.Parameters,Bullet) && Bullet.Level)return Bullet.Ranks[Bullet.Level-1];
    }
    return {};
}
Bytes EncodeAIEngine(const AIEngine& Engine)
{
    if(Engine.Ranks.empty() || Engine.Ranks.size()>16 || Engine.Level>Engine.Ranks.size())return {};
    Writer W; W.U32(Engine.Level); W.U32(std::uint32_t(Engine.Ranks.size()));
    for(const auto& Rank:Engine.Ranks)
    {
        if(!std::isfinite(Rank.Seconds) || Rank.Seconds<=0 || !std::isfinite(Rank.RangeCm) || Rank.RangeCm<=0)return {};
        W.F32(Rank.Seconds); W.F32(Rank.RangeCm);
    }
    const auto& geometry=Engine.Ranks.front();
    if(geometry.AperturePixels || geometry.HexPixels)
    {
        if(!std::isfinite(geometry.AperturePixels) || !std::isfinite(geometry.HexPixels) ||
           geometry.HexPixels<=0 || geometry.HexPixels>geometry.AperturePixels)return {};
        W.F32(geometry.AperturePixels); W.F32(geometry.HexPixels);
    }
    return W.Take();
}
bool DecodeAIEngine(std::span<const std::uint8_t> Data,AIEngine& Out)
{
    Reader R(Data); AIEngine Engine; std::uint32_t Count;
    if(!R.U32(Engine.Level) || !R.U32(Count) || !Count || Count>16 || Engine.Level>Count)return false;
    Engine.Ranks.resize(Count);
    for(auto& Rank:Engine.Ranks)if(!R.F32(Rank.Seconds) || !R.F32(Rank.RangeCm))return false;
    if(!R.Done())
    {
        float aperture,hex;
        if(!R.F32(aperture) || !R.F32(hex) || aperture<=0 || hex<=0)return false;
        for(auto& rank:Engine.Ranks){rank.AperturePixels=aperture;rank.HexPixels=hex;}
    }
    if(!R.Done() || EncodeAIEngine(Engine).empty())return false;
    Out=std::move(Engine); return true;
}
AILockRank GetAILockRank(const Set& Effects)
{
    for(const auto& D:Effects)if(D.Handler=="ground/ai_engine")
    {
        AIEngine Engine;
        if(DecodeAIEngine(D.Parameters,Engine) && Engine.Level)
        {
            auto rank=Engine.Ranks[Engine.Level-1];
            // The original catalogue stored only time and range.
            if(!rank.AperturePixels){rank.AperturePixels=342;rank.HexPixels=62;}
            return rank;
        }
    }
    return {};
}
Bytes EncodeSplitBarrel(const SplitBarrel& profile)
{
    if(profile.Ranks.empty() || profile.Ranks.size()>16 || profile.Level>profile.Ranks.size())return {};
    Writer w;
    w.U32(profile.Level); w.U32(std::uint32_t(profile.Ranks.size()));
    std::uint32_t previous=0;
    for(auto cap:profile.Ranks)
    {
        if(cap<=previous || cap>MaximumLockTargets)return {};
        w.U32(cap); previous=cap;
    }
    return w.Take();
}
bool DecodeSplitBarrel(std::span<const std::uint8_t> bytes,SplitBarrel& out)
{
    SplitBarrel profile;
    Reader r(bytes); std::uint32_t count;
    if(!r.U32(profile.Level) || !r.U32(count) || !count || count>16)return false;
    profile.Ranks.resize(count);
    for(auto& cap:profile.Ranks)if(!r.U32(cap))return false;
    if(!r.Done() || EncodeSplitBarrel(profile).empty())return false;
    out=std::move(profile); return true;
}
std::uint32_t LockCapacity(const Set& effects)
{
    for(const auto& def:effects)if(def.Handler=="ground/split_barrel")
    {
        SplitBarrel profile;
        if(DecodeSplitBarrel(def.Parameters,profile) && profile.Level)return profile.Ranks[profile.Level-1];
    }
    return 1;
}
Bytes EncodeElectromagnetic(const Electromagnetic& profile)
{
    if(profile.ArcVfx>INT32_MAX || profile.Ranks.empty() || profile.Ranks.size()>16 || profile.Level>profile.Ranks.size())return {};
    Writer w;
    w.U32(profile.Level); w.U32(profile.ArcVfx); w.U32(std::uint32_t(profile.Ranks.size()));
    for(const auto& rank:profile.Ranks)
    {
        if(!(rank.SlowFraction>0 && rank.SlowFraction<1 && rank.Duration>0 && rank.Duration<=3600 &&
             rank.ShieldBonus>=0 && rank.ShieldBonus<=10))return {};
        w.F32(rank.SlowFraction); w.F32(rank.Duration); w.F32(rank.ShieldBonus);
    }
    return w.Take();
}
bool DecodeElectromagnetic(std::span<const std::uint8_t> bytes,Electromagnetic& out)
{
    Electromagnetic profile;
    Reader r(bytes); std::uint32_t count;
    if(!r.U32(profile.Level) || !r.U32(profile.ArcVfx) || !r.U32(count) || !count || count>16)return false;
    profile.Ranks.resize(count);
    for(auto& rank:profile.Ranks)
        if(!r.F32(rank.SlowFraction) || !r.F32(rank.Duration) || !r.F32(rank.ShieldBonus))return false;
    if(!r.Done() || EncodeElectromagnetic(profile).empty())return false;
    out=std::move(profile); return true;
}
ElectromagneticRank GetElectromagnetic(const Set& effects,std::uint32_t* arc)
{
    if(arc)*arc=0;
    for(const auto& def:effects)if(def.Handler=="ground/electromagnetic")
    {
        Electromagnetic profile;
        if(DecodeElectromagnetic(def.Parameters,profile) && profile.Level)
        {
            if(arc)*arc=profile.ArcVfx;
            return profile.Ranks[profile.Level-1];
        }
    }
    return {};
}

namespace
{
struct SlowEntry { Id Player; Tick Expires; float Fraction; };
bool ReadSlows(std::span<const std::uint8_t> bytes,std::vector<SlowEntry>& entries)
{
    Reader r(bytes); std::uint32_t count;
    if(!r.U32(count) || !count || count>256)return false;
    entries.resize(count);
    for(auto& e:entries)
        if(!r.Identity(e.Player) || !e.Player.Valid() || !r.U64(e.Expires) || !e.Expires ||
           !r.F32(e.Fraction) || !(e.Fraction>0 && e.Fraction<1))return false;
    return r.Done();
}
bool ApplySlow(const Set& effects,const TransactionRequest& request,const CommitResult&,
               const std::map<ObjectKey,ObjectRecord>& previous,std::map<ObjectKey,ObjectRecord>& changes)
{
    auto host=changes.find(request.Target);
    if(host==changes.end() || host->second.Vitals.Dead)return true;
    const auto rank=GetElectromagnetic(effects);
    const auto player=previous.at(request.Header.Object).Grant.Controller;
    if(!player.Valid() || !rank.Duration)return false;
    auto& record=host->second;
    const auto now=record.LogicalAge;
    std::vector<SlowEntry> entries;
    if(auto it=record.Capabilities.find(SlowCapability);it!=record.Capabilities.end() && !ReadSlows(it->second.Data,entries))return false;
    std::erase_if(entries,[&](const SlowEntry& e){return e.Expires<=now || e.Player==player;});
    const Tick duration=std::max<Tick>(1,Tick(std::llround(rank.Duration*TicksPerSecond)));
    if(now>UINT64_MAX-duration || entries.size()>=256)return false;
    entries.push_back({player,now+duration,rank.SlowFraction});
    Writer w; w.U32(std::uint32_t(entries.size()));
    for(const auto& e:entries){w.Identity(e.Player);w.U64(e.Expires);w.F32(e.Fraction);}
    record.Capabilities[SlowCapability]={1,w.Take()};
    return true;
}
const bool SlowRegistered=[]
{
    CapabilityContract contract;
    contract.Name=SlowCapability; contract.Version=1;
    contract.ValidateState=[](const ObjectRecord&,const CapabilityState& state)
    {std::vector<SlowEntry> entries;return state.Version==1 && ReadSlows(state.Data,entries);};
    contract.ValidateRule=[](const Rule&){return false;};
    contract.Prepare=[](const CapabilityContext&,CapabilityDelta&){return CommitStatus::InvalidRule;};
    contract.ValidateTransition=[](const ObjectRecord&,const ObjectRecord&){return false;};
    return RegisterCapability(std::move(contract));
}();
template<class Profile,auto DecodeProfile,auto EncodeProfile>
Handler RankedHandler(const char* name,WeaponAuthoring authoring)
{
    return {name,1,[](const Definition& def){Profile profile;return DecodeProfile(def.Parameters,profile);},
        [](const Definition& def,std::uint32_t level,Set& output)
        {
            Profile profile;
            if(!DecodeProfile(def.Parameters,profile) || !level || level>profile.Ranks.size())return false;
            profile.Level=level;
            auto resolved=def; resolved.Parameters=EncodeProfile(profile);
            output.push_back(std::move(resolved)); return true;
        },{},{},authoring};
}
const bool SplitRegistered=Register(RankedHandler<SplitBarrel,DecodeSplitBarrel,EncodeSplitBarrel>("ground/split_barrel",WeaponData::Split));
const bool RailRegistered=[]
{
    auto handler=RankedHandler<Electromagnetic,DecodeElectromagnetic,EncodeElectromagnetic>("ground/electromagnetic",WeaponData::Rail);
    handler.AcceptedHit=ApplySlow;
    handler.ModifyHit=[](const Definition& d,GroundHitProperties& hit)
    {
        Electromagnetic profile;
        if(!DecodeElectromagnetic(d.Parameters,profile) || !profile.Level)return false;
        hit.Instant=true;
        hit.ArcVfx=profile.ArcVfx;
        hit.ShieldMultiplier+=profile.Ranks[profile.Level-1].ShieldBonus;
        return true;
    };
    return Register(std::move(handler));
}();
}
float MovementMultiplier(std::span<const std::uint8_t> status,Tick now)
{
    std::vector<SlowEntry> entries;
    if(!ReadSlows(status,entries))return 1;
    float strongest=0;
    for(const auto& entry:entries)if(entry.Expires>now)strongest=std::max(strongest,entry.Fraction);
    return 1.f-strongest;
}
Bytes EncodeBarrage(const Barrage& V)
{
    if(V.Ranks.empty()||V.Ranks.size()>16||V.Level>V.Ranks.size())return {};
    Writer W(4096);W.U32(V.Level);W.U32(std::uint32_t(V.Ranks.size()));
    for(const auto& Rank:V.Ranks)
    {
        if(!Rank.IntervalTicks||Rank.IntervalTicks>3600||!Rank.ShotCount||Rank.ShotCount>256||
           !std::isfinite(Rank.DamageBonus)||Rank.DamageBonus<0||Rank.DamageBonus>10||
           !std::isfinite(Rank.ConeHalfAngleDegrees)||Rank.ConeHalfAngleDegrees<0||Rank.ConeHalfAngleDegrees>90)return {};
        W.U32(Rank.IntervalTicks);W.U32(Rank.ShotCount);W.F32(Rank.DamageBonus);W.F32(Rank.ConeHalfAngleDegrees);
    }
    return W.Take();
}
bool DecodeBarrage(std::span<const std::uint8_t> B,Barrage& Out)
{
    Reader R(B);Barrage V;std::uint32_t Count;
    if(!R.U32(V.Level)||!R.U32(Count)||!Count||Count>16)return false;
    V.Ranks.resize(Count);
    for(auto& Rank:V.Ranks)
        if(!R.U32(Rank.IntervalTicks)||!R.U32(Rank.ShotCount)||!R.F32(Rank.DamageBonus)||!R.F32(Rank.ConeHalfAngleDegrees))return false;
    if(!R.Done()||EncodeBarrage(V).empty())return false;
    Out=std::move(V);return true;
}
const Definition* FindBarrage(const Set& S)
{
    for(const auto& D:S)if(D.Handler==BarrageHandler)return &D;
    return nullptr;
}
Bytes EncodeExplosiveEnhancement(const ExplosiveEnhancement& V)
{
    const auto N=V.RadiusBonuses.size();if(!N||N>16||V.DamageBonuses.size()!=N||V.Level>N)return {};
    Writer W;W.U32(std::uint32_t(N));W.U32(V.Level);
    for(std::size_t I=0;I<N;++I)
    {
        const float R=V.RadiusBonuses[I],D=V.DamageBonuses[I];
        if(!std::isfinite(R)||R<=0||R>1||!std::isfinite(D)||D<=0||D>1)return {};
        W.F32(R);W.F32(D);
    }
    return W.Take();
}
bool DecodeExplosiveEnhancement(std::span<const std::uint8_t> B,ExplosiveEnhancement& Out)
{
    Reader R(B);ExplosiveEnhancement V;std::uint32_t N;
    if(!R.U32(N)||!N||N>16||!R.U32(V.Level)||V.Level>N)return false;
    V.RadiusBonuses.resize(N);V.DamageBonuses.resize(N);
    for(std::uint32_t I=0;I<N;++I)if(!R.F32(V.RadiusBonuses[I])||!R.F32(V.DamageBonuses[I]))return false;
    if(!R.Done()||EncodeExplosiveEnhancement(V).empty())return false;Out=std::move(V);return true;
}
ExplosivePackage GetExplosive(const Set& S)
{
    ExplosivePackage V;double Radius=1,Damage=1;
    for(const auto& D:S)
    {
        if(D.Handler=="ground/explosive_package"&&!DecodeExplosive(D.Parameters,V))return {};
        if(D.Handler=="ground/explosive_enhancement")
        {
            ExplosiveEnhancement E;if(!DecodeExplosiveEnhancement(D.Parameters,E))return {};
            for(std::uint32_t I=0;I<E.Level;++I){Radius*=1.0+E.RadiusBonuses[I];Damage*=1.0+E.DamageBonuses[I];}
        }
    }
    // Apply after collecting both effects: catalogue order cannot replace a modifier.
    // Keep the package level intact; only upgrading that level resets its shot counter.
    V.Radius=float(V.Radius*Radius);V.DamageMultiplier=float(V.DamageMultiplier*Damage);return V;
}
namespace
{
const bool RateRegistered=Register({"ground/fire_rate",1,[](const Definition& D){float V;return DecodeFireRate(D.Parameters,V);},
    [](const Definition& D,std::uint32_t N,Set& S){float V;if(!N||!DecodeFireRate(D.Parameters,V))return false;auto Copy=D;Copy.Parameters=EncodeFireRate(float(std::pow(double(V),N)));if(Copy.Parameters.empty())return false;S.push_back(std::move(Copy));return true;},{},{},WeaponData::FireRate});
const bool EnhancedRegistered=Register({"ground/enhanced_bullet",1,[](const Definition& D){EnhancedBullet B;return D.Parameters.empty() || DecodeEnhancedBullet(D.Parameters,B);},
    [](const Definition& D,std::uint32_t N,Set& S)
    {
        // The original single-rank catalogue and already airborne shots use an empty payload.
        if(D.Parameters.empty()){if(N!=1)return false;S.push_back(D);return true;}
        EnhancedBullet B;if(!N || !DecodeEnhancedBullet(D.Parameters,B) || N>B.Ranks.size())return false;
        B.Level=N;auto Resolved=D;Resolved.Parameters=EncodeEnhancedBullet(B);S.push_back(std::move(Resolved));return true;
    },{},[](const Definition& D,std::uint32_t& Flight,std::uint32_t&,std::uint32_t&)
    {
        if(D.Parameters.empty())return true;
        EnhancedBullet B;if(!DecodeEnhancedBullet(D.Parameters,B) || !B.Level)return false;
        Flight=B.Ranks[B.Level-1].Vfx;return true;
    },WeaponData::Enhanced});
const bool ExplosiveRegistered=Register({"ground/explosive_package",2,[](const Definition& D){ExplosivePackage V;return DecodeExplosive(D.Parameters,V);},
    [](const Definition& D,std::uint32_t N,Set& S){ExplosivePackage V;if(!N||!DecodeExplosive(D.Parameters,V)||N>V.Intervals.size())return false;V.Level=N;auto Copy=D;Copy.Parameters=EncodeExplosive(V);S.push_back(std::move(Copy));return true;},{},{},WeaponData::Explosive});
const bool AIEngineRegistered=Register({"ground/ai_engine",1,[](const Definition& D){AIEngine Engine;return DecodeAIEngine(D.Parameters,Engine);},
    [](const Definition& D,std::uint32_t Level,Set& Effects)
    {
        AIEngine Engine;
        if(!Level || !DecodeAIEngine(D.Parameters,Engine) || Level>Engine.Ranks.size())return false;
        Engine.Level=Level;
        auto Resolved=D; Resolved.Parameters=EncodeAIEngine(Engine); Effects.push_back(std::move(Resolved)); return true;
    },{},{},WeaponData::AI});
const bool BarrageRegistered=Register({BarrageHandler,1,[](const Definition& D){Barrage V;return DecodeBarrage(D.Parameters,V);},
    [](const Definition& D,std::uint32_t N,Set& S){
        Barrage V;if(!N||!DecodeBarrage(D.Parameters,V)||N>V.Ranks.size()||FindBarrage(S))return false;
        V.Level=N;auto Copy=D;Copy.Parameters=EncodeBarrage(V);S.push_back(std::move(Copy));return true;
    },{},{},WeaponData::Salvo});
const bool ExplosiveEnhancementRegistered=Register({"ground/explosive_enhancement",1,[](const Definition& D){ExplosiveEnhancement V;return DecodeExplosiveEnhancement(D.Parameters,V);},
    [](const Definition& D,std::uint32_t N,Set& S){ExplosiveEnhancement V;if(!N||!DecodeExplosiveEnhancement(D.Parameters,V)||N>V.RadiusBonuses.size())return false;V.Level=N;auto Copy=D;Copy.Parameters=EncodeExplosiveEnhancement(V);S.push_back(std::move(Copy));return true;},{},{},WeaponData::BlastBonus});
const bool Registered=Register({"ricochet",1,[](const Definition& D){Ricochet V;return DecodeRicochet(D.Parameters,V);},
    [](const Definition& D,std::uint32_t N,Set& S)
    {
        Ricochet E;if(!N||!DecodeRicochet(D.Parameters,E))return false;
        const auto Extra=std::uint64_t(N-1)*E.PerStack;if(Extra+E.Maximum>MaximumRicochetHops)return false;
        E.Minimum+=std::uint32_t(Extra);E.Maximum+=std::uint32_t(Extra);
        auto I=std::find_if(S.begin(),S.end(),[](const auto& V){return V.Handler=="ricochet";});
        if(I!=S.end())
        {Ricochet Old;if(!DecodeRicochet(I->Parameters,Old)||Old.Range!=E.Range||Old.Retention!=E.Retention||std::uint64_t(Old.Maximum)+E.Maximum>MaximumRicochetHops)return false;E.Minimum+=Old.Minimum;E.Maximum+=Old.Maximum;I->Parameters=EncodeRicochet(E);}
        else {auto V=D;V.Parameters=EncodeRicochet(E);S.push_back(std::move(V));}return true;
    }});
}
}
