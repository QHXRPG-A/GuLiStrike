#pragma once
#include "CoreMinimal.h"
#include "Commander/Network/GuLiCommanderTypes.h"
class UTexture2D;

/** A local screen marker, never an authority or an independently replicated entity. */
struct FGuLiCommanderOverviewMarker
{
	FGuLiSoldierId SoldierId;
	FGuLiControllableActorId ActorId;
	TWeakObjectPtr<AActor> Actor;
	TWeakObjectPtr<UTexture2D> Icon;
	FVector WorldLocation = FVector::ZeroVector;
	FVector2D ScreenPosition = FVector2D::ZeroVector;
	EGuLiTeam Team = EGuLiTeam::Unassigned;
	float Size = 14;
	float HealthFraction = 1;
	float BarOpacity = 0;
	bool bBuilding = false;
	bool bSelected = false;
};
