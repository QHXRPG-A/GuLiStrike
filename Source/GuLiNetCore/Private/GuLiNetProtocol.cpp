#include "GuLiNetProtocol.h"
#include <bit>
#include <cmath>
#include <limits>
#include <utility>

namespace GuLiNet
{
void Writer::U8(std::uint8_t Value)
{
    if (!Valid || Buffer.size() >= Maximum) { Valid = false; return; }
    Buffer.push_back(Value);
}
void Writer::U16(std::uint16_t V) { for (unsigned I = 0; I < 2; ++I) U8(static_cast<std::uint8_t>(V >> (I * 8))); }
void Writer::U32(std::uint32_t V) { for (unsigned I = 0; I < 4; ++I) U8(static_cast<std::uint8_t>(V >> (I * 8))); }
void Writer::U64(std::uint64_t V) { for (unsigned I = 0; I < 8; ++I) U8(static_cast<std::uint8_t>(V >> (I * 8))); }
void Writer::F32(float V) { if (!std::isfinite(V)) Valid = false; U32(std::bit_cast<std::uint32_t>(V)); }
void Writer::F64(double V) { if (!std::isfinite(V)) Valid = false; U64(std::bit_cast<std::uint64_t>(V)); }
void Writer::Identity(const Id& V) { for (auto W : V.Words) U32(W); }
void Writer::Key(const ObjectKey& V) { U8(static_cast<std::uint8_t>(V.Kind)); Identity(V.StableId); U32(V.Generation); }
void Writer::Text(const std::string& V)
{ Blob(std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(V.data()), V.size())); }
void Writer::Blob(std::span<const std::uint8_t> V)
{
    if (V.size() > UINT32_MAX) { Valid = false; return; }
    U32(static_cast<std::uint32_t>(V.size())); Raw(V);
}
void Writer::Raw(std::span<const std::uint8_t> V)
{
    if (!Valid || V.size() > Maximum - Buffer.size()) { Valid = false; return; }
    Buffer.insert(Buffer.end(), V.begin(), V.end());
}
Bytes Writer::Take() { return Valid ? std::move(Buffer) : Bytes{}; }
bool Reader::Raw(std::size_t Count, std::span<const std::uint8_t>& Out)
{
    if (!Valid || Count > Remaining()) { Valid = false; return false; }
    Out = Buffer.subspan(Position, Count); Position += Count; return true;
}
bool Reader::U8(std::uint8_t& Out)
{ std::span<const std::uint8_t> V; if (!Raw(1, V)) return false; Out = V[0]; return true; }
bool Reader::U16(std::uint16_t& Out)
{
    std::span<const std::uint8_t> V; if (!Raw(2, V)) return false;
    Out = static_cast<std::uint16_t>(V[0] | (static_cast<std::uint16_t>(V[1]) << 8)); return true;
}
bool Reader::U32(std::uint32_t& Out)
{
    std::span<const std::uint8_t> V; if (!Raw(4, V)) return false;
    Out = 0; for (unsigned I = 0; I < 4; ++I) Out |= static_cast<std::uint32_t>(V[I]) << (I * 8); return true;
}
bool Reader::U64(std::uint64_t& Out)
{
    std::span<const std::uint8_t> V; if (!Raw(8, V)) return false;
    Out = 0; for (unsigned I = 0; I < 8; ++I) Out |= static_cast<std::uint64_t>(V[I]) << (I * 8); return true;
}
bool Reader::F32(float& Out)
{ std::uint32_t V; if (!U32(V)) return false; Out = std::bit_cast<float>(V); Valid = std::isfinite(Out); return Valid; }
bool Reader::F64(double& Out)
{ std::uint64_t V; if (!U64(V)) return false; Out = std::bit_cast<double>(V); Valid = std::isfinite(Out); return Valid; }
bool Reader::Identity(Id& Out) { for (auto& W : Out.Words) if (!U32(W)) return false; return true; }
bool Reader::Key(ObjectKey& Out)
{
    std::uint8_t Kind;
    if (!U8(Kind) || Kind > static_cast<std::uint8_t>(ObjectKind::BusinessWork)) { Valid = false; return false; }
    Out.Kind = static_cast<ObjectKind>(Kind); return Identity(Out.StableId) && U32(Out.Generation);
}
bool Reader::Text(std::string& Out, std::size_t Limit)
{
    std::uint32_t Count; std::span<const std::uint8_t> View;
    if (!U32(Count) || Count > Limit || !Raw(Count, View)) { Valid = false; return false; }
    Out.assign(reinterpret_cast<const char*>(View.data()), View.size()); return true;
}
bool Reader::Blob(Bytes& Out, std::size_t Limit)
{
    std::uint32_t Count; std::span<const std::uint8_t> View;
    if (!U32(Count) || Count > Limit || !Raw(Count, View)) { Valid = false; return false; }
    Out.assign(View.begin(), View.end()); return true;
}
bool EncodeMessage(const Message& Value, Bytes& Out)
{
    Writer W; const auto& H = Value.Header;
    W.U32(0x474c4e43); W.U16(ProtocolVersion); W.U16(static_cast<std::uint16_t>(H.Kind));
    W.Identity(H.Match); W.U64(H.Epoch); W.Identity(H.Sender); W.Identity(H.Event); W.Key(H.Object);
    W.U64(H.OwnerEpoch); W.U64(H.ControlEpoch); W.U64(H.Sequence); W.U64(H.At);
    W.Blob(Value.Body); Out = W.Take(); return !Out.empty();
}
bool DecodeMessage(std::span<const std::uint8_t> Value, Message& Out)
{
    if (Value.size() > MaxMessageBytes) return false;
    Reader R(Value); Message Result; auto& H = Result.Header;
    std::uint32_t Magic; std::uint16_t Version, Kind;
    if (!R.U32(Magic) || Magic != 0x474c4e43 || !R.U16(Version) || Version != ProtocolVersion
        || !R.U16(Kind) || Kind < static_cast<std::uint16_t>(MessageKind::Hello)
        || Kind > static_cast<std::uint16_t>(MessageKind::WorkState)) return false;
    H.Kind = static_cast<MessageKind>(Kind);
    if (!R.Identity(H.Match) || !R.U64(H.Epoch) || !R.Identity(H.Sender) || !R.Identity(H.Event)
        || !R.Key(H.Object) || !R.U64(H.OwnerEpoch) || !R.U64(H.ControlEpoch) || !R.U64(H.Sequence)
        || !R.U64(H.At) || !R.Blob(Result.Body) || !R.Done()) return false;
    Out = std::move(Result); return true;
}
Lane MessageLane(MessageKind K)
{
    if (K == MessageKind::Motion || K == MessageKind::MotionAcknowledgment || K == MessageKind::ComponentState) return Lane::Motion;
    // Birth fragments, object events and retirement share one ordered stream.
    // An event must never overtake its registration on a different lane.
    if (K == MessageKind::HandoffState || K == MessageKind::BootstrapState || K == MessageKind::BootstrapRequest) return Lane::Handoff;
    return Lane::Control;
}
bool ReliableMessage(MessageKind K) { return K != MessageKind::Motion && K != MessageKind::MotionAcknowledgment && K != MessageKind::ComponentState; }
void WriteHealth(Writer& W, const Health& H)
{ W.F32(H.Value); W.F32(H.Maximum); W.F32(H.Shield); W.F32(H.MaximumShield); W.U32(H.Version); W.U8(H.Dead ? 1 : 0); }
bool ReadHealth(Reader& R, Health& H)
{
    std::uint8_t Dead;
    if (!R.F32(H.Value) || !R.F32(H.Maximum) || !R.F32(H.Shield) || !R.F32(H.MaximumShield)
        || !R.U32(H.Version) || !R.U8(Dead) || Dead > 1) return false;
    H.Dead = Dead != 0; return ValidHealth(H);
}
void WriteResources(Writer& W, const Resources& R)
{ W.U32(static_cast<std::uint32_t>(R.Blue)); W.U32(static_cast<std::uint32_t>(R.Red)); W.U32(R.Version); }
bool ReadResources(Reader& R, Resources& Out)
{
    std::uint32_t Blue, Red;
    if (!R.U32(Blue) || !R.U32(Red) || Blue > INT32_MAX || Red > INT32_MAX || !R.U32(Out.Version)) return false;
    Out.Blue = static_cast<std::int32_t>(Blue); Out.Red = static_cast<std::int32_t>(Red); return true;
}
void WriteAuthority(Writer& W, const Authority& A)
{
    W.Key(A.Object); W.U8(static_cast<std::uint8_t>(A.Faction)); W.Identity(A.Controller); W.U64(A.ControlEpoch);
    W.Identity(A.SimOwner); W.U64(A.OwnerEpoch); W.U8(static_cast<std::uint8_t>(A.State));
    W.Identity(A.SourceLease); W.F64(A.EstimatedCost);
    W.U8(A.Dormant ? 1 : 0); W.Identity(A.StateCustodian);
    W.U8(static_cast<std::uint8_t>(A.Policy));W.U8(static_cast<std::uint8_t>(A.ControllerSeat));W.U8(A.RetireOnVacancy?1:0);
    W.Identity(A.OwnershipGroup);W.U64(A.GroupEpoch);
}
bool ReadAuthority(Reader& R, Authority& A)
{
    std::uint8_t Faction, State, Dormant, Policy, Seat, Retire;
    if (!R.Key(A.Object) || !A.Object.Valid() || !R.U8(Faction) || Faction > 2
        || !R.Identity(A.Controller) || !R.U64(A.ControlEpoch) || !R.Identity(A.SimOwner)
        || !R.U64(A.OwnerEpoch) || !R.U8(State) || State > 3 || !R.Identity(A.SourceLease)
        || !R.F64(A.EstimatedCost) || A.EstimatedCost < 0 || !R.U8(Dormant) || Dormant>1
        || !R.Identity(A.StateCustodian) || !R.U8(Policy) || Policy>static_cast<std::uint8_t>(OwnershipPolicy::ParentWork)
        || !R.U8(Seat) || Seat>static_cast<std::uint8_t>(Role::Observer) || !R.U8(Retire) || Retire>1
        || !R.Identity(A.OwnershipGroup) || !R.U64(A.GroupEpoch)) return false;
    A.Policy=static_cast<OwnershipPolicy>(Policy);A.ControllerSeat=static_cast<Role>(Seat);A.RetireOnVacancy=Retire!=0;
    if ((A.Policy==OwnershipPolicy::SharedGroup)!=A.OwnershipGroup.Valid()
        || (!A.OwnershipGroup.Valid() && A.GroupEpoch)) return false;
    A.Dormant=Dormant!=0;
    if (A.Dormant && ((A.Policy!=OwnershipPolicy::ControllerPinned || A.RetireOnVacancy) || A.Controller.Valid() || A.SimOwner.Valid())) return false;
    if (!A.Dormant && A.StateCustodian.Valid()) return false;
    A.Faction = static_cast<Team>(Faction); A.State = static_cast<Life>(State); return true;
}
Bytes EncodeHello(const Hello& V)
{
    Writer W; W.U8(static_cast<std::uint8_t>(V.Carrier)); W.U8(static_cast<std::uint8_t>(V.Faction));
    W.U8(static_cast<std::uint8_t>(V.Seat)); W.Text(V.ConfigHash); W.Text(V.MapHash); W.Text(V.Endpoint);
    W.Text(V.ProductUserId); W.Text(V.IdentityToken); return W.Take();
}
bool DecodeHello(std::span<const std::uint8_t> Value, Hello& Out)
{
    Reader R(Value); Hello V; std::uint8_t Carrier, Faction, Seat;
    if (!R.U8(Carrier) || Carrier > 1 || !R.U8(Faction) || Faction < 1 || Faction > 2
        || !R.U8(Seat) || Seat > 3 || !R.Text(V.ConfigHash, 128) || !R.Text(V.MapHash, 128)
        || !R.Text(V.Endpoint, 512) || !R.Text(V.ProductUserId, 128) || !R.Text(V.IdentityToken, 16384) || !R.Done()) return false;
    V.Carrier = static_cast<Transport>(Carrier); V.Faction = static_cast<Team>(Faction); V.Seat = static_cast<Role>(Seat);
    Out = std::move(V); return true;
}
}
