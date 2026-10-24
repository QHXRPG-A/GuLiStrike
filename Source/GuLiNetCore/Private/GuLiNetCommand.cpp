#include "GuLiNetClient.h"
#include <utility>
namespace GuLiNet
{
Bytes EncodeCommandReceipt(const CommandReceipt& V)
{
    Writer W; W.Identity(V.Event); W.U64(V.At); W.U32(static_cast<std::uint32_t>(V.Members.size()));
    for (const auto& M : V.Members)
    { W.Key(M.Object); W.U64(M.ControlEpoch); W.U64(M.OwnerEpoch); W.U8(M.Accepted ? 1 : 0); }
    return W.Take();
}
bool DecodeCommandReceipt(std::span<const std::uint8_t> Data, CommandReceipt& Out)
{
    Reader R(Data); CommandReceipt V; std::uint32_t Count; std::set<ObjectKey> Seen;
    if (!R.Identity(V.Event) || !V.Event.Valid() || !R.U64(V.At) || !R.U32(Count) || !Count || Count>4096) return false;
    for (std::uint32_t I=0; I<Count; ++I)
    {
        CommandReceiptMember M; std::uint8_t Accepted;
        if (!R.Key(M.Object) || !M.Object.Valid() || !Seen.insert(M.Object).second
            || !R.U64(M.ControlEpoch) || !M.ControlEpoch || !R.U64(M.OwnerEpoch) || !M.OwnerEpoch
            || !R.U8(Accepted) || Accepted>1) return false;
        M.Accepted=Accepted!=0; V.Members.push_back(M);
    }
    if (!R.Done()) return false; Out=std::move(V); return true;
}
Bytes EncodeCommand(const FrozenCommand& V)
{
    Writer W; W.Identity(V.Event); W.U64(V.At);W.U64(V.AcceptedSequence); W.Text(V.Operation); W.Blob(V.Arguments);
    W.U32(static_cast<std::uint32_t>(V.Members.size()));
    for (const auto& M : V.Members)
    { W.Key(M.Object); W.U64(M.ControlEpoch); W.U64(M.OwnerEpoch); W.U32(M.Slot); W.F64(M.X); W.F64(M.Y); W.F64(M.Z); }
    return W.Take();
}
bool DecodeCommand(std::span<const std::uint8_t> Data, FrozenCommand& Out)
{
    Reader R(Data); FrozenCommand V; std::uint32_t Count;
    if (!R.Identity(V.Event) || !V.Event.Valid() || !R.U64(V.At) || !R.U64(V.AcceptedSequence) || !R.Text(V.Operation,128) || V.Operation.empty()
        || !R.Blob(V.Arguments,65536) || !R.U32(Count) || !Count || Count>4096) return false;
    std::set<ObjectKey> Objects; std::set<std::uint32_t> Slots;
    for (std::uint32_t I=0; I<Count; ++I)
    {
        CommandMember M;
        if (!R.Key(M.Object) || !M.Object.Valid() || !R.U64(M.ControlEpoch) || !M.ControlEpoch
            || !R.U64(M.OwnerEpoch) || !M.OwnerEpoch || !R.U32(M.Slot) || !R.F64(M.X) || !R.F64(M.Y) || !R.F64(M.Z)
            || !Objects.insert(M.Object).second || !Slots.insert(M.Slot).second) return false;
        V.Members.push_back(M);
    }
    if (!R.Done()) return false; Out=std::move(V); return true;
}
}
