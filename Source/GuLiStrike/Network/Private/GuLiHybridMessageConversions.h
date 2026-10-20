#pragma once
#include "Network/GuLiHybridSessionSubsystem.h"
#include "Network/Private/GuLiNetConversions.h"
#include "GuLiNetProtocol.h"

namespace GuLiNetBridge
{
inline GuLiNet::Message ToCore(const FGuLiHybridMessage& V)
{
    GuLiNet::Message Out; auto& H = Out.Header;
    H.Kind = static_cast<GuLiNet::MessageKind>(V.Kind); H.Match = ToCore(V.Match); H.Sender = ToCore(V.Sender);
    H.Event = ToCore(V.Event); H.Object = {static_cast<GuLiNet::ObjectKind>(V.ObjectKind), ToCore(V.ObjectId), V.LifeGeneration};
    H.Epoch = V.MatchEpoch; H.OwnerEpoch = V.OwnerEpoch; H.ControlEpoch = V.ControlEpoch;
    H.Sequence = V.Sequence; H.At = V.LogicalTick;
    H.ResourceGroup=ToCore(V.ResourceGroup);H.ResourceEpoch=V.ResourceEpoch;
    if (!V.Body.IsEmpty()) Out.Body.assign(V.Body.GetData(), V.Body.GetData() + V.Body.Num());
    return Out;
}
inline FGuLiHybridMessage ToUE(const GuLiNet::Message& V)
{
    FGuLiHybridMessage Out; const auto& H = V.Header;
    Out.Kind = static_cast<uint16>(H.Kind); Out.Match = ToUE(H.Match); Out.Sender = ToUE(H.Sender); Out.Event = ToUE(H.Event);
    Out.ObjectKind = static_cast<uint8>(H.Object.Kind); Out.ObjectId = ToUE(H.Object.StableId); Out.LifeGeneration = H.Object.Generation;
    Out.MatchEpoch = H.Epoch; Out.OwnerEpoch = H.OwnerEpoch; Out.ControlEpoch = H.ControlEpoch;
    Out.Sequence = H.Sequence; Out.LogicalTick = H.At;
    Out.ResourceGroup=ToUE(H.ResourceGroup);Out.ResourceEpoch=H.ResourceEpoch;
    if (!V.Body.empty()) Out.Body.Append(V.Body.data(), static_cast<int32>(V.Body.size())); return Out;
}
}
