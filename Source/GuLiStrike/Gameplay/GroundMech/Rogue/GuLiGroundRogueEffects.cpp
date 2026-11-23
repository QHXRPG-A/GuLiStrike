#include "Gameplay/GroundMech/Rogue/GuLiGroundRogueRuntime.h"
#include "Gameplay/Cards/GuLiRogueCardSettings.h"
#include "Gameplay/Buffs/GuLiBuffDefinition.h"
#include "Engine/DataTable.h"
#include "Gameplay/Data/GuLiSpellFieldDataSubsystem.h"
#include "GuLiStrike.h"
#include "UObject/UnrealType.h"
#include <cstdlib>

namespace GuLiGroundRogue
{
namespace
{
template<class T> bool Row(const TSoftObjectPtr<UDataTable>& Path,int32 Id,T& Out)
{const auto* TBL=Path.LoadSynchronous();if(!TBL||TBL->GetRowStruct()!=T::StaticStruct())return false;for(const auto& [_,V]:TBL->GetRowMap()){const auto& R=*reinterpret_cast<const T*>(V);if(R.Id==Id){Out=R;return true;}}return false;}
bool ReadWeaponColumn(const FGuLiStrikeRogueCardsGroundWeaponEffectsRow& row,const char* name,std::vector<double>& values)
{
    const FProperty* property=row.StaticStruct()->FindPropertyByName(FName(UTF8_TO_TCHAR(name)));
    if(!property)return false;
    const void* data=property->ContainerPtrToValuePtr<void>(&row);
    if(const auto* number=CastField<FNumericProperty>(property))
    {
        values.push_back(number->IsFloatingPoint()?number->GetFloatingPointPropertyValue(data):double(number->GetSignedIntPropertyValue(data)));
        return true;
    }
    const auto* array=CastField<FArrayProperty>(property);
    if(!array || !CastField<FStrProperty>(array->Inner))return false;
    FScriptArrayHelper items(array,data);
    for(int32 i=0;i<items.Num();++i)
    {
        const auto& text=*reinterpret_cast<const FString*>(items.GetRawPtr(i));
        const FTCHARToUTF8 number(*text);
        char* end=nullptr;
        const double value=std::strtod(number.Get(),&end);
        if(end==number.Get() || *end!=0 || !FMath::IsFinite(value))return false;
        values.push_back(value);
    }
    return true;
}

bool BuildParameters(const FGuLiStrikeRogueCardsEffectsRow& row,const GuLiNet::Effects::Handler& handler,
    GuLiNet::Effects::Definition& effect,FString& error)
{
    const auto* settings=GetDefault<UGuLiRogueCardSettings>();
    if(handler.Authoring.Build)
    {
        FGuLiStrikeRogueCardsGroundWeaponEffectsRow weapon;
        if(!Row(settings->GroundWeaponEffects,row.ConfigId,weapon))return false;
        effect.Parameters=handler.Authoring.Build([&weapon](const char* name,std::vector<double>& values)
        {return ReadWeaponColumn(weapon,name,values);});
    }
    else if(handler.Name=="ricochet")
    {
        FGuLiStrikeRogueCardsGroundRicochetRow config;
        if(!Row(settings->GroundRicochet,row.ConfigId,config))return false;
        effect.Parameters=GuLiNet::Effects::EncodeRicochet({uint32(config.MinimumHops),uint32(config.MaximumHops),
            uint32(config.HopsPerStack),config.RangeCentimeters,config.DamageRetention});
    }
    else if(handler.Name=="buff/on_hit")
    {
        GuLiNet::Buffs::Definition buff;
        if(!UGuLiBuffHandler::Load(row.BuffId,buff,error))return false;
        effect.Parameters=GuLiNet::Buffs::EncodeDefinition(buff);
    }
    return !effect.Parameters.empty() && GuLiNet::Effects::Validate(effect);
}

bool ReadGroundCard(const FGuLiStrikeRogueCardsCardsRow& row,GuLiNet::GroundRogue::Card& card,
    GuLiNet::GroundRogue::Catalog& catalog,FBarrageCard& barrage,FString& error)
{
    card.Id=TCHAR_TO_UTF8(*row.Id);
    if(card.Id.empty() || row.MaxAcquisitions<0 || row.EffectIds.IsEmpty())return false;
    card.MaximumAcquisitions=uint32(row.MaxAcquisitions);
    for(const auto& ref:row.EffectIds)
    {
        int32 id;
        if(!LexTryParseString(id,*ref) || id<=0)return false;
        auto it=catalog.Effects.find(uint32(id));
        if(it==catalog.Effects.end())
        {
            GuLiNet::Effects::Definition effect;
            if(!LoadEffect(id,effect,error))return false;
            it=catalog.Effects.emplace(uint32(id),std::move(effect)).first;
        }
        const auto& effect=it->second;
        if(effect.Handler==GuLiNet::Effects::BarrageHandler)
        {
            FGuLiSpellFieldConfig field;
            const float seconds=BarrageWindupSeconds(row);
            if(!FMath::IsFinite(seconds) || seconds<=0 || double(seconds)*GuLiNet::TicksPerSecond>MAX_int32
                || BarrageWindupTicks(row)<1 || !barrage.CardId.IsEmpty()
                || !GuLiNet::Effects::DecodeBarrage(effect.Parameters,barrage.Profile)
                || barrage.Profile.Ranks.size()!=card.MaximumAcquisitions
                || !UGuLiSpellFieldDataSubsystem::ResolveAuthoredConfig(BarrageFieldId,field) || !FMath::IsFinite(field.Damage) || field.Damage<=0)
            {error=TEXT("Invalid barrage timing, ranks or field");return false;}
            barrage.CardId=row.Id;
            barrage.WindupTicks=BarrageWindupTicks(row);
        }
        card.EffectIds.push_back(uint32(id));
    }
    for(const auto& ref:row.RequiredCardLevels)
    {
        int32 minimum;
        if(!LexTryParseString(minimum,*ref) || minimum<1)return false;
        card.RequiredLevels.push_back(uint32(minimum));
    }
    for(const auto& ref:row.RequiredCardIds)card.Required.push_back(TCHAR_TO_UTF8(*ref));
    for(const auto& ref:row.ExcludedCardIds)card.Excluded.push_back(TCHAR_TO_UTF8(*ref));
    return true;
}
}

bool LoadEffect(int32 id,GuLiNet::Effects::Definition& effect,FString& error)
{
    FGuLiStrikeRogueCardsEffectsRow row;
    if(!Row(GetDefault<UGuLiRogueCardSettings>()->Effects,id,row)){error=FString::Printf(TEXT("Missing effect %d"),id);return false;}
    effect={};effect.Id=uint32(row.Id);effect.Handler=TCHAR_TO_UTF8(*row.Handler);effect.VersionValue=uint32(row.Version);
    const auto* handler=GuLiNet::Effects::Find(effect.Handler);
    if(handler && BuildParameters(row,*handler,effect,error))return true;
    if(error.IsEmpty())error=FString::Printf(TEXT("Invalid effect %d (%s)"),id,*row.Handler);
    return false;
}

bool LoadCatalog(GuLiNet::GroundRogue::Catalog& catalog,FString& error,FBarrageCard* outBarrage)
{
    catalog={};
    if(outBarrage)*outBarrage={};
    const auto* table=GetDefault<UGuLiRogueCardSettings>()->Cards.LoadSynchronous();
    if(!table || table->GetRowStruct()!=FGuLiStrikeRogueCardsCardsRow::StaticStruct()){error=TEXT("Missing Ground card table");return false;}
    FBarrageCard barrage;
    std::set<std::string> seen, barrageCards;
    for(const auto& pair:table->GetRowMap())
    {
        const auto& row=*reinterpret_cast<const FGuLiStrikeRogueCardsCardsRow*>(pair.Value);
        if(row.Type!=3)continue;
        if(!seen.insert(TCHAR_TO_UTF8(*row.Id)).second){error=TEXT("Duplicate Ground card ")+row.Id;return false;}
        GuLiNet::GroundRogue::Card card;
        FBarrageCard ability;
        FString reason;
        if(!ReadGroundCard(row,card,catalog,ability,reason))
        {
            UE_LOG(LogGuLiStrike,Warning,TEXT("Ground card %s unavailable: %s"),*row.Id,*reason);
            continue;
        }
        if(!ability.CardId.IsEmpty()){barrage=MoveTemp(ability);barrageCards.insert(card.Id);}
        catalog.Cards.emplace(card.Id,std::move(card));
    }
    if(barrageCards.size()>1)
    {
        UE_LOG(LogGuLiStrike,Warning,TEXT("Multiple Ground cards bind the barrage ability; omitting the ambiguous grants"));
        for(const auto& id:barrageCards)catalog.Cards.erase(id);
    }
    // An unavailable prerequisite also disables its descendants. Its exclusions cease to apply.
    bool changed;
    do
    {
        changed=std::erase_if(catalog.Cards,[&](const auto& entry)
        {
            const auto& required=entry.second.Required;
            return std::any_of(required.begin(),required.end(),[&](const auto& id){return !catalog.Cards.contains(id);});
        })!=0;
    }while(changed);
    std::set<uint32> used;
    for(auto& [id,card]:catalog.Cards)
    {
        std::erase_if(card.Excluded,[&](const auto& other){return !catalog.Cards.contains(other);});
        used.insert(card.EffectIds.begin(),card.EffectIds.end());
    }
    std::erase_if(catalog.Effects,[&](const auto& entry){return !used.contains(entry.first);});
    GuLiNet::CompiledUpgradeCatalog graph;
    std::string why;
    if(!GuLiNet::GroundRogue::Compile(catalog,graph,why)){error=UTF8_TO_TCHAR(why.c_str());return false;}
    if(outBarrage && !barrage.CardId.IsEmpty() && catalog.Cards.contains(TCHAR_TO_UTF8(*barrage.CardId)))*outBarrage=MoveTemp(barrage);
    return true;
}

FText Preview(const FGuLiStrikeRogueCardsCardsRow& R,const FText& Pattern,int32 N)
{
    FFormatOrderedArguments Args;FNumberFormattingOptions Format;Format.SetUseGrouping(false);Format.SetMaximumFractionalDigits(2);
    for(const auto& Ref:R.EffectIds)
    {
        GuLiNet::Effects::Definition D;FString Error;if(!LoadEffect(FCString::Atoi(*Ref),D,Error))return FText::GetEmpty();
        const auto* handler=GuLiNet::Effects::Find(D.Handler);
        if(!handler)return FText::GetEmpty();
        const uint32 level=uint32(FMath::Max(1,N));
        Args.Add(FText::AsNumber(level,&Format));
        if(handler->Authoring.Describe)
        {
            std::vector<double> values;
            if(!handler->Authoring.Describe(D,level,values))return FText::GetEmpty();
            for(double value:values)Args.Add(FText::AsNumber(value,&Format));
        }
        else if(D.Handler=="ricochet")
        {
            GuLiNet::Effects::Set resolved;
            if(!handler->Accumulate(D,level,resolved))return FText::GetEmpty();
        }
    }
    return FText::Format(Pattern,Args);
}
}
