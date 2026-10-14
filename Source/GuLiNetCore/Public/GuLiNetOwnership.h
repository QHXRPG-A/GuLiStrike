#pragma once
#include "GuLiNetTypes.h"
#include <optional>
#include <span>

namespace GuLiNet
{
// Authored by a capability family. The scheduler never switches on ObjectKind.
enum class HandoffReason : std::uint8_t { Selection, DirectOperation, IdleOffload, SharedGroup, ControllerJoin, Placement, Leave };
enum class HandoffActionKind : std::uint8_t { PrepareTarget, FreezeSource, Commit, Cancel };

struct OwnershipSettings
{
    double IdleSeconds=10;
    double EvaluationSeconds=1;
    double HandoffTimeoutSeconds=5;
    double MinimumRelativeGap=.20;
    double MinimumGapMilliseconds=.5;
    std::uint32_t MaximumBackgroundBatch=32;
};
struct OwnershipMember
{
    Id Player;
    Team Faction=Team::None;
    Role Seat=Role::Observer;
    bool Ready=false;
};
struct OwnershipEntry
{
    Authority Grant;
    OwnershipPolicy Policy=OwnershipPolicy::Fixed;
    ObjectKey Parent;
};
struct OwnershipActivity
{
    bool Selected=false;
    double LastOperation=0,LastDeselection=0;
};
struct ObjectHandoff
{
    Revision IdValue=0;
    ObjectKey Object;
    Id Source,Target;
    Id Group;
    Revision PreviousOwnerVersion=0,NextOwnerVersion=0;
    HandoffReason Reason=HandoffReason::Selection;
    double Deadline=0,ReservedMilliseconds=0;
    bool TargetPrepared=false;
    Revision FrozenState=0,InstalledState=0;
};
struct HandoffAction
{
    HandoffActionKind Kind=HandoffActionKind::PrepareTarget;
    ObjectHandoff Transfer;
};
GULINETCORE_API Bytes EncodeHandoff(const ObjectHandoff& Value);
GULINETCORE_API bool DecodeHandoff(std::span<const std::uint8_t> Data,ObjectHandoff& Out);

// Room metadata only: selection, grants, load and handoff acknowledgments.
// Gameplay state travels directly between peers, never through this scheduler.
class GULINETCORE_API OwnershipScheduler
{
public:
    explicit OwnershipScheduler(OwnershipSettings Settings={});
    bool Configure(OwnershipSettings Settings);
    bool SetMember(OwnershipMember Member);
    void RemoveMember(Id Player);
    bool SetObject(OwnershipEntry Object,double Now);
    void RemoveObject(ObjectKey Object);
    bool ReportLoad(Id Player,double SimulationMillisecondsPerTick);
    bool Select(Id Commander,Revision SelectionVersion,std::span<const ObjectKey> Objects,double Now);
    bool Operate(Id Commander,std::span<const ObjectKey> Objects,double Now);
    bool Transfer(ObjectKey Object,Id Target,HandoffReason Reason,double Now,Id Group={});
    // Only the room's join/leave state machine may call this method.
    bool SetMembershipPaused(bool Paused,double Now);
    void Poll(double Now);
    bool TargetPrepared(Id Player,ObjectKey Object,Revision Transfer);
    bool SourceFrozen(Id Player,ObjectKey Object,Revision Transfer,Revision StateVersion);
    bool TargetInstalled(Id Player,ObjectKey Object,Revision Transfer,Revision StateVersion);
    bool Reject(Id Player,ObjectKey Object,Revision Transfer);
    std::vector<HandoffAction> TakeActions();
    const std::map<ObjectKey,ObjectHandoff>& Transfers() const {return Pending;}
    const std::map<ObjectKey,OwnershipEntry>& Objects() const {return Entries;}
    const std::map<ObjectKey,OwnershipActivity>& Activities() const {return Activity;}
    std::optional<double> SmoothedLoad(Id Player) const
    {const auto I=Loads.find(Player);return I==Loads.end() || I->second.Count<3?std::optional<double>{}:I->second.Mean();}
    double ActiveSeconds(double Now) const;
    Revision NextTransferId() const {return NextTransfer;}
    bool SetNextTransferId(Revision Value) {if (!Value || !Pending.empty()) return false;NextTransfer=Value;return true;}
private:
    struct Load
    {
        std::array<double,3> Samples{};
        std::uint32_t Count=0,Next=0;
        double Mean() const;
    };
    bool EligibleCommander(Id Player,ObjectKey Object) const;
    bool Request(ObjectKey Object,Id Target,HandoffReason Reason,double Now,double Reserved=0,Id Group={});
    bool RequestOne(ObjectKey Object,Id Target,HandoffReason Reason,double Now,double Reserved,Id Group);
    void Cancel(ObjectKey Object);
    void TryCommit(ObjectKey Object);
    double Weight(Id Player) const;
    double Reserved(Id Player) const;
    double Estimate(ObjectKey Object,Id Target) const;
    void Offload(Team Faction,double Now);
    OwnershipSettings Config;
    std::map<Id,OwnershipMember> Members;
    std::map<Id,Load> Loads;
    std::map<Id,Revision> Selections;
    std::map<Id,std::set<ObjectKey>> SelectionSets;
    std::map<ObjectKey,OwnershipEntry> Entries;
    std::map<ObjectKey,OwnershipActivity> Activity;
    std::map<ObjectKey,ObjectHandoff> Pending;
    std::vector<HandoffAction> Actions;
    Revision NextTransfer=1;
    double NextEvaluation=0,PausedAt=0,PausedDuration=0,LastWallTime=0;
    bool MembershipPaused=false;
};
}
