#pragma once
#include "CoreMinimal.h"
#include "Network/GuLiNetworkSettings.h"
#include "GuLiNetTransport.h"

struct FGuLiTransportIdentity
{
    FGuid Player;
    FString Endpoint;
    FString ProductUserId;
    FString IdentityToken;
};
struct FGuLiTransportByteCounters
{
    uint64 SentBytes=0,ReceivedBytes=0,SentPackets=0,ReceivedPackets=0;
    uint64 SendFailures=0,RejectedPackets=0;
    uint64 ReceiveQueueOverflows=0,ExpiredReceivePackets=0,CoalescedReceivePackets=0;
    uint64 QueuedReceiveBytes=0,QueuedReceivePackets=0;
    double OldestReceiveSeconds=0;
    uint64 QueuedHandoffReceivePackets=0,QueuedHandoffReceiveBytes=0;
    double OldestHandoffReceiveSeconds=0;
};

class IGuLiHybridTransport
{
public:
    using FReceive = TFunction<void(const FGuid&, const TArray<uint8>&)>;
    using FReady = TFunction<void(const FGuLiTransportIdentity&)>;
    using FFailure = TFunction<void(const FString&)>;
    virtual ~IGuLiHybridTransport() = default;
    virtual void Start(const FGuLiNetworkLaunchConfig& Config, FReady Ready, FFailure Failure) = 0;
    virtual void SetPeers(const TMap<FGuid, FString>& Peers) = 0;
    virtual bool Send(const FGuid& Peer, TConstArrayView<uint8> Packet) = 0;
    virtual void Poll(const FReceive& Receive) = 0;
    virtual void Stop() = 0;
    virtual FGuLiTransportByteCounters GetByteCounters() const = 0;
};
TSharedRef<IGuLiHybridTransport> MakeGuLiLocalUdpTransport();
TSharedRef<IGuLiHybridTransport> MakeGuLiEosTransport();
