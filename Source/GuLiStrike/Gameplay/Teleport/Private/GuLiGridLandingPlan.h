#pragma once
#include "Gameplay/Teleport/Private/GuLiHybridTeleportState.h"

namespace GuLiHybridTeleport
{
enum class ELandingPlan : uint8 { Pending,Ready,Failed };
/** Transient search work. The prepared command remains the existing checkpointed transaction payload. */
class FGridLandingPlan
{
public:
    FGridLandingPlan();
    ~FGridLandingPlan();
    ELandingPlan Advance(UWorld& World,const Net::State& State,Net::Command& Command,bool Returning);
private:
    struct FImpl;
    TUniquePtr<FImpl> Impl;
};
}
