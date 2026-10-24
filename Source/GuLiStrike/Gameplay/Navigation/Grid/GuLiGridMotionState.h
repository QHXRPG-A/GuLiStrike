#pragma once
#include "Gameplay/Navigation/Grid/GuLiGridMotion.h"
#include "Network/Private/GuLiHybridStateArchive.h"

namespace GuLiHybridState
{
inline void GridMotion(FGuLiHybridStateArchive& A,FGuLiGridMotion& M)
{
    auto Point=[&](FIntPoint& P)
    { A.Value(P.X);A.Value(P.Y);if (P.X<-65536 || P.Y<-65536 || P.X>65536 || P.Y>65536) A.Reject(); };
    auto& S=M.State;
    A.Enum(S.State,uint8(GuLiGrid::EMoveState::PausedWork));Point(S.Anchor);Point(S.Target);Point(S.StepTarget);
    A.Value(S.Footprint);A.Value(S.BlockedSeconds);A.Value(S.RetryAt);A.Value(S.bStepCommitted);A.Value(S.bUserStopped);
    A.Enum(S.Priority.Class,uint8(GuLiGrid::ETrafficClass::DirectPlayer));
    A.Value(S.Priority.AcceptedSequence);A.Value(S.Priority.Command);
    A.Enum(M.End,uint8(EGuLiGridMoveEnd::Yielded));Point(M.SharedGoal);Point(M.Join);
    A.Value(M.bFinalConnector);A.Value(M.bKeepWork);A.Value(M.bYield);
    if (S.Footprint<1 || S.Footprint>64 || S.BlockedSeconds<0 || S.BlockedSeconds>86400 || S.RetryAt<0
        || S.bStepCommitted && (FMath::Abs(S.StepTarget.X-S.Anchor.X)>1 || FMath::Abs(S.StepTarget.Y-S.Anchor.Y)>1)) A.Reject();
    // Authority grants, path handles and future reservations belong to the receiving world.
    if (A.IsReading()) { M.Key={};M.Owner.Invalidate();M.Epoch=0; }
}
}
