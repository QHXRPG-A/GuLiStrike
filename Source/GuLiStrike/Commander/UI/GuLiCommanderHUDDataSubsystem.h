#pragma once
#include "Subsystems/LocalPlayerSubsystem.h"
#include "Gameplay/Resources/GuLiResourceTypes.h"
#include "Commander/UI/GuLiCommanderUIPresentation.h"
#include "Commander/Orders/GuLiUnitTaskTypes.h"
#include "Gameplay/CommanderSkills/GuLiCommanderSkillTypes.h"
#include "Gameplay/Teleport/GuLiTeleportTypes.h"
#include "GuLiCommanderHUDDataSubsystem.generated.h"

class AGuLiCommanderPlayerController;
class AGuLiBattlePlayerState;
class AGuLiSoldierStateReplicator;
class UGuLiCommanderNetSyncComponent;
class UGuLiHybridWorldSubsystem;
struct FGuLiSoldierRosterDelta;

enum class EGuLiHUDData : uint8
{
    None=0,Resources=1,Roster=2,Selection=4,Context=8,Tasks=16,All=31
};
ENUM_CLASS_FLAGS(EGuLiHUDData);

/** Display facts only. Gameplay admission always reads the current World replica. */
struct FGuLiCommanderHUDReadState
{
    uint64 Revision=0;
    FGuLiCommanderUIPresentation Selection;
    FGuLiResourceAmounts Inventory;
    FGuLiResourceAmounts SelectedCargo;
    bool bHasSelectedCargo=false;
    int32 BlueTotal=0,BlueAlive=0,RedTotal=0,RedAlive=0;
    FGuLiTeleportCastState Teleport;
    FText TeleportText;
    int32 TeleportLevel=1;
    bool bTeleportAiming=false;
    TArray<FGuLiUnitSkillRuntimeView> UnitSkills;
    TArray<FGuLiUnitTaskSummary> Tasks;
    FString TaskFeedback;
    FGuLiActiveSkillReply SkillReply;
};
DECLARE_MULTICAST_DELEGATE_OneParam(FGuLiHUDUrgentData,EGuLiHUDData);

/** One room-bound read model per LocalPlayer; survives widget reconstruction. */
UCLASS()
class GULISTRIKE_API UGuLiCommanderHUDDataSubsystem final : public ULocalPlayerSubsystem
{
    GENERATED_BODY()
public:
    virtual void Deinitialize() override;
    void Bind(AGuLiCommanderPlayerController* Player,AGuLiSoldierStateReplicator* Roster);
    void Invalidate(EGuLiHUDData Groups) { Dirty|=Groups; }
    const FGuLiCommanderHUDReadState& Read(EGuLiHUDData Groups);
    EGuLiHUDData Pending() const { return Dirty; }
    void PollDisplayTime();
    FGuLiHUDUrgentData OnUrgent;
private:
    void Unbind();
    void ChangedObject(const FGuLiHybridObjectKey& Key);
    void ChangedRoster(const FGuLiSoldierRosterDelta& Delta);
    bool TrackSelectedObject(const FGuLiHybridObjectKey& Key);
    void ChangedLocalTeleport();
    void ChangedSelection(const FGuLiCommanderSelectionState& Selection);
    void EndFrame();
    UFUNCTION() void ChangedResources();
    UFUNCTION() void ChangedPlayer();
    UFUNCTION() void ChangedSkill(const FGuLiActiveSkillReply& Reply);
    TWeakObjectPtr<AGuLiCommanderPlayerController> Controller;
    TWeakObjectPtr<AGuLiBattlePlayerState> PlayerState;
    TWeakObjectPtr<class UGuLiCommanderSkillComponent> SkillsSource;
    TWeakObjectPtr<AGuLiSoldierStateReplicator> Roster;
    TWeakObjectPtr<UGuLiCommanderNetSyncComponent> Sync;
    TWeakObjectPtr<UGuLiHybridWorldSubsystem> Hybrid;
    FDelegateHandle ObjectHandle,RosterHandle,SelectionHandle,FrameHandle;
    FDelegateHandle TeleportHandle;
    TWeakObjectPtr<class UGuLiTeleportInputComponent> TeleportInput;
    FGuid Room;
    uint64 Epoch=0;
    FGuLiHybridObjectKey ArmyRoot;
    TSet<FGuLiHybridObjectKey> SelectedObjects;
    TSet<FGuLiSoldierId> SelectedSoldiers;
    struct FSelectedNumbers
    {
        float Health=0,MaxHealth=0,Shield=0,MaxShield=0;
        FName Definition;EGuLiTeam Team=EGuLiTeam::Unassigned;uint8 Life=0;bool Phased=false;
        bool operator==(const FSelectedNumbers&) const=default;
    };
    TMap<FGuLiHybridObjectKey,FSelectedNumbers> SelectedNumbers;
    EGuLiHUDData Dirty=EGuLiHUDData::All,Urgent=EGuLiHUDData::None;
    FGuLiCommanderHUDReadState View;
    double ContextTime=0;
};
