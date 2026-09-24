#pragma once
#include "CoreMinimal.h"

class ACharacter;
enum class EGuLiWorkPositionAvailability : uint8 { Pending, NoValidPositions, Occupied, Available };

/** Geometry only; reservations and work semantics belong to resources/buildings. */
namespace GuLiWorkPosition
{
	bool ProjectPose(const ACharacter& Vehicle, const FVector& DesiredGround, const FRotator& Rotation, FTransform& Out);
}
