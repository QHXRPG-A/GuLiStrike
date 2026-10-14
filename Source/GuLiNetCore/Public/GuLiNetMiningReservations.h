#pragma once
#include "GuLiNetProtocol.h"
#include <map>
#include <set>

namespace GuLiNet
{
// The authored ring is sampled at eight fixed angles. The original sample
// identity survives locally rejected/projected positions and cache rebuilding.
inline constexpr std::uint8_t MiningSamplesPerCluster=8;
enum class MiningReservationOperation : std::uint8_t { ClaimPosition, ClaimNode, Release };
enum class MiningReservationStatus : std::uint8_t
{ Committed, Duplicate, Invalid, Conflict, Occupied, MissingLease, Deferred };
struct MiningPositionLease
{
    ObjectKey Source;
    std::uint32_t Cluster=0,Task=0;
    std::uint8_t Sample=0;
    Id Token;
    std::uint32_t Node=0;
    Revision Version=0;
    friend bool operator==(const MiningPositionLease&,const MiningPositionLease&)=default;
};
struct MiningReservationOutcome
{
    Id Event;
    MiningReservationOperation Operation=MiningReservationOperation::ClaimPosition;
    MiningReservationStatus Status=MiningReservationStatus::Invalid;
    ObjectKey Source;
    Revision Version=0;
    MiningPositionLease Lease;
    bool IsCommitted() const { return Status==MiningReservationStatus::Committed || Status==MiningReservationStatus::Duplicate; }
};
struct MiningReservationDecision
{
    Bytes Fingerprint;
    MiningReservationOutcome Outcome;
};
struct MiningReservationState
{
    bool Configured=false;
    Revision Version=0;
    std::map<std::uint32_t,std::uint32_t> NodeClusters;
    std::map<ObjectKey,MiningPositionLease> Leases;
    std::map<Id,MiningReservationDecision> Decisions;
};
struct MiningReservationDirectory
{
    bool Configured=false;
    Revision Version=0;
    std::map<ObjectKey,MiningPositionLease> Leases;
};

/** Room-serial ownership of data identities. The simulator supplies navigation
 * and visibility facts; the caller authenticates the life, grant and epoch.
 * Failed claims retain the current position, and late releases require its
 * immutable token. Snapshot restoration rebuilds both occupancy indexes. */
class GULINETCORE_API MiningReservations
{
public:
    bool Configure(std::map<std::uint32_t,std::uint32_t> NodeClusters);
    MiningReservationOutcome ClaimPosition(ObjectKey Source,std::uint32_t Cluster,std::uint8_t Sample,std::uint32_t Task,Id Event);
    MiningReservationOutcome ClaimNode(ObjectKey Source,Id Token,std::uint32_t Node,Id Event);
    MiningReservationOutcome Release(ObjectKey Source,Id Token,Id Event);
    bool AbandonSource(ObjectKey Source);
    bool OwnsNode(ObjectKey Source,Id Token,std::uint32_t Node) const;
    const MiningReservationState& Snapshot() const { return State; }
    bool Restore(MiningReservationState Value);
private:
    MiningReservationOutcome Begin(MiningReservationOperation Operation,ObjectKey Source,Id Event,
        std::uint32_t Cluster,std::uint8_t Sample,std::uint32_t Task,Id Token,std::uint32_t Node,Bytes& Fingerprint) const;
    MiningReservationOutcome Finish(MiningReservationOutcome Value,Bytes Fingerprint);
    void RemoveIndexes(const MiningPositionLease& Value);
    void RebuildIndexes();
    MiningReservationState State;
    std::map<std::pair<std::uint32_t,std::uint8_t>,ObjectKey> PositionOwners;
    std::map<std::uint32_t,ObjectKey> NodeOwners;
    std::set<std::uint32_t> Clusters;
};
GULINETCORE_API bool ValidMiningReservations(const MiningReservationState& State);
GULINETCORE_API Bytes EncodeMiningReservationOutcome(const MiningReservationOutcome& Value);
GULINETCORE_API bool DecodeMiningReservationOutcome(std::span<const std::uint8_t> Bytes,MiningReservationOutcome& Out);
GULINETCORE_API Bytes EncodeMiningReservations(const MiningReservationState& State);
GULINETCORE_API bool DecodeMiningReservations(std::span<const std::uint8_t> Bytes,MiningReservationState& Out);
GULINETCORE_API Bytes EncodeMiningReservationDirectory(const MiningReservationState& State);
GULINETCORE_API bool DecodeMiningReservationDirectory(std::span<const std::uint8_t> Bytes,
    const std::map<std::uint32_t,std::uint32_t>& NodeClusters,MiningReservationDirectory& Out);
}
