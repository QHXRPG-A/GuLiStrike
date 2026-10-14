#pragma once

#include "CoreMinimal.h"
#include "Battle/Contracts/GuLiWingmanProtocolTypes.h"

class UWorld;
struct FGuLiCombatTargetSnapshot;

struct GULISTRIKE_API FGuLiWingmanTargetingTuning
{
    float AcquireRadiusCentimeters = 30000.0f;
    float ReleaseRadiusCentimeters = 36000.0f;
    float GuardRejoinFraction = 0.8f;
    float ScanIntervalSeconds = 0.2f;
    float MaximumPoseAgeSeconds = 1.0f;
    bool IsWellFormed() const;
};

struct GULISTRIKE_API FGuLiWingmanGuardPoseObservation
{
    FVector Position = FVector::ZeroVector;
    bool bAlive = true;
    bool bHasFreshAcceptedPose = false;
};

/** Inputs come from the active simulation owner or an accepted CS pose. */
struct GULISTRIKE_API FGuLiWingmanTargetMember
{
    FGuLiWingmanHandle Emitter;
    FVector Position = FVector::ZeroVector;
    FGuLiTargetHandle PreviousTarget;
    bool bPreviousTargetRetainable = false;
};

/** Derived geometry cache; no authorization or private gameplay state lives here. */
struct GULISTRIKE_API FGuLiWingmanGroundCorridorCache
{
    FVector TargetLocation = FVector::ZeroVector;
    double ValidUntilSeconds = 0.0;
    bool bClear = false;
};

namespace GuLiWingmanTargeting
{
    GULISTRIKE_API bool IsWithinAcquireRange(double DistanceCentimeters,
        const FGuLiWingmanTargetingTuning& Tuning);
    GULISTRIKE_API bool IsWithinReleaseRange(double DistanceCentimeters,
        const FGuLiWingmanTargetingTuning& Tuning);
    GULISTRIKE_API bool IsGuardRejoinComplete(TConstArrayView<FGuLiWingmanGuardPoseObservation> Observations,
        const FVector& ShipLocation, float CatchUpDistanceCentimeters,
        float RecoveryDistanceCentimeters, float RequiredFraction);
    GULISTRIKE_API const FGuLiWingmanAttackTarget* ResolveTargetForEmitter(
        const FGuLiWingmanAttackAuthorityState& State, const FGuLiWingmanHandle& Emitter);

    /** Shared gameplay algorithm: terrain projection, hysteresis, corridor checks and Hungarian assignment.
     * The caller owns target revisions, authority and publication; this function never settles damage. */
    GULISTRIKE_API bool SelectAutomaticTargets(UWorld* World,
        const FGuLiCombatTargetSnapshot& Source, const FGuLiGroupAbilityConfigSnapshot& Configuration,
        const FGuLiWingmanTargetingTuning& Tuning, double LogicalSeconds,
        TConstArrayView<FGuLiWingmanTargetMember> Members,
        TConstArrayView<FGuLiCombatTargetSnapshot> TargetCatalog,
        TConstArrayView<FGuLiWingmanAutoTargetAssignment> PreviousAssignments,
        TMap<FGuLiTargetHandle, FGuLiWingmanGroundCorridorCache>& CorridorCache,
        TArray<FGuLiWingmanAutoTargetAssignment>& OutAssignments);
}
