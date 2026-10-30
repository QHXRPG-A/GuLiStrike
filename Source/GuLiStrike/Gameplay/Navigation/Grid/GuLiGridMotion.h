#pragma once
#include "CoreMinimal.h"
#include "Gameplay/Navigation/Grid/GuLiGridTypes.h"
#include "Network/GuLiHybridObjectKey.h"
#include "Gameplay/Units/GuLiUnitMovementLayer.h"

class UGuLiGroundGridSubsystem;
struct FGuLiGridSnapshot;
enum class EGuLiGridMoveEnd : uint8 { None,Reached,Blocked,NoPath,Capacity,Stopped,Yielded };

/** Legacy recovery payload plus actual footprint ownership; never drives ground movement. */
struct FGuLiGridMotion
{
    GuLiGrid::FMoveState State;
    FGuLiHybridObjectKey Key;
    FGuid Owner;
    uint64 Epoch=0;
    uint8 Team=0;
    EGuLiGridMoveEnd End=EGuLiGridMoveEnd::None;
    // Retained in the existing checkpoint byte layout, not route handles or grants.
    FIntPoint SharedGoal=FIntPoint::ZeroValue,Join=FIntPoint::ZeroValue;
    bool bFinalConnector=false,bKeepWork=false,bYield=false;
    static GuLiGrid::FRect Coverage(const FGuLiGridSnapshot& Grid,const FVector& Position,int32 Size);
    bool Bind(UGuLiGroundGridSubsystem& Grid,const FGuLiHybridObjectKey& InKey,const FGuid& InOwner,uint64 InEpoch,
        uint8 InTeam,const FVector& Position,int32 Size,TOptional<EGuLiUnitMovementLayer> Layer={});
    void Stop(UGuLiGroundGridSubsystem& Grid,const FVector& Actual,double Now,EGuLiGridMoveEnd Reason);
};
