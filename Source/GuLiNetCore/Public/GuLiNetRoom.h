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
    const std::map<ObjectKey,Assignment>& Offers() const {return PendingAssignments;}
    const std::map<Id,OwnershipGroupAuthority>& OwnershipGroups() const {return Groups;}
    const std::optional<MembershipChange>& Membership() const {return MembershipValue;}
private:
    bool Live(Id Player) const;
    Id Controller(Team Faction,Role Seat) const;
    Id Custodian(Team Faction,Id Exclude={}) const;
    Id Destination(const Authority& Grant,Id Exclude={}) const;
    bool BeginMembership(Id Player,MembershipOperation Operation,bool Abrupt,double Now);
    void FinishMembership(double Now);
    void RefreshControllers(double Now);
    void RefreshGroups(double Now);
    Id GroupDestination(Id Group,Id Exclude={}) const;
    Id Match;
    std::string ConfigurationHash,LevelHash,FailureReason;
    Defaults Settings;
    RoomPhase CurrentPhase=RoomPhase::Preparing;
    OwnershipScheduler Scheduler;
    std::map<std::string,OwnershipDefinition> Definitions;
    std::map<Id,Member> Players;
    std::map<ObjectKey,ObjectRegistration> Directory;
    std::map<ObjectKey,Assignment> PendingAssignments;
    std::map<Id,OwnershipGroupAuthority> Groups;
    std::optional<MembershipChange> MembershipValue;
    Revision NextMembership=1;
    std::array<double,2> EmptySince{-1,-1};
    bool InitialRosterReady=false;
};
}
