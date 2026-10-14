#pragma once
#include "GuLiNetProtocol.h"

namespace GuLiNet
{
struct ObjectRecord;
inline constexpr const char* ControlPointCapability="control_point";
inline constexpr std::uint32_t ControlPointCapabilityVersion=4;
/** The assigned client observes space. Its capability transaction commits
 * capture and linked facilities' affiliation together. */
struct ControlPointState
{
    std::uint32_t Index=UINT32_MAX;
    std::uint32_t UpkeepRank=UINT32_MAX;
    Team Owner=Team::None;
    float Progress=0;
    std::uint32_t Red=0,Blue=0;
    Tick SampledAt=0,SamplePeriod=15,CaptureDuration=1200,UpkeepPeriod=1800;
    Resources UpkeepCost{1,1,0};
    Id FirstCapture;
    bool Enabled() const {return Index!=UINT32_MAX;}
};
GULINETCORE_API bool ValidControlPoint(const ControlPointState& Value);
GULINETCORE_API void WriteControlPoint(Writer& W,const ControlPointState& Value);
GULINETCORE_API bool ReadControlPoint(Reader& R,ControlPointState& Value);
GULINETCORE_API bool SameControlPointDefinition(const ControlPointState& A,const ControlPointState& B);
GULINETCORE_API bool AdvanceControlPoint(ControlPointState& Value,std::uint32_t Red,std::uint32_t Blue,Tick At,Id Event);
GULINETCORE_API bool GetControlPoint(const ObjectRecord& Object,ControlPointState& Out);
GULINETCORE_API bool SetControlPoint(ObjectRecord& Object,const ControlPointState& Value);
GULINETCORE_API Bytes EncodeControlPointSample(std::uint32_t Red,std::uint32_t Blue);
}
