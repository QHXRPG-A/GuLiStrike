#include "Commander/Mass/GuLiMassCrowdObstacleSubsystem.h"
#include "Commander/Mass/GuLiBattleAuthoritySubsystem.h"
#include "Engine/World.h"

bool UGuLiMassCrowdObstacleSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	const auto* World = CastChecked<UWorld>(Outer);
	return Super::ShouldCreateSubsystem(Outer) && World->IsGameWorld() && World->GetNetMode() != NM_Client;
}

void UGuLiMassCrowdObstacleSubsystem::Tick(float)
{
	// NetMode can change after PIE creates its World subsystems.
	if (GetWorld()->GetNetMode() == NM_Client) return;
	GetWorld()->GetSubsystem<UGuLiBattleAuthoritySubsystem>()->BuildGroundAvoidanceSnapshot(Bodies);
	CastChecked<UGuLiGroundCrowdManager>(UCrowdManager::GetCurrent(GetWorld()))->SetGroundObstacles(Bodies);
}

TStatId UGuLiMassCrowdObstacleSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UGuLiMassCrowdObstacleSubsystem, STATGROUP_Tickables);
}
