#include "Gameplay/GroundMech/Rogue/GuLiGroundRogueRuntime.h"
#include "Gameplay/Cards/GuLiRogueCardSettings.h"
#include "Gameplay/Buffs/GuLiBuffDefinition.h"
#include "Engine/DataTable.h"

namespace GuLiGroundRogue
{
namespace
{
template<class T> bool Row(const TSoftObjectPtr<UDataTable>& Path,int32 Id,T& Out)
{const auto* TBL=Path.LoadSynchronous();if(!TBL||TBL->GetRowStruct()!=T::StaticStruct())return false;for(const auto& [_,V]:TBL->GetRowMap()){const auto& R=*reinterpret_cast<const T*>(V);if(R.Id==Id){Out=R;return true;}}return false;}
using FBuild=bool(*)(const FGuLiStrikeRogueCardsEffectsRow&,GuLiNet::Effects::Definition&,FString&);
bool Ricochet(const FGuLiStrikeRogueCardsEffectsRow& R,GuLiNet::Effects::Definition& D,FString& Error)
{FGuLiStrikeRogueCardsGroundRicochetRow V;if(!Row(GetDefault<UGuLiRogueCardSettings>()->GroundRicochet,R.ConfigId,V)){Error=TEXT("Missing ricochet configuration");return false;}D.Parameters=GuLiNet::Effects::EncodeRicochet({uint32(V.MinimumHops),uint32(V.MaximumHops),uint32(V.HopsPerStack),V.RangeCentimeters,V.DamageRetention});return !D.Parameters.empty();}
bool OnHit(const FGuLiStrikeRogueCardsEffectsRow& R,GuLiNet::Effects::Definition& D,FString& Error)
{GuLiNet::Buffs::Definition B;if(!UGuLiBuffHandler::Load(R.BuffId,B,Error))return false;D.Parameters=GuLiNet::Buffs::EncodeDefinition(B);return !D.Parameters.empty();}
bool RicochetPreview(const GuLiNet::Effects::Definition& D,uint32 N,FFormatOrderedArguments& Args,const FNumberFormattingOptions& Format)
{GuLiNet::Effects::Set S;const auto* H=GuLiNet::Effects::Find(D.Handler);if(!H||!H->Accumulate(D,N,S))return false;Args.Add(FText::AsNumber(N,&Format));return true;}
bool BuffPreview(const GuLiNet::Effects::Definition& D,uint32 N,FFormatOrderedArguments& Args,const FNumberFormattingOptions& Format)
{GuLiNet::Buffs::Definition B;if(!GuLiNet::Buffs::DecodeDefinition(D.Parameters,B))return false;Args.Add(FText::AsNumber(N,&Format));return true;}
bool WeaponEffect(const FGuLiStrikeRogueCardsEffectsRow& R,GuLiNet::Effects::Definition& D,FString& Error)
{
    FGuLiStrikeRogueCardsGroundWeaponEffectsRow V;
    if(!Row(GetDefault<UGuLiRogueCardSettings>()->GroundWeaponEffects,R.ConfigId,V)){Error=TEXT("Missing Ground weapon effect configuration");return false;}
    if(R.Handler==TEXT("ground/fire_rate"))D.Parameters=GuLiNet::Effects::EncodeFireRate(1.f+V.FireRateBonusPercent);
    else if(R.Handler==TEXT("ground/enhanced_bullet"))
    {
        GuLiNet::Effects::EnhancedBullet Bullet;
        if(V.BulletDamageMultipliers.Num()!=V.BulletRateMultipliers.Num() || V.BulletFlightVfxIds.Num()!=V.BulletRateMultipliers.Num())return false;
        for(int32 I=0;I<V.BulletFlightVfxIds.Num();++I)
        {
            GuLiNet::Effects::BulletRank Rank;
            if(!LexTryParseString(Rank.DamageMultiplier,*V.BulletDamageMultipliers[I]) || !LexTryParseString(Rank.RateMultiplier,*V.BulletRateMultipliers[I])
                || !LexTryParseString(Rank.Vfx,*V.BulletFlightVfxIds[I]))return false;
            Bullet.Ranks.push_back(Rank);
        }
        D.Parameters=GuLiNet::Effects::EncodeEnhancedBullet(Bullet);
        if(D.Parameters.empty())return false;
    }
    else if(R.Handler==TEXT("ground/ai_engine"))
    {
        GuLiNet::Effects::AIEngine Engine;
        if(V.LockSeconds.Num()!=V.LockRangeCentimeters.Num())return false;
        for(int32 I=0;I<V.LockSeconds.Num();++I)
        {
            GuLiNet::Effects::AILockRank Rank;
            if(!LexTryParseString(Rank.Seconds,*V.LockSeconds[I]) || !LexTryParseString(Rank.RangeCm,*V.LockRangeCentimeters[I]))return false;
            Engine.Ranks.push_back(Rank);
        }
        D.Parameters=GuLiNet::Effects::EncodeAIEngine(Engine);
    }
    else if(R.Handler==TEXT("ground/no_survivors"))
    {
        GuLiNet::Effects::Barrage Profile;
        const int32 Count=V.BarrageShotCounts.Num();
        if(V.BarrageIntervalsSeconds.Num()!=Count || V.BarrageDamageBonuses.Num()!=Count || V.BarrageConeHalfAnglesDegrees.Num()!=Count)return false;
        for(int32 I=0;I<Count;++I)
        {
            float Seconds,Bonus,HalfAngle;int32 Shots;
            if(!LexTryParseString(Seconds,*V.BarrageIntervalsSeconds[I]) || !LexTryParseString(Shots,*V.BarrageShotCounts[I]) ||
               !LexTryParseString(Bonus,*V.BarrageDamageBonuses[I]) || !LexTryParseString(HalfAngle,*V.BarrageConeHalfAnglesDegrees[I]))return false;
            const float Ticks=Seconds*60.f;
            if(!FMath::IsFinite(Ticks)||Ticks<1||Ticks>3600||!FMath::IsNearlyEqual(Ticks,FMath::RoundToFloat(Ticks)))return false;
            Profile.Ranks.push_back({uint32(FMath::RoundToInt(Ticks)),uint32(Shots),Bonus,HalfAngle});
        }
        D.Parameters=GuLiNet::Effects::EncodeBarrage(Profile);
    }
    else if(R.Handler==TEXT("ground/explosive_enhancement"))
    {
        GuLiNet::Effects::ExplosiveEnhancement E;
        for(const auto& I:V.ExplosiveRadiusBonuses)E.RadiusBonuses.push_back(FCString::Atof(*I));
        for(const auto& I:V.ExplosiveDamageBonuses)E.DamageBonuses.push_back(FCString::Atof(*I));
        D.Parameters=GuLiNet::Effects::EncodeExplosiveEnhancement(E);
    }
    else
    {
        GuLiNet::Effects::ExplosivePackage E;E.DamageMultiplier=V.ExplosiveDamageMultiplier;E.Radius=V.ExplosiveRadiusCentimeters;
        E.BulletVfx=uint32(V.ExplosiveBulletVfxId);E.ImpactVfx=uint32(V.ExplosiveImpactVfxId);
        E.SpeedMultiplier=V.ExplosiveSpeedMultiplier;E.OutwardOffset=V.ExplosiveOutwardOffsetCentimeters;
        for(const auto& I:V.ExplosiveIntervals)E.Intervals.push_back(uint32(FCString::Atoi(*I)));
        D.Parameters=GuLiNet::Effects::EncodeExplosive(E);
    }
    return GuLiNet::Effects::Validate(D);
}
bool WeaponPreview(const GuLiNet::Effects::Definition& D,uint32 N,FFormatOrderedArguments& Args,const FNumberFormattingOptions& Format)
{
    Args.Add(FText::AsNumber(N,&Format));
    if(D.Handler=="ground/enhanced_bullet")
    {
        GuLiNet::Effects::EnhancedBullet Bullet;
        if(!N || !GuLiNet::Effects::DecodeEnhancedBullet(D.Parameters,Bullet) || N>Bullet.Ranks.size())return false;
        const auto& Rank=Bullet.Ranks[N-1];
        Args.Add(FText::AsNumber(FMath::RoundToInt((Rank.DamageMultiplier-1.f)*100),&Format));
        Args.Add(FText::AsNumber(FMath::RoundToInt((1.f-Rank.RateMultiplier)*100),&Format));
        return true;
    }
    if(D.Handler=="ground/ai_engine")
    {
        GuLiNet::Effects::AIEngine Engine;
        if(!N || !GuLiNet::Effects::DecodeAIEngine(D.Parameters,Engine) || N>Engine.Ranks.size())return false;
        const auto& Rank=Engine.Ranks[N-1];
        Args.Add(FText::AsNumber(Rank.Seconds,&Format)); Args.Add(FText::AsNumber(Rank.RangeCm/100.f,&Format));
        return true;
    }
    if(D.Handler=="ground/no_survivors")
    {
        GuLiNet::Effects::Barrage Profile;
        if(!GuLiNet::Effects::DecodeBarrage(D.Parameters,Profile)||!N||N>Profile.Ranks.size())return false;
        const auto& Rank=Profile.Ranks[N-1];
        Args.Add(FText::AsNumber(Rank.IntervalTicks/60.,&Format));Args.Add(FText::AsNumber(Rank.ShotCount,&Format));
        Args.Add(FText::AsNumber(Rank.DamageBonus*100.f,&Format));Args.Add(FText::AsNumber(Rank.ConeHalfAngleDegrees,&Format));
        return true;
    }
    if(D.Handler=="ground/fire_rate")
    {float Rate;if(!GuLiNet::Effects::DecodeFireRate(D.Parameters,Rate))return false;Args.Add(FText::AsNumber(FMath::RoundToFloat((Rate-1.f)*10000.f)/100.f,&Format));}
    else if(D.Handler=="ground/explosive_package")
    {
        GuLiNet::Effects::ExplosivePackage E;if(!GuLiNet::Effects::DecodeExplosive(D.Parameters,E)||!N||N>E.Intervals.size())return false;
        Args.Add(FText::AsNumber(E.Intervals[N-1],&Format));Args.Add(FText::AsNumber(E.DamageMultiplier,&Format));Args.Add(FText::AsNumber(E.Radius/100.f,&Format));
    }
    else if(D.Handler=="ground/explosive_enhancement")
    {
        GuLiNet::Effects::ExplosiveEnhancement E;
        if(!GuLiNet::Effects::DecodeExplosiveEnhancement(D.Parameters,E)||!N||N>E.RadiusBonuses.size())return false;
        Args.Add(FText::AsNumber(E.RadiusBonuses[N-1]*100.f,&Format));Args.Add(FText::AsNumber(E.DamageBonuses[N-1]*100.f,&Format));
    }
    return true;
}
struct FHandler
{
    FBuild Build;
    bool(*Preview)(const GuLiNet::Effects::Definition&,uint32,FFormatOrderedArguments&,const FNumberFormattingOptions&);
};
const TMap<FName,FHandler> Handlers={{TEXT("ricochet"),{Ricochet,RicochetPreview}},{TEXT("buff/on_hit"),{OnHit,BuffPreview}},
    {TEXT("ground/fire_rate"),{WeaponEffect,WeaponPreview}},{TEXT("ground/enhanced_bullet"),{WeaponEffect,WeaponPreview}},{TEXT("ground/explosive_package"),{WeaponEffect,WeaponPreview}},
    {TEXT("ground/explosive_enhancement"),{WeaponEffect,WeaponPreview}},{TEXT("ground/no_survivors"),{WeaponEffect,WeaponPreview}},
    {TEXT("ground/ai_engine"),{WeaponEffect,WeaponPreview}}};
}
bool LoadEffect(int32 Id,GuLiNet::Effects::Definition& D,FString& Error)
{
    FGuLiStrikeRogueCardsEffectsRow R;if(!Row(GetDefault<UGuLiRogueCardSettings>()->Effects,Id,R)){Error=TEXT("Missing effect ID ")+FString::FromInt(Id);return false;}
    const auto* Handler=Handlers.Find(FName(*R.Handler));if(!Handler){Error=TEXT("Unregistered effect builder: ")+R.Handler;return false;}
    D={};D.Id=uint32(R.Id);D.Handler=TCHAR_TO_UTF8(*R.Handler);D.VersionValue=uint32(R.Version);return Handler->Build(R,D,Error)&&GuLiNet::Effects::Validate(D);
}
bool LoadCatalog(GuLiNet::GroundRogue::Catalog& C,FString& Error,int32* BarrageWindup)
{
    C={};const auto* T=GetDefault<UGuLiRogueCardSettings>()->Cards.LoadSynchronous();if(!T||T->GetRowStruct()!=FGuLiStrikeRogueCardsCardsRow::StaticStruct())return false;
    for(const auto& [_,V]:T->GetRowMap())
    {
        const auto& R=*reinterpret_cast<const FGuLiStrikeRogueCardsCardsRow*>(V);if(R.Type!=3)continue;GuLiNet::GroundRogue::Card D;D.Id=TCHAR_TO_UTF8(*R.Id);D.MaximumAcquisitions=uint32(R.MaxAcquisitions);
        if (BarrageWindup && R.Id == BarrageCardId) *BarrageWindup = BarrageWindupTicks(R);
        for(const auto& Ref:R.EffectIds){int32 Id=FCString::Atoi(*Ref);GuLiNet::Effects::Definition E;if(!LoadEffect(Id,E,Error))return false;D.EffectIds.push_back(uint32(Id));C.Effects[uint32(Id)]=std::move(E);}
        for(const auto& Ref:R.RequiredCardLevels){int32 Min;if(!LexTryParseString(Min,*Ref)||Min<1)return false;D.RequiredLevels.push_back(uint32(Min));}
        for(const auto& Ref:R.RequiredCardIds)D.Required.push_back(TCHAR_TO_UTF8(*Ref));for(const auto& Ref:R.ExcludedCardIds)D.Excluded.push_back(TCHAR_TO_UTF8(*Ref));C.Cards.emplace(D.Id,std::move(D));
    }
    GuLiNet::CompiledUpgradeCatalog G;std::string Why;if(!GuLiNet::GroundRogue::Compile(C,G,Why)){Error=UTF8_TO_TCHAR(Why.c_str());return false;}return true;
}
bool LoadBarrage(GuLiNet::Effects::Barrage& Out,FString& Error)
{
    const UDataTable* Table=GetDefault<UGuLiRogueCardSettings>()->Cards.LoadSynchronous();
    if(!Table||Table->GetRowStruct()!=FGuLiStrikeRogueCardsCardsRow::StaticStruct())return false;
    for(const auto& Pair:Table->GetRowMap())
    {
        const auto& Card=*reinterpret_cast<const FGuLiStrikeRogueCardsCardsRow*>(Pair.Value);
        if(Card.Id!=BarrageCardId)continue;
        for(const auto& Ref:Card.EffectIds)
        {
            GuLiNet::Effects::Definition D;
            if(!LoadEffect(FCString::Atoi(*Ref),D,Error))return false;
            if(D.Handler=="ground/no_survivors")return GuLiNet::Effects::DecodeBarrage(D.Parameters,Out);
        }
    }
    Error=TEXT("Missing Ground barrage card or effect");return false;
}
FText Preview(const FGuLiStrikeRogueCardsCardsRow& R,const FText& Pattern,int32 N)
{
    FFormatOrderedArguments Args;FNumberFormattingOptions Format;Format.SetUseGrouping(false);Format.SetMaximumFractionalDigits(2);
    for(const auto& Ref:R.EffectIds)
    {
        GuLiNet::Effects::Definition D;FString Error;if(!LoadEffect(FCString::Atoi(*Ref),D,Error))return FText::GetEmpty();
        const auto* H=Handlers.Find(FName(UTF8_TO_TCHAR(D.Handler.c_str())));if(!H||!H->Preview(D,uint32(FMath::Max(1,N)),Args,Format))return FText::GetEmpty();
    }
    return FText::Format(Pattern,Args);
}
}
