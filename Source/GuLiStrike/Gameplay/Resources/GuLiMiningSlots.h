#pragma once
#include "CoreMinimal.h"
#include "Gameplay/Navigation/GuLiWorkPosition.h"
class AGuLiMiningVehiclePawn;

inline constexpr float GULI_MINING_SLOT_ARRIVAL_RADIUS_CM = 50.0f;
inline constexpr float GULI_MINING_SLOT_WORK_TOLERANCE_CM = 100.0f;

/** Owned by the resource domain; a reservation is meaningful only with its generation and task. */
struct FGuLiMiningSlotReservation
{
	int32 Profile = 0;
	uint16 Cluster = 0;
	int32 Slot = INDEX_NONE;
	uint32 Generation = 0;
	uint32 Task = 0;
	bool IsValid() const { return Cluster!=0 && Slot!=INDEX_NONE; }
};
struct FGuLiMiningSlot
{
	FTransform Pose;
	TArray<uint32> Nodes;
	TWeakObjectPtr<AGuLiMiningVehiclePawn> Owner;
	uint32 Task = 0;
};

enum class EGuLiMiningSlotFailure : uint8
{
	Movement,
	NoVisibleNodes
};
struct FGuLiMiningClusterSlots
{
	TArray<FGuLiMiningSlot> Slots;
	uint32 Generation = 1;
	int32 NextSample = 0;
	bool bPending = true;
};
struct FGuLiMiningSlotProfile
{
	TWeakObjectPtr<AGuLiMiningVehiclePawn> Prototype;
	TArray<FGuLiMiningClusterSlots> Clusters;
	int32 BuildCursor = 0;
};
