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
    FGuLiStrikeBuffsBuffsRow R;if(!Row(GetDefault<UGuLiBuffSettings>()->Buffs,Id,R)){Error=TEXT("Missing Buff ID ")+FString::FromInt(Id);return false;}
    auto* Class=R.ImplementationClass.LoadSynchronous();if(!Class||!Class->IsChildOf(StaticClass())){Error=TEXT("Buff class must derive from UGuLiBuffHandler");return false;}
    D={};D.Id=uint32(R.Id);D.VersionValue=uint32(R.Version);
    if(!Class->GetDefaultObject<UGuLiBuffHandler>()->Build(R,D,Error)||!GuLiNet::Buffs::Validate(D)){if(Error.IsEmpty())Error=TEXT("Invalid Buff definition");return false;}return true;
}
bool UGuLiBurningBuffHandler::Build(const FGuLiStrikeBuffsBuffsRow& R,GuLiNet::Buffs::Definition& D,FString& Error) const
{
    FGuLiStrikeBuffsBurningRow V;if(!Row(GetDefault<UGuLiBuffSettings>()->Burning,R.ConfigId,V)){Error=TEXT("Missing burning configuration");return false;}
    D.Handler="burning";D.Duration=Ticks(V.DurationSeconds);D.Interval=Ticks(V.TickIntervalSeconds);D.Ratio=V.MaxHealthDamageRatio;D.Vfx=uint32(V.StatusVfxId);D.BulletVfx=uint32(V.BulletVfxId);return true;
}
bool UGuLiBurningBurstBuffHandler::Build(const FGuLiStrikeBuffsBuffsRow& R,GuLiNet::Buffs::Definition& D,FString& Error) const
{
    FGuLiStrikeBuffsBurningBurstRow V;if(!Row(GetDefault<UGuLiBuffSettings>()->BurningBurst,R.ConfigId,V)){Error=TEXT("Missing burning burst configuration");return false;}
    D.Handler="burning_burst";D.Parent=uint32(V.ParentBuffId);D.Interval=Ticks(V.IntervalSeconds);D.Ratio=V.LostHealthDamageRatio;D.RadiusMultiplier=V.RadiusMultiplier;D.Vfx=uint32(V.BurstVfxId);return true;
}
