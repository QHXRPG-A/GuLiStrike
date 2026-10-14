#include "GuLiNetHash.h"
#include <algorithm>
#include <bit>

namespace GuLiNet
{
namespace
{
constexpr std::array<std::uint32_t, 64> K{
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
constexpr void Compress(std::array<std::uint32_t, 8>& S, std::span<const std::uint8_t, 64> Block)
{
    std::array<std::uint32_t, 64> W{};
    for (std::size_t I = 0; I < 16; ++I)
        W[I] = std::uint32_t(Block[I*4]) << 24 | std::uint32_t(Block[I*4+1]) << 16
            | std::uint32_t(Block[I*4+2]) << 8 | Block[I*4+3];
    for (std::size_t I = 16; I < 64; ++I)
    {
        const auto A = W[I-15], B = W[I-2];
        W[I] = W[I-16] + (std::rotr(A,7) ^ std::rotr(A,18) ^ (A >> 3))
            + W[I-7] + (std::rotr(B,17) ^ std::rotr(B,19) ^ (B >> 10));
    }
    auto [A,B,C,D,E,F,G,H] = S;
    for (std::size_t I = 0; I < 64; ++I)
    {
        const auto T1 = H + (std::rotr(E,6) ^ std::rotr(E,11) ^ std::rotr(E,25))
            + ((E & F) ^ (~E & G)) + K[I] + W[I];
        const auto T2 = (std::rotr(A,2) ^ std::rotr(A,13) ^ std::rotr(A,22)) + ((A & B) ^ (A & C) ^ (B & C));
        H=G; G=F; F=E; E=D+T1; D=C; C=B; B=A; A=T1+T2;
    }
    S[0]+=A; S[1]+=B; S[2]+=C; S[3]+=D; S[4]+=E; S[5]+=F; S[6]+=G; S[7]+=H;
}
}
std::string Sha256(std::span<const std::uint8_t> Data)
{
    std::array<std::uint32_t, 8> State{0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    std::size_t Offset = 0;
    while (Data.size() - Offset >= 64) { Compress(State, std::span<const std::uint8_t,64>(Data.data()+Offset,64)); Offset += 64; }
    std::array<std::uint8_t, 128> Tail{};
    std::copy(Data.begin()+Offset, Data.end(), Tail.begin()); Tail[Data.size()-Offset] = 0x80;
    const std::size_t Size = Data.size()-Offset < 56 ? 64 : 128;
    const auto Bits = static_cast<std::uint64_t>(Data.size()) * 8;
    for (std::size_t I=0; I<8; ++I) Tail[Size-1-I] = static_cast<std::uint8_t>(Bits >> (I*8));
    Compress(State, std::span<const std::uint8_t,64>(Tail.data(),64));
    if (Size == 128) Compress(State, std::span<const std::uint8_t,64>(Tail.data()+64,64));
    constexpr char Hex[]="0123456789abcdef"; std::string Out; Out.reserve(64);
    for (auto V : State) for (int Shift=28; Shift>=0; Shift-=4) Out.push_back(Hex[(V >> Shift)&15]);
    return Out;
}
bool IsSha256(std::string_view Text)
{ return Text.size() == 64 && std::all_of(Text.begin(), Text.end(), [](char C) { return (C>='0' && C<='9') || (C>='a' && C<='f'); }); }
Id StableId(std::string_view Namespace, std::string_view Name)
{
    Bytes Data(Namespace.begin(), Namespace.end()); Data.push_back(0); Data.insert(Data.end(), Name.begin(), Name.end());
    Id Out; Id::Parse(Sha256(Data).substr(0,32), Out); if (!Out.Valid()) Out.Words[3]=1; return Out;
}
}
