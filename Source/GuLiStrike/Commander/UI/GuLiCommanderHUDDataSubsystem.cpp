#include "Commander/UI/GuLiCommanderHUDDataSubsystem.h"
#include "Commander/Framework/GuLiCommanderPlayerController.h"
#include "Commander/Framework/GuLiCommanderNetSyncComponent.h"
#include "Commander/Network/GuLiSoldierStateReplicator.h"
#include "Battle/Framework/GuLiBattlePlayerState.h"
#include "Battle/Framework/GuLiBattleGameState.h"
#include "Gameplay/CommanderSkills/GuLiCommanderSkillComponent.h"
#include "Gameplay/Teleport/GuLiTeleportInputComponent.h"
#include "Gameplay/Building/GuLiBuildingPlacementComponent.h"
#include "Network/GuLiHybridSessionSubsystem.h"
#include "Network/GuLiHybridWorldSubsystem.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "Misc/CoreDelegates.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

namespace
{
bool SameTaskDisplay(const TArray<FGuLiUnitTaskSummary>& A,const TArray<FGuLiUnitTaskSummary>& B)
{
    if (A.Num()!=B.Num()) return false;
    for (int32 I=0;I<A.Num();++I)
    {
        const auto& X=A[I];const auto& Y=B[I];
        if (X.UnitCount!=Y.UnitCount || X.ManualTaskCount!=Y.ManualTaskCount || X.bStopped!=Y.bStopped
            || X.bWaitingSafeExit!=Y.bWaitingSafeExit || X.Error!=Y.Error || X.RecoverableTasks!=Y.RecoverableTasks || X.Tasks.Num()!=Y.Tasks.Num()) return false;
        for (int32 J=0;J<X.Tasks.Num();++J)
        {
            const auto& P=X.Tasks[J];const auto& Q=Y.Tasks[J];
            if (P.Status!=Q.Status || P.bAutomatic!=Q.bAutomatic || P.DisplayName!=Q.DisplayName
                || P.bHasLocation!=Q.bHasLocation || P.Location!=Q.Location) return false;
        }
    }
    return true;
}
}

void UGuLiCommanderHUDDataSubsystem::Deinitialize()
{Unbind();OnUrgent.Clear();Super::Deinitialize();}

void UGuLiCommanderHUDDataSubsystem::Unbind()
{
    if (auto* H=Hybrid.Get()) H->OnObjectChanged.Remove(ObjectHandle);
    if (auto* R=Roster.Get()) R->OnRosterDelta.Remove(RosterHandle);
    if (auto* S=Sync.Get()) S->OnSelectionChanged.Remove(SelectionHandle);
    if (auto* T=TeleportInput.Get()) T->OnHUDStateChanged.Remove(TeleportHandle);
    if (auto* P=PlayerState.Get())
    {
        P->OnResourcePrivateStateChanged.RemoveDynamic(this,&ThisClass::ChangedResources);
        P->OnCommanderPlayerStateChanged.RemoveDynamic(this,&ThisClass::ChangedPlayer);
    }
    if (auto* C=SkillsSource.Get()) C->OnSkillReply.RemoveDynamic(this,&ThisClass::ChangedSkill);
    FCoreDelegates::OnEndFrame.Remove(FrameHandle);
    Controller.Reset();PlayerState.Reset();SkillsSource.Reset();Roster.Reset();Sync.Reset();Hybrid.Reset();
    Room={};Epoch=0;ArmyRoot={};SelectedObjects.Reset();SelectedSoldiers.Reset();SelectedNumbers.Reset();TeleportInput.Reset();
    View={};Dirty=EGuLiHUDData::All;Urgent=EGuLiHUDData::None;
}
void UGuLiCommanderHUDDataSubsystem::Bind(AGuLiCommanderPlayerController* PC,AGuLiSoldierStateReplicator* InRoster)
{
    auto* PS=PC?PC->GetPlayerState<AGuLiBattlePlayerState>():nullptr;
    auto* W=PC?PC->GetWorld():nullptr;
    const auto* Session=W&&W->GetGameInstance()?W->GetGameInstance()->GetSubsystem<UGuLiHybridSessionSubsystem>():nullptr;
    const FGuid NewRoom=Session?Session->GetRoomId():FGuid{};const uint64 NewEpoch=Session?Session->GetMatchEpoch():0;
    if (Controller==PC && PlayerState==PS && SkillsSource==(PS?PS->GetCommanderSkills():nullptr) && Roster==InRoster && Room==NewRoom && Epoch==NewEpoch) return;
    Unbind();if (!PC || !W) return;
    Controller=PC;PlayerState=PS;Roster=InRoster;Room=NewRoom;Epoch=NewEpoch;
    Sync=PC->GetCommanderNetSyncComponent();Hybrid=W->GetSubsystem<UGuLiHybridWorldSubsystem>();
    TeleportInput=PC->FindComponentByClass<UGuLiTeleportInputComponent>();
    if (auto* T=TeleportInput.Get()) TeleportHandle=T->OnHUDStateChanged.AddUObject(this,&ThisClass::ChangedLocalTeleport);
    if (auto* H=Hybrid.Get()) ObjectHandle=H->OnObjectChanged.AddUObject(this,&ThisClass::ChangedObject);
    if (auto* R=Roster.Get()) RosterHandle=R->OnRosterDelta.AddUObject(this,&ThisClass::ChangedRoster);
    if (auto* S=Sync.Get())
    {SelectionHandle=S->OnSelectionChanged.AddUObject(this,&ThisClass::ChangedSelection);ChangedSelection(S->GetSelectionState());}
    if (PS)
    {
        PS->OnResourcePrivateStateChanged.AddUniqueDynamic(this,&ThisClass::ChangedResources);
        PS->OnCommanderPlayerStateChanged.AddUniqueDynamic(this,&ThisClass::ChangedPlayer);
        SkillsSource=PS->GetCommanderSkills();if (auto* C=SkillsSource.Get()) C->OnSkillReply.AddUniqueDynamic(this,&ThisClass::ChangedSkill);
    }
    FrameHandle=FCoreDelegates::OnEndFrame.AddUObject(this,&ThisClass::EndFrame);
}
void UGuLiCommanderHUDDataSubsystem::ChangedResources() {Invalidate(EGuLiHUDData::Resources);}
void UGuLiCommanderHUDDataSubsystem::ChangedPlayer()
{Invalidate(EGuLiHUDData::Resources|EGuLiHUDData::Roster|EGuLiHUDData::Context);Urgent|=EGuLiHUDData::Context;}
void UGuLiCommanderHUDDataSubsystem::ChangedSkill(const FGuLiActiveSkillReply&)
{Invalidate(EGuLiHUDData::Context|EGuLiHUDData::Tasks);Urgent|=EGuLiHUDData::Context|EGuLiHUDData::Tasks;}
void UGuLiCommanderHUDDataSubsystem::ChangedLocalTeleport()
{Invalidate(EGuLiHUDData::Context);Urgent|=EGuLiHUDData::Context;}
bool UGuLiCommanderHUDDataSubsystem::TrackSelectedObject(const FGuLiHybridObjectKey& Key)
{
    FGuLiHybridObjectState S;FSelectedNumbers Next;
    if (const auto* H=Hybrid.Get();H&&H->GetObjectView(Key,S))
        Next={S.Health.Health,S.Health.MaxHealth,S.Health.Shield,S.Health.MaxShield,S.Definition,S.Team,S.LifeState,S.bPhased};
    const auto* Previous=SelectedNumbers.Find(Key);if (Previous&&*Previous==Next) return false;
    SelectedNumbers.Add(Key,Next);return true;
}
void UGuLiCommanderHUDDataSubsystem::ChangedSelection(const FGuLiCommanderSelectionState& S)
{
    SelectedObjects.Reset();SelectedNumbers.Reset();for (const auto& Key:S.Objects) {SelectedObjects.Add(Key);TrackSelectedObject(Key);}SelectedSoldiers.Reset();
    for (const auto& C:S.Cohorts) for (const auto Id:C.MemberIds) SelectedSoldiers.Add(Id);
    Invalidate(EGuLiHUDData::Selection|EGuLiHUDData::Context|EGuLiHUDData::Tasks);
}
void UGuLiCommanderHUDDataSubsystem::ChangedObject(const FGuLiHybridObjectKey& Key)
{
    if (Key==ArmyRoot || !ArmyRoot.IsValid()) Invalidate(EGuLiHUDData::Context|EGuLiHUDData::Tasks);
    if (!SelectedObjects.Contains(Key)) return;
    if (TrackSelectedObject(Key)) Invalidate(EGuLiHUDData::Selection);
    Invalidate(EGuLiHUDData::Context|EGuLiHUDData::Tasks);
    FGuLiHybridObjectState State;
    if (const auto* H=Hybrid.Get();H && (!H->GetObjectView(Key,State) || State.LifeState!=1))
        Urgent|=EGuLiHUDData::Selection|EGuLiHUDData::Context;
}
void UGuLiCommanderHUDDataSubsystem::ChangedRoster(const FGuLiSoldierRosterDelta& Delta)
{
    bool RosterChanged=Delta.bReset || !Delta.Added.IsEmpty() || !Delta.Removed.IsEmpty();
    for (const auto& [Id,Flags]:Delta.Changed) RosterChanged|=EnumHasAnyFlags(Flags,EGuLiSoldierStateChange::Team|EGuLiSoldierStateChange::Life);
    if (RosterChanged) Invalidate(EGuLiHUDData::Roster);
    bool SelectionChanged=Delta.bReset;
    for (const auto Id:Delta.Removed) if (SelectedSoldiers.Contains(Id)) {SelectionChanged=true;Urgent|=EGuLiHUDData::Selection|EGuLiHUDData::Context;}
    for (const auto& [Id,Flags]:Delta.Changed) if (SelectedSoldiers.Contains(Id)
        && EnumHasAnyFlags(Flags,EGuLiSoldierStateChange::Health|EGuLiSoldierStateChange::Life|EGuLiSoldierStateChange::Team|EGuLiSoldierStateChange::Type|EGuLiSoldierStateChange::Phase))
    {SelectionChanged=true;if (EnumHasAnyFlags(Flags,EGuLiSoldierStateChange::Life)) Urgent|=EGuLiHUDData::Selection|EGuLiHUDData::Context;}
    if (SelectionChanged) Invalidate(EGuLiHUDData::Selection|EGuLiHUDData::Context|EGuLiHUDData::Tasks);
}
void UGuLiCommanderHUDDataSubsystem::EndFrame()
{
    if (!Controller.IsValid()) {Unbind();return;}
    const auto PendingUrgent=Urgent;Urgent=EGuLiHUDData::None;
    if (PendingUrgent!=EGuLiHUDData::None) OnUrgent.Broadcast(PendingUrgent);
}
void UGuLiCommanderHUDDataSubsystem::PollDisplayTime()
{
    const auto* PC=Controller.Get();const auto* C=PC?PC->FindComponentByClass<UGuLiTeleportInputComponent>():nullptr;
    if (const auto* B=PC?PC->GetBuildingPlacementComponent():nullptr;B&&B->IsBuildModeActive()) Invalidate(EGuLiHUDData::Context);
    if (C && (View.Teleport.IsActive() || C->IsAiming()!=View.bTeleportAiming)) Invalidate(EGuLiHUDData::Context);
    if (const auto* S=Sync.Get();S && (S->GetLastTaskFeedback()!=View.TaskFeedback || !SameTaskDisplay(S->GetTaskSummaries(),View.Tasks))) Invalidate(EGuLiHUDData::Tasks);
    const auto* W=PC?PC->GetWorld():nullptr;const auto* GS=W?W->GetGameState():nullptr;
    const double Now=GS?GS->GetServerWorldTimeSeconds():0;
    for (const auto& Skill:View.UnitSkills) if (Skill.Runtime.ReadyAt>ContextTime || Skill.Runtime.ReadyAt>Now) {Invalidate(EGuLiHUDData::Context);break;}
}
const FGuLiCommanderHUDReadState& UGuLiCommanderHUDDataSubsystem::Read(EGuLiHUDData Groups)
{
    const auto Refresh=Dirty&Groups;if (Refresh==EGuLiHUDData::None) return View;
    TRACE_CPUPROFILER_EVENT_SCOPE(GuLiCommanderHUD_BuildReadState);
    FGuLiHybridHUDReadScope QueryScope(Hybrid.Get());
    auto* PC=Controller.Get();auto* P=PlayerState.Get();auto* S=Sync.Get();auto* R=Roster.Get();auto* W=PC?PC->GetWorld():nullptr;
    if (!PC || !W) return View;
    if (EnumHasAnyFlags(Refresh,EGuLiHUDData::Resources)) View.Inventory=P?P->GetResourceInventory():FGuLiResourceAmounts{};
    if (EnumHasAnyFlags(Refresh,EGuLiHUDData::Roster))
    {
        View.BlueTotal=View.BlueAlive=View.RedTotal=View.RedAlive=0;
        if (R) for (const auto& Unit:R->GetItems())
        {
            if (Unit.Team==EGuLiTeam::Blue) {++View.BlueTotal;View.BlueAlive+=Unit.IsAlive()?1:0;}
            if (Unit.Team==EGuLiTeam::Red) {++View.RedTotal;View.RedAlive+=Unit.IsAlive()?1:0;}
        }
    }
    if (EnumHasAnyFlags(Refresh,EGuLiHUDData::Selection))
    {
        const auto* GS=W->GetGameState<AGuLiBattleGameState>();
        const bool Ready=S&&S->IsSoldierStreamReady()&&GS&&(!R||R->GetSnapshotMatchEpoch()==GS->GetMatchEpoch());
        View.Selection=BuildGuLiCommanderUIPresentation(W,S?S->GetSelectionState():FGuLiCommanderSelectionState{},R,Ready);
    }
    if (EnumHasAnyFlags(Refresh,EGuLiHUDData::Resources|EGuLiHUDData::Selection))
    {
        View.bHasSelectedCargo=false;View.SelectedCargo={};
        if (P && View.Selection.Members.Num()==1)
            if (const auto* Private=P->FindMiningVehiclePrivateState(View.Selection.Members[0].Actor))
            {View.SelectedCargo=Private->Cargo;View.bHasSelectedCargo=true;}
    }
    if (EnumHasAnyFlags(Refresh,EGuLiHUDData::Context))
    {
        const auto* GS=W->GetGameState();ContextTime=GS?GS->GetServerWorldTimeSeconds():0;
        View.UnitSkills=P&&P->GetCommanderSkills()?P->GetCommanderSkills()->GetSelectedUnitSkills():TArray<FGuLiUnitSkillRuntimeView>{};
        if (const auto* C=PC->FindComponentByClass<UGuLiTeleportInputComponent>())
        {C->ReadHUDState(View.Teleport,View.TeleportLevel,View.TeleportText);View.bTeleportAiming=C->IsAiming();}
        if (const auto* H=Hybrid.Get();H&&P) ArmyRoot=H->GetTeamCapabilityKey(P->GetTeam(),TEXT("army_state"));
    }
    if (EnumHasAnyFlags(Refresh,EGuLiHUDData::Tasks))
    {
        View.Tasks=S?S->GetTaskSummaries():TArray<FGuLiUnitTaskSummary>{};View.TaskFeedback=S?S->GetLastTaskFeedback():FString{};
        View.SkillReply=P&&P->GetCommanderSkills()?P->GetCommanderSkills()->GetLastReply():FGuLiActiveSkillReply{};
    }
    Dirty&=~Refresh;++View.Revision;return View;
}
