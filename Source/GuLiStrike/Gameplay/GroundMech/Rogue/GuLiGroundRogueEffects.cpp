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
    else if(R.Handler==TEXT("ground/enhanced_bullet"))D.Parameters.clear();
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
    {TEXT("ground/explosive_enhancement"),{WeaponEffect,WeaponPreview}}};
}
bool LoadEffect(int32 Id,GuLiNet::Effects::Definition& D,FString& Error)
{
    FGuLiStrikeRogueCardsEffectsRow R;if(!Row(GetDefault<UGuLiRogueCardSettings>()->Effects,Id,R)){Error=TEXT("Missing effect ID ")+FString::FromInt(Id);return false;}
    const auto* Handler=Handlers.Find(FName(*R.Handler));if(!Handler){Error=TEXT("Unregistered effect builder: ")+R.Handler;return false;}
    D={};D.Id=uint32(R.Id);D.Handler=TCHAR_TO_UTF8(*R.Handler);D.VersionValue=uint32(R.Version);return Handler->Build(R,D,Error)&&GuLiNet::Effects::Validate(D);
}
bool LoadCatalog(GuLiNet::GroundRogue::Catalog& C,FString& Error)
{
    C={};const auto* T=GetDefault<UGuLiRogueCardSettings>()->Cards.LoadSynchronous();if(!T||T->GetRowStruct()!=FGuLiStrikeRogueCardsCardsRow::StaticStruct())return false;
    for(const auto& [_,V]:T->GetRowMap())
    {
        const auto& R=*reinterpret_cast<const FGuLiStrikeRogueCardsCardsRow*>(V);if(R.Type!=3)continue;GuLiNet::GroundRogue::Card D;D.Id=TCHAR_TO_UTF8(*R.Id);D.MaximumAcquisitions=uint32(R.MaxAcquisitions);
        for(const auto& Ref:R.EffectIds){int32 Id=FCString::Atoi(*Ref);GuLiNet::Effects::Definition E;if(!LoadEffect(Id,E,Error))return false;D.EffectIds.push_back(uint32(Id));C.Effects[uint32(Id)]=std::move(E);}
        for(const auto& Ref:R.RequiredCardIds)D.Required.push_back(TCHAR_TO_UTF8(*Ref));for(const auto& Ref:R.ExcludedCardIds)D.Excluded.push_back(TCHAR_TO_UTF8(*Ref));C.Cards.emplace(D.Id,std::move(D));
    }
    GuLiNet::CompiledUpgradeCatalog G;std::string Why;if(!GuLiNet::GroundRogue::Compile(C,G,Why)){Error=UTF8_TO_TCHAR(Why.c_str());return false;}return true;
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
