#include "Gameplay/Buffs/GuLiBuffDefinition.h"
#include "Gameplay/Data/Generated/GuLiStrikeBuffsTableRows.h"
#include "Engine/DataTable.h"

namespace
{
template<class T> bool Row(const TSoftObjectPtr<UDataTable>& Path,int32 Id,T& Out)
{const auto* Table=Path.LoadSynchronous();if(!Table||Table->GetRowStruct()!=T::StaticStruct())return false;for(const auto& [_,V]:Table->GetRowMap()){const auto& R=*reinterpret_cast<const T*>(V);if(R.Id==Id){Out=R;return true;}}return false;}
GuLiNet::Tick Ticks(float S){return FMath::IsFinite(S)&&S>0&&S<=3600?uint64(FMath::RoundToInt64(double(S)*GuLiNet::TicksPerSecond)):0;}
}
UGuLiBuffSettings::UGuLiBuffSettings()
{
    Buffs=FSoftObjectPath(TEXT("/Game/GuLiStrike/Data/DT_GuLiStrikeBuffs_Buffs.DT_GuLiStrikeBuffs_Buffs"));
    Burning=FSoftObjectPath(TEXT("/Game/GuLiStrike/Data/DT_GuLiStrikeBuffs_Burning.DT_GuLiStrikeBuffs_Burning"));
    BurningBurst=FSoftObjectPath(TEXT("/Game/GuLiStrike/Data/DT_GuLiStrikeBuffs_BurningBurst.DT_GuLiStrikeBuffs_BurningBurst"));
}
bool UGuLiBuffHandler::Build(const FGuLiStrikeBuffsBuffsRow&,GuLiNet::Buffs::Definition&,FString& Error) const
{Error=TEXT("Buff handler has no definition builder");return false;}
float UGuLiBuffHandler::PreviewSeconds(const GuLiNet::Buffs::Definition& D) const
{return float(D.Duration?D.Duration:D.Interval)/GuLiNet::TicksPerSecond;}
bool UGuLiBuffHandler::Load(int32 Id,GuLiNet::Buffs::Definition& D,FString& Error)
{
    std::vector<GuLiNet::Buffs::Definition> ranks;
    if(!LoadRanks(Id,ranks,Error))return false;
    D=std::move(ranks.front());
    return true;
}
bool UGuLiBuffHandler::LoadRanks(int32 Id,std::vector<GuLiNet::Buffs::Definition>& Out,FString& Error)
{
    FGuLiStrikeBuffsBuffsRow R;if(!Row(GetDefault<UGuLiBuffSettings>()->Buffs,Id,R)){Error=TEXT("Missing Buff ID ")+FString::FromInt(Id);return false;}
    auto* Class=R.ImplementationClass.LoadSynchronous();if(!Class||!Class->IsChildOf(StaticClass())){Error=TEXT("Buff class must derive from UGuLiBuffHandler");return false;}
    if(R.AdditionalRankConfigIds.Num()>=16){Error=TEXT("Too many Buff ranks");return false;}
    const auto* handler=Class->GetDefaultObject<UGuLiBuffHandler>();
    std::vector<GuLiNet::Buffs::Definition> ranks;
    for(int32 index=0;index<=R.AdditionalRankConfigIds.Num();++index)
    {
        if(index && (!LexTryParseString(R.ConfigId,*R.AdditionalRankConfigIds[index-1]) || R.ConfigId<1))
        {Error=TEXT("Invalid Buff rank configuration ID");return false;}
        GuLiNet::Buffs::Definition rank;
        rank.Id=uint32(R.Id);rank.VersionValue=uint32(R.Version);
        if(!handler->Build(R,rank,Error) || !GuLiNet::Buffs::Validate(rank))
        {if(Error.IsEmpty())Error=TEXT("Invalid Buff rank definition");return false;}
        ranks.push_back(std::move(rank));
    }
    Out=std::move(ranks);
    return true;
}
bool UGuLiBurningBuffHandler::Build(const FGuLiStrikeBuffsBuffsRow& R,GuLiNet::Buffs::Definition& D,FString& Error) const
{
    FGuLiStrikeBuffsBurningRow V;if(!Row(GetDefault<UGuLiBuffSettings>()->Burning,R.ConfigId,V)){Error=TEXT("Missing burning configuration");return false;}
    D.Handler="burning";D.Duration=Ticks(V.DurationSeconds);D.Interval=Ticks(V.TickIntervalSeconds);D.Ratio=V.MaxHealthDamageRatio;D.Vfx=uint32(V.StatusVfxId);D.BulletVfx=uint32(V.BulletVfxId);return true;
}
bool UGuLiBurningBurstBuffHandler::Build(const FGuLiStrikeBuffsBuffsRow& R,GuLiNet::Buffs::Definition& D,FString& Error) const
{
    FGuLiStrikeBuffsBurningBurstRow V;if(!Row(GetDefault<UGuLiBuffSettings>()->BurningBurst,R.ConfigId,V)){Error=TEXT("Missing burning burst configuration");return false;}
    D.Handler="burning_burst";D.Parent=uint32(V.ParentBuffId);D.Interval=Ticks(V.IntervalSeconds);D.Ratio=V.LostHealthDamageRatio;D.RadiusCentimeters=V.RadiusCentimeters;D.Vfx=uint32(V.BurstVfxId);return true;
}
