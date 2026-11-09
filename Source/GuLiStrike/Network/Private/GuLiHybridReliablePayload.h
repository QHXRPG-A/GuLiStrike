#pragma once
#include "Network/Private/GuLiHybridPeerPayload.h"
#include <algorithm>
THIRD_PARTY_INCLUDES_START
#include "zstd.h"
THIRD_PARTY_INCLUDES_END

namespace GuLiHybridReliablePayload
{
inline constexpr uint32 Tag=0x47555253;
inline constexpr size_t MaximumBodyBytes=65536;
inline constexpr int WindowLog=21;
inline bool Eligible(const GuLiNet::Message& M)
{
    return (M.Header.Kind==GuLiNet::MessageKind::ObjectEvent || M.Header.Kind==GuLiNet::MessageKind::StateFragments
        || M.Header.Kind==GuLiNet::MessageKind::TransactionResult) && M.Body.size()>=128 && M.Body.size()<=MaximumBodyBytes;
}
inline size_t Bound(size_t PlainBytes) {return ZSTD_compressBound(PlainBytes)+153;}
inline uint32 Checksum(std::span<const uint8> Body) {return uint32(adler32(1,Body.data(),uInt(Body.size())));}

// One direction of one authenticated, reliable, ordered connection. The send
// stream may advance only inside PeerChannel's reserved encoding callback.
// Decoded input is retained until application admission, so backpressure never
// feeds the same compressed chunk into the stream twice.
struct FHistory
{
    uint64 Sequence=0;
    bool Failed=false;
    ZSTD_CCtx* Encoder=nullptr;
    ZSTD_DCtx* Decoder=nullptr;
    GuLiNet::Bytes PendingWire,PendingPlain;
    FHistory()=default;
    FHistory(const FHistory&)=delete;
    FHistory& operator=(const FHistory&)=delete;
    ~FHistory() {ZSTD_freeCCtx(Encoder);ZSTD_freeDCtx(Decoder);}
    void Commit(std::span<const uint8> Body)
    {
        if (!PendingPlain.empty() && (Body.size()!=PendingPlain.size()
            || !std::equal(Body.begin(),Body.end(),PendingPlain.begin()))) {Failed=true;return;}
        PendingWire.clear();PendingPlain.clear();++Sequence;
    }
};
struct FPeerHistory {FHistory Outgoing,Incoming;};

inline bool Pack(const GuLiNet::Message& M,FHistory& History,GuLiNet::Bytes& Out)
{
    if (!Eligible(M) || History.Failed || History.Sequence==MAX_uint64) return false;
    if (!History.Encoder)
    {
        History.Encoder=ZSTD_createCCtx();
        if (!History.Encoder || ZSTD_isError(ZSTD_CCtx_setParameter(History.Encoder,ZSTD_c_compressionLevel,6))
            || ZSTD_isError(ZSTD_CCtx_setParameter(History.Encoder,ZSTD_c_windowLog,WindowLog)))
        {History.Failed=true;return false;}
    }
    GuLiNet::Bytes Encoded(ZSTD_compressBound(M.Body.size())+128);
    ZSTD_inBuffer Input{M.Body.data(),M.Body.size(),0};ZSTD_outBuffer Output{Encoded.data(),Encoded.size(),0};
    size_t Remaining;
    do
    {
        Remaining=ZSTD_compressStream2(History.Encoder,&Output,&Input,ZSTD_e_flush);
        if (ZSTD_isError(Remaining) || (Remaining && Output.pos==Output.size)) {History.Failed=true;return false;}
    } while (Remaining);
    if (Input.pos!=Input.size) {History.Failed=true;return false;}
    Encoded.resize(Output.pos);
    GuLiNet::Writer W;W.U32(Tag);W.U8(1);W.U32(uint32(M.Body.size()));W.U64(History.Sequence);
    W.U32(Checksum(M.Body));W.Blob(Encoded);
    if (!W.Good()) {History.Failed=true;return false;}
    Out=W.Take();return true;
}

inline bool Read(const GuLiNet::Message& M,uint32& Size,uint64& Sequence,uint32& Hash,std::span<const uint8>& Encoded)
{
    GuLiNet::Reader R(M.Body);uint32 Magic;uint8 Version;
    return R.U32(Magic)&&Magic==Tag&&R.U8(Version)&&Version==1&&R.U32(Size)
        &&Size>=128&&Size<=MaximumBodyBytes&&R.U64(Sequence)&&Sequence!=MAX_uint64&&R.U32(Hash)
        &&R.BlobView(Encoded)&&R.Done()&&!Encoded.empty()&&Encoded.size()<=ZSTD_compressBound(Size)+128
        &&(M.Header.Kind==GuLiNet::MessageKind::ObjectEvent||M.Header.Kind==GuLiNet::MessageKind::StateFragments
            ||M.Header.Kind==GuLiNet::MessageKind::TransactionResult);
}
inline bool UsesStream(const GuLiNet::Message& M)
{
    if (M.Header.Kind!=GuLiNet::MessageKind::ObjectEvent&&M.Header.Kind!=GuLiNet::MessageKind::StateFragments
        &&M.Header.Kind!=GuLiNet::MessageKind::TransactionResult) return false;
    GuLiNet::Reader R(M.Body);uint32 Magic;return R.U32(Magic)&&Magic==Tag;
}
inline bool PlainSize(const GuLiNet::Message& M,uint64& Out)
{
    if (!UsesStream(M)) return GuLiHybridPeerPayload::PlainSize(M,Out);
    uint32 Size,Hash;uint64 Sequence;std::span<const uint8> Encoded;
    if (!Read(M,Size,Sequence,Hash,Encoded)) return false;
    Out=Size;return true;
}
inline bool Unpack(GuLiNet::Message& M,FHistory& History,bool& Used)
{
    Used=false;
    if (!UsesStream(M)) return GuLiHybridPeerPayload::Unpack(M);
    uint32 Size,Hash;uint64 Sequence;std::span<const uint8> Encoded;
    if (History.Failed || !Read(M,Size,Sequence,Hash,Encoded) || Sequence!=History.Sequence) return false;
    if (!History.PendingPlain.empty())
    {
        if (History.PendingWire!=M.Body) return false;
        M.Body=History.PendingPlain;Used=true;return true;
    }
    if (!History.Decoder)
    {
        History.Decoder=ZSTD_createDCtx();
        if (!History.Decoder || ZSTD_isError(ZSTD_DCtx_setParameter(History.Decoder,ZSTD_d_windowLogMax,WindowLog)))
        {History.Failed=true;return false;}
    }
    GuLiNet::Bytes Plain(size_t(Size)+1);
    ZSTD_inBuffer Input{Encoded.data(),Encoded.size(),0};ZSTD_outBuffer Output{Plain.data(),Plain.size(),0};
    while (Input.pos<Input.size)
    {
        const auto Before=Input.pos;
        const auto Remaining=ZSTD_decompressStream(History.Decoder,&Output,&Input);
        if (ZSTD_isError(Remaining) || !Remaining || Output.pos>Size || Input.pos==Before)
        {History.Failed=true;return false;}
    }
    Plain.resize(Output.pos);
    if (Plain.size()!=Size || Checksum(Plain)!=Hash) {History.Failed=true;return false;}
    History.PendingWire=M.Body;History.PendingPlain=Plain;M.Body=std::move(Plain);Used=true;return true;
}
}
