#pragma once
#include "CoreMinimal.h"
#include "GuLiNetProtocol.h"
#include "Misc/Compression.h"
THIRD_PARTY_INCLUDES_START
#include "zlib.h"
THIRD_PARTY_INCLUDES_END

/** Lossless transport envelope. Application identity and private-state hashes
 * are calculated before packing and after unpacking. Queue budgets stay bytes
 * actually sent; only private cuts may expand to their bounded 16 MiB limit.
 * Every queued wire message still fits the existing 4 MiB cap. */
namespace GuLiHybridPeerPayload
{
inline constexpr uint32 Tag=0x47555a50,Version=1;
inline size_t PlainLimit(GuLiNet::MessageKind Kind)
{return (Kind==GuLiNet::MessageKind::HandoffState||Kind==GuLiNet::MessageKind::ResourceSnapshot||Kind==GuLiNet::MessageKind::ResourceSnapshotInstall)?GuLiNet::MaxPrivateStateBytes:GuLiNet::MaxMessageBytes;}
// Stateless dictionary: only protocol vocabulary and immutable message identity.
// No previous packet, acknowledgment or evictable baseline is needed to decode.
inline GuLiNet::Bytes Dictionary(const GuLiNet::Envelope& H)
{
    GuLiNet::Writer D(4096);
    for (const char* S:{"object","event/","attack/","cooldown/","tasks","linear_flight","control_point",
        "army_rules","population","relocation_body","data/unit","flight/","territory/"}) D.Text(S);
    for (const auto& I:{H.Match,H.Sender,H.Event,H.Object.StableId})
    {D.Text(I.ToString());D.Identity(I);}
    D.Key(H.Object);return D.Take();
}
inline bool CompressDictionary(const GuLiNet::Message& M,GuLiNet::Bytes& Out)
{
    z_stream Z{};if (deflateInit(&Z,Z_DEFAULT_COMPRESSION)!=Z_OK) return false;
    const auto D=Dictionary(M.Header);
    const auto Bound=deflateBound(&Z,uLong(M.Body.size()))+16;
    bool Good=Bound<=PlainLimit(M.Header.Kind)+65536 && deflateSetDictionary(&Z,D.data(),uInt(D.size()))==Z_OK;
    if (Good)
    {
        Out.resize(size_t(Bound));Z.next_in=const_cast<Bytef*>(M.Body.data());Z.avail_in=uInt(M.Body.size());
        Z.next_out=Out.data();Z.avail_out=uInt(Out.size());
        Good=deflate(&Z,Z_FINISH)==Z_STREAM_END && Z.avail_in==0;
        if (Good) Out.resize(size_t(Z.total_out));
    }
    deflateEnd(&Z);if (!Good) Out.clear();return Good;
}
inline bool ExpandDictionary(const GuLiNet::Envelope& H,std::span<const uint8> Encoded,GuLiNet::Bytes& Out)
{
    z_stream Z{};if (inflateInit(&Z)!=Z_OK) return false;
    Z.next_in=const_cast<Bytef*>(Encoded.data());Z.avail_in=uInt(Encoded.size());
    Z.next_out=Out.data();Z.avail_out=uInt(Out.size());
    int Status=inflate(&Z,Z_FINISH);
    if (Status==Z_NEED_DICT)
    {
        const auto D=Dictionary(H);
        if (inflateSetDictionary(&Z,D.data(),uInt(D.size()))==Z_OK) Status=inflate(&Z,Z_FINISH);
    }
    const bool Good=Status==Z_STREAM_END && Z.total_out==Out.size() && Z.avail_in==0;
    inflateEnd(&Z);return Good;
}
inline bool Pack(const GuLiNet::Message& Message,GuLiNet::Bytes& Out,size_t* ReferenceBytes=nullptr)
{
    const auto& Body=Message.Body;
    const auto Limit=PlainLimit(Message.Header.Kind);
    if (!GuLiNet::ReliableMessage(Message.Header.Kind) || Body.size()>Limit) return false;
    GuLiNet::Reader Prefix(Body);uint32 First=0;const bool Escape=Prefix.U32(First) && First==Tag;
    if (ReferenceBytes) *ReferenceBytes=Body.size()+(Escape?17:0);
    GuLiNet::Bytes Encoded;uint8 Codec=0;
    if (Body.size()>=128)
    {
        int64 Bound=0;
        if (FCompression::CompressMemoryBound(NAME_Zlib,Bound,int64(Body.size())) && Bound>0 && Bound<=int64(Limit+65536))
        {
            Encoded.resize(size_t(Bound));int64 Size=Bound;
            if (FCompression::CompressMemory(NAME_Zlib,Encoded.data(),Size,Body.data(),int64(Body.size()))
                && Size>0 && uint64(Size)*64>=Body.size() && uint64(Size)+32<Body.size()*9/10)
            {Encoded.resize(size_t(Size));Codec=1;}
        }
        if (ReferenceBytes && Body.size()>=512 && Codec) *ReferenceBytes=Encoded.size()+17;
        GuLiNet::Bytes Candidate;
        if (CompressDictionary(Message,Candidate) && !Candidate.empty() && Candidate.size()*64>=Body.size()
            && Candidate.size()+32<Body.size()*9/10 && (!Codec || Candidate.size()<Encoded.size()))
        {Encoded=std::move(Candidate);Codec=2;}
    }
    if (!Codec && !Escape) return false;
    GuLiNet::Writer Wire;Wire.U32(Tag);Wire.U32(Version);Wire.U8(Codec);Wire.U32(uint32(Body.size()));
    Wire.Blob(Codec?std::span<const uint8>(Encoded):std::span<const uint8>(Body));
    if (!Wire.Good()) return false;Out=Wire.Take();return true;
}
inline bool Unpack(GuLiNet::Message& Message)
{
    if (!GuLiNet::ReliableMessage(Message.Header.Kind)) return true;
    GuLiNet::Reader Wire(Message.Body);uint32 First=0,Ver=0,Size=0;uint8 Codec=0;GuLiNet::Bytes Encoded;
    if (!Wire.U32(First) || First!=Tag) return true;
    if (!Wire.U32(Ver) || Ver!=Version || !Wire.U8(Codec) || Codec>2 || !Wire.U32(Size)
        || Size>PlainLimit(Message.Header.Kind) || !Wire.Blob(Encoded) || !Wire.Done()) return false;
    if (!Codec) {if (Encoded.size()!=Size) return false;Message.Body=std::move(Encoded);return true;}
    if (Size<128 || Encoded.empty() || Encoded.size()>=Size || Encoded.size()*64<Size) return false;
    GuLiNet::Bytes Decoded(Size);
    if (Codec==2 ? !ExpandDictionary(Message.Header,Encoded,Decoded)
        : !FCompression::UncompressMemory(NAME_Zlib,Decoded.data(),int64(Size),Encoded.data(),int64(Encoded.size()))) return false;
    Message.Body=std::move(Decoded);return true;
}
// Inspect without allocating/decompressing while the consumer is backpressured.
inline bool PlainSize(const GuLiNet::Message& Message,uint64& Out)
{
    Out=Message.Body.size();if (!GuLiNet::ReliableMessage(Message.Header.Kind)) return true;
    GuLiNet::Reader R(Message.Body);uint32 First=0,Ver=0,Size=0,Encoded=0;uint8 Codec=0;
    if (!R.U32(First)||First!=Tag) return true;
    if (!R.U32(Ver)||Ver!=Version||!R.U8(Codec)||Codec>2||!R.U32(Size)||Size>PlainLimit(Message.Header.Kind)
        ||!R.U32(Encoded)||Encoded!=R.Remaining()) return false;
    if ((!Codec&&Encoded!=Size)||(Codec&&(Size<128||!Encoded||Encoded>=Size||uint64(Encoded)*64<Size))) return false;
    Out=Size;return true;
}
}
