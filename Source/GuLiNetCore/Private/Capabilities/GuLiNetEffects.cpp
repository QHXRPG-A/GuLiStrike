#include "GuLiNetEffects.h"
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
Bytes EncodeFireRate(float Multiplier)
{if(!std::isfinite(Multiplier)||Multiplier<=1||Multiplier>MaximumGroundFireRate)return {};Writer W;W.F32(Multiplier);return W.Take();}
bool DecodeFireRate(std::span<const std::uint8_t> B,float& Out)
{Reader R(B);float V;if(!R.F32(V)||!R.Done()||EncodeFireRate(V).empty())return false;Out=V;return true;}
float FireRateMultiplier(const Set& S)
{float Value=1;for(const auto& D:S)if(D.Handler=="ground/fire_rate"){float V;if(!DecodeFireRate(D.Parameters,V))return 0;Value*=V;}return Value;}
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
    [](const Definition& D,std::uint32_t N,Set& S){float V;if(!N||!DecodeFireRate(D.Parameters,V))return false;auto Copy=D;Copy.Parameters=EncodeFireRate(float(std::pow(double(V),N)));if(Copy.Parameters.empty())return false;S.push_back(std::move(Copy));return true;}});
const bool EnhancedRegistered=Register({"ground/enhanced_bullet",1,[](const Definition& D){return D.Parameters.empty();},
    [](const Definition& D,std::uint32_t N,Set& S){if(N!=1)return false;S.push_back(D);return true;}});
const bool ExplosiveRegistered=Register({"ground/explosive_package",2,[](const Definition& D){ExplosivePackage V;return DecodeExplosive(D.Parameters,V);},
    [](const Definition& D,std::uint32_t N,Set& S){ExplosivePackage V;if(!N||!DecodeExplosive(D.Parameters,V)||N>V.Intervals.size())return false;V.Level=N;auto Copy=D;Copy.Parameters=EncodeExplosive(V);S.push_back(std::move(Copy));return true;}});
const bool ExplosiveEnhancementRegistered=Register({"ground/explosive_enhancement",1,[](const Definition& D){ExplosiveEnhancement V;return DecodeExplosiveEnhancement(D.Parameters,V);},
    [](const Definition& D,std::uint32_t N,Set& S){ExplosiveEnhancement V;if(!N||!DecodeExplosiveEnhancement(D.Parameters,V)||N>V.RadiusBonuses.size())return false;V.Level=N;auto Copy=D;Copy.Parameters=EncodeExplosiveEnhancement(V);S.push_back(std::move(Copy));return true;}});
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
