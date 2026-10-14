#include "TimerManager.h"
#include "Commander/Network/GuLiMoveLatency.h"
#include "Commander/Framework/GuLiCommanderNetSyncComponent.h"
#include "Gameplay/Data/GuLiGameText.h"
#include "Commander/Framework/GuLiCommanderPlayerController.h"
#include "Commander/Orders/GuLiUnitTaskSubsystem.h"
#include "Commander/Behavior/GuLiCommanderBehaviorSchema.h"
#include "Commander/Mass/GuLiBattleAuthoritySubsystem.h"
#include "Battle/Framework/GuLiBattlePlayerState.h"
#include "Gameplay/Units/GuLiEngineeringTravelComponent.h"
#include "Battle/Combat/GuLiCombatDamageLedger.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "Network/GuLiNetworkSettings.h"
#include "Network/GuLiHybridAdapterRegistry.h"
#include "Commander/Mass/GuLiCommanderSelectionQuery.h"

namespace
{
bool SelectAuthoredObjects(UWorld& W,const FGuLiSelectionRequest& Input,FGuLiCommanderSelectionState& Selection)
{
    auto* H=W.GetSubsystem<UGuLiHybridWorldSubsystem>();if (!H) return false;
    TArray<FGuLiHybridObjectState> Records;H->GetObjects(Records);
    TArray<GuLiCommanderSelectionQuery::FCandidate> Population;TArray<FGuLiHybridObjectKey> Keys;
    TMap<FName,uint16> Types;EGuLiTeam Team=EGuLiTeam::Unassigned;
    for (const auto& O:Records)
    {
        const auto* Actor=H->FindBoundActor(O.Key);
        if (!Actor || Cast<IGuLiEngineeringVehicle>(Actor) || !H->HasControl(O.Key)
            || O.OwnershipPolicy!=uint8(EGuLiHybridOwnershipPolicy::CommanderActivity)) continue;
        auto* Type=Types.Find(O.Definition);if (!Type) Type=&Types.Add(O.Definition,uint16(Types.Num()+1));
        Team=O.Team;Keys.Add(O.Key);auto& C=Population.AddDefaulted_GetRef();C.SoldierId=FGuLiSoldierId(Keys.Num());
        C.Location=Actor->GetActorLocation();C.Team=O.Team;C.UnitTypeId=*Type;C.WorldBounds=Actor->GetComponentsBoundingBox(true);
    }
    auto Request=Input;TArray<FGuLiSoldierId> Hits,Existing,Combined;
    for (int32 I=0;I<Keys.Num();++I) if (Selection.Objects.Contains(Keys[I])) Existing.Add(FGuLiSoldierId(I+1));
    if (Request.SeedObject.IsValid())
    {
        const int32 Index=Keys.IndexOfByKey(Request.SeedObject);if (Index==INDEX_NONE) return false;
        Request.SeedSoldierId=FGuLiSoldierId(Index+1);Request.SeedObject={};
    }
    else if (Request.Kind==EGuLiSelectionKind::Point || Request.Kind==EGuLiSelectionKind::SameType)
    {Request.Modifier=Input.Modifier;Population.Reset();}
    if (!Population.IsEmpty() && !GuLiCommanderSelectionQuery::ResolveCandidates(Request,Team,Population,Hits,&W)) return false;
    GuLiCommanderSelectionQuery::CombineMembership(Existing,Hits,Input.Modifier,Combined);
    TArray<FGuLiHybridObjectKey> Next;for (auto Id:Combined) if (Keys.IsValidIndex(Id.Value-1)) Next.Add(Keys[Id.Value-1]);
    if (Next!=Selection.Objects) {Selection.Objects=MoveTemp(Next);if (!++Selection.SelectionRevision) ++Selection.SelectionRevision;}
    return true;
}
}

const TArray<FGuLiUnitTaskSummary>& UGuLiCommanderNetSyncComponent::GetTaskSummaries() const
{
	static const TArray<FGuLiUnitTaskSummary> Empty;
	return DisplayedTaskSelectionRevision == SelectionState.SelectionRevision ? TaskSummaries : Empty;
}

void UGuLiCommanderNetSyncComponent::SubmitOrderedSelection(FGuLiSelectionRequest Request)
{
	const uint32 Sequence = NextOrderedSequence++; if (!NextOrderedSequence) ++NextOrderedSequence;

        Request.KnownSelectionRevision=SelectionState.SelectionRevision;
        FGuLiCommandAck Ack; InitializeAck(Ack,Request.ClientRequestId,EGuLiCommandKind::Selection);
        auto* Player=GetBattlePlayerState(); auto* Authority=GetWorld()->GetSubsystem<UGuLiBattleAuthoritySubsystem>();
        auto Candidate=SelectionState;
        bOrderedSelectionValid=IsSoldierStreamReady() && Player && Authority && Authority->ResolveSelection(*Player,Request,Candidate,Ack);
        if (bOrderedSelectionValid) bOrderedSelectionValid=SelectAuthoredObjects(*GetWorld(),Request,Candidate);
        if (bOrderedSelectionValid) SelectionState=MoveTemp(Candidate);
        Ack.ServerSelectionRevision=SelectionState.SelectionRevision;
        CompleteHybridCommand(Ack,bOrderedSelectionValid?FString():TEXT("Hybrid selection was rejected"));
        NotifySelectionChanged(); return;

}

bool UGuLiCommanderNetSyncComponent::SubmitPanelSelection(FGuLiPanelSelectionRequest Request)
{
	// Portraits display a confirmed snapshot. Do not reinterpret a stale tile after another selection intent.
	if (!IsConnectionReady() || HasUnresolvedSelectionIntent())
	{ LastTaskFeedback = GuLiGameText::Text(TEXT("UI.OrderedCommands.102")); NotifySelectionChanged(); return false; }
	Request.SelectionRevision = SelectionState.SelectionRevision;
    if (Request.Object.IsValid())
    {
        auto* H=GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>();FGuLiHybridObjectState Anchor;
        if (!H || !H->HasControl(Request.Object) || !H->GetObject(Request.Object,Anchor)
            || !SelectionState.Objects.Contains(Request.Object) || uint8(Request.Action)>uint8(EGuLiPanelSelectionAction::RemoveGroup)) return false;
        const bool GroupAction=Request.Action==EGuLiPanelSelectionAction::SingleGroup || Request.Action==EGuLiPanelSelectionAction::RemoveGroup;
        if (GroupAction)
        {
            if (Request.ObjectGroup.IsEmpty() || Request.ObjectGroup.Num()>GULI_PANEL_PORTRAIT_GROUP_SIZE || !Request.ObjectGroup.Contains(Request.Object)) return false;
            TSet<FGuLiHybridObjectKey> Unique;
            for (const auto& K:Request.ObjectGroup)
            {
                FGuLiHybridObjectState O;
                if (Unique.Contains(K) || !SelectionState.Objects.Contains(K) || !H->GetObject(K,O) || O.Definition!=Anchor.Definition) return false;
                Unique.Add(K);
            }
        }
        if (Request.Action==EGuLiPanelSelectionAction::Single || Request.Action==EGuLiPanelSelectionAction::SingleGroup)
        {SelectionState.Cohorts.Reset();SelectionState.ActorIds.Reset();SelectionState.Objects=GroupAction?Request.ObjectGroup:TArray<FGuLiHybridObjectKey>{Request.Object};}
        else SelectionState.Objects.RemoveAll([&](const auto& K)
        {
            FGuLiHybridObjectState O;if (!H->GetObject(K,O)) return true;
            if (Request.Action==EGuLiPanelSelectionAction::KeepType) return O.Definition!=Anchor.Definition;
            if (Request.Action==EGuLiPanelSelectionAction::RemoveType) return O.Definition==Anchor.Definition;
            if (Request.Action==EGuLiPanelSelectionAction::RemoveGroup) return Request.ObjectGroup.Contains(K);
            return K==Request.Object;
        });
        if (Request.Action==EGuLiPanelSelectionAction::KeepType) {SelectionState.Cohorts.Reset();SelectionState.ActorIds.Reset();}
        if (!++SelectionState.SelectionRevision) ++SelectionState.SelectionRevision;
        bOrderedSelectionValid=true;NotifySelectionChanged();return true;
    }
	const uint32 Sequence = NextOrderedSequence++; if (!NextOrderedSequence) ++NextOrderedSequence;

	ApplyPanelSelection(Request, Sequence, GetConnectionGeneration());
	return true;
}

void UGuLiCommanderNetSyncComponent::ApplyPanelSelection(
	FGuLiPanelSelectionRequest Request, uint32 Sequence, uint32 Generation)
{
	auto Reject = [&](const TCHAR* Reason)
	{
		FGuLiCommandAck Ack; InitializeAck(Ack, Sequence, EGuLiCommandKind::Selection);
		Ack.Result = EGuLiCommandAckResult::InvalidRequest;
		ApplyTaskReceipt(Ack, Reason, Generation);
		ApplySelection(SelectionState, Sequence, false, Generation);
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
	if (bOrderedSelectionValid && (Request.Action==EGuLiPanelSelectionAction::Single
        || Request.Action==EGuLiPanelSelectionAction::SingleGroup || Request.Action==EGuLiPanelSelectionAction::KeepType)) SelectionState.Objects.Reset();
	ApplySelection(SelectionState, Sequence, false, Generation);
	NotifySelectionChanged(); NextOrderedSummaryTime = 0;
}
void UGuLiCommanderNetSyncComponent::SubmitOrderedTask(FGuLiUnitTaskCommand Command)
{ SubmitHybridTask(Command); }

void UGuLiCommanderNetSyncComponent::SubmitControlGroup(uint8 Slot, bool bSet, bool bAppend, bool bSteal, bool bFocus)
{
	const uint32 Sequence = NextOrderedSequence++; if (!NextOrderedSequence) ++NextOrderedSequence;
    ApplyControlGroup(Slot,bSet,bAppend,bSteal,bFocus,Sequence,GetConnectionGeneration());
}
bool UGuLiCommanderNetSyncComponent::AdmitOrderedSequence(uint32 Sequence, uint32 Generation)
{
	FGuLiCommandAck Ack;
	if (!Sequence || Generation != GetConnectionGeneration() || !CanProcessCommanderRequest(Ack, TEXT("CommanderOrderedInput"))) return false;
	if (OrderedGeneration != Generation) { OrderedGeneration = Generation; LastOrderedSequence = 0; }
	if (LastOrderedSequence && !IsNewerSerial(Sequence, LastOrderedSequence)) return false;
	LastOrderedSequence = Sequence; return true;
}
void UGuLiCommanderNetSyncComponent::ApplySelection(const FGuLiCommanderSelectionState& State,
	uint32 Sequence, bool bFocus, uint32 Generation)
{

	if (Generation != GetConnectionGeneration() || !IsConnectionReady()) return;
	if (!Sequence && State.SelectionRevision != SelectionState.SelectionRevision && !IsNewerSerial(State.SelectionRevision, SelectionState.SelectionRevision)) return;
	SelectionState = State;
	NotifySelectionChanged();
	if (bFocus) if (auto* Controller = Cast<AGuLiCommanderPlayerController>(GetOwner())) Controller->FocusSelectedUnits();
}
void UGuLiCommanderNetSyncComponent::ApplyTaskReceipt(const FGuLiCommandAck& Ack, const FString& Message, uint32 Generation)
{
	if (Generation != GetConnectionGeneration() || !IsConnectionReady()) return;
	LastTaskFeedback = Message; LastCommandAck = Ack;
	TGuardValue<bool> TaskReceiptScope(bDispatchingTaskReceipt, true);
	OnCommandAckChanged.Broadcast(Ack);
}
void UGuLiCommanderNetSyncComponent::PruneControlGroup(FGuLiCommanderControlGroup& Group, EGuLiTeam Team) const
{
    const auto* H=GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>();
    Group.Objects.RemoveAll([&](const auto& K){return !H || !H->HasControl(K);});
	const auto* Authority = GetWorld()->GetSubsystem<UGuLiBattleAuthoritySubsystem>();
	Group.Soldiers.RemoveAll([&](auto Id) { EGuLiTeam Actual; uint16 Type; FVector Position;
		return !Authority->GetTaskSoldierInfo(Id, Actual, Type, Position) || Actual != Team; });
	TSet<FGuLiControllableActorId> Live;
	for (TActorIterator<APawn> It(GetWorld()); It; ++It)
		if (const auto* Vehicle = Cast<IGuLiEngineeringVehicle>(*It); Vehicle && Vehicle->GetTeam() == Team)
			if (const auto* Health = It->FindComponentByClass<UGuLiCombatHealthComponent>(); Health && Health->IsAlive()) Live.Add(Vehicle->GetStableActorId());
	Group.Actors.RemoveAll([&](auto Id) { return !Live.Contains(Id); });
}
void UGuLiCommanderNetSyncComponent::PruneSelection()
{
    const auto* Player=GetBattlePlayerState();
    if (!Player || !Player->IsCommander() || !IsConnectionReady()) return;
    FGuLiCommanderControlGroup Live;
    for (const auto& Cohort:SelectionState.Cohorts) Live.Soldiers.Append(Cohort.MemberIds);
    Live.Actors=SelectionState.ActorIds; Live.Objects=SelectionState.Objects;
    const int32 Before=Live.Soldiers.Num()+Live.Actors.Num()+Live.Objects.Num();
    if (!Before) return;
    PruneControlGroup(Live,Player->GetTeam());
    if (Before==Live.Soldiers.Num()+Live.Actors.Num()+Live.Objects.Num()) return;
    // Preserve surviving cohort identities. Lifecycle changes invalidate only
    // the removed members, not the next command for the remaining selection.
    const TSet<FGuLiSoldierId> Soldiers(Live.Soldiers);
    for (auto& Cohort:SelectionState.Cohorts)
    {
        Cohort.MemberIds.RemoveAll([&](auto Id){return !Soldiers.Contains(Id);});
        Cohort.AliveCount=static_cast<uint8>(Cohort.MemberIds.Num());
    }
    SelectionState.Cohorts.RemoveAll([](const auto& Cohort){return Cohort.MemberIds.IsEmpty();});
    SelectionState.ActorIds=MoveTemp(Live.Actors); SelectionState.Objects=MoveTemp(Live.Objects);
    if (!++SelectionState.SelectionRevision) ++SelectionState.SelectionRevision;
    NotifySelectionChanged();
}
void UGuLiCommanderNetSyncComponent::ApplyControlGroup(uint8 Slot, bool bSet, bool bAppend,
	bool bSteal, bool bFocus, uint32 Sequence, uint32 Generation)
{
	if (!AdmitOrderedSequence(Sequence, Generation) || Slot >= 10) { ApplySelection(SelectionState, Sequence, false, Generation); return; }
	ControlGroups.SetNum(10); auto& Group = ControlGroups[Slot];
	const EGuLiTeam Team = GetBattlePlayerState()->GetTeam(); PruneControlGroup(Group, Team);
	if (bSet || bAppend)
	{
		FGuLiCommanderControlGroup Next = bAppend ? Group : FGuLiCommanderControlGroup{};
		for (const auto& Cohort : SelectionState.Cohorts) for (auto Id : Cohort.MemberIds) Next.Soldiers.AddUnique(Id);
		for (auto Id : SelectionState.ActorIds) Next.Actors.AddUnique(Id);
		for (const auto& K:SelectionState.Objects) Next.Objects.AddUnique(K);
		PruneControlGroup(Next, Team);
		if (Next.Soldiers.Num() > int32(GULI_MAX_CONTROL_COHORTS * GULI_CONTROL_COHORT_TARGET_SIZE)
				|| Next.Actors.Num()+Next.Objects.Num() > int32(GULI_MAX_CONTROLLABLE_ACTOR_SELECTION))
		{
			FGuLiCommandAck Ack; InitializeAck(Ack, Sequence, EGuLiCommandKind::Selection); Ack.Result = EGuLiCommandAckResult::InvalidRequest;
			ApplyTaskReceipt(Ack, GuLiGameText::Text(TEXT("UI.OrderedCommands.109")), Generation); ApplySelection(SelectionState, Sequence, false, Generation); return;
		}
		Group = MoveTemp(Next);
		if (bSteal)
		{
			TSet<FGuLiSoldierId> Selected; for (const auto& Cohort : SelectionState.Cohorts) for (auto Id : Cohort.MemberIds) Selected.Add(Id);
			for (int32 Index=0; Index<10; ++Index) if (Index != Slot)
			{ ControlGroups[Index].Soldiers.RemoveAll([&](auto Id){ return Selected.Contains(Id); });
			  ControlGroups[Index].Actors.RemoveAll([&](auto Id){ return SelectionState.ActorIds.Contains(Id); }); }
			for (int32 Index=0;Index<10;++Index) if (Index!=Slot)
			    ControlGroups[Index].Objects.RemoveAll([&](const auto& K){return SelectionState.Objects.Contains(K);});
		}
	}
	else if (!Group.Soldiers.IsEmpty() || !Group.Actors.IsEmpty() || !Group.Objects.IsEmpty())
	{
        const auto PreviousObjects=SelectionState.Objects;
		GetWorld()->GetSubsystem<UGuLiBattleAuthoritySubsystem>()->SetExplicitSelection(Team, Group.Soldiers, Group.Actors, SelectionState);
        SelectionState.Objects=Group.Objects;
        if (SelectionState.Objects!=PreviousObjects && !++SelectionState.SelectionRevision) ++SelectionState.SelectionRevision;
		bOrderedSelectionValid = true;
	}
	ApplySelection(SelectionState, Sequence, bFocus && (!Group.Soldiers.IsEmpty() || !Group.Actors.IsEmpty() || !Group.Objects.IsEmpty()), Generation);
	NotifySelectionChanged(); NextOrderedSummaryTime = 0;
}
void UGuLiCommanderNetSyncComponent::TickOrderedCommands()
{
    const auto* Player=GetBattlePlayerState();
    if (!GetWorld() || !Player || !Player->IsCommander() || !IsConnectionReady()) return;
    if (GetWorld()->GetRealTimeSeconds()<NextOrderedSummaryTime) return;
    PruneSelection();
    NextOrderedSummaryTime=GetWorld()->GetRealTimeSeconds()+0.2;
	ControlGroups.SetNum(10); ControlGroupCounts.SetNum(10);
	TSet<FGuLiSoldierId> SelectedSoldiers;
	for (const auto& Cohort : SelectionState.Cohorts) for (auto Id : Cohort.MemberIds) SelectedSoldiers.Add(Id);
	RelatedControlGroups = 0;
	for (int32 Index=0; Index<10; ++Index)
	{
		PruneControlGroup(ControlGroups[Index], GetBattlePlayerState()->GetTeam());
		const auto& Group = ControlGroups[Index]; ControlGroupCounts[Index] = Group.Soldiers.Num() + Group.Actors.Num()+Group.Objects.Num();
		if (Group.Soldiers.ContainsByPredicate([&](auto Id){ return SelectedSoldiers.Contains(Id); })
			|| Group.Actors.ContainsByPredicate([&](auto Id){ return SelectionState.ActorIds.Contains(Id); })
            || Group.Objects.ContainsByPredicate([&](const auto& K){return SelectionState.Objects.Contains(K);})) RelatedControlGroups |= uint16(1 << Index);
	}
	GetWorld()->GetSubsystem<UGuLiUnitTaskSubsystem>()->BuildSummary(SelectionState, TaskSummaries);
	DisplayedTaskSelectionRevision = SelectionState.SelectionRevision;
    RefreshSelectedMoveEndpoints();
}


bool UGuLiCommanderNetSyncComponent::SubmitMoveResponseDiagnostic(FName Action, int32 CommandId, int32 SoldierSeed, FVector Position)
{
#if !UE_BUILD_SHIPPING
 if (!GetWorld() || !GuLiMoveLatency::IsEnabled()) return false;
 // Editor Python enables local-only actor script execution. Dispatch on the real world tick
 // so the normal RPC callspace, input sequence and owning connection are used.
 GetWorld()->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this,[this,Action,CommandId,SoldierSeed,Position]()
 { DispatchMoveResponseDiagnostic(Action,CommandId,SoldierSeed,Position); }));
 return true;
#else
 return false;
#endif
}

bool UGuLiCommanderNetSyncComponent::DispatchMoveResponseDiagnostic(FName Action, int32 CommandId, int32 SoldierSeed, FVector Position)
{
#if !UE_BUILD_SHIPPING
 const auto* PC=Cast<APlayerController>(GetOwner());
 if (!GuLiMoveLatency::IsEnabled() || !PC || !PC->IsLocalController() || !IsSoldierStreamReady() || CommandId<=0 || Position.ContainsNaN()) return false;
 if (Action==TEXT("radius") || Action==TEXT("same-type") || Action==TEXT("clear"))
 {
  FGuLiSelectionRequest Request; Request.ClientRequestId=uint32(CommandId); Request.Center=Position;
  Request.RadiusPreset=EGuLiSelectionRadiusPreset::Large;
  if (Action==TEXT("clear")) Request.Modifier=EGuLiSelectionModifier::Clear;
  if (Action==TEXT("same-type"))
  {
   Request.Kind=EGuLiSelectionKind::SameType; Request.SeedSoldierId=FGuLiSoldierId(uint32(SoldierSeed));
   Request.RayOrigin=Position+FVector(0,0,20000); Request.RayDirection=FVector(0,0,-1);
   Request.BoxTopLeftRay=FVector(-60000,-60000,-20000).GetSafeNormal();
   Request.BoxTopRightRay=FVector(60000,-60000,-20000).GetSafeNormal();
   Request.BoxBottomRightRay=FVector(60000,60000,-20000).GetSafeNormal();
   Request.BoxBottomLeftRay=FVector(-60000,60000,-20000).GetSafeNormal();
  }
  SubmitOrderedSelection(Request); return true;
 }
 if (Action==TEXT("group") || Action==TEXT("remove-group"))
 {
  FGuLiPanelSelectionRequest Request; Request.Action=Action==TEXT("remove-group") ? EGuLiPanelSelectionAction::RemoveGroup : EGuLiPanelSelectionAction::SingleGroup;
  Request.SoldierId=FGuLiSoldierId(uint32(SoldierSeed)); return SubmitPanelSelection(Request);
 }

 if (Action!=TEXT("move") && Action!=TEXT("stop")) return false;
 FGuLiUnitTaskCommand Command; Command.CommandId=uint32(CommandId); Command.Target=Position; Command.bGroundMoveOnly=true;
 Command.Disposition=Action==TEXT("stop") ? EGuLiTaskDisposition::Stop : EGuLiTaskDisposition::Replace;
 SubmitOrderedTask(Command); return true;
#else
 return false;
#endif
}
