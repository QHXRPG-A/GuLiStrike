#pragma once
#include "Network/Private/GuLiHybridStateArchive.h"
#include "Gameplay/Stronghold/GuLiStrongholdTransitTypes.h"

namespace GuLiHybridEngineeringState
{
inline void Transit(FGuLiHybridStateArchive& A,FGuLiStrongholdTransitState& V)
{
    A.Value(V.JourneyId); A.Enum(V.Phase,uint8(EGuLiTransitPhase::ExitFlash));
    A.Value(V.TransitFieldId); A.Value(V.DestinationTerritory); A.Value(V.bEmergencyExit); A.Value(V.NetworkRevision);
    A.Array(V.Route,512,[](auto& Ar,auto& P)
    { Ar.Value(P.TerritoryIndex); FVector Position=P.Position; Ar.Value(Position); if (Ar.IsReading()) P.Position=Position; });
    auto& T=V.Timing;
    A.Value(T.InitialSpeed); A.Value(T.PeakSpeed); A.Value(T.Acceleration); A.Value(T.Cruise); A.Value(T.Deceleration); A.Value(T.Length);
    A.Value(V.StartServerTime); A.Value(V.ExitServerTime); A.Value(V.AscentSeconds); A.Value(V.FlashSeconds);
    auto Vector=[&](auto& P){FVector Value=P; A.Value(Value); if (A.IsReading()) P=Value;};
    Vector(V.EntryPosition); Vector(V.FinalGroundTarget); Vector(V.ExitCenter); Vector(V.ExitPosition);
    if (T.InitialSpeed<0 || T.PeakSpeed<0 || T.Acceleration<0 || T.Cruise<0 || T.Deceleration<0 || T.Length<0
        || V.StartServerTime<0 || V.ExitServerTime<0 || V.AscentSeconds<0 || V.FlashSeconds<0
        || (V.Phase!=EGuLiTransitPhase::Ground && (!V.JourneyId.IsValid() || V.TransitFieldId<=0))
        || (V.IsPhased() && (V.Route.Num()<2 || T.PeakSpeed<=0 || T.InitialSpeed>T.PeakSpeed))) A.Reject();
}
}
