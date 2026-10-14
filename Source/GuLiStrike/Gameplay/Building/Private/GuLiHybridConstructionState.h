#pragma once
#include "Network/Private/GuLiHybridStateArchive.h"

/** Queries need the owner's slot poses, occupants and acknowledgements as well as progress. */
struct FGuLiHybridConstructionPublicState
{
    struct FSlot { FTransform Pose=FTransform::Identity; FGuLiHybridObjectKey Owner; uint32 Task=0; };
    float Work=0;
    uint32 Generation=1;
    bool PendingSlots=true;
    TArray<FSlot> Slots;
    TArray<FGuLiHybridObjectKey> Contributors;
    void SerializeCommon(FGuLiHybridStateArchive& A)
    {
        A.Value(Work); A.Value(Generation); A.Value(PendingSlots);
        A.Array(Slots,4,[](auto& Ar,auto& S)
        {
            Ar.Value(S.Pose); Ar.Value(S.Owner); Ar.Value(S.Task);
            if (S.Owner.IsValid()?S.Owner.Kind!=EGuLiHybridObjectKind::Engineer || !S.Task:S.Task!=0) Ar.Reject();
        });
        A.Array(Contributors,4,[](auto& Ar,auto& K)
        { Ar.Value(K); if (!K.IsValid() || K.Kind!=EGuLiHybridObjectKind::Engineer) Ar.Reject(); });
        if (!FMath::IsFinite(Work) || Work<0 || !Generation) A.Reject();
        TSet<FGuLiHybridObjectKey> Occupants,Workers;
        for (const auto& S:Slots) if (S.Owner.IsValid())
        { if (PendingSlots || Occupants.Contains(S.Owner)) A.Reject(); Occupants.Add(S.Owner); }
        for (const auto& K:Contributors)
        { if (Workers.Contains(K) || !Occupants.Contains(K)) A.Reject(); Workers.Add(K); }
    }
    void Serialize(FGuLiHybridStateArchive& A)
    {uint32 Schema=1; A.Value(Schema); if (Schema!=1) A.Reject(); SerializeCommon(A);}
};

/** Process-independent construction state shared by placed and resource buildings. */
struct FGuLiHybridConstructionPrivateState: FGuLiHybridConstructionPublicState
{
    float PendingWork=0;
    int32 NextSample=0;
    FGuid Completion;
    FGuLiHybridObjectKey Prototype;
    void Serialize(FGuLiHybridStateArchive& A)
    {
        uint32 Schema=2; A.Value(Schema); if (Schema!=2) A.Reject(); SerializeCommon(A);
        A.Value(PendingWork); A.Value(NextSample); A.Value(Completion); A.Value(Prototype);
        if (!FMath::IsFinite(PendingWork) || PendingWork<0 || NextSample<0 || NextSample>4
            || (Prototype.IsValid() && Prototype.Kind!=EGuLiHybridObjectKind::Engineer)) A.Reject();
    }
};
