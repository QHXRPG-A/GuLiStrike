#pragma once
#include "GuLiNetProtocol.h"
#include <functional>
#include <tuple>

namespace GuLiNet
{
struct QueueLimits
{
    std::array<double, 4> BytesPerSecond{32000, 160000, 32000, 1024*1024};
    std::size_t MaxQueuedBytes = 16 * 1024 * 1024;
    std::size_t MaxPendingMessages = 4096;
    std::size_t MaxReassemblyBytes = 16 * 1024 * 1024;
    std::size_t MaxPartialMessages = 64;
    // Complete application messages, not datagrams. A 64-message window
    // serializes a high-rate stream behind a slow game thread's ACK batches.
    // Byte budgets and queue/reassembly limits still bound traffic and memory;
    // this remains below the receiver's 4096-message ordering window.
    std::uint64_t ReliableSendWindow = 512;
    double ResendSeconds = 0.3;
    double ReliableTimeoutSeconds = 15;
    bool CompactPeerEnvelope = true;
};
struct TransportMetrics
{
    std::uint64_t SentBytes = 0;
    std::uint64_t ReceivedBytes = 0;
    std::uint64_t RetransmitBytes = 0;
    std::uint64_t ReplacedStates = 0;
    std::uint64_t RejectedPackets = 0;
    std::uint64_t DuplicatePackets = 0, OutOfOrderMessages = 0;
    std::array<std::uint64_t,4> SentLaneBytes{}, ReceivedLaneBytes{};
    std::array<std::size_t,4> QueuedLaneBytes{},QueuedLaneMessages{};
    std::array<double,4> OldestLaneSeconds{},OldestPayloadSeconds{};
    std::size_t QueuedBytes = 0;
    double OldestSeconds = 0;
    std::uint64_t DeliveryBackpressure = 0;
    std::size_t PendingDeliveryBytes = 0, PendingDeliveryMessages = 0;
    struct MessageBytes {std::uint64_t Messages=0,Canonical=0,Encoded=0,Body=0;};
    // First successful transmission, not queue admission. Both sizes refer to
    // the identical logical message, excluding fragments, retries and ACKs.
    std::array<MessageBytes,static_cast<std::size_t>(MessageKind::ResourceSnapshotInstalled)+1> Encodings{};
};

// Read-only diagnostics use the same packet decoder as reliable delivery.
GULINETCORE_API bool InspectDatagramLane(std::span<const std::uint8_t> Packet,Lane& Out);
// Only complete one-fragment application messages; acknowledgments and partial
// fragments cannot prove that an application header was actually transmitted.
GULINETCORE_API bool InspectDatagramMessage(std::span<const std::uint8_t> Packet,Message& Out,PeerMessageContext Context={});

// Immutable authenticated roster binding for this connection key. Omitted
// identities are reconstructed only after that exact key has been verified.
struct PeerChannelBinding {Id Match,LocalPlayer,RemotePlayer;};

// One authenticated directed connection. The adapter binds the peer before calling Receive.
// Acks acknowledge complete reassembly, never a gameplay transaction.
class GULINETCORE_API PeerChannel
{
public:
    using SendPacket = std::function<bool(std::span<const std::uint8_t>)>;
    using Deliver = std::function<void(Message)>;
    // False retains a complete reliable message in the bounded ordering inbox.
    // ACK means reassembled; ReceivedThrough advances only after application admission.
    using TryDeliver = std::function<bool(const Message&)>;
    explicit PeerChannel(Id ConnectionKey, QueueLimits Limits = {},PeerChannelBinding Binding={});
    bool Queue(const Message& Value, double NowSeconds);
    bool CanQueueHandoff() const;
    bool Receive(std::span<const std::uint8_t> Packet, double NowSeconds, const SendPacket& Ack, const Deliver& Apply);
    bool ReceiveWithAdmission(std::span<const std::uint8_t> Packet,double NowSeconds,const SendPacket& Ack,const TryDeliver& Apply);
    void DrainDeliveries(const TryDeliver& Apply);
    void Pump(double NowSeconds, const SendPacket& Send);
    bool HasFailed() const { return !Failure.empty(); }
    const std::string& Error() const { return Failure; }
    const TransportMetrics& Metrics() const { return Counters; }
    std::vector<Message> Unacknowledged() const;
    std::uint64_t SentThrough(Lane Stream) const { return NextSequence[static_cast<std::size_t>(Stream)] - 1; }
    // A reliable queue admission is not a transmission. This becomes true
    // after every fragment's first successful send, without waiting for ACKs.
    bool HasTransmittedReliable(Lane Stream,std::uint64_t Sequence) const;
    std::uint64_t ReceivedThrough(Lane Stream) const { return ReliableThrough[static_cast<std::size_t>(Stream)]; }
private:
    using Key = std::pair<Lane, std::uint64_t>;
    struct Outbound
    {
        Message Value;
        Bytes Wire;
        double Created = 0;
        double NextAttempt = 0;
        std::uint16_t NextFragment = 0;
        std::uint32_t Attempts = 0;
        double FirstQueued = 0;
        std::uint64_t ServiceOrder = 0;
    };
    struct Inbound
    {
        double Created = 0;
        bool Reliable = false;
        std::uint32_t TotalBytes = 0;
        std::vector<Bytes> Fragments;
        std::size_t Received = 0;
        std::size_t Count = 0;
    };
    Id Connection;
    const PeerChannelBinding Binding;
    QueueLimits Budget;
    TransportMetrics Counters;
    std::map<Key, Outbound> Pending;
    using StateQueueKey=std::tuple<Lane,MessageKind,ObjectKey>;
    // Exactly one not-yet-started state per object/kind. A fragmented sample
    // already in flight finishes independently of its newest waiting successor.
    std::map<StateQueueKey,Key> WaitingStates;
    std::map<Key, Inbound> Partial;
    std::map<Key,std::pair<Message,std::size_t>> Ordered;
    std::size_t OrderedBytes=0;
    std::array<std::uint64_t, 4> NextSequence{1, 1, 1, 1};
    std::array<SequenceWindow, 4> Completed;
    std::array<std::uint64_t, 4> ReliableThrough{};
    std::array<std::set<std::uint64_t>, 4> ReliableBeyond;
    std::array<double, 4> Tokens{};
    double LastPump = -1;
    std::size_t ReassemblyReserved = 0;
    std::string Failure;
};
}
