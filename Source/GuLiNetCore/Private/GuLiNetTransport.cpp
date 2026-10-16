#include "GuLiNetTransport.h"
#include "GuLiNetTrace.h"
#include <algorithm>
#include <cmath>
#include <utility>

namespace GuLiNet
{
namespace
{
constexpr std::size_t HeaderBytes = 42;
constexpr std::size_t FragmentBytes = MaxDatagramBytes - HeaderBytes;
constexpr std::uint8_t ReliableFlag = 1;
constexpr std::uint8_t AckFlag = 2;
struct PacketHeader
{
    Lane Stream = Lane::Control;
    std::uint8_t Flags = 0;
    Id Connection;
    std::uint64_t Sequence = 0;
    std::uint16_t Index = 0;
    std::uint16_t Count = 0;
    std::uint32_t Total = 0;
};
Bytes MakePacket(const PacketHeader& H, std::span<const std::uint8_t> Body)
{
    Writer W(MaxDatagramBytes); W.U32(0x474c504b); W.U16(ProtocolVersion);
    W.U8(static_cast<std::uint8_t>(H.Stream)); W.U8(H.Flags); W.Identity(H.Connection);
    W.U64(H.Sequence); W.U16(H.Index); W.U16(H.Count); W.U32(H.Total);
    W.U16(static_cast<std::uint16_t>(Body.size())); W.Raw(Body); return W.Take();
}
bool ReadPacket(std::span<const std::uint8_t> Wire, PacketHeader& H, std::span<const std::uint8_t>& Body)
{
    if (Wire.size() < HeaderBytes || Wire.size() > MaxDatagramBytes) return false;
    Reader R(Wire); std::uint32_t Magic; std::uint16_t Version, Size; std::uint8_t LaneValue;
    if (!R.U32(Magic) || Magic != 0x474c504b || !R.U16(Version) || Version != ProtocolVersion
        || !R.U8(LaneValue) || LaneValue > 3 || !R.U8(H.Flags) || (H.Flags & ~3u)
        || !R.Identity(H.Connection) || !R.U64(H.Sequence) || !H.Sequence || !R.U16(H.Index)
        || !R.U16(H.Count) || !R.U32(H.Total) || !R.U16(Size) || !R.Raw(Size, Body) || !R.Done()) return false;
    H.Stream = static_cast<Lane>(LaneValue);
    if (H.Flags & AckFlag) return H.Flags == AckFlag && H.Index == 0 && H.Count == 0 && H.Total == 0 && Size == 0;
    if (!H.Total || H.Total > MaxMessageBytes || !H.Count || H.Index >= H.Count
        || H.Count != (H.Total + FragmentBytes - 1) / FragmentBytes) return false;
    const auto Expected = std::min(FragmentBytes, H.Total - static_cast<std::size_t>(H.Index) * FragmentBytes);
    return Size == Expected;
}
}
PeerChannel::PeerChannel(Id ConnectionKey, QueueLimits Limits) : Connection(ConnectionKey), Budget(Limits)
{
    if (!Connection.Valid() || Budget.ResendSeconds <= 0 || Budget.ReliableTimeoutSeconds <= 0)
        Failure = "Invalid peer channel configuration";
    for (std::size_t I = 0; I < Tokens.size(); ++I)
    {
        if (!std::isfinite(Budget.BytesPerSecond[I]) || Budget.BytesPerSecond[I] <= 0)
            Failure = "Invalid lane budget";
        Tokens[I] = std::max<double>(MaxDatagramBytes, Budget.BytesPerSecond[I] / 10);
    }
}
bool InspectDatagramLane(std::span<const std::uint8_t> Packet,Lane& Out)
{
    PacketHeader H;std::span<const std::uint8_t> Body;
    if (!ReadPacket(Packet,H,Body)) return false;Out=H.Stream;return true;
}
bool InspectDatagramMessage(std::span<const std::uint8_t> Packet,Message& Out)
{
    PacketHeader H;std::span<const std::uint8_t> Body;
    return ReadPacket(Packet,H,Body) && !(H.Flags&AckFlag) && H.Count==1 && H.Index==0
        && H.Total==Body.size() && DecodeMessage(Body,Out);
}
bool PeerChannel::Queue(const Message& Value, double Now)
{
    TraceScope Scope("GuLiHybrid_QueuePeerMessage");
    if (HasFailed() || !std::isfinite(Now)) return false;
    if (Value.Header.Kind==MessageKind::HandoffState && !CanQueueHandoff()) return false;
    const Lane Stream = MessageLane(Value.Header.Kind);
    const bool Reliable = ReliableMessage(Value.Header.Kind);
    auto& Sequence = NextSequence[static_cast<std::size_t>(Stream)];
    if (Sequence == UINT64_MAX) { Failure = "Peer stream sequence exhausted"; return false; }
    Message Sequenced = Value; Sequenced.Header.Sequence = Sequence;
    Bytes Wire; if (!EncodeMessage(Sequenced, Wire)) return false;
    double FirstQueued=Now; std::uint64_t ServiceOrder=Sequence;
    const StateQueueKey StateKey{Stream,Value.Header.Kind,Value.Header.Object};
    if (!Reliable)
    {
        // Replace waiting state, retaining its turn. Finish an already-started
        // fragmented sample before the one newest sample waiting behind it.
        if (const auto Waiting=WaitingStates.find(StateKey);Waiting!=WaitingStates.end())
        {
            if (const auto It=Pending.find(Waiting->second);It!=Pending.end())
            {
                FirstQueued=std::min(FirstQueued,It->second.FirstQueued); ServiceOrder=std::min(ServiceOrder,It->second.ServiceOrder);
                Counters.QueuedBytes-=It->second.Wire.size();Pending.erase(It);++Counters.ReplacedStates;
            }
            WaitingStates.erase(Waiting);
        }
    }
    if (Pending.size()>=Budget.MaxPendingMessages
        || Wire.size() > Budget.MaxQueuedBytes - std::min(Budget.MaxQueuedBytes, Counters.QueuedBytes))
    {
        return false;
    }
    Counters.QueuedBytes += Wire.size();
    const Key Queued{Stream,Sequence++};
    Pending.emplace(Queued, Outbound{std::move(Sequenced), std::move(Wire), Now, Now, 0, 0,FirstQueued,ServiceOrder});
    if (!Reliable) WaitingStates.insert_or_assign(StateKey,Queued);
    return true;
}
bool PeerChannel::CanQueueHandoff() const
{
    // Admission precedes the object's freeze. Keep at most a quarter-second
    // of configured lane capacity awaiting its first transmission; one larger
    // snapshot may enter an empty lane. Reliable storage awaiting ACK remains
    // bounded by the unchanged total queue and reliable send window.
    const double WindowBytes=std::max<double>(MaxDatagramBytes,Budget.BytesPerSecond[static_cast<std::size_t>(Lane::Handoff)]*.25);
    std::size_t Bytes=0;
    for (const auto& [K,V]:Pending) if (K.first==Lane::Handoff && !V.Attempts)
    {
        const auto Sent=std::min(V.Wire.size(),std::size_t(V.NextFragment)*FragmentBytes);
        Bytes+=V.Wire.size()-Sent;if (Bytes>=WindowBytes) return false;
    }
    return !HasFailed() && Pending.size()<Budget.MaxPendingMessages && Counters.QueuedBytes<Budget.MaxQueuedBytes;
}
bool PeerChannel::Receive(std::span<const std::uint8_t> Wire, double Now, const SendPacket& Ack, const Deliver& Apply)
{
    Counters.ReceivedBytes += Wire.size();
    PacketHeader H; std::span<const std::uint8_t> Body;
    if (HasFailed() || !std::isfinite(Now) || !ReadPacket(Wire, H, Body) || H.Connection != Connection)
    { ++Counters.RejectedPackets; return false; }
    Counters.ReceivedLaneBytes[static_cast<std::size_t>(H.Stream)]+=Wire.size();
    const Key K{H.Stream, H.Sequence};
    if (H.Flags & AckFlag)
    {
        if (auto It = Pending.find(K); It != Pending.end() && ReliableMessage(It->second.Value.Header.Kind))
        { Counters.QueuedBytes -= It->second.Wire.size(); Pending.erase(It); }
        return true;
    }
    auto SendAck = [&]()
    {
        if (!(H.Flags & ReliableFlag)) return;
        const auto BytesToSend = MakePacket({H.Stream, AckFlag, Connection, H.Sequence, 0, 0, 0}, {});
        if (Ack(BytesToSend))
        { Counters.SentBytes+=BytesToSend.size(); Counters.SentLaneBytes[static_cast<std::size_t>(H.Stream)]+=BytesToSend.size(); }
    };
    const auto LaneIndex = static_cast<std::size_t>(H.Stream);
    const bool IsReliable = (H.Flags & ReliableFlag) != 0;
    auto& Through = ReliableThrough[LaneIndex];
    auto& Beyond = ReliableBeyond[LaneIndex];
    auto& Window = Completed[LaneIndex];
    if (IsReliable && (H.Sequence <= Through || Beyond.contains(H.Sequence))) { ++Counters.DuplicatePackets;SendAck(); return true; }
    // Keep holes until received. Advancing a highest-seen floor would lose an old reliable message.
    if (IsReliable && H.Sequence - Through > 4096) { ++Counters.RejectedPackets; return false; }
    const auto Verdict = IsReliable ? SequenceWindow::Verdict::Fresh : Window.Inspect(H.Sequence);
    if (Verdict == SequenceWindow::Verdict::Duplicate) { ++Counters.DuplicatePackets;SendAck(); return true; }
    if (Verdict != SequenceWindow::Verdict::Fresh) { ++Counters.RejectedPackets; return false; }
    auto It = Partial.find(K);
    if (It == Partial.end())
    {
        if (Partial.size() >= Budget.MaxPartialMessages || H.Total > Budget.MaxReassemblyBytes - ReassemblyReserved)
        { ++Counters.RejectedPackets; return false; }
        Inbound In; In.Created = Now; In.Reliable = (H.Flags & ReliableFlag) != 0; In.TotalBytes = H.Total;
        In.Fragments.resize(H.Count); ReassemblyReserved += H.Total;
        It = Partial.emplace(K, std::move(In)).first;
    }
    auto& In = It->second;
    if (In.TotalBytes != H.Total || In.Fragments.size() != H.Count || In.Reliable != ((H.Flags & ReliableFlag) != 0))
    { ++Counters.RejectedPackets; return false; }
    auto& Fragment = In.Fragments[H.Index];
    if (Fragment.empty()) { Fragment.assign(Body.begin(), Body.end()); In.Received += Body.size(); ++In.Count; }
    else if (Fragment.size() != Body.size() || !std::equal(Fragment.begin(), Fragment.end(), Body.begin()))
    { ++Counters.RejectedPackets; return false; }
    else ++Counters.DuplicatePackets;
    if (In.Count != In.Fragments.size()) return true;
    Bytes Complete; Complete.reserve(In.TotalBytes);
    for (const auto& Part : In.Fragments) Complete.insert(Complete.end(), Part.begin(), Part.end());
    Message Value;
    const bool Decoded = DecodeMessage(Complete, Value) && MessageLane(Value.Header.Kind) == H.Stream
        && ReliableMessage(Value.Header.Kind) == In.Reliable && Value.Header.Sequence == H.Sequence;
    ReassemblyReserved -= In.TotalBytes; Partial.erase(It);
    if (!Decoded) { ++Counters.RejectedPackets; return false; }
    if (IsReliable)
    {
        if (Complete.size()>Budget.MaxReassemblyBytes-std::min(Budget.MaxReassemblyBytes,OrderedBytes))
        {++Counters.RejectedPackets;return false;}
        if (H.Sequence>Through+1) ++Counters.OutOfOrderMessages;
        OrderedBytes+=Complete.size();Ordered.emplace(K,std::pair{std::move(Value),Complete.size()});
        Beyond.insert(H.Sequence);
        SendAck();
        while (Through!=UINT64_MAX)
        {
            const auto Next=Ordered.find({H.Stream,Through+1});if (Next==Ordered.end()) break;
            auto Delivered=std::move(Next->second.first);OrderedBytes-=Next->second.second;Ordered.erase(Next);
            Beyond.erase(++Through);Apply(std::move(Delivered));
        }
        return true;
    }
    else Window.Observe(H.Sequence);
    SendAck(); Apply(std::move(Value)); return true;
}
void PeerChannel::Pump(double Now, const SendPacket& Send)
{
    TraceScope Scope("GuLiHybrid_PumpPeerPackets");
    if (HasFailed() || !std::isfinite(Now)) return;
    const double Delta = LastPump < 0 ? 0 : std::clamp(Now - LastPump, 0.0, 1.0);
    LastPump = Now; Counters.OldestSeconds = 0;
    Counters.QueuedLaneBytes={};Counters.QueuedLaneMessages={};Counters.OldestLaneSeconds={};Counters.OldestPayloadSeconds={};
    for (std::size_t I = 0; I != Tokens.size(); ++I)
        Tokens[I] = std::min(std::max<double>(MaxDatagramBytes, Budget.BytesPerSecond[I]), Tokens[I] + Delta * Budget.BytesPerSecond[I]);
    for (auto It = Partial.begin(); It != Partial.end();)
    {
        if (Now - It->second.Created > (It->second.Reliable ? Budget.ReliableTimeoutSeconds : 0.5))
        {
            ReassemblyReserved -= It->second.TotalBytes; It = Partial.erase(It);
        }
        else ++It;
    }
    struct Service {Key Identity;double Due;std::uint64_t Order;};
    std::vector<Service> Order; Order.reserve(Pending.size());
    for (const auto& [K,V]:Pending) Order.push_back({K,K.first==Lane::Motion?V.FirstQueued:V.NextAttempt,V.ServiceOrder});
    std::sort(Order.begin(),Order.end(),[](const Service& A,const Service& B)
    {
        if (A.Identity.first!=B.Identity.first) return A.Identity.first<B.Identity.first;
        // Service the oldest due attempt, including first transmissions. Sorting
        // only by sequence lets a slow ACK consume the entire lane with retries
        // while later requests never receive their first transmission.
        return std::tie(A.Due,A.Order)<std::tie(B.Due,B.Order);
    });
    std::array<std::uint64_t,4> FirstUnacknowledged{};
    for (const auto& [K,V]:Pending) if (ReliableMessage(V.Value.Header.Kind))
    {auto& First=FirstUnacknowledged[static_cast<std::size_t>(K.first)];if (!First) First=K.second;}
    std::array<bool,4> LaneBlocked{};
    for (const auto& Entry:Order)
    {
        const auto& K=Entry.Identity;
        auto It=Pending.find(K); if (It==Pending.end()) continue;
        auto& Value = It->second; const bool Reliable = ReliableMessage(Value.Value.Header.Kind);
        auto UnindexState=[&]
        {
            if (Reliable) return;
            const auto I=WaitingStates.find({K.first,Value.Value.Header.Kind,Value.Value.Header.Object});
            if (I!=WaitingStates.end() && I->second==K) WaitingStates.erase(I);
        };
        if (!Reliable && Now - Value.Created > 0.5)
        { UnindexState();Counters.QueuedBytes -= Value.Wire.size(); Pending.erase(It); continue; }
        const auto LaneIndex=static_cast<std::size_t>(K.first);
        Counters.OldestSeconds=std::max(Counters.OldestSeconds,Now-Value.FirstQueued);
        Counters.QueuedLaneBytes[LaneIndex]+=Value.Wire.size();++Counters.QueuedLaneMessages[LaneIndex];
        Counters.OldestLaneSeconds[LaneIndex]=std::max(Counters.OldestLaneSeconds[LaneIndex],Now-Value.FirstQueued);
        Counters.OldestPayloadSeconds[LaneIndex]=std::max(Counters.OldestPayloadSeconds[LaneIndex],Now-Value.Created);
        // Completed out-of-order packets can be ACKed while an earlier packet
        // is missing. Bound new sends by that oldest hole, not queue length.
        if (Reliable && K.second-FirstUnacknowledged[LaneIndex]>=std::clamp<std::uint64_t>(Budget.ReliableSendWindow,1,4096)) continue;
        if (Now < Value.NextAttempt || LaneBlocked[LaneIndex]) continue;
        auto& Available = Tokens[static_cast<std::size_t>(It->first.first)];
        const auto Count = static_cast<std::uint16_t>((Value.Wire.size() + FragmentBytes - 1) / FragmentBytes);
        while (Value.NextFragment < Count)
        {
            const std::size_t Offset = static_cast<std::size_t>(Value.NextFragment) * FragmentBytes;
            const std::size_t Size = std::min(FragmentBytes, Value.Wire.size() - Offset);
            if (Available < Size + HeaderBytes)
            {
                // Let tokens accumulate for the oldest sample; smaller newer
                // packets must not consume its share on every pump.
                if (!Reliable) LaneBlocked[static_cast<std::size_t>(K.first)]=true;
                break;
            }
            const Bytes Packet = MakePacket({It->first.first, Reliable ? ReliableFlag : std::uint8_t{0},
                Connection, It->first.second, Value.NextFragment, Count, static_cast<std::uint32_t>(Value.Wire.size())},
                std::span<const std::uint8_t>(Value.Wire).subspan(Offset, Size));
            if (!Send(Packet)) break;
            if (!Value.NextFragment) UnindexState();
            Available -= Packet.size(); Counters.SentBytes += Packet.size();
            Counters.SentLaneBytes[static_cast<std::size_t>(It->first.first)]+=Packet.size();
            if (Value.Attempts) Counters.RetransmitBytes += Packet.size();
            ++Value.NextFragment;
        }
        if (Value.NextFragment == Count)
        {
            if (!Reliable) { Counters.QueuedBytes -= Value.Wire.size(); Pending.erase(It); continue; }
            ++Value.Attempts; Value.NextFragment = 0;
            const auto Backoff=std::min<std::uint32_t>(Value.Attempts-1,3);
            Value.NextAttempt=Now+Budget.ResendSeconds*double(std::uint32_t{1}<<Backoff);
        }
    }
}
std::vector<Message> PeerChannel::Unacknowledged() const
{
    std::vector<Message> Out;
    for (const auto& [KeyValue, Value] : Pending)
    { (void)KeyValue; if (ReliableMessage(Value.Value.Header.Kind)) Out.push_back(Value.Value); }
    return Out;
}
bool PeerChannel::HasTransmittedReliable(Lane Stream,std::uint64_t Sequence) const
{
    const auto Index=static_cast<std::size_t>(Stream);
    if (HasFailed() || Index>=NextSequence.size() || Stream==Lane::Motion
        || !Sequence || Sequence>=NextSequence[Index]) return false;
    const auto I=Pending.find({Stream,Sequence});
    // Reliable entries leave Pending only after complete reassembly is ACKed.
    return I==Pending.end() || I->second.Attempts>0;
}
}
