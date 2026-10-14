#include "Gameplay/Stronghold/GuLiHybridControlPoint.h"
#include "Network/Private/GuLiHybridMessageConversions.h"
#include "GuLiNetControlPoint.h"

FGuLiHybridControlPointState GuLiHybridControlPoint::Read(const FGuLiHybridObjectState& O)
{
    FGuLiHybridControlPointState Result;
    const auto* Data=O.Capabilities.Find(FName(UTF8_TO_TCHAR(GuLiNet::ControlPointCapability)));
    if (!Data || Data->Version!=GuLiNet::ControlPointCapabilityVersion) return Result;
    GuLiNet::ControlPointState P;GuLiNet::Reader Reader({Data->Data.GetData(),static_cast<size_t>(Data->Data.Num())});
    if (!GuLiNet::ReadControlPoint(Reader,P) || !Reader.Done() || !P.Enabled()) return Result;
    Result.Index=int32(P.Index);Result.Owner=GuLiNetBridge::ToUE(P.Owner);Result.Progress=P.Progress;
    Result.Red=P.Red;Result.Blue=P.Blue;Result.SampledAt=P.SampledAt;Result.SamplePeriod=P.SamplePeriod;
    Result.FirstCapture=GuLiNetBridge::ToUE(P.FirstCapture);
    return Result;
}
FGuid GuLiHybridControlPoint::Submit(UGuLiHybridWorldSubsystem& H,const FGuLiHybridObjectState& O,FGuid Event,
    uint32 Red,uint32 Blue)
{
    if (Read(O).Index==INDEX_NONE || Red>100000 || Blue>100000) return {};
    const auto Bytes=GuLiNet::EncodeControlPointSample(Red,Blue);
    const FName Rule(*(FString(TEXT("capture/"))+O.Definition.ToString()));
    return H.SubmitCapability(O.Key,Event,Rule,O.RelationRevision,{Bytes.data(),static_cast<int32>(Bytes.size())},EGuLiTeam::Unassigned);
}
