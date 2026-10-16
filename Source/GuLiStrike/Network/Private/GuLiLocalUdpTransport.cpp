#include "Network/Private/GuLiHybridTransport.h"
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "IPAddress.h"
#include "Common/UdpSocketReceiver.h"
#include "Containers/Queue.h"
#include "Misc/ScopeLock.h"
#include <set>

namespace
{
class FGuLiLocalUdpTransport final : public IGuLiHybridTransport
{
public:
    virtual ~FGuLiLocalUdpTransport() override { Stop(); }
    virtual void Start(const FGuLiNetworkLaunchConfig& Config, FReady Ready, FFailure Failure) override
    {
        Stop();
        { FScopeLock Lock(&CounterMutex); Counters={}; QueuedMotionPackets=0; }
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
            GuLiNet::Lane Lane;
            const bool Parsed=GuLiNet::InspectDatagramLane({Bytes->GetData(),size_t(Bytes->Num())},Lane);
            const bool Motion=Parsed && Lane==GuLiNet::Lane::Motion;
            const bool Handoff=Parsed && Lane==GuLiNet::Lane::Handoff;
            const FQueuedPacket Packet{Bytes,Source,Now};
            // Keep one exact datagram per endpoint while it waits for parsing.
            // No ACK is sent here; PeerChannel validates and acknowledges it.
            if (!Motion && PendingReliable.contains(Packet))
            { ++Counters.CoalescedReceivePackets; return; }
            if (Counters.QueuedReceivePackets>=MaxReceivePackets || Counters.QueuedReceiveBytes+Bytes->Num()>MaxReceiveBytes
                || (Motion && QueuedMotionPackets>=MaxMotionPackets)
                || (!Handoff && (Counters.QueuedReceivePackets-Counters.QueuedHandoffReceivePackets>=MaxReceivePackets*3/4
                    || Counters.QueuedReceiveBytes-Counters.QueuedHandoffReceiveBytes+Bytes->Num()>MaxReceiveBytes*3/4)))
            { ++Counters.ReceiveQueueOverflows; return; }
            ++Counters.QueuedReceivePackets; Counters.QueuedReceiveBytes+=Bytes->Num();
            if (Handoff)
            {++Counters.QueuedHandoffReceivePackets;Counters.QueuedHandoffReceiveBytes+=Bytes->Num();PendingReliable.insert(Packet);HandoffInbox.Enqueue(Packet);}
            else if (Motion) {++QueuedMotionPackets;MotionInbox.Enqueue({Bytes,Source,Now});}
            else {PendingReliable.insert(Packet);ReliableInbox.Enqueue(Packet);}
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
        // Inbox capacity is a storage limit. Processing the entire capacity in
        // one frame can starve simulation and local input under sustained load.
        // Preserve FIFO within each class. Reliable ACK/control/handoff traffic
        // gets a turn before replaceable state, while state keeps a fixed share.
        uint64 Count=0;
        const auto Drain=[&](auto& Inbox,bool Motion,bool Handoff,uint64 Limit)
        {
          FQueuedPacket Packet;
          for (; Count<Limit && Inbox.Dequeue(Packet); ++Count)
          {
            const bool Expired=FPlatformTime::Seconds()-Packet.At>15;
            {
                FScopeLock Lock(&CounterMutex);
                --Counters.QueuedReceivePackets; Counters.QueuedReceiveBytes-=Packet.Bytes->Num();
                if (Motion) --QueuedMotionPackets;else PendingReliable.erase(Packet);
                if (Handoff) {--Counters.QueuedHandoffReceivePackets;Counters.QueuedHandoffReceiveBytes-=Packet.Bytes->Num();}
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
        };
        // Private transfer fragments and their ACKs have five-second work
        // deadlines. Keep them out of the bulk publication FIFO while leaving
        // a fixed share for control and replaceable motion in the same budget.
        Drain(HandoffInbox,false,true,MaxPollPackets/2);
        Drain(ReliableInbox,false,false,MaxPollPackets*3/4);
        Drain(MotionInbox,true,false,MaxPollPackets);
        Drain(HandoffInbox,false,true,MaxPollPackets);
        Drain(ReliableInbox,false,false,MaxPollPackets);
    }
    virtual void Stop() override
    {
        if (Receiver) { Receiver->Stop(); Receiver.Reset(); }
        ReliableInbox.Empty();HandoffInbox.Empty(); MotionInbox.Empty(); ReceiveFailure={}; Peers.Reset();
        { FScopeLock Lock(&CounterMutex); Counters.QueuedReceiveBytes=0; Counters.QueuedReceivePackets=0;
          Counters.QueuedHandoffReceivePackets=0;Counters.QueuedHandoffReceiveBytes=0;QueuedMotionPackets=0;PendingReliable.clear(); }
        if (Socket) { Socket->Close(); ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(Socket); Socket = nullptr; }
    }
    virtual FGuLiTransportByteCounters GetByteCounters() const override
    {
        FScopeLock Lock(&CounterMutex); auto Out=Counters;
        FQueuedPacket Packet;double Oldest=FPlatformTime::Seconds();bool Waiting=false;
        if (ReliableInbox.Peek(Packet)) {Oldest=FMath::Min(Oldest,Packet.At);Waiting=true;}
        if (MotionInbox.Peek(Packet)) {Oldest=FMath::Min(Oldest,Packet.At);Waiting=true;}
        if (HandoffInbox.Peek(Packet))
        {Oldest=FMath::Min(Oldest,Packet.At);Waiting=true;Out.OldestHandoffReceiveSeconds=FMath::Max(0.,FPlatformTime::Seconds()-Packet.At);}
        if (Waiting) Out.OldestReceiveSeconds=FMath::Max(0.,FPlatformTime::Seconds()-Oldest);
        return Out;
    }
private:
    // The worker drains the socket independently of rendering. Identity binding,
    // decoding and all gameplay callbacks remain on the owning game thread.
    struct FQueuedPacket { FArrayReaderPtr Bytes; FIPv4Endpoint Source; double At=0; };
    struct FQueuedPacketLess
    {
        bool operator()(const FQueuedPacket& A,const FQueuedPacket& B) const
        {
            if (A.Source.Address.Value!=B.Source.Address.Value) return A.Source.Address.Value<B.Source.Address.Value;
            if (A.Source.Port!=B.Source.Port) return A.Source.Port<B.Source.Port;
            if (A.Bytes->Num()!=B.Bytes->Num()) return A.Bytes->Num()<B.Bytes->Num();
            return FMemory::Memcmp(A.Bytes->GetData(),B.Bytes->GetData(),A.Bytes->Num())<0;
        }
    };
    // Shared payload references add no second datagram body. Membership is
    // bounded by the existing inbox and guarded by CounterMutex on both threads.
    std::set<FQueuedPacket,FQueuedPacketLess> PendingReliable;
    static constexpr uint64 MaxReceivePackets=16384,MaxReceiveBytes=16*1024*1024;
    static constexpr uint64 MaxPollPackets=2048;
    static constexpr uint64 MaxMotionPackets=MaxReceivePackets/4;
    mutable FCriticalSection CounterMutex;
    FGuLiTransportByteCounters Counters;
    TUniquePtr<FUdpSocketReceiver> Receiver;
    uint64 QueuedMotionPackets=0;
    TQueue<FQueuedPacket,EQueueMode::Spsc> ReliableInbox,HandoffInbox,MotionInbox;
    FFailure ReceiveFailure;
    FSocket* Socket = nullptr;
    TMap<FGuid, TSharedRef<FInternetAddr>> Peers;
};
}
TSharedRef<IGuLiHybridTransport> MakeGuLiLocalUdpTransport() { return MakeShared<FGuLiLocalUdpTransport>(); }
