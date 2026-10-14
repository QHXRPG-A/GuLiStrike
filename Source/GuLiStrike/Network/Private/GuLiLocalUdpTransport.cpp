#include "Network/Private/GuLiHybridTransport.h"
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "IPAddress.h"
#include "Common/UdpSocketReceiver.h"
#include "Containers/Queue.h"
#include "Misc/ScopeLock.h"

namespace
{
class FGuLiLocalUdpTransport final : public IGuLiHybridTransport
{
public:
    virtual ~FGuLiLocalUdpTransport() override { Stop(); }
    virtual void Start(const FGuLiNetworkLaunchConfig& Config, FReady Ready, FFailure Failure) override
    {
        Stop();
        { FScopeLock Lock(&CounterMutex); Counters={}; }
        ReceiveFailure=MoveTemp(Failure);
        if (Config.LocalUdpPort < 0 || Config.LocalUdpPort > 65535)
        { ReceiveFailure(TEXT("LocalUdp port is out of range")); return; }
        auto* System = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
        Socket = System->CreateSocket(NAME_DGram, TEXT("GuLiHybridLocal"), false);
        const auto Address = System->CreateInternetAddr(); bool bValid = false;
        Address->SetIp(TEXT("127.0.0.1"), bValid); Address->SetPort(Config.LocalUdpPort);
        if (!Socket || !bValid || !Socket->SetNonBlocking(true) || !Socket->Bind(*Address))
        { auto Callback=MoveTemp(ReceiveFailure); Stop(); Callback(TEXT("Cannot bind LocalUdp loopback socket")); return; }
        int32 ActualSize = 0; Socket->SetReceiveBufferSize(1024 * 1024, ActualSize); Socket->SetSendBufferSize(1024 * 1024, ActualSize);
        Socket->GetAddress(*Address);
        Receiver=MakeUnique<FUdpSocketReceiver>(Socket,FTimespan::FromMilliseconds(5),TEXT("GuLiLocalUdpReceive"));
        Receiver->OnDataReceived().BindLambda([this](const FArrayReaderPtr& Bytes,const FIPv4Endpoint& Source)
        {
            const double Now=FPlatformTime::Seconds();
            FScopeLock Lock(&CounterMutex);
            Counters.ReceivedBytes+=Bytes->Num(); ++Counters.ReceivedPackets;
            if (Bytes->IsEmpty() || Bytes->Num()>static_cast<int32>(GuLiNet::MaxDatagramBytes))
            { ++Counters.RejectedPackets; return; }
            if (Counters.QueuedReceivePackets>=MaxReceivePackets || Counters.QueuedReceiveBytes+Bytes->Num()>MaxReceiveBytes)
            { ++Counters.ReceiveQueueOverflows; return; }
            ++Counters.QueuedReceivePackets; Counters.QueuedReceiveBytes+=Bytes->Num();
            Inbox.Enqueue({Bytes,Source,Now});
        });
        Receiver->Start();
        FGuLiTransportIdentity Identity; Identity.Player = Config.DevelopmentPlayerId.IsValid() ? Config.DevelopmentPlayerId : FGuid::NewGuid();
        Identity.Endpoint = Address->ToString(true); Ready(Identity);
    }
    virtual void SetPeers(const TMap<FGuid, FString>& Values) override
    {
        Peers.Reset(); auto* System = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
        for (const auto& Pair : Values)
        {
            FString Host, Port; if (!Pair.Value.Split(TEXT(":"), &Host, &Port, ESearchCase::CaseSensitive, ESearchDir::FromEnd)
                || Host != TEXT("127.0.0.1") || !Port.IsNumeric()) continue;
            const int32 PortValue = FCString::Atoi(*Port); if (PortValue < 1 || PortValue > 65535) continue;
            auto Address = System->CreateInternetAddr(); bool bValid = false;
            Address->SetIp(*Host, bValid); Address->SetPort(PortValue); if (bValid) Peers.Add(Pair.Key, Address);
        }
    }
    virtual bool Send(const FGuid& Peer, TConstArrayView<uint8> Packet) override
    {
        const auto* Address = Peers.Find(Peer); int32 Sent = 0;
        const bool Success=Socket && Address && Packet.Num()>0 && Packet.Num()<=static_cast<int32>(GuLiNet::MaxDatagramBytes)
            && Socket->SendTo(Packet.GetData(),Packet.Num(),Sent,**Address);
        {
            FScopeLock Lock(&CounterMutex);
            if (Sent>0) { Counters.SentBytes+=Sent; ++Counters.SentPackets; }
            if (!Success || Sent!=Packet.Num()) ++Counters.SendFailures;
        }
        return Success && Sent==Packet.Num();
    }
    virtual void Poll(const FReceive& Receive) override
    {
        if (!Socket) return;
        // The bounded UDP inbox can drop packets, like the OS receive buffer.
        // Continue draining after a burst: reliable fragments are retried by
        // PeerChannel, while later state samples replace stale motion. A lost
        // datagram is not a connection failure and cannot stop the whole world.
        FQueuedPacket Packet;
        for (uint64 Count=0; Count<MaxReceivePackets && Inbox.Dequeue(Packet); ++Count)
        {
            const bool Expired=FPlatformTime::Seconds()-Packet.At>15;
            {
                FScopeLock Lock(&CounterMutex);
                --Counters.QueuedReceivePackets; Counters.QueuedReceiveBytes-=Packet.Bytes->Num();
                if (Expired) ++Counters.ExpiredReceivePackets;
            }
            if (Expired)
            {
                continue;
            }
            const auto Source=Packet.Source.ToInternetAddr();
            for (const auto& Pair : Peers)
                if (*Pair.Value == *Source) { Receive(Pair.Key,*Packet.Bytes); break; }
        }
    }
    virtual void Stop() override
    {
        if (Receiver) { Receiver->Stop(); Receiver.Reset(); }
        Inbox.Empty(); ReceiveFailure={}; Peers.Reset();
        { FScopeLock Lock(&CounterMutex); Counters.QueuedReceiveBytes=0; Counters.QueuedReceivePackets=0; }
        if (Socket) { Socket->Close(); ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(Socket); Socket = nullptr; }
    }
    virtual FGuLiTransportByteCounters GetByteCounters() const override
    {
        FScopeLock Lock(&CounterMutex); auto Out=Counters;
        FQueuedPacket Oldest;
        if (Inbox.Peek(Oldest)) Out.OldestReceiveSeconds=FMath::Max(0.,FPlatformTime::Seconds()-Oldest.At);
        return Out;
    }
private:
    // The worker drains the socket independently of rendering. Identity binding,
    // decoding and all gameplay callbacks remain on the owning game thread.
    struct FQueuedPacket { FArrayReaderPtr Bytes; FIPv4Endpoint Source; double At=0; };
    static constexpr uint64 MaxReceivePackets=16384,MaxReceiveBytes=16*1024*1024;
    mutable FCriticalSection CounterMutex;
    FGuLiTransportByteCounters Counters;
    TUniquePtr<FUdpSocketReceiver> Receiver;
    TQueue<FQueuedPacket,EQueueMode::Spsc> Inbox;
    FFailure ReceiveFailure;
    FSocket* Socket = nullptr;
    TMap<FGuid, TSharedRef<FInternetAddr>> Peers;
};
}
TSharedRef<IGuLiHybridTransport> MakeGuLiLocalUdpTransport() { return MakeShared<FGuLiLocalUdpTransport>(); }
