#pragma once

#include "CoreTypes.h"

class UWorld;
class AActor;

// Editor-only preparation for movement captures and isolated play ranges.
namespace GuLiMovementCapture
{
enum EActivityHold : uint8 { TerritoryCapture = 1, AutomaticEconomy = 2 };
#if WITH_EDITOR
inline constexpr TCHAR RoomHoldMaskKey[] = TEXT("editor/fixture/world_activity_hold");
#endif
bool HoldTerritoryChanges(const UWorld* World);
bool HoldAutomaticWork(const UWorld* World, const AActor* Worker = nullptr);
}
