#pragma once
#include "GuLiNetRoomTypes.h"
#include "GuLiNetOwnership.h"
#include <optional>

namespace GuLiNet
{
// Serialized room metadata. No gameplay evaluator, resource state or battle clock.
class GULINETCORE_API Room
{
public:
    Room(Id Match,std::string ConfigurationHash,std::string MapHash,Defaults Settings={},OwnershipSettings Ownership={});
    bool ConfigureDefinitions(std::map<std::string,OwnershipDefinition> Definitions);
    bool Join(Member Value,const std::string& ConfigurationHash,const std::string& MapHash,double Now,std::string& Error);
    bool Ready(Id Player);
    bool Heartbeat(Id Player,double Now);
    bool LoadReport(Id Player,double MillisecondsPerTick);
    bool BeginLeave(Id Player,bool Abrupt,double Now);
    bool MembershipReady(Id Player,Revision Version,double Now);
    bool Register(ObjectRegistration Value,double Now,Id Creator={});
    bool CanRecoverBirth(const ObjectRegistration& Value,Id Custodian) const;
    bool UpdateLifecycle(Id Sender,ObjectKey Object,Revision OwnerVersion,Life State,double Now);
    bool SetController(ObjectKey Object,Id Player,double Now);
    bool ImportMember(Member Value);
    Bytes ExportMetadata() const;
    bool ImportMetadata(std::span<const std::uint8_t> Value,double Now);
    std::vector<Assignment> Propose(double Now);
    bool CommitAssignment(Id Player,ObjectKey Object,Revision OwnerVersion,double Now);
    bool Select(Id Player,Revision Version,std::span<const ObjectKey> Objects,double Now);
    bool Operate(Id Player,std::span<const ObjectKey> Objects,double Now);
    OwnershipScheduler& Ownership() {return Scheduler;}
    const OwnershipScheduler& Ownership() const {return Scheduler;}
    std::vector<HandoffAction> TakeOwnershipActions(double Now);
    void Poll(double Now);
    void Abort(std::string Reason);
    void SetInitialRosterReady(bool Value) {InitialRosterReady=Value;}
    Id MatchId() const {return Match;}
    Revision Epoch() const {return 1;}
    RoomPhase Phase() const {return CurrentPhase;}
    const std::string& Failure() const {return FailureReason;}
    const std::map<Id,Member>& Members() const {return Players;}
    const std::map<ObjectKey,ObjectRegistration>& Objects() const {return Directory;}
    const std::set<ObjectKey>& PendingDirectoryUpdates() const {return DirectoryUpdates;}
    void AcknowledgeDirectoryUpdates() {DirectoryUpdates.clear();}
    const std::map<ObjectKey,Assignment>& Offers() const {return PendingAssignments;}
    const std::map<Id,OwnershipGroupAuthority>& OwnershipGroups() const {return Groups;}
    std::map<Id,ResourceGroupState> ResourceGroups() const;
    bool ResourceSnapshotInstalled(Id Player,Id Group,Revision Recovery,Revision Number,const std::string& Digest,
        std::span<const ObjectRegistration> Objects,double Now);
    const std::map<std::string,OwnershipDefinition>& ObjectDefinitions() const {return Definitions;}
    const std::optional<MembershipChange>& Membership() const {return MembershipValue;}
private:
    bool Live(Id Player) const;
    Id Controller(Team Faction,Role Seat) const;
    Id Custodian(Team Faction,Id Exclude={}) const;
    Id Destination(const Authority& Grant,Id Exclude={},const std::set<Id>* Unavailable=nullptr) const;
    bool BeginMembership(Id Player,MembershipOperation Operation,bool Abrupt,double Now);
    void FinishMembership(double Now);
    void ScheduleLeaveTransfers(double Now);
    void RefreshControllers(double Now);
    void RefreshGroups(double Now);
    void RefreshResourceBackups();
    bool InResourceRecovery(ObjectKey Object) const;
    Id ResourceGroupOf(ObjectKey Object) const;
    struct ResourceRecovery
    {
        ResourceGroupState State;
        std::set<Id> Waiting;
        Revision Number=0;
        std::string Digest;
        std::vector<ObjectRegistration> Objects;
    };
    std::map<Id,ResourceRecovery> ResourceRecoveries;
    Id GroupDestination(Id Group,Id Exclude={}) const;
    Id Match;
    std::string ConfigurationHash,LevelHash,FailureReason;
    Defaults Settings;
    RoomPhase CurrentPhase=RoomPhase::Preparing;
    OwnershipScheduler Scheduler;
    std::uint32_t LeaveTransferWindow=32;
    std::map<std::string,OwnershipDefinition> Definitions;
    std::map<Id,Member> Players;
    // Every directory mutation also calls MarkDirectoryChanged.
    std::map<ObjectKey,ObjectRegistration> Directory;
    static std::uint16_t MetadataBucket(const ObjectKey& K)
    {
        return (static_cast<std::uint16_t>(K.Kind)<<8)|static_cast<std::uint16_t>(K.StableId.Words[0]>>24);
    }
    void MarkDirectoryChanged(const ObjectKey& K)
    {
        DirectoryUpdates.insert(K);DirtyMetadataBuckets.insert(MetadataBucket(K));
    }
    // Cache and publication bookkeeping only; neither is persisted or grants authority.
    std::set<ObjectKey> DirectoryUpdates;
    mutable std::set<std::uint16_t> DirtyMetadataBuckets;
    mutable std::map<std::uint16_t,Bytes> MetadataBuckets;
    std::map<ObjectKey,Assignment> PendingAssignments;
    std::map<Id,OwnershipGroupAuthority> Groups;
    std::optional<MembershipChange> MembershipValue;
    Revision NextMembership=1;
    std::array<double,2> EmptySince{-1,-1};
    bool InitialRosterReady=false;
};
}
