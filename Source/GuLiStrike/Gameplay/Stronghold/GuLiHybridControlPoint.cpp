#include "Gameplay/Stronghold/GuLiHybridControlPoint.h"
#include "Network/Private/GuLiHybridMessageConversions.h"
#include "GuLiNetControlPoint.h"

namespace
{
FName ControlPointName()
{
    static const FName Name(UTF8_TO_TCHAR(GuLiNet::ControlPointCapability));
    return Name;
}
FGuLiHybridControlPointState ReadControlPointCapability(const FGuLiHybridCapabilityState* Data)
{
    FGuLiHybridControlPointState Result;
    if (!Data || Data->Version!=GuLiNet::ControlPointCapabilityVersion) return Result;
    GuLiNet::ControlPointState P;GuLiNet::Reader Reader({Data->Data.GetData(),static_cast<size_t>(Data->Data.Num())});
    if (!GuLiNet::ReadControlPoint(Reader,P) || !Reader.Done() || !P.Enabled()) return Result;
    Result.Index=int32(P.Index);Result.Owner=GuLiNetBridge::ToUE(P.Owner);Result.Progress=P.Progress;
    Result.Red=P.Red;Result.Blue=P.Blue;Result.SampledAt=P.SampledAt;Result.SamplePeriod=P.SamplePeriod;
    Result.FirstCapture=GuLiNetBridge::ToUE(P.FirstCapture);
    return Result;
}
}
FGuLiHybridControlPointState GuLiHybridControlPoint::Read(const FGuLiHybridObjectState& O)
{
    return ReadControlPointCapability(O.Capabilities.Find(ControlPointName()));
}
FGuLiHybridControlPointState GuLiHybridControlPoint::Read(const UGuLiHybridWorldSubsystem& World,const FGuLiHybridObjectKey& Key)
{
    FGuLiHybridCapabilityState Data;
    return World.GetObjectCapability(Key,ControlPointName(),Data) ? ReadControlPointCapability(&Data) : FGuLiHybridControlPointState{};
}
FGuid GuLiHybridControlPoint::Submit(UGuLiHybridWorldSubsystem& H,const FGuLiHybridObjectState& O,FGuid Event,
    uint32 Red,uint32 Blue)
{
    if (Read(O).Index==INDEX_NONE || Red>100000 || Blue>100000) return {};
    const auto Bytes=GuLiNet::EncodeControlPointSample(Red,Blue);
    const FName Rule(*(FString(TEXT("capture/"))+O.Definition.ToString()));
    return H.SubmitCapability(O.Key,Event,Rule,O.RelationRevision,{Bytes.data(),static_cast<int32>(Bytes.size())},EGuLiTeam::Unassigned);
}
