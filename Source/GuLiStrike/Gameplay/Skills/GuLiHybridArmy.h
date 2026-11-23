#pragma once
#include "CoreMinimal.h"
#include "Battle/Network/GuLiBattleTypes.h"
class UWorld;
class UGuLiCommanderNetSyncComponent;
class AGuLiBattlePlayerState;
struct FGuLiHybridObjectState;
enum class EGuLiHybridArmyOperation:uint8 {Offer,Reroll,Choose,Equip,ClosePresentation,SetCardLevel};
namespace GuLiHybridArmy
{
GULISTRIKE_API bool FindRoot(UWorld&,EGuLiTeam,FGuLiHybridObjectState&);
GULISTRIKE_API FGuid Request(UWorld&,EGuLiTeam,EGuLiHybridArmyOperation,FGuid Request,FGuid Session={},
    FString Choice={},uint32 ExpectedRevision=0,uint16 Unit=0,FName Slot=NAME_None,uint32 TargetLevel=0);
GULISTRIKE_API bool Equip(const AGuLiBattlePlayerState&,uint16,FName,FName,uint32,FString&);
}
