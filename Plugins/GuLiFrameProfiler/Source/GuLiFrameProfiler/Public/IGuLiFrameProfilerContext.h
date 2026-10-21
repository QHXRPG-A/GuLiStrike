#pragma once

#include "CoreMinimal.h"
#include "Features/IModularFeature.h"

class UWorld;

/** Optional project-owned native counters. All methods execute on the game thread.
 * The profiler never depends on a gameplay module or stops another capture.
 */
class IGuLiFrameProfilerContext : public IModularFeature
{
public:
    static FName GetFeatureName() { return TEXT("GuLiFrameProfilerContext"); }
    virtual bool CanBegin(UWorld* World, FString& Reason) const = 0;
    virtual bool Begin(UWorld* World, const FString& Directory, FString& Reason) = 0;
    /** Ends only this adapter's work and returns a JSON object containing its evidence. */
    virtual FString End() = 0;
};
