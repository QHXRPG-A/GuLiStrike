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
    bool LoadEffect(int32 Id,GuLiNet::Effects::Definition& Out,FString& Error);
    bool LoadCatalog(GuLiNet::GroundRogue::Catalog& Out,FString& Error);
    FText Preview(const FGuLiStrikeRogueCardsCardsRow&,const FText&,int32 Acquisitions);
    FGuid Request(const AGuLiBattlePlayerState&,EGuLiHybridArmyOperation,FGuid Event,FGuid Session={},FString Choice={});
    bool ReadBuild(UWorld&,FGuid Player,TMap<FString,int32>& Counts,uint64& Revision,GuLiNet::Effects::Set* Resolved=nullptr);
}
