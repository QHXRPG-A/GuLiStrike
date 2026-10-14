#pragma once
#include "Network/Private/GuLiHybridStateArchive.h"
#include "Gameplay/Resources/GuLiResourceInventoryComponent.h"

struct FGuLiHybridResourceInventoryState
{
    static constexpr uint32 Version=3;
    float RatePerSecond=0,Accumulator=0;
    FName ProcessingRule;
    FGuid PendingEvent;
    uint64 PendingAt=0,NextProcessingTick=0,AppliedRevision=0;
    TArray<FGuLiFactoryQueueEntry> Queue;
    FGuLiResourceAmounts Cargo;
    FGuid PendingCargo;
    uint64 CargoRevision=0;
    void Serialize(FGuLiHybridStateArchive& A)
    {
        uint32 Schema=Version;A.Value(Schema);if (Schema!=Version) A.Reject();
        A.Value(RatePerSecond);A.Value(ProcessingRule);A.Value(Accumulator);A.Value(PendingEvent);
        A.Value(PendingAt);A.Value(NextProcessingTick);A.Value(AppliedRevision);
        A.Value(Cargo.Blue);A.Value(Cargo.Red);A.Value(Cargo.Revision);A.Value(PendingCargo);A.Value(CargoRevision);
        A.Array(Queue,16384,[](auto& Ar,auto& E)
        {
            Ar.Enum(E.ResourceType,uint8(EGuLiResourceType::Red));Ar.Value(E.Amount);
            Ar.Enum(E.SettlementTeam,uint8(EGuLiTeam::Blue));Ar.Value(E.DeliveryEvent);Ar.Value(E.Source);
            if (E.Amount<=0 || !E.DeliveryEvent.IsValid() || !E.Source.IsValid()
                || (E.SettlementTeam!=EGuLiTeam::Red && E.SettlementTeam!=EGuLiTeam::Blue)) Ar.Reject();
        });
        int64 Total=0;for (const auto& E:Queue) Total+=E.Amount;
        if (RatePerSecond<=0 || RatePerSecond>60 || Accumulator<0 || Accumulator>2 || Total>10000000
            || (PendingCargo.IsValid() && PendingEvent.IsValid())
            || Cargo.Blue<0 || Cargo.Red<0 || int64(Cargo.Blue)+Cargo.Red>1000000) A.Reject();
    }
};
