#pragma once
#include "CoreMinimal.h"
#include "GuLiNetProtocol.h"
#include "Misc/Compression.h"

/** Lossless transport envelope. Application identity and private-state hashes
 * are calculated before packing and after unpacking. Queue budgets stay bytes
 * actually sent; only private cuts may expand to their bounded 16 MiB limit.
 * Every queued wire message still fits the existing 4 MiB cap. */
namespace GuLiHybridPeerPayload
{
inline constexpr uint32 Tag=0x47555a50,Version=1;
inline size_t PlainLimit(GuLiNet::MessageKind Kind)
{return Kind==GuLiNet::MessageKind::HandoffState?GuLiNet::MaxPrivateStateBytes:GuLiNet::MaxMessageBytes;}
inline bool Pack(const GuLiNet::Message& Message,GuLiNet::Bytes& Out)
{
    const auto& Body=Message.Body;
    const auto Limit=PlainLimit(Message.Header.Kind);
    if (!GuLiNet::ReliableMessage(Message.Header.Kind) || Body.size()>Limit) return false;
    GuLiNet::Reader Prefix(Body);uint32 First=0;const bool Escape=Prefix.U32(First) && First==Tag;
    GuLiNet::Bytes Encoded;uint8 Codec=0;
    if (Body.size()>=512)
    {
        int64 Bound=0;
        if (FCompression::CompressMemoryBound(NAME_Zlib,Bound,int64(Body.size())) && Bound>0 && Bound<=int64(Limit+65536))
        {
            Encoded.resize(size_t(Bound));int64 Size=Bound;
            if (FCompression::CompressMemory(NAME_Zlib,Encoded.data(),Size,Body.data(),int64(Body.size()))
                && Size>0 && uint64(Size)*64>=Body.size() && uint64(Size)+32<Body.size()*9/10)
            {Encoded.resize(size_t(Size));Codec=1;}
        }
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
    if (!Wire.U32(Ver) || Ver!=Version || !Wire.U8(Codec) || Codec>1 || !Wire.U32(Size)
        || Size>PlainLimit(Message.Header.Kind) || !Wire.Blob(Encoded) || !Wire.Done()) return false;
    if (!Codec) {if (Encoded.size()!=Size) return false;Message.Body=std::move(Encoded);return true;}
    if (Size<512 || Encoded.empty() || Encoded.size()>=Size || Encoded.size()*64<Size) return false;
    GuLiNet::Bytes Decoded(Size);
    if (!FCompression::UncompressMemory(NAME_Zlib,Decoded.data(),int64(Size),Encoded.data(),int64(Encoded.size()))) return false;
    Message.Body=std::move(Decoded);return true;
}
}
