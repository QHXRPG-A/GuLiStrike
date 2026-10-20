#pragma once
#include "GuLiNetProtocol.h"
#include <deque>
#include <optional>

namespace GuLiNet
{
// Coordinator-owned metadata only. Backup confers storage, never simulation.
struct ResourceGroupState
{
    Id Group,Owner,Backup;
    Revision OwnerVersion=0,RequestEpoch=1;
    Revision Recovery=0;
    Id PreviousOwner,Publisher,Target;
    bool Abrupt=false;
    double Deadline=0;
    friend bool operator==(const ResourceGroupState&,const ResourceGroupState&)=default;
};
GULINETCORE_API void WriteResourceGroup(Writer&,const ResourceGroupState&);
GULINETCORE_API bool ReadResourceGroup(Reader&,ResourceGroupState&);

// Sections are canonical current values (object, capability, shared task).
// A delta is never a usable snapshot until its baseline and full digest verify.
struct ResourceSnapshot
{
    Id Match,Group,Owner;
    Revision OwnerVersion=0,RequestEpoch=0,Number=0;
    Tick Sample=0;
    std::uint64_t CapturedUtcMicroseconds=0;
    std::map<std::string,Bytes> Parts;
    std::string Digest;
};
struct ResourceSnapshotDelta
{
    ResourceSnapshot Value;
    Revision Baseline=0;
    std::string BaselineDigest;
    std::vector<std::string> Removed;
};
struct ResourceSnapshotStatus
{
    ResourceGroupState Authority;
    Revision InstalledRecovery=0,InstalledSnapshot=0,InstalledEpoch=0;
    Tick InstalledSample=0;
    std::string InstalledDigest;
    std::vector<ObjectKey> InstalledObjects;
    std::size_t CompleteCopies=0,StoredBytes=0,QueuedVersions=0;
};
GULINETCORE_API std::string ResourceSnapshotDigest(const ResourceSnapshot&);
GULINETCORE_API ResourceSnapshotDelta DiffResourceSnapshot(const ResourceSnapshot&,const ResourceSnapshot* Baseline);
GULINETCORE_API Bytes EncodeResourceSnapshotDelta(const ResourceSnapshotDelta&);
GULINETCORE_API bool DecodeResourceSnapshotDelta(std::span<const std::uint8_t>,ResourceSnapshotDelta&);
class GULINETCORE_API ResourceSnapshotStore
{
public:
    enum class Result { Installed,Duplicate,MissingBaseline,Rejected };
    Result Receive(const ResourceSnapshotDelta&,double ReceivedAt);
    const ResourceSnapshot* Latest() const;
    const ResourceSnapshot* Find(Revision Number,const std::string& Digest) const;
    double CompletedAt() const {return LastCompletedAt;}
    std::size_t Size() const {return Complete.size();}
    std::size_t BytesHeld() const;
    void Clear();
private:
    std::deque<ResourceSnapshot> Complete;
    double LastCompletedAt=0;
};
}
