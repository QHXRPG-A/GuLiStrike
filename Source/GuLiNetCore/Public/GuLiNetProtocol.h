#pragma once
#include "GuLiNetRules.h"
#include "GuLiNetMotionDelivery.h"
#include <array>
#include <memory>
#include <span>

namespace GuLiNet
{
enum class MessageKind : std::uint16_t
{
    Hello=1,Welcome,Error,Heartbeat,Roster,WorldReady,RegisterObject,
    AssignmentOffer,AssignmentReady,AssignmentCommit,Command,CommandReceipt,
    Motion,ObjectEvent,HitFact,Transaction,TransactionResult,StateFragments,
    LoadReport,MembershipBegin,MembershipReady,MembershipCommit,Leave,MatchResult,
    BootstrapRequest,BootstrapState,DirectoryComplete,MotionAcknowledgment,
    ComponentState,
    SelectionActivity,DirectOperation,HandoffPrepare,HandoffReady,HandoffFreeze,
    HandoffState,HandoffInstalled,HandoffCommit,HandoffCancel,HandoffCommands,
    WorkPrepare,WorkPrepared,WorkCommit,WorkReceipt,WorkCancel,WorkStateRequest,PeerStatus,CommandState,TransactionIntent,WorkState,
    ResourceGroups,ResourceSnapshot,ResourceSnapshotStored,ResourceSnapshotInstall,ResourceSnapshotInstalled,
    SharedStateEvent,
    // Keep last: exclusive bound for message validation and per-kind storage.
    // This sentinel is never transmitted and does not change existing wire IDs.
    Count
};
enum class Lane : std::uint8_t { Control,Motion,Effects,Handoff };

struct Envelope
{
    MessageKind Kind = MessageKind::Heartbeat;
    Id Match;
    Revision Epoch = 0;
    Id Sender;
    Id Event;
    ObjectKey Object;
    Revision OwnerEpoch = 0;
    Revision ControlEpoch = 0;
    std::uint64_t Sequence = 0;
    Tick At = 0;
    Id ResourceGroup;
    Revision ResourceEpoch = 0;
};
struct Message
{
    Envelope Header;Bytes Body;
    // Local measurement only: the same body's previous transport encoding size.
    // Not serialized, hashed, received or used for admission/queue budgets.
    std::size_t ReferenceBodyBytes=0;
    // Sender-local diagnostic carried with this exact queued sample. Never serialized.
    bool StationaryUnitPose=false;
    std::shared_ptr<MotionDelivery> PoseDelivery{};
};

// Explicit little-endian, bounded wire values; never memcpy native structs or UE archives.
class GULINETCORE_API Writer
{
public:
    explicit Writer(std::size_t Limit = MaxMessageBytes) : Maximum(Limit) {}
    void U8(std::uint8_t Value);
    void U16(std::uint16_t Value);
    void U32(std::uint32_t Value);
    void U64(std::uint64_t Value);
    void VarUnsigned(std::uint64_t Value);
    void F32(float Value);
    void F64(double Value);
    void Identity(const Id& Value);
    void Key(const ObjectKey& Value);
    void Text(const std::string& Value);
    void Blob(std::span<const std::uint8_t> Value);
    void Raw(std::span<const std::uint8_t> Value);
    bool Good() const { return Valid; }
    const Bytes& Data() const;
    // Borrow until the next mutation/destruction. Small encodings need no vector allocation.
    std::span<const std::uint8_t> View() const;
    // Consume encoded bytes while retaining the writer's existing failure state.
    void Clear();
    Bytes Take();
private:
    void AppendUnchecked(std::span<const std::uint8_t> Value);
    void AppendFixed(std::span<const std::uint8_t> Value);
    std::size_t Size() const { return Heap ? Buffer.size() : InlineSize; }
    // Data() materializes the legacy vector reference once; subsequent writes
    // keep using that same vector, including after Take().
    mutable Bytes Buffer;
    std::array<std::uint8_t,256> Inline{};
    std::size_t InlineSize=0;
    mutable bool Heap=false;
    std::size_t Maximum;
    bool Valid = true;
};
class GULINETCORE_API Reader
{
public:
    explicit Reader(std::span<const std::uint8_t> Value) : Buffer(Value) {}
    bool U8(std::uint8_t& Out);
    bool U16(std::uint16_t& Out);
    bool U32(std::uint32_t& Out);
    bool U64(std::uint64_t& Out);
    bool VarUnsigned(std::uint64_t& Out);
    bool F32(float& Out);
    bool F64(double& Out);
    bool Identity(Id& Out);
    bool Key(ObjectKey& Out);
    bool Text(std::string& Out, std::size_t Limit = 4096);
    bool Blob(Bytes& Out, std::size_t Limit = MaxMessageBytes);
    // Borrowed from the reader's source buffer; the length and bounds checks match Blob.
    bool BlobView(std::span<const std::uint8_t>& Out, std::size_t Limit = MaxMessageBytes);
    bool Raw(std::size_t Count, std::span<const std::uint8_t>& Out);
    bool Done() const { return Valid && Position == Buffer.size(); }
    std::size_t Remaining() const { return Buffer.size() - Position; }
private:
    std::span<const std::uint8_t> Buffer;
    std::size_t Position = 0;
    bool Valid = true;
};
GULINETCORE_API bool EncodeMessage(const Message& Value, Bytes& Out);
// Lossless peer transport envelope only. Canonical records, hashes and
// coordinator messages continue to use EncodeMessage and their original bytes.
inline constexpr std::size_t CanonicalMessageHeaderBytes=145;
// Sorted full object keys from the hash-checked starting manifest. Short IDs
// are local to this match; dynamic objects and new lives retain their full key.
using MotionObjectTable = std::vector<ObjectKey>;
struct PeerMessageContext {Id Match,Sender;const MotionObjectTable* Objects=nullptr;};
GULINETCORE_API bool EncodePeerMessage(const Message& Value,Bytes& Out,PeerMessageContext Context={});
GULINETCORE_API bool DecodeMessage(std::span<const std::uint8_t> Value, Message& Out,PeerMessageContext Context={});
// One datagram, one shared envelope. Encodes the oldest fitting prefix (at most
// 64 records); record byte counts exclude the separately reported shared header.
GULINETCORE_API std::size_t EncodeMotionBatch(std::span<const Message* const> Values,Bytes& Out,
    std::vector<std::size_t>& RecordBytes,std::size_t MaxBytes,PeerMessageContext Context={});
GULINETCORE_API bool DecodeMotionBatch(std::span<const std::uint8_t> Value,std::vector<Message>& Out,PeerMessageContext Context={});
GULINETCORE_API Lane MessageLane(MessageKind Kind);
GULINETCORE_API bool ReliableMessage(MessageKind Kind);
GULINETCORE_API void WriteHealth(Writer& Out, const Health& Value);
GULINETCORE_API bool ReadHealth(Reader& In, Health& Value);
GULINETCORE_API void WriteResources(Writer& Out, const Resources& Value);
GULINETCORE_API bool ReadResources(Reader& In, Resources& Value);
GULINETCORE_API void WriteAuthority(Writer& Out, const Authority& Value);
GULINETCORE_API bool ReadAuthority(Reader& In, Authority& Value);

struct Hello
{
    Transport Carrier = Transport::LocalUdp;
    Team Faction = Team::None;
    Role Seat = Role::Commander;
    std::string ConfigHash;
    std::string MapHash;
    std::string Endpoint;
    std::string ProductUserId;
    std::string IdentityToken;
};
GULINETCORE_API Bytes EncodeHello(const Hello& Value);
GULINETCORE_API bool DecodeHello(std::span<const std::uint8_t> Bytes, Hello& Out);
}
