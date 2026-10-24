#pragma once
#include "Network/Private/GuLiHybridStateArchive.h"

struct FGuLiHybridProductionPrivateState
{
    struct FJob {FGuid Event;FGuLiHybridObjectKey Target;FVector Location;float FacingYaw=0;bool Submitted=false;};
    float Cycle=0;
    EGuLiTeam ObservedTeam=EGuLiTeam::Unassigned;
    bool Producing=false;
    int32 GridRemaining=0,GridCandidate=0;
    double GridRetryDelay=0;
    TArray<FJob> Jobs;
    void Serialize(FGuLiHybridStateArchive& A)
    {
        uint32 Version=3;A.Value(Version);if (Version!=3) A.Reject();
        A.Value(Cycle);A.Enum(ObservedTeam,2);A.Value(Producing);
        A.Value(GridRemaining);A.Value(GridCandidate);A.Value(GridRetryDelay);
        if (GridRemaining<0 || GridRemaining>256 || GridCandidate<0 || GridCandidate>=128 || GridRetryDelay<0 || GridRetryDelay>6) A.Reject();
        A.Array(Jobs,256,[](auto& Ar,auto& J)
        {
            Ar.Value(J.Event);Ar.Value(J.Target);Ar.Value(J.Location);Ar.Value(J.FacingYaw);Ar.Value(J.Submitted);
            if (!J.Event.IsValid() || !J.Target.IsValid()) Ar.Reject();
        });
        TSet<FGuid> Events;TSet<FGuLiHybridObjectKey> Targets;
        for (const auto& J:Jobs)
        {if (Events.Contains(J.Event) || Targets.Contains(J.Target)) A.Reject();Events.Add(J.Event);Targets.Add(J.Target);}
        if (Cycle<0) A.Reject();
    }
};
