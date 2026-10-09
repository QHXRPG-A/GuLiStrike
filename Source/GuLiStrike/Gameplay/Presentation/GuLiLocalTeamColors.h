#pragma once

#include "CoreMinimal.h"
#include "Battle/Network/GuLiBattleTypes.h"

class AActor;
class APlayerController;

/** Client-view relation only. Never substitute this for a combat or ownership team. */
namespace GuLiLocalTeamColors
{
	GULISTRIKE_API EGuLiTeam GetViewTeam(const APlayerController* Controller);
	GULISTRIKE_API EGuLiTeam GetActorTeam(const AActor* Actor);
	GULISTRIKE_API bool IsAssigned(EGuLiTeam Team);
	GULISTRIKE_API bool IsEnemy(EGuLiTeam ActualTeam, EGuLiTeam ViewTeam);
	/** Transparent only while identity is unresolved; assigned colors are fully opaque. */
	GULISTRIKE_API FLinearColor GetUI(EGuLiTeam ActualTeam, EGuLiTeam ViewTeam);
}
