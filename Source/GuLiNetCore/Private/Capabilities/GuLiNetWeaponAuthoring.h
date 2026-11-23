#pragma once

#include "GuLiNetEffects.h"
#include <cmath>
#include <limits>
#include <type_traits>

namespace GuLiNet::Effects::WeaponData
{
template<class T>
bool Number(double value,T& out)
{
    if(!std::isfinite(value) || value<std::numeric_limits<T>::lowest() || value>std::numeric_limits<T>::max())return false;
    if constexpr(std::is_integral_v<T>)if(std::trunc(value)!=value)return false;
    out=static_cast<T>(value);
    return true;
}

template<class T>
bool Scalar(const ReadWeaponColumn& read,const char* name,T& out)
{
    std::vector<double> values;
    return read(name,values) && values.size()==1 && Number(values.front(),out);
}

template<class T>
bool Column(const ReadWeaponColumn& read,const char* name,std::vector<T>& out)
{
    std::vector<double> values;
    if(!read(name,values) || values.empty())return false;
    out.resize(values.size());
    for(std::size_t i=0;i<values.size();++i)if(!Number(values[i],out[i]))return false;
    return true;
}

template<class Rank,class T>
bool Ranks(const ReadWeaponColumn& read,const char* name,std::vector<Rank>& ranks,T Rank::*member)
{
    std::vector<double> values;
    if(!read(name,values) || values.empty() || values.size()>16 || (!ranks.empty() && ranks.size()!=values.size()))return false;
    ranks.resize(values.size());
    for(std::size_t i=0;i<ranks.size();++i)if(!Number(values[i],ranks[i].*member))return false;
    return true;
}

inline const WeaponAuthoring FireRate{
    [](const ReadWeaponColumn& read){float bonus;return Scalar(read,"FireRateBonusPercent",bonus)?EncodeFireRate(1.f+bonus):Bytes{};},
    [](const Definition& def,std::uint32_t,std::vector<double>& args)
    {
        float multiplier;
        if(!DecodeFireRate(def.Parameters,multiplier))return false;
        args.push_back(std::round((multiplier-1.f)*10000.f)/100.f);
        return true;
    }
};

inline const WeaponAuthoring Enhanced{
    [](const ReadWeaponColumn& read)
    {
        EnhancedBullet profile;
        if(!Ranks(read,"BulletDamageMultipliers",profile.Ranks,&BulletRank::DamageMultiplier)
            || !Ranks(read,"BulletRateMultipliers",profile.Ranks,&BulletRank::RateMultiplier)
            || !Ranks(read,"BulletFlightVfxIds",profile.Ranks,&BulletRank::Vfx))return Bytes{};
        return EncodeEnhancedBullet(profile);
    },
    [](const Definition& def,std::uint32_t level,std::vector<double>& args)
    {
        EnhancedBullet profile;
        if(!DecodeEnhancedBullet(def.Parameters,profile) || !level || level>profile.Ranks.size())return false;
        const auto& rank=profile.Ranks[level-1];
        args={std::floor((rank.DamageMultiplier-1.f)*100.f+0.5f),std::floor((1.f-rank.RateMultiplier)*100.f+0.5f)};
        return true;
    }
};

inline const WeaponAuthoring AI{
    [](const ReadWeaponColumn& read)
    {
        AIEngine profile;
        float aperture,hex;
        if(!Ranks(read,"LockSeconds",profile.Ranks,&AILockRank::Seconds) || !Ranks(read,"LockRangeCentimeters",profile.Ranks,&AILockRank::RangeCm)
            || !Scalar(read,"LockAperturePixels",aperture) || !Scalar(read,"LockHexPixels",hex) || hex<=0 || aperture<hex)return Bytes{};
        for(auto& rank:profile.Ranks){rank.AperturePixels=aperture;rank.HexPixels=hex;}
        return EncodeAIEngine(profile);
    },
    [](const Definition& def,std::uint32_t level,std::vector<double>& args)
    {
        AIEngine profile;
        if(!DecodeAIEngine(def.Parameters,profile) || !level || level>profile.Ranks.size())return false;
        const auto& rank=profile.Ranks[level-1];
        args={rank.Seconds,rank.RangeCm/100.f};
        return true;
    }
};

inline const WeaponAuthoring Split{
    [](const ReadWeaponColumn& read){SplitBarrel profile;return Column(read,"LockTargetCounts",profile.Ranks)?EncodeSplitBarrel(profile):Bytes{};},
    [](const Definition& def,std::uint32_t level,std::vector<double>& args)
    {
        SplitBarrel profile;
        if(!DecodeSplitBarrel(def.Parameters,profile) || !level || level>profile.Ranks.size())return false;
        args={double(profile.Ranks[level-1])};
        return true;
    }
};

inline const WeaponAuthoring Rail{
    [](const ReadWeaponColumn& read)
    {
        Electromagnetic profile;
        if(!Scalar(read,"ArcVfxId",profile.ArcVfx) || !Ranks(read,"SlowFractions",profile.Ranks,&ElectromagneticRank::SlowFraction)
            || !Ranks(read,"SlowDurations",profile.Ranks,&ElectromagneticRank::Duration)
            || !Ranks(read,"ShieldDamageBonuses",profile.Ranks,&ElectromagneticRank::ShieldBonus))return Bytes{};
        return EncodeElectromagnetic(profile);
    },
    [](const Definition& def,std::uint32_t level,std::vector<double>& args)
    {
        Electromagnetic profile;
        if(!DecodeElectromagnetic(def.Parameters,profile) || !level || level>profile.Ranks.size())return false;
        const auto& rank=profile.Ranks[level-1];
        args={rank.SlowFraction*100.f,rank.Duration,rank.ShieldBonus*100.f};
        return true;
    }
};

inline const WeaponAuthoring Salvo{
    [](const ReadWeaponColumn& read)
    {
        Barrage profile;
        std::vector<float> intervals;
        if(!Column(read,"BarrageIntervalsSeconds",intervals) || !Ranks(read,"BarrageShotCounts",profile.Ranks,&BarrageRank::ShotCount)
            || !Ranks(read,"BarrageDamageBonuses",profile.Ranks,&BarrageRank::DamageBonus)
            || !Ranks(read,"BarrageConeHalfAnglesDegrees",profile.Ranks,&BarrageRank::ConeHalfAngleDegrees)
            || intervals.size()!=profile.Ranks.size())return Bytes{};
        for(std::size_t i=0;i<intervals.size();++i)
        {
            const float ticks=intervals[i]*TicksPerSecond;
            if(ticks<1 || ticks>3600 || std::abs(ticks-std::round(ticks))>1.e-8f)return Bytes{};
            profile.Ranks[i].IntervalTicks=std::uint32_t(std::round(ticks));
        }
        return EncodeBarrage(profile);
    },
    [](const Definition& def,std::uint32_t level,std::vector<double>& args)
    {
        Barrage profile;
        if(!DecodeBarrage(def.Parameters,profile) || !level || level>profile.Ranks.size())return false;
        const auto& rank=profile.Ranks[level-1];
        args={double(rank.IntervalTicks)/TicksPerSecond,double(rank.ShotCount),rank.DamageBonus*100.f,rank.ConeHalfAngleDegrees};
        return true;
    }
};

inline const WeaponAuthoring Explosive{
    [](const ReadWeaponColumn& read)
    {
        ExplosivePackage profile;
        if(!Column(read,"ExplosiveIntervals",profile.Intervals) || !Scalar(read,"ExplosiveDamageMultiplier",profile.DamageMultiplier)
            || !Scalar(read,"ExplosiveRadiusCentimeters",profile.Radius) || !Scalar(read,"ExplosiveBulletVfxId",profile.BulletVfx)
            || !Scalar(read,"ExplosiveImpactVfxId",profile.ImpactVfx) || !Scalar(read,"ExplosiveSpeedMultiplier",profile.SpeedMultiplier)
            || !Scalar(read,"ExplosiveOutwardOffsetCentimeters",profile.OutwardOffset))return Bytes{};
        return EncodeExplosive(profile);
    },
    [](const Definition& def,std::uint32_t level,std::vector<double>& args)
    {
        ExplosivePackage profile;
        if(!DecodeExplosive(def.Parameters,profile) || !level || level>profile.Intervals.size())return false;
        args={double(profile.Intervals[level-1]),profile.DamageMultiplier,profile.Radius/100.f};
        return true;
    }
};

inline const WeaponAuthoring BlastBonus{
    [](const ReadWeaponColumn& read)
    {
        ExplosiveEnhancement profile;
        if(!Column(read,"ExplosiveRadiusBonuses",profile.RadiusBonuses) || !Column(read,"ExplosiveDamageBonuses",profile.DamageBonuses))return Bytes{};
        return EncodeExplosiveEnhancement(profile);
    },
    [](const Definition& def,std::uint32_t level,std::vector<double>& args)
    {
        ExplosiveEnhancement profile;
        if(!DecodeExplosiveEnhancement(def.Parameters,profile) || !level || level>profile.RadiusBonuses.size())return false;
        args={profile.RadiusBonuses[level-1]*100.f,profile.DamageBonuses[level-1]*100.f};
        return true;
    }
};
}
