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
/** Requires a full object record containing the control_point capability. */
FGuLiHybridControlPointState Read(const FGuLiHybridObjectState& Object);
/** Reads only the applied control_point payload; suitable alongside GetObjectView. */
FGuLiHybridControlPointState Read(const UGuLiHybridWorldSubsystem& World,const FGuLiHybridObjectKey& Key);
FGuid Submit(UGuLiHybridWorldSubsystem& World,const FGuLiHybridObjectState& Object,FGuid Event,uint32 Red,uint32 Blue);
}
