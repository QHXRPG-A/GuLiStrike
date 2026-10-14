#pragma once
#include "Network/GuLiHybridWorldSubsystem.h"

struct FGuLiHybridControlPointState
{
    int32 Index=INDEX_NONE;
    EGuLiTeam Owner=EGuLiTeam::Unassigned;
    float Progress=0;
    uint32 Red=0,Blue=0;
    uint64 SampledAt=0,SamplePeriod=0;
    FGuid FirstCapture;
};

namespace GuLiHybridControlPoint
{
FGuLiHybridControlPointState Read(const FGuLiHybridObjectState& Object);
FGuid Submit(UGuLiHybridWorldSubsystem& World,const FGuLiHybridObjectState& Object,FGuid Event,uint32 Red,uint32 Blue);
}
