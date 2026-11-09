#include "GuLiNetProtocol.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <limits>
#include <utility>

namespace GuLiNet
{
namespace
{
template<class T> void EncodeLittleEndian(T Value,std::uint8_t* Out)
{
    for (unsigned I=0;I<sizeof(T);++I) Out[I]=static_cast<std::uint8_t>(Value>>(I*8));
}
}
void Writer::AppendUnchecked(std::span<const std::uint8_t> Value)
{
    if (!Heap && Value.size()<=Inline.size()-InlineSize)
    {
        std::copy(Value.begin(),Value.end(),Inline.begin()+InlineSize);
        InlineSize+=Value.size();return;
    }
    if (!Heap)
    {
        Buffer.reserve(std::min(Maximum,std::max(InlineSize+Value.size(),Inline.size()*2)));
        Buffer.insert(Buffer.end(),Inline.begin(),Inline.begin()+InlineSize);Heap=true;
    }
    Buffer.insert(Buffer.end(),Value.begin(),Value.end());
}
void Writer::AppendFixed(std::span<const std::uint8_t> Value)
{
    if (!Valid) return;
    // U8 used to retain the fitting prefix before the first overflowing byte.
    // Preserve Data(), Good(), and Take() behavior even on that failure path.
    const auto Count=std::min(Value.size(),Maximum-Size());
    if (Count) AppendUnchecked(Value.first(Count));
    if (Count!=Value.size()) Valid=false;
}
void Writer::U8(std::uint8_t Value)
{
    if (!Valid || Size() >= Maximum) { Valid = false; return; }
    if (Heap) Buffer.push_back(Value);else if (InlineSize<Inline.size()) Inline[InlineSize++]=Value;
    else AppendUnchecked(std::span<const std::uint8_t>(&Value,1));
}
void Writer::U16(std::uint16_t V)
{std::array<std::uint8_t,2> B;EncodeLittleEndian(V,B.data());AppendFixed(B);}
void Writer::U32(std::uint32_t V)
{std::array<std::uint8_t,4> B;EncodeLittleEndian(V,B.data());AppendFixed(B);}
void Writer::U64(std::uint64_t V)
{std::array<std::uint8_t,8> B;EncodeLittleEndian(V,B.data());AppendFixed(B);}
void Writer::F32(float V) { if (!std::isfinite(V)) Valid = false; U32(std::bit_cast<std::uint32_t>(V)); }
void Writer::F64(double V) { if (!std::isfinite(V)) Valid = false; U64(std::bit_cast<std::uint64_t>(V)); }
void Writer::Identity(const Id& V)
{
    std::array<std::uint8_t,16> B;
    for (unsigned I=0;I<4;++I) EncodeLittleEndian(V.Words[I],B.data()+I*4);
    AppendFixed(B);
}
void Writer::Key(const ObjectKey& V)
{
    std::array<std::uint8_t,21> B;B[0]=static_cast<std::uint8_t>(V.Kind);
    for (unsigned I=0;I<4;++I) EncodeLittleEndian(V.StableId.Words[I],B.data()+1+I*4);
    EncodeLittleEndian(V.Generation,B.data()+17);AppendFixed(B);
}
void Writer::Text(const std::string& V)
{ Blob(std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(V.data()), V.size())); }
void Writer::Blob(std::span<const std::uint8_t> V)
{
    if (V.size() > UINT32_MAX) { Valid = false; return; }
    U32(static_cast<std::uint32_t>(V.size())); Raw(V);
}
void Writer::Raw(std::span<const std::uint8_t> V)
{
    if (!Valid || V.size() > Maximum - Size()) { Valid = false; return; }
    AppendUnchecked(V);
}
std::span<const std::uint8_t> Writer::View() const
{return Heap?std::span<const std::uint8_t>(Buffer):std::span<const std::uint8_t>(Inline.data(),InlineSize);}
const Bytes& Writer::Data() const
{
    if (!Heap) {Buffer.assign(Inline.begin(),Inline.begin()+InlineSize);Heap=true;}
    return Buffer;
}
void Writer::Clear() {if (Heap) Buffer.clear();else InlineSize=0;}
Bytes Writer::Take()
{
    if (!Valid) return {};
    if (Heap) return std::move(Buffer);
    Bytes Result(Inline.begin(),Inline.begin()+InlineSize);InlineSize=0;return Result;
}
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
    std::span<const std::uint8_t> View;if (!BlobView(View,Limit)) return false;
    Out.assign(View.begin(), View.end()); return true;
}
bool Reader::BlobView(std::span<const std::uint8_t>& Out,std::size_t Limit)
{
    std::uint32_t Count;
    if (!U32(Count) || Count>Limit || !Raw(Count,Out)) {Valid=false;return false;}
    return true;
}
bool EncodeMessage(const Message& Value, Bytes& Out)
{
    Writer W; const auto& H = Value.Header;
    W.U32(0x474c4e43); W.U16(ProtocolVersion); W.U16(static_cast<std::uint16_t>(H.Kind));
    W.Identity(H.Match); W.U64(H.Epoch); W.Identity(H.Sender); W.Identity(H.Event); W.Key(H.Object);
    W.U64(H.OwnerEpoch); W.U64(H.ControlEpoch); W.U64(H.Sequence); W.U64(H.At);W.Identity(H.ResourceGroup);W.U64(H.ResourceEpoch);
    W.Blob(Value.Body); Out = W.Take(); return !Out.empty();
}
namespace
{
constexpr std::uint32_t CompactPeerMagic=0x474c4e50;
void VarUInt(Writer& W,std::uint64_t V)
{
    while (V>=128) {W.U8(static_cast<std::uint8_t>(V)|128);V>>=7;}
    W.U8(static_cast<std::uint8_t>(V));
}
bool VarUInt(Reader& R,std::uint64_t& Out)
{
    Out=0;
    for (unsigned I=0;I<10;++I)
    {
        std::uint8_t V;if (!R.U8(V)||(I==9&&V>1)) return false;
        Out|=std::uint64_t(V&127)<<(I*7);
        if (!(V&128)) return !I||V!=0; // reject overlong/non-canonical representations
    }
    return false;
}
std::uint64_t ShortObjectIndex(const ObjectKey& Key,const MotionObjectTable* Table)
{
    if (!Table || !Key.Valid()) return 0;
    const auto It=std::lower_bound(Table->begin(),Table->end(),Key);
    return It!=Table->end() && *It==Key ? 1+static_cast<std::uint64_t>(It-Table->begin()) : 0;
}
bool ReadShortObject(Reader& R,const MotionObjectTable* Table,ObjectKey& Out)
{
    std::uint64_t Index=0;
    if (!Table || !VarUInt(R,Index) || !Index || Index>Table->size()) return false;
    Out=(*Table)[static_cast<std::size_t>(Index-1)];
    return true;
}
bool DecodePeerMessage(Reader& R,Message& Out,PeerMessageContext Context)
{
    Message M;auto& H=M.Header;std::uint8_t Kind,Flags,ObjectKindValue=0;std::uint64_t Generation=0,Size=0;
    if (!R.U8(Kind)||Kind<unsigned(MessageKind::Hello)||Kind>=unsigned(MessageKind::Count)
        ||!R.U8(Flags)||(Flags&~31u)) return false;
    if (Flags&4) {if (!Context.Match.Valid()) return false;H.Match=Context.Match;}
    else if (!R.Identity(H.Match)) return false;
    if (!VarUInt(R,H.Epoch)) return false;
    if (Flags&8) {if (!Context.Sender.Valid()) return false;H.Sender=Context.Sender;}
    else if (!R.Identity(H.Sender)) return false;
    H.Kind=MessageKind(Kind);
    if ((Flags&1)&&!R.Identity(H.Event)) return false;
    if (Flags&16)
    {
        if (!(Flags&2) || MessageLane(H.Kind)!=Lane::Motion || !ReadShortObject(R,Context.Objects,H.Object)) return false;
    }
    else if (Flags&2)
    {
        if (!R.U8(ObjectKindValue)||ObjectKindValue>unsigned(ObjectKind::BusinessWork)
            ||!R.Identity(H.Object.StableId)||!VarUInt(R,Generation)||Generation>UINT32_MAX) return false;
        H.Object.Kind=ObjectKind(ObjectKindValue);H.Object.Generation=static_cast<std::uint32_t>(Generation);
    }
    if (!VarUInt(R,H.OwnerEpoch)||!VarUInt(R,H.ControlEpoch)||!VarUInt(R,H.Sequence)||!VarUInt(R,H.At)||!R.Identity(H.ResourceGroup)||!VarUInt(R,H.ResourceEpoch)
        ||!VarUInt(R,Size)||Size>MaxMessageBytes-CanonicalMessageHeaderBytes) return false;
    std::span<const std::uint8_t> Body;if (!R.Raw(static_cast<std::size_t>(Size),Body)||!R.Done()) return false;
    M.Body.assign(Body.begin(),Body.end());Out=std::move(M);return true;
}
}
void Writer::VarUnsigned(std::uint64_t Value) { VarUInt(*this,Value); }
bool Reader::VarUnsigned(std::uint64_t& Out) { return VarUInt(*this,Out); }
bool EncodePeerMessage(const Message& M,Bytes& Out,PeerMessageContext Context)
{
    const auto& H=M.Header;
    if (M.Body.size()>MaxMessageBytes-CanonicalMessageHeaderBytes || H.Kind<MessageKind::Hello || H.Kind>=MessageKind::Count
        || H.Object.Kind>ObjectKind::BusinessWork) return false;
    const bool Event=H.Event!=Id{},Object=H.Object!=ObjectKey{};
    const auto Short=MessageLane(H.Kind)==Lane::Motion ? ShortObjectIndex(H.Object,Context.Objects) : 0;
    const bool Match=Context.Match.Valid()&&H.Match==Context.Match,Sender=Context.Sender.Valid()&&H.Sender==Context.Sender;
    Writer W;W.U32(CompactPeerMagic);W.U16(ProtocolVersion);W.U8(static_cast<std::uint8_t>(H.Kind));
    W.U8((Event?1:0)|(Object?2:0)|(Match?4:0)|(Sender?8:0)|(Short?16:0));
    if (!Match) W.Identity(H.Match);VarUInt(W,H.Epoch);if (!Sender) W.Identity(H.Sender);
    if (Event) W.Identity(H.Event);
    if (Short) VarUInt(W,Short);
    else if (Object) {W.U8(static_cast<std::uint8_t>(H.Object.Kind));W.Identity(H.Object.StableId);VarUInt(W,H.Object.Generation);}
    VarUInt(W,H.OwnerEpoch);VarUInt(W,H.ControlEpoch);VarUInt(W,H.Sequence);VarUInt(W,H.At);W.Identity(H.ResourceGroup);VarUInt(W,H.ResourceEpoch);VarUInt(W,M.Body.size());W.Raw(M.Body);
    Out=W.Take();
    // Extreme counters can be wider as varints; never inflate a wire message.
    if (Out.size()>=M.Body.size()+CanonicalMessageHeaderBytes) return EncodeMessage(M,Out);
    return !Out.empty();
}
bool DecodeMessage(std::span<const std::uint8_t> Value, Message& Out,PeerMessageContext Context)
{
    if (Value.size() > MaxMessageBytes) return false;
    Reader R(Value); Message Result; auto& H = Result.Header;
    std::uint32_t Magic; std::uint16_t Version, Kind;
    if (!R.U32(Magic) || !R.U16(Version) || Version != ProtocolVersion) return false;
    if (Magic==CompactPeerMagic) return DecodePeerMessage(R,Out,Context);
    if (Magic!=0x474c4e43 || !R.U16(Kind) || Kind < static_cast<std::uint16_t>(MessageKind::Hello)
        || Kind >= static_cast<std::uint16_t>(MessageKind::Count)) return false;
    H.Kind = static_cast<MessageKind>(Kind);
    if (!R.Identity(H.Match) || !R.U64(H.Epoch) || !R.Identity(H.Sender) || !R.Identity(H.Event)
        || !R.Key(H.Object) || !R.U64(H.OwnerEpoch) || !R.U64(H.ControlEpoch) || !R.U64(H.Sequence)
        || !R.U64(H.At) || !R.Identity(H.ResourceGroup) || !R.U64(H.ResourceEpoch) || !R.Blob(Result.Body) || !R.Done()) return false;
    Out = std::move(Result); return true;
}
namespace
{
std::size_t EncodeBatch(std::span<const Message* const> Values,Bytes& Out,
    std::vector<std::size_t>& RecordBytes,std::size_t MaxBytes,PeerMessageContext Context,bool Reliable)
{
    Out.clear();RecordBytes.clear();
    if (Values.empty() || !Values.front() || MaxBytes>MaxDatagramBytes) return 0;
    Message Base;Base.Header=Values.front()->Header;
    Base.Header.Kind=Reliable?MessageKind::Heartbeat:MessageKind::Motion;
    Base.Header.Object={};Base.Header.Sequence=0;Base.Header.Event={};
    Bytes Shared;if (!EncodePeerMessage(Base,Shared,Context) || Shared.size()+3>MaxBytes) return 0;
    Writer Records(MaxBytes);const auto& B=Base.Header;std::set<std::uint64_t> Sequences;
    for (const auto* Value:Values)
    {
        if (RecordBytes.size()==64 || !Value) break;
        const auto& H=Value->Header;
        if (H.Kind<MessageKind::Hello || H.Kind>=MessageKind::Count
            || MessageLane(H.Kind)!=(Reliable?Lane::Control:Lane::Motion) || ReliableMessage(H.Kind)!=Reliable || !H.Sequence
            || H.Object.Kind>ObjectKind::BusinessWork || !Sequences.insert(H.Sequence).second) break;
        const auto Short=ShortObjectIndex(H.Object,Context.Objects);
        const std::uint16_t Flags=(H.Event!=Id{}?1:0)|(Short?2:0)|(H.Object!=ObjectKey{} && !Short?1024:0)
            |(H.Match!=B.Match?4:0)|(H.Sender!=B.Sender?8:0)|(H.Epoch!=B.Epoch?16:0)
            |(H.OwnerEpoch!=B.OwnerEpoch?32:0)|(H.ControlEpoch!=B.ControlEpoch?64:0)
            |(H.At!=B.At?128:0)|(H.ResourceGroup!=B.ResourceGroup?256:0)|(H.ResourceEpoch!=B.ResourceEpoch?512:0);
        Writer R(MaxBytes);R.U8(static_cast<std::uint8_t>(H.Kind));VarUInt(R,Flags);
        if (Flags&1) R.Identity(H.Event);
        if (Short) VarUInt(R,Short);
        else if (Flags&1024) {R.U8(static_cast<std::uint8_t>(H.Object.Kind));R.Identity(H.Object.StableId);VarUInt(R,H.Object.Generation);}
        if (Flags&4) R.Identity(H.Match);if (Flags&8) R.Identity(H.Sender);
        if (Flags&16) VarUInt(R,H.Epoch);if (Flags&32) VarUInt(R,H.OwnerEpoch);
        if (Flags&64) VarUInt(R,H.ControlEpoch);if (Flags&128) VarUInt(R,H.At);
        if (Flags&256) R.Identity(H.ResourceGroup);if (Flags&512) VarUInt(R,H.ResourceEpoch);
        VarUInt(R,H.Sequence);VarUInt(R,Value->Body.size());R.Raw(Value->Body);
        if (!R.Good() || 3+Shared.size()+Records.View().size()+R.View().size()>MaxBytes) break;
        RecordBytes.push_back(R.View().size());Records.Raw(R.View());
    }
    if (RecordBytes.size()<2) {RecordBytes.clear();return 0;}
    Writer W(MaxBytes);W.U8(static_cast<std::uint8_t>(RecordBytes.size()));W.U16(static_cast<std::uint16_t>(Shared.size()));
    W.Raw(Shared);W.Raw(Records.View());Out=W.Take();return RecordBytes.size();
}
bool DecodeBatch(std::span<const std::uint8_t> Value,std::vector<Message>& Out,PeerMessageContext Context,bool Reliable)
{
    if (Value.size()>MaxDatagramBytes) return false;
    Reader R(Value);std::uint8_t Count;std::uint16_t SharedSize;std::span<const std::uint8_t> Shared;
    Message Base;
    if (!R.U8(Count) || Count<2 || Count>64 || !R.U16(SharedSize) || !R.Raw(SharedSize,Shared)
        || !DecodeMessage(Shared,Base,Context) || !Base.Body.empty()
        || Base.Header.Kind!=(Reliable?MessageKind::Heartbeat:MessageKind::Motion)
        || Base.Header.Object!=ObjectKey{} || Base.Header.Sequence || Base.Header.Event!=Id{}) return false;
    std::vector<Message> Result;Result.reserve(Count);std::set<std::uint64_t> Sequences;
    for (std::uint8_t I=0;I<Count;++I)
    {
        Message M=Base;auto& H=M.Header;std::uint8_t Kind,ObjectKindValue;std::uint64_t Flags,Generation,Size;
        if (!R.U8(Kind) || !VarUInt(R,Flags) || Flags>2047 || ((Flags&2) && (Flags&1024))) return false;
        H.Kind=static_cast<MessageKind>(Kind);
        if (H.Kind<MessageKind::Hello || H.Kind>=MessageKind::Count
            || MessageLane(H.Kind)!=(Reliable?Lane::Control:Lane::Motion) || ReliableMessage(H.Kind)!=Reliable) return false;
        if ((Flags&1) && !R.Identity(H.Event)) return false;
        if ((Flags&2) && !ReadShortObject(R,Context.Objects,H.Object)) return false;
        if (Flags&1024)
        {
            if (!R.U8(ObjectKindValue) || ObjectKindValue>unsigned(ObjectKind::BusinessWork)
                || !R.Identity(H.Object.StableId) || !VarUInt(R,Generation) || Generation>UINT32_MAX) return false;
            H.Object.Kind=static_cast<ObjectKind>(ObjectKindValue);H.Object.Generation=static_cast<std::uint32_t>(Generation);
        }
        if (((Flags&4) && !R.Identity(H.Match)) || ((Flags&8) && !R.Identity(H.Sender))
            || ((Flags&16) && !VarUInt(R,H.Epoch)) || ((Flags&32) && !VarUInt(R,H.OwnerEpoch))
            || ((Flags&64) && !VarUInt(R,H.ControlEpoch)) || ((Flags&128) && !VarUInt(R,H.At))
            || ((Flags&256) && !R.Identity(H.ResourceGroup)) || ((Flags&512) && !VarUInt(R,H.ResourceEpoch))
            || !VarUInt(R,H.Sequence) || !H.Sequence || !Sequences.insert(H.Sequence).second
            || !VarUInt(R,Size) || Size>R.Remaining()) return false;
        std::span<const std::uint8_t> Body;if (!R.Raw(static_cast<std::size_t>(Size),Body)) return false;
        M.Body.assign(Body.begin(),Body.end());Result.push_back(std::move(M));
    }
    if (!R.Done()) return false;
    Out=std::move(Result);return true;
}
}
std::size_t EncodeMotionBatch(std::span<const Message* const> Values,Bytes& Out,
    std::vector<std::size_t>& RecordBytes,std::size_t MaxBytes,PeerMessageContext Context)
{return EncodeBatch(Values,Out,RecordBytes,MaxBytes,Context,false);}
bool DecodeMotionBatch(std::span<const std::uint8_t> Value,std::vector<Message>& Out,PeerMessageContext Context)
{return DecodeBatch(Value,Out,Context,false);}
std::size_t EncodeReliableBatch(std::span<const Message* const> Values,Bytes& Out,
    std::vector<std::size_t>& RecordBytes,std::size_t MaxBytes,PeerMessageContext Context)
{return EncodeBatch(Values,Out,RecordBytes,MaxBytes,Context,true);}
bool DecodeReliableBatch(std::span<const std::uint8_t> Value,std::vector<Message>& Out,PeerMessageContext Context)
{return DecodeBatch(Value,Out,Context,true);}
Lane MessageLane(MessageKind K)
{
    if (K == MessageKind::Motion || K == MessageKind::MotionAcknowledgment || K == MessageKind::ComponentState) return Lane::Motion;
    // Birth fragments, object events and retirement share one ordered stream.
    // An event must never overtake its registration on a different lane.
    if (K == MessageKind::HandoffState || K == MessageKind::BootstrapState || K == MessageKind::BootstrapRequest || K == MessageKind::ResourceSnapshot || K == MessageKind::ResourceSnapshotInstall) return Lane::Handoff;
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
