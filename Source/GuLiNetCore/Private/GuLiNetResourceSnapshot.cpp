#include "GuLiNetResourceSnapshot.h"
#include "GuLiNetHash.h"
#include <algorithm>
#include <cmath>

namespace GuLiNet
{
void WriteResourceGroup(Writer& W,const ResourceGroupState& V)
{
    W.Identity(V.Group);W.Identity(V.Owner);W.Identity(V.Backup);W.U64(V.OwnerVersion);W.U64(V.RequestEpoch);
    W.U64(V.Recovery);W.Identity(V.PreviousOwner);W.Identity(V.Publisher);W.Identity(V.Target);W.U8(V.Abrupt);W.F64(V.Deadline);
}
bool ReadResourceGroup(Reader& R,ResourceGroupState& V)
{
    std::uint8_t Abrupt;
    if (!R.Identity(V.Group)||!V.Group.Valid()||!R.Identity(V.Owner)||!R.Identity(V.Backup)
        ||!R.U64(V.OwnerVersion)||!R.U64(V.RequestEpoch)||!V.RequestEpoch||!R.U64(V.Recovery)
        ||!R.Identity(V.PreviousOwner)||!R.Identity(V.Publisher)||!R.Identity(V.Target)||!R.U8(Abrupt)||Abrupt>1
        ||!R.F64(V.Deadline)||!std::isfinite(V.Deadline)||(V.Owner==V.Backup&&V.Owner.Valid())) return false;
    V.Abrupt=Abrupt!=0;
    return !V.Recovery||(V.PreviousOwner.Valid()&&V.Publisher.Valid()&&V.Target.Valid()&&V.Deadline>0);
}
namespace
{
void Header(Writer& W,const ResourceSnapshot& V)
{
    W.U32(HandoffVersion);W.Identity(V.Match);W.Identity(V.Group);W.Identity(V.Owner);
    W.U64(V.OwnerVersion);W.U64(V.RequestEpoch);W.U64(V.Number);W.U64(V.Sample);W.U64(V.CapturedUtcMicroseconds);
}
bool Header(Reader& R,ResourceSnapshot& V)
{
    std::uint32_t Version;
    return R.U32(Version)&&Version==HandoffVersion&&R.Identity(V.Match)&&V.Match.Valid()&&R.Identity(V.Group)&&V.Group.Valid()
        &&R.Identity(V.Owner)&&V.Owner.Valid()&&R.U64(V.OwnerVersion)&&V.OwnerVersion&&R.U64(V.RequestEpoch)&&V.RequestEpoch
        &&R.U64(V.Number)&&V.Number&&R.U64(V.Sample)&&R.U64(V.CapturedUtcMicroseconds);
}
bool Fits(const ResourceSnapshot& V)
{
    if (V.Parts.empty()||V.Parts.size()>100000) return false;
    std::size_t Size=256;
    for (const auto& [K,B]:V.Parts)
    {
        if (K.empty()||K.size()>512||B.empty()||B.size()>MaxPrivateStateBytes) return false;
        Size+=K.size()+B.size()+8;if (Size>MaxPrivateStateBytes) return false;
    }
    return true;
}
}
std::string ResourceSnapshotDigest(const ResourceSnapshot& V)
{
    if (!Fits(V)) return {};
    Writer W(MaxPrivateStateBytes);Header(W,V);W.U32(static_cast<std::uint32_t>(V.Parts.size()));
    for (const auto& [K,B]:V.Parts) {W.Text(K);W.Blob(B);}
    return W.Good()?Sha256(W.Take()):std::string{};
}
ResourceSnapshotDelta DiffResourceSnapshot(const ResourceSnapshot& V,const ResourceSnapshot* Base)
{
    ResourceSnapshotDelta D;D.Value=V;
    if (!Base||Base->Match!=V.Match||Base->Group!=V.Group||Base->Owner!=V.Owner||Base->OwnerVersion!=V.OwnerVersion
        ||Base->RequestEpoch!=V.RequestEpoch||Base->Number>=V.Number) return D;
    D.Baseline=Base->Number;D.BaselineDigest=Base->Digest;
    for (auto I=D.Value.Parts.begin();I!=D.Value.Parts.end();)
    {const auto Old=Base->Parts.find(I->first);if (Old!=Base->Parts.end()&&Old->second==I->second) I=D.Value.Parts.erase(I);else ++I;}
    for (const auto& [K,B]:Base->Parts) if (!V.Parts.contains(K)) D.Removed.push_back(K);
    return D;
}
Bytes EncodeResourceSnapshotDelta(const ResourceSnapshotDelta& D)
{
    Writer W(MaxPrivateStateBytes);Header(W,D.Value);W.U64(D.Baseline);W.Text(D.BaselineDigest);W.Text(D.Value.Digest);
    W.U32(static_cast<std::uint32_t>(D.Value.Parts.size()));for (const auto& [K,B]:D.Value.Parts) {W.Text(K);W.Blob(B);}
    W.U32(static_cast<std::uint32_t>(D.Removed.size()));for (const auto& K:D.Removed) W.Text(K);return W.Take();
}
bool DecodeResourceSnapshotDelta(std::span<const std::uint8_t> Data,ResourceSnapshotDelta& Out)
{
    if (Data.size()>MaxPrivateStateBytes) return false;
    Reader R(Data);ResourceSnapshotDelta D;std::uint32_t N;
    if (!Header(R,D.Value)||!R.U64(D.Baseline)||D.Baseline>=D.Value.Number||!R.Text(D.BaselineDigest,64)
        ||(D.Baseline?D.BaselineDigest.size()!=64:!D.BaselineDigest.empty())||!R.Text(D.Value.Digest,64)||D.Value.Digest.size()!=64
        ||!R.U32(N)||N>100000) return false;
    for (std::uint32_t I=0;I<N;++I)
    {std::string K;Bytes B;if (!R.Text(K,512)||K.empty()||!R.Blob(B,MaxPrivateStateBytes)||B.empty()||!D.Value.Parts.emplace(K,std::move(B)).second) return false;}
    if (!R.U32(N)||N>100000||(!D.Baseline&&N)) return false;std::set<std::string> Seen;
    for (std::uint32_t I=0;I<N;++I)
    {std::string K;if (!R.Text(K,512)||K.empty()||D.Value.Parts.contains(K)||!Seen.insert(K).second) return false;D.Removed.push_back(std::move(K));}
    if (!R.Done()) return false;Out=std::move(D);return true;
}
ResourceSnapshotStore::Result ResourceSnapshotStore::Receive(const ResourceSnapshotDelta& D,double At)
{
    if (!std::isfinite(At)||!D.Value.Match.Valid()||!D.Value.Group.Valid()||!D.Value.Owner.Valid()
        ||!D.Value.OwnerVersion||!D.Value.RequestEpoch||!D.Value.Number||D.Baseline>=D.Value.Number) return Result::Rejected;
    auto Candidate=D.Value;
    if (D.Baseline)
    {
        const auto Base=Find(D.Baseline,D.BaselineDigest);if (!Base) return Result::MissingBaseline;
        if (Base->Match!=Candidate.Match||Base->Group!=Candidate.Group||Base->Owner!=Candidate.Owner
            ||Base->OwnerVersion!=Candidate.OwnerVersion||Base->RequestEpoch!=Candidate.RequestEpoch) return Result::Rejected;
        Candidate.Parts=Base->Parts;
        for (const auto& K:D.Removed) if (!Candidate.Parts.erase(K)) return Result::Rejected;
        for (const auto& [K,B]:D.Value.Parts) Candidate.Parts.insert_or_assign(K,B);
    }
    else if (!D.Removed.empty()||!D.BaselineDigest.empty()) return Result::Rejected;
    if (Candidate.Digest.empty()||ResourceSnapshotDigest(Candidate)!=Candidate.Digest) return Result::Rejected;
    // A repeated sequence number is not proof of integrity. Validate the actual
    // payload before acknowledging it, including a retransmitted full snapshot.
    if (const auto Old=Find(Candidate.Number,Candidate.Digest);Old) return Result::Duplicate;
    if (const auto Old=Latest();Old)
    {
        if (Old->Match!=Candidate.Match||Old->Group!=Candidate.Group||Candidate.RequestEpoch<Old->RequestEpoch
            ||Candidate.OwnerVersion<Old->OwnerVersion||(Candidate.RequestEpoch==Old->RequestEpoch&&Candidate.OwnerVersion==Old->OwnerVersion
                &&(Candidate.Owner!=Old->Owner||Candidate.Number<=Old->Number))) return Result::Rejected;
    }
    Complete.push_back(std::move(Candidate));while (Complete.size()>2) Complete.pop_front();LastCompletedAt=At;return Result::Installed;
}
const ResourceSnapshot* ResourceSnapshotStore::Latest() const {return Complete.empty()?nullptr:&Complete.back();}
const ResourceSnapshot* ResourceSnapshotStore::Find(Revision N,const std::string& Hash) const
{for (const auto& V:Complete) if (V.Number==N&&V.Digest==Hash) return &V;return nullptr;}
std::size_t ResourceSnapshotStore::BytesHeld() const
{std::size_t Bytes=0;for (const auto& V:Complete) for (const auto& [K,B]:V.Parts) Bytes+=K.size()+B.size();return Bytes;}
void ResourceSnapshotStore::Clear() {Complete.clear();LastCompletedAt=0;}
}
