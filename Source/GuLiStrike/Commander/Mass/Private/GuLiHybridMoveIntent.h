#pragma once
#include "Network/Private/GuLiHybridStateArchive.h"
#include "Commander/Mass/Navigation/GuLiSharedMoveRoutes.h"

namespace GuLiHybridState
{
inline void MoveIntent(FGuLiHybridStateArchive& A,TSharedPtr<FGuLiSharedMoveIntent>& P)
{
    if (A.IsReading()) P=MakeShared<FGuLiSharedMoveIntent>();
    if (!P || !P->Goal) { if (!A.IsReading()) { A.Reject(); return; } P->Goal=MakeShared<FGuLiSharedRouteGoal>(); }
    auto& V=*P; A.Value(V.Click); A.Value(V.Goal->Target);
    A.Array(V.Points,10000,[](auto& Ar,auto& N){ Ar.Value(N.Location); });
    TArray<uint32> Members;
    if (!A.IsReading()) { V.AssignedPoints.GenerateKeyArray(Members); Members.Sort(); }
    A.Array(Members,10000,[&](auto& Ar,uint32& K)
    {
        Ar.Value(K); FVector Location=Ar.IsReading()?FVector::ZeroVector:V.AssignedPoints.FindChecked(K).Location; Ar.Value(Location);
        if (!K || (Ar.IsReading() && V.AssignedPoints.Contains(K))) Ar.Reject();
        if (Ar.IsReading() && Ar.Good()) V.AssignedPoints.Add(K,FNavLocation(Location));
    });
    A.Array(V.UnassignedMembers,10000,[](auto& Ar,uint32& K){ Ar.Value(K); if (!K) Ar.Reject(); });
    TArray<uint16> Types;
    if (!A.IsReading()) { V.WidthByUnitType.GenerateKeyArray(Types); Types.Sort(); }
    A.Array(Types,4096,[&](auto& Ar,uint16& K)
    {
        Ar.Value(K); float Width=Ar.IsReading()?0:V.WidthByUnitType.FindChecked(K); Ar.Value(Width);
        if (!K || Width<=0 || (Ar.IsReading() && V.WidthByUnitType.Contains(K))) Ar.Reject();
        if (Ar.IsReading() && Ar.Good()) V.WidthByUnitType.Add(K,Width);
    });
    A.Random(V.Random); A.Value(V.Pitch); A.Value(V.GeneratedRadius); A.Value(V.WantedPoints);
    A.Value(V.MetadataCursor); A.Value(V.Ring); A.Value(V.Step); A.Value(V.CoverageGeneration);
    A.Value(V.bMetadataReady); A.Value(V.bExhausted);
    if (V.Pitch<0 || V.GeneratedRadius<0 || V.WantedPoints<0 || V.WantedPoints>10000
        || V.MetadataCursor<0 || V.Ring<0 || V.Step<0) A.Reject();
}
}
