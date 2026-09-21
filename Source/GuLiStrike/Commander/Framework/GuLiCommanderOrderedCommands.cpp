#include "Commander/Framework/GuLiCommanderNetSyncComponent.h"
#include "Gameplay/Data/GuLiGameText.h"
#include "Commander/Framework/GuLiCommanderPlayerController.h"
#include "Commander/Orders/GuLiUnitTaskSubsystem.h"
#include "Commander/Orders/GuLiSpecialTaskCatalog.h"
#include "Commander/Mass/GuLiBattleAuthoritySubsystem.h"
#include "Battle/Framework/GuLiBattlePlayerState.h"
#include "Gameplay/Units/GuLiEngineeringTravelComponent.h"
#include "Battle/Combat/GuLiCombatDamageLedger.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"

const TArray<FGuLiUnitTaskSummary>& UGuLiCommanderNetSyncComponent::GetTaskSummaries() const
{
	static const TArray<FGuLiUnitTaskSummary> Empty;
	return DisplayedTaskSelectionRevision == SelectionState.SelectionRevision ? TaskSummaries : Empty;
}

void UGuLiCommanderNetSyncComponent::SubmitOrderedSelection(FGuLiSelectionRequest Request)
{
	const uint32 Sequence = NextOrderedSequence++; if (!NextOrderedSequence) ++NextOrderedSequence;
	PendingOrderedSelections.Add(Sequence); ServerOrderedSelection(Request, Sequence, GetConnectionGeneration());
}
bool UGuLiCommanderNetSyncComponent::SubmitPanelSelection(FGuLiPanelSelectionRequest Request)
{
	// Portraits display a confirmed snapshot. Do not reinterpret a stale tile after another selection intent.
	if (!IsConnectionReady() || HasUnresolvedSelectionIntent())
	{ LastTaskFeedback = GuLiGameText::Text(TEXT("UI.OrderedCommands.102")); NotifySelectionChanged(); return false; }
	Request.SelectionRevision = SelectionState.SelectionRevision;
	const uint32 Sequence = NextOrderedSequence++; if (!NextOrderedSequence) ++NextOrderedSequence;
	PendingOrderedSelections.Add(Sequence);
	ServerPanelSelection(Request, Sequence, GetConnectionGeneration());
	return true;
}

void UGuLiCommanderNetSyncComponent::ServerPanelSelection_Implementation(
	FGuLiPanelSelectionRequest Request, uint32 Sequence, uint32 Generation)
{
	auto Reject = [&](const TCHAR* Reason)
	{
		FGuLiCommandAck Ack; InitializeAck(Ack, Sequence, EGuLiCommandKind::Selection);
		Ack.Result = EGuLiCommandAckResult::InvalidRequest;
		ClientTaskReceipt(Ack, Reason, Generation);
		ClientOrderedSelection(SelectionState, Sequence, false, Generation);
	};
	if (!AdmitOrderedSequence(Sequence, Generation))
	{ Reject(GuLiGameText::Text(TEXT("UI.OrderedCommands.103"))); return; }
	if (Request.SelectionRevision != SelectionState.SelectionRevision
		|| Request.SoldierId.IsValid() == Request.ActorId.IsValid()
		|| uint8(Request.Action) > uint8(EGuLiPanelSelectionAction::RemoveGroup))
	{ bOrderedSelectionValid = false; Reject(GuLiGameText::Text(TEXT("UI.OrderedCommands.104"))); return; }
	auto* Authority = GetWorld()->GetSubsystem<UGuLiBattleAuthoritySubsystem>();
	const EGuLiTeam Team = GetBattlePlayerState()->GetTeam();
	TMap<FGuLiSoldierId, uint16> Soldiers;
	TMap<FGuLiControllableActorId, uint16> Actors;
	for (const auto& Cohort : SelectionState.Cohorts) for (auto Id : Cohort.MemberIds)
	{
		EGuLiTeam Actual; uint16 Type; FVector Position;
		if (Authority->GetTaskSoldierInfo(Id, Actual, Type, Position) && Actual == Team) Soldiers.Add(Id, Type);
	}
	for (TActorIterator<APawn> It(GetWorld()); It; ++It)
		if (const auto* Vehicle = Cast<IGuLiEngineeringVehicle>(*It); Vehicle && Vehicle->GetTeam() == Team
			&& SelectionState.ActorIds.Contains(Vehicle->GetStableActorId()))
			if (const auto* Health = It->FindComponentByClass<UGuLiCombatHealthComponent>(); Health && Health->IsAlive())
				Actors.Add(Vehicle->GetStableActorId(), uint16(Vehicle->GetUnitTypeId()));
	const uint16* Type = Request.SoldierId.IsValid() ? Soldiers.Find(Request.SoldierId) : Actors.Find(Request.ActorId);
	if (!Type) { bOrderedSelectionValid = false; Reject(GuLiGameText::Text(TEXT("UI.OrderedCommands.105"))); return; }
	const uint16 RequestedType = *Type;
	TSet<FGuLiSoldierId> GroupSoldiers;
	TSet<FGuLiControllableActorId> GroupActors;
	if (Request.Action == EGuLiPanelSelectionAction::SingleGroup
		|| Request.Action == EGuLiPanelSelectionAction::RemoveGroup)
	{
		TArray<FGuLiSoldierId> TypeSoldiers;
		TArray<FGuLiControllableActorId> TypeActors;
		for (const auto& Item : Soldiers) if (Item.Value == RequestedType) TypeSoldiers.Add(Item.Key);
		for (const auto& Item : Actors) if (Item.Value == RequestedType) TypeActors.Add(Item.Key);
		TypeSoldiers.Sort([](auto A, auto B){ return A.Value < B.Value; });
		TypeActors.Sort([](auto A, auto B){ return A.Value < B.Value; });
		const int32 AnchorIndex = Request.SoldierId.IsValid()
			? TypeSoldiers.IndexOfByKey(Request.SoldierId)
			: TypeSoldiers.Num() + TypeActors.IndexOfByKey(Request.ActorId);
		if (AnchorIndex < 0)
		{
			bOrderedSelectionValid = false;
			Reject(GuLiGameText::Text(TEXT("UI.OrderedCommands.105")));
			return;
		}
		const int32 GroupStart = (AnchorIndex / GULI_PANEL_PORTRAIT_GROUP_SIZE)
			* GULI_PANEL_PORTRAIT_GROUP_SIZE;
		const int32 GroupEnd = FMath::Min(
			GroupStart + GULI_PANEL_PORTRAIT_GROUP_SIZE,
			TypeSoldiers.Num() + TypeActors.Num());
		for (int32 Index = GroupStart; Index < GroupEnd; ++Index)
		{
			if (Index < TypeSoldiers.Num()) GroupSoldiers.Add(TypeSoldiers[Index]);
			else GroupActors.Add(TypeActors[Index - TypeSoldiers.Num()]);
		}
	}
	auto Keep = [&](bool bSameMember, bool bInGroup, uint16 UnitType)
	{
		switch (Request.Action)
		{
		case EGuLiPanelSelectionAction::Single: return bSameMember;
		case EGuLiPanelSelectionAction::Remove: return !bSameMember;
		case EGuLiPanelSelectionAction::KeepType: return UnitType == RequestedType;
		case EGuLiPanelSelectionAction::RemoveType: return UnitType != RequestedType;
		case EGuLiPanelSelectionAction::SingleGroup: return bInGroup;
		case EGuLiPanelSelectionAction::RemoveGroup: return !bInGroup;
		default: return false;
		}
	};
	TArray<FGuLiSoldierId> NextSoldiers; TArray<FGuLiControllableActorId> NextActors;
	for (const auto& Item : Soldiers) if (Keep(Item.Key == Request.SoldierId, GroupSoldiers.Contains(Item.Key), Item.Value)) NextSoldiers.Add(Item.Key);
	for (const auto& Item : Actors) if (Keep(Item.Key == Request.ActorId, GroupActors.Contains(Item.Key), Item.Value)) NextActors.Add(Item.Key);
	NextSoldiers.Sort([](auto A, auto B){ return A.Value < B.Value; });
	NextActors.Sort([](auto A, auto B){ return A.Value < B.Value; });
	bOrderedSelectionValid = Authority->SetExplicitSelection(Team, NextSoldiers, NextActors, SelectionState);
	ClientOrderedSelection(SelectionState, Sequence, false, Generation);
	NotifySelectionChanged(); NextOrderedSummaryTime = 0;
}
void UGuLiCommanderNetSyncComponent::SubmitOrderedTask(FGuLiUnitTaskCommand Command)
{
	const uint32 Sequence = NextOrderedSequence++; if (!NextOrderedSequence) ++NextOrderedSequence;
	PendingOrderedTasks.Add(Command.CommandId);
	if (Command.Disposition != EGuLiTaskDisposition::Stop)
	{
		FGuLiMoveRequest Preview; Preview.ClientCommandId = Command.CommandId; Preview.Target = Command.Target;
		OnMoveReadyToSend.Broadcast(Preview, SelectionState);
	}
	ServerOrderedTask(Command, Sequence, GetConnectionGeneration());
}
void UGuLiCommanderNetSyncComponent::SubmitControlGroup(uint8 Slot, bool bSet, bool bAppend, bool bSteal, bool bFocus)
{
	const uint32 Sequence = NextOrderedSequence++; if (!NextOrderedSequence) ++NextOrderedSequence;
	PendingOrderedSelections.Add(Sequence); ServerControlGroup(Slot, bSet, bAppend, bSteal, bFocus, Sequence, GetConnectionGeneration());
}
bool UGuLiCommanderNetSyncComponent::AdmitOrderedSequence(uint32 Sequence, uint32 Generation)
{
	FGuLiCommandAck Ack;
	if (!Sequence || Generation != GetConnectionGeneration() || !CanProcessCommanderRequest(Ack, TEXT("CommanderOrderedInput"))) return false;
	if (OrderedGeneration != Generation) { OrderedGeneration = Generation; LastOrderedSequence = 0; }
	if (LastOrderedSequence && !IsNewerSerial(Sequence, LastOrderedSequence)) return false;
	LastOrderedSequence = Sequence; return true;
}
void UGuLiCommanderNetSyncComponent::ServerOrderedSelection_Implementation(FGuLiSelectionRequest Request, uint32 Sequence, uint32 Generation)
{
	if (!AdmitOrderedSequence(Sequence, Generation))
	{
		if (Generation == GetConnectionGeneration() && IsNewerSerial(Sequence, LastOrderedSequence)) bOrderedSelectionValid = false;
		ClientOrderedSelection(SelectionState, Sequence, false, Generation); return;
	}
	// Reliable stream order, rather than an asynchronously replicated client revision, identifies the selection predecessor.
	Request.KnownSelectionRevision = SelectionState.SelectionRevision;
	FGuLiCommandAck Ack;
	bOrderedSelectionValid = GetWorld()->GetSubsystem<UGuLiBattleAuthoritySubsystem>()->ResolveSelection(*GetBattlePlayerState(), Request, SelectionState, Ack);
	if (!bOrderedSelectionValid) ClientTaskReceipt(Ack, GuLiGameText::Text(TEXT("UI.OrderedCommands.106")), Generation);
	ClientOrderedSelection(SelectionState, Sequence, false, Generation); NotifySelectionChanged(); NextOrderedSummaryTime = 0;
}
void UGuLiCommanderNetSyncComponent::ServerOrderedTask_Implementation(FGuLiUnitTaskCommand Command, uint32 Sequence, uint32 Generation)
{
	FGuLiCommandAck Ack; InitializeAck(Ack, Command.CommandId, EGuLiCommandKind::Move);
	if (!AdmitOrderedSequence(Sequence, Generation) || !bOrderedSelectionValid)
	{ Ack.Result = EGuLiCommandAckResult::InvalidRequest; ClientTaskReceipt(Ack, GuLiGameText::Text(TEXT("UI.OrderedCommands.107")), Generation); return; }
	if (!SelectionState.SelectionRevision) SelectionState.SelectionRevision = 1;
	Command.SelectionRevision = SelectionState.SelectionRevision;
	auto* Tasks = GetWorld()->GetSubsystem<UGuLiUnitTaskSubsystem>();
	// The client requests task semantics only. The server resolves context and the authored class.
	Tasks->BuildContextCommand(SelectionState, Command);
	const auto* Catalog = GetWorld()->GetSubsystem<UGuLiSpecialTaskCatalog>();
	const auto* Special = Catalog ? Catalog->Find(Command.SpecialTaskId) : nullptr;
	const FName Intent = Command.TargetIntent;
	const bool bTargetMatches = Intent.IsNone()
		|| (Intent == TEXT("Move") && Command.Kind == EGuLiUnitTaskKind::Move)
		|| (Intent == TEXT("Mine") && Command.Kind == EGuLiUnitTaskKind::Special && Special && Special->Tag == FGameplayTag::RequestGameplayTag(TEXT("Task.Special.Mining")))
		|| (Intent == TEXT("Construct") && Command.Kind == EGuLiUnitTaskKind::Special && Special && Special->Tag == FGameplayTag::RequestGameplayTag(TEXT("Task.Special.Construction")))
		|| (Intent == TEXT("Return") && Command.Kind == EGuLiUnitTaskKind::ReturnToFactory)
		|| (Intent == TEXT("Transit") && Command.Kind == EGuLiUnitTaskKind::Transit);
	if (!bTargetMatches)
	{
		Ack.Result = EGuLiCommandAckResult::InvalidTarget;
		ClientTaskReceipt(Ack, GuLiGameText::Text(TEXT("UI.OrderedCommands.108")), Generation); return;
	}
	int32 Accepted = 0, Rejected = 0; FString Message;
	TSet<FGuLiTaskUnitId> AcceptedUnits;
	const bool bAccepted = Tasks->Submit(*GetBattlePlayerState(), SelectionState, Command, Message, Accepted, Rejected, &AcceptedUnits);
	// The same frozen membership used for admission defines the per-member result masks.
	for (const auto& Cohort : SelectionState.Cohorts)
	{
		auto& Result = Ack.CohortResults.AddDefaulted_GetRef(); Result.CohortId = Cohort.CohortId;
		Result.MemberCount = uint8(Cohort.MemberIds.Num());
		for (int32 Index = 0; Index < Cohort.MemberIds.Num(); ++Index)
			if (AcceptedUnits.Contains(FGuLiTaskUnitId::Soldier(Cohort.MemberIds[Index]))) Result.AcceptedMemberMask |= 1u << Index;
		Result.EligibleMemberMask = Result.AcceptedMemberMask;
		Result.Result = !Result.AcceptedMemberMask ? EGuLiCommandAckResult::InvalidTarget
			: Result.GetAcceptedMemberCount() == Result.MemberCount ? EGuLiCommandAckResult::Accepted : EGuLiCommandAckResult::PartiallyAccepted;
	}
	for (auto Id : SelectionState.ActorIds)
		Ack.EngineeringResults.Add({Id, AcceptedUnits.Contains(FGuLiTaskUnitId::Actor(Id)) ? EGuLiTransitOrderResult::Accepted : EGuLiTransitOrderResult::InvalidTarget});
	Ack.Result = !bAccepted ? EGuLiCommandAckResult::InvalidTarget : Rejected ? EGuLiCommandAckResult::PartiallyAccepted : EGuLiCommandAckResult::Accepted;
	Ack.ServerSelectionRevision = SelectionState.SelectionRevision;
	const FString Counts = GuLiGameText::Format(TEXT("UI.OrderedCommands.101"), {FString::Printf(TEXT("%d"), Accepted), FString::Printf(TEXT("%d"), Rejected)});
	ClientTaskReceipt(Ack, Message.IsEmpty() ? Counts : Counts + TEXT("：") + Message, Generation); NextOrderedSummaryTime = 0;
}
void UGuLiCommanderNetSyncComponent::ClientOrderedSelection_Implementation(const FGuLiCommanderSelectionState& State,
	uint32 Sequence, bool bFocus, uint32 Generation)
{
	PendingOrderedSelections.Remove(Sequence);
	if (Generation != GetConnectionGeneration() || !IsConnectionReady()) return;
	if (Sequence && LastOrderedSelectionSequence && !IsNewerSerial(Sequence, LastOrderedSelectionSequence)) return;
	if (!Sequence && State.SelectionRevision != SelectionState.SelectionRevision && !IsNewerSerial(State.SelectionRevision, SelectionState.SelectionRevision)) return;
	if (Sequence) LastOrderedSelectionSequence = Sequence;
	SelectionState = State; LastAppliedClientSelection = State;
	NotifySelectionChanged();
	if (bFocus) if (auto* Controller = Cast<AGuLiCommanderPlayerController>(GetOwner())) Controller->FocusSelectedUnits();
}
void UGuLiCommanderNetSyncComponent::ClientTaskReceipt_Implementation(const FGuLiCommandAck& Ack, const FString& Message, uint32 Generation)
{
	if (Ack.CommandKind == EGuLiCommandKind::Move) PendingOrderedTasks.Remove(Ack.ClientCommandId);
	if (Generation != GetConnectionGeneration() || !IsConnectionReady()) return;
	LastTaskFeedback = Message; LastCommandAck = Ack;
	TGuardValue<bool> TaskReceiptScope(bDispatchingTaskReceipt, true);
	OnCommandAckChanged.Broadcast(Ack);
}
void UGuLiCommanderNetSyncComponent::PruneControlGroup(FGuLiCommanderControlGroup& Group, EGuLiTeam Team) const
{
	const auto* Authority = GetWorld()->GetSubsystem<UGuLiBattleAuthoritySubsystem>();
	Group.Soldiers.RemoveAll([&](auto Id) { EGuLiTeam Actual; uint16 Type; FVector Position;
		return !Authority->GetTaskSoldierInfo(Id, Actual, Type, Position) || Actual != Team; });
	TSet<FGuLiControllableActorId> Live;
	for (TActorIterator<APawn> It(GetWorld()); It; ++It)
		if (const auto* Vehicle = Cast<IGuLiEngineeringVehicle>(*It); Vehicle && Vehicle->GetTeam() == Team)
			if (const auto* Health = It->FindComponentByClass<UGuLiCombatHealthComponent>(); Health && Health->IsAlive()) Live.Add(Vehicle->GetStableActorId());
	Group.Actors.RemoveAll([&](auto Id) { return !Live.Contains(Id); });
}
void UGuLiCommanderNetSyncComponent::ServerControlGroup_Implementation(uint8 Slot, bool bSet, bool bAppend,
	bool bSteal, bool bFocus, uint32 Sequence, uint32 Generation)
{
	if (!AdmitOrderedSequence(Sequence, Generation) || Slot >= 10) { ClientOrderedSelection(SelectionState, Sequence, false, Generation); return; }
	ControlGroups.SetNum(10); auto& Group = ControlGroups[Slot];
	const EGuLiTeam Team = GetBattlePlayerState()->GetTeam(); PruneControlGroup(Group, Team);
	if (bSet || bAppend)
	{
		FGuLiCommanderControlGroup Next = bAppend ? Group : FGuLiCommanderControlGroup{};
		for (const auto& Cohort : SelectionState.Cohorts) for (auto Id : Cohort.MemberIds) Next.Soldiers.AddUnique(Id);
		for (auto Id : SelectionState.ActorIds) Next.Actors.AddUnique(Id);
		PruneControlGroup(Next, Team);
		if (Next.Soldiers.Num() > int32(GULI_MAX_CONTROL_COHORTS * GULI_CONTROL_COHORT_TARGET_SIZE)
			|| Next.Actors.Num() > int32(GULI_MAX_CONTROLLABLE_ACTOR_SELECTION))
		{
			FGuLiCommandAck Ack; InitializeAck(Ack, Sequence, EGuLiCommandKind::Selection); Ack.Result = EGuLiCommandAckResult::InvalidRequest;
			ClientTaskReceipt(Ack, GuLiGameText::Text(TEXT("UI.OrderedCommands.109")), Generation); ClientOrderedSelection(SelectionState, Sequence, false, Generation); return;
		}
		Group = MoveTemp(Next);
		if (bSteal)
		{
			TSet<FGuLiSoldierId> Selected; for (const auto& Cohort : SelectionState.Cohorts) for (auto Id : Cohort.MemberIds) Selected.Add(Id);
			for (int32 Index=0; Index<10; ++Index) if (Index != Slot)
			{ ControlGroups[Index].Soldiers.RemoveAll([&](auto Id){ return Selected.Contains(Id); });
			  ControlGroups[Index].Actors.RemoveAll([&](auto Id){ return SelectionState.ActorIds.Contains(Id); }); }
		}
	}
	else if (!Group.Soldiers.IsEmpty() || !Group.Actors.IsEmpty())
	{
		GetWorld()->GetSubsystem<UGuLiBattleAuthoritySubsystem>()->SetExplicitSelection(Team, Group.Soldiers, Group.Actors, SelectionState);
		bOrderedSelectionValid = true;
	}
	ClientOrderedSelection(SelectionState, Sequence, bFocus && (!Group.Soldiers.IsEmpty() || !Group.Actors.IsEmpty()), Generation);
	NotifySelectionChanged(); NextOrderedSummaryTime = 0;
}
void UGuLiCommanderNetSyncComponent::TickOrderedCommands()
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || !GetWorld() || !GetBattlePlayerState() || !GetBattlePlayerState()->IsCommander()) return;
	if (!IsConnectionReady()) return;
	if (GetWorld()->GetTimeSeconds() < NextOrderedSummaryTime) return;
	NextOrderedSummaryTime = GetWorld()->GetTimeSeconds() + .2;
	if (TaskSnapshotSelectionRevision != SelectionState.SelectionRevision) TaskSnapshotOffset = INDEX_NONE;
	if (TaskSnapshotOffset == INDEX_NONE)
	{
	ControlGroups.SetNum(10); ControlGroupCounts.SetNum(10);
	TSet<FGuLiSoldierId> SelectedSoldiers;
	for (const auto& Cohort : SelectionState.Cohorts) for (auto Id : Cohort.MemberIds) SelectedSoldiers.Add(Id);
	RelatedControlGroups = 0;
	for (int32 Index=0; Index<10; ++Index)
	{
		PruneControlGroup(ControlGroups[Index], GetBattlePlayerState()->GetTeam());
		const auto& Group = ControlGroups[Index]; ControlGroupCounts[Index] = Group.Soldiers.Num() + Group.Actors.Num();
		if (Group.Soldiers.ContainsByPredicate([&](auto Id){ return SelectedSoldiers.Contains(Id); })
			|| Group.Actors.ContainsByPredicate([&](auto Id){ return SelectionState.ActorIds.Contains(Id); })) RelatedControlGroups |= uint16(1 << Index);
	}
	GetWorld()->GetSubsystem<UGuLiUnitTaskSubsystem>()->BuildSummary(SelectionState, TaskSummaries);
	DisplayedTaskSelectionRevision = SelectionState.SelectionRevision;
	bool bChanged = TaskSnapshotRevision == 0 || TaskSnapshotSelectionRevision != SelectionState.SelectionRevision
		|| PendingGroupCounts != ControlGroupCounts || PendingRelatedGroups != RelatedControlGroups || PendingTaskSnapshot.Num() != TaskSummaries.Num();
	for (int32 Index = 0; !bChanged && Index < TaskSummaries.Num(); ++Index)
		bChanged = !FGuLiUnitTaskSummary::StaticStruct()->CompareScriptStruct(&TaskSummaries[Index], &PendingTaskSnapshot[Index], 0);
	if (!bChanged) return;
	PendingTaskSnapshot = TaskSummaries; PendingGroupCounts = ControlGroupCounts; PendingRelatedGroups = RelatedControlGroups;
	TaskSnapshotSelectionRevision = SelectionState.SelectionRevision;
	if (!++TaskSnapshotRevision) ++TaskSnapshotRevision;
	TaskSnapshotOffset = 0;
	}
	// Bound reliable traffic even when every selected unit owns a different 32-command queue.
	for (int32 Budget = 0; Budget < 4 && TaskSnapshotOffset != INDEX_NONE; ++Budget)
	{
		const int32 Count = FMath::Min(2, PendingTaskSnapshot.Num() - TaskSnapshotOffset);
		TArray<FGuLiUnitTaskSummary> Chunk;
		for (int32 Index = 0; Index < Count; ++Index) Chunk.Add(PendingTaskSnapshot[TaskSnapshotOffset + Index]);
		ClientTaskSnapshot(TaskSnapshotRevision, PendingTaskSnapshot.Num(), TaskSnapshotOffset, Chunk,
			TaskSnapshotOffset == 0 ? PendingGroupCounts : TArray<int32>{}, PendingRelatedGroups, TaskSnapshotSelectionRevision, GetConnectionGeneration());
		TaskSnapshotOffset += Count;
		if (TaskSnapshotOffset >= PendingTaskSnapshot.Num()) TaskSnapshotOffset = INDEX_NONE;
	}
}

void UGuLiCommanderNetSyncComponent::ClientTaskSnapshot_Implementation(uint32 Revision, int32 Total, int32 Offset,
	const TArray<FGuLiUnitTaskSummary>& Chunk, const TArray<int32>& Counts, uint16 RelatedGroups, uint32 SelectionRevision, uint32 Generation)
{
	if (Generation != GetConnectionGeneration() || !IsConnectionReady() || Total < 0 || Total > 10064 || Offset < 0 || Offset + Chunk.Num() > Total) return;
	if (Offset == 0)
	{
		if (ReceivedTaskSnapshotRevision && !IsNewerSerial(Revision, ReceivedTaskSnapshotRevision)) return;
		ReceivedTaskSnapshotRevision = Revision; ReceivedTaskSnapshot.Reset(); ReceivedGroupCounts = Counts; ReceivedRelatedGroups = RelatedGroups;
	}
	if (Revision != ReceivedTaskSnapshotRevision || Offset != ReceivedTaskSnapshot.Num()) return;
	ReceivedTaskSnapshot.Append(Chunk);
	if (ReceivedTaskSnapshot.Num() == Total)
	{
		TaskSummaries = MoveTemp(ReceivedTaskSnapshot); ControlGroupCounts = ReceivedGroupCounts; RelatedControlGroups = ReceivedRelatedGroups;
		DisplayedTaskSelectionRevision = SelectionRevision;
	}
}
