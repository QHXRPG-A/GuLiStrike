#pragma once
#include "GuLiNetRules.h"
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
    void F32(float Value);
    void F64(double Value);
    void Identity(const Id& Value);
    void Key(const ObjectKey& Value);
    void Text(const std::string& Value);
    void Blob(std::span<const std::uint8_t> Value);
    void Raw(std::span<const std::uint8_t> Value);
    bool Good() const { return Valid; }
    const Bytes& Data() const { return Buffer; }
    Bytes Take();
private:
    Bytes Buffer;
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
    bool F32(float& Out);
    bool F64(double& Out);
    bool Identity(Id& Out);
    bool Key(ObjectKey& Out);
    bool Text(std::string& Out, std::size_t Limit = 4096);
    bool Blob(Bytes& Out, std::size_t Limit = MaxMessageBytes);
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
struct PeerMessageContext {Id Match,Sender;};
GULINETCORE_API bool EncodePeerMessage(const Message& Value,Bytes& Out,PeerMessageContext Context={});
GULINETCORE_API bool DecodeMessage(std::span<const std::uint8_t> Value, Message& Out,PeerMessageContext Context={});
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
