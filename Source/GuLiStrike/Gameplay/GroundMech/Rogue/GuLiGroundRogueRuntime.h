#pragma once
#include "CoreMinimal.h"
#include "Gameplay/Data/Generated/GuLiStrikeRogueCardsTableRows.h"
#include "GuLiNetGroundRogue.h"
class AGuLiBattlePlayerState;
class UWorld;
struct FGuLiHybridObjectState;
enum class EGuLiHybridArmyOperation : uint8;
namespace GuLiGroundRogue
{
    struct FBarrageCard
    {
        FString CardId;
        int32 WindupTicks=0;
        GuLiNet::Effects::Barrage Profile;
    };
    inline const FName BarrageFieldId(TEXT("GroundNoSurvivors"));
    inline constexpr TCHAR BarrageWindupKey[]=TEXT("ground_rogue/barrage/windup_ticks");
    inline float BarrageDeployedAtSeconds(const FGuLiStrikeRogueCardsCardsRow& Row)
    {
        return Row.CastFoldSeconds + Row.CastCrossedHoldSeconds + Row.CastOpenSeconds;
    }
    inline float BarrageWindupSeconds(const FGuLiStrikeRogueCardsCardsRow& Row)
    {
        return BarrageDeployedAtSeconds(Row) + Row.CastFlashSeconds;
    }
    inline int32 BarrageWindupTicks(const FGuLiStrikeRogueCardsCardsRow& Row)
    {
        return FMath::RoundToInt(60.f * BarrageWindupSeconds(Row));
    }
    inline FName BarrageRuleSkill(int32 Level,bool bTrial=false)
    {
        if(bTrial && Level==1)return TEXT("GroundNoSurvivors_Trial");
        return Level==1 ? BarrageFieldId : FName(*FString::Printf(TEXT("GroundNoSurvivors_Lv%d"),Level));
    }
    bool LoadEffect(int32 Id,GuLiNet::Effects::Definition& Out,FString& Error);
    GULISTRIKE_API bool LoadCatalog(GuLiNet::GroundRogue::Catalog& Out,FString& Error,FBarrageCard* Barrage=nullptr);
    FText Preview(const FGuLiStrikeRogueCardsCardsRow&,const FText&,int32 Acquisitions);
    FGuid Request(const AGuLiBattlePlayerState&,EGuLiHybridArmyOperation,FGuid Event,FGuid Session={},FString Choice={},uint32 TargetLevel=0);
    bool ReadBuild(UWorld&,FGuid Player,TMap<FString,int32>& Counts,uint64& Revision,GuLiNet::Effects::Set* Resolved=nullptr);
}
