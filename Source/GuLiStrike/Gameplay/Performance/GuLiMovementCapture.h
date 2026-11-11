#pragma once

class UWorld;
class AActor;

// Explicit disposable-scene preparation only. Defaults to zero, and never
// affects a packaged runtime. Full unit populations remain in Mass/indexes.
namespace GuLiMovementCapture
{
bool HoldTerritoryChanges(const UWorld* World);
bool HoldAutomaticWork(const UWorld* World, const AActor* Worker = nullptr);
}
