#pragma once
#include "CoreMinimal.h"
#include "Gameplay/Navigation/GuLiWorkPosition.h"
class ACharacter;

struct FGuLiConstructionSlotReservation
{
	uint32 Building = 0;
	int32 Slot = INDEX_NONE;
	uint32 Generation = 0;
	uint32 Task = 0;
	bool IsValid() const { return Building!=0 && Slot!=INDEX_NONE; }
};
struct FGuLiConstructionSlot
{
	FTransform Pose;
	TWeakObjectPtr<ACharacter> Owner;
	uint32 Task = 0;
};
