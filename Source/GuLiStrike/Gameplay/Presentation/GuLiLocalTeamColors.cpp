#include "Gameplay/Presentation/GuLiLocalTeamColors.h"
#include "Battle/Framework/GuLiBattlePlayerState.h"
#include "Battle/Combat/GuLiCombatDamageLedger.h"
#include "Gameplay/Building/GuLiBuildingLifecycleComponent.h"
#include "Gameplay/Presentation/GuLiTeamOutlineComponent.h"
#include "Gameplay/Units/GuLiEngineeringTravelComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"

bool GuLiLocalTeamColors::IsAssigned(const EGuLiTeam Team)
{ return Team == EGuLiTeam::Red || Team == EGuLiTeam::Blue; }

EGuLiTeam GuLiLocalTeamColors::GetViewTeam(const APlayerController* Controller)
{
	const auto* State = Controller ? Controller->GetPlayerState<AGuLiBattlePlayerState>() : nullptr;
	return State ? State->GetTeam() : EGuLiTeam::Unassigned;
}

EGuLiTeam GuLiLocalTeamColors::GetActorTeam(const AActor* Actor)
{
	if (!Actor) return EGuLiTeam::Unassigned;
	if (const auto* Life = Actor->FindComponentByClass<UGuLiBuildingLifecycleComponent>()) return Life->GetTeam();
	if (const auto* Vehicle = Cast<IGuLiEngineeringVehicle>(Actor)) return Vehicle->GetTeam();
	if (const auto* Pawn = Cast<APawn>(Actor))
		if (const auto* State = Pawn->GetPlayerState<AGuLiBattlePlayerState>()) return State->GetTeam();
	if (const auto* Health = Actor->FindComponentByClass<UGuLiCombatHealthComponent>()) return Health->GetCombatTeam();
	if (const auto* Outline = Actor->FindComponentByClass<UGuLiTeamOutlineComponent>()) return Outline->GetOutlineTeam();
	if (const auto* Parent = Actor->GetParentActor(); Parent && Parent != Actor) return GetActorTeam(Parent);
	return EGuLiTeam::Unassigned;
}

bool GuLiLocalTeamColors::IsEnemy(const EGuLiTeam ActualTeam, const EGuLiTeam ViewTeam)
{ return IsAssigned(ActualTeam) && IsAssigned(ViewTeam) && ActualTeam != ViewTeam; }

FLinearColor GuLiLocalTeamColors::GetUI(const EGuLiTeam ActualTeam, const EGuLiTeam ViewTeam)
{
	if (!IsAssigned(ActualTeam) || !IsAssigned(ViewTeam)) return FLinearColor::Transparent;
	return ActualTeam == ViewTeam ? FLinearColor(.02f, .28f, 1.f, 1.f) : FLinearColor(1.f, .04f, .03f, 1.f);
}
