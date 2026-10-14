#include "Commander/Framework/GuLiCommanderNetSyncComponent.h"
#include "Commander/Mass/GuLiBattleAuthoritySubsystem.h"
#include "Commander/Mass/Navigation/GuLiSharedMoveRoutes.h"
#include "Commander/Orders/GuLiUnitTaskSubsystem.h"
#include "Commander/Behavior/GuLiCommanderBehaviorSchema.h"
#include "Gameplay/Data/GuLiCommanderDataSubsystem.h"
#include "Battle/Framework/GuLiBattleGameState.h"
#include "Battle/Framework/GuLiBattlePlayerState.h"
#include "Network/GuLiHybridManifest.h"
#include "Commander/Orders/Private/GuLiHybridTaskCommand.h"
#include "NavigationSystem.h"
#include "Engine/World.h"
#include "Dom/JsonObject.h"

#if !UE_BUILD_SHIPPING
TSharedRef<FJsonObject> UGuLiCommanderNetSyncComponent::GetReviewCommandEvidence() const
{
    auto Root=MakeShared<FJsonObject>();TArray<TSharedPtr<FJsonValue>> Pending;
    Root->SetNumberField(TEXT("preparing"),HybridInputs.Num());
    for (const auto& [Event,C]:HybridPendingCommands)
    {
        auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("event"),Event.ToString(EGuidFormats::Digits));
        Row->SetNumberField(TEXT("command"),C.Command.CommandId);
        TArray<TSharedPtr<FJsonValue>> Members;
        for (const auto& [Key,Accepted]:C.Results)
        {
            auto Member=MakeShared<FJsonObject>();Member->SetStringField(TEXT("object"),Key.StableId.ToString(EGuidFormats::Digits));
            Member->SetNumberField(TEXT("kind"),uint8(Key.Kind));Member->SetNumberField(TEXT("generation"),Key.LifeGeneration);
            Member->SetBoolField(TEXT("resolved"),Accepted.IsSet());
            if (Accepted.IsSet()) Member->SetBoolField(TEXT("accepted"),Accepted.GetValue());
            Members.Add(MakeShared<FJsonValueObject>(Member));
        }
        Row->SetArrayField(TEXT("members"),Members);Pending.Add(MakeShared<FJsonValueObject>(Row));
    }
    Root->SetArrayField(TEXT("pending"),Pending);return Root;
}
#endif

void UGuLiCommanderNetSyncComponent::CompleteHybridCommand(FGuLiCommandAck Ack,const FString& Message)
{
    LastCommandAck=Ack; LastTaskFeedback=Message;
    PendingCommandAcks.Add(Ack);
    if (PendingCommandAcks.Num()>256) PendingCommandAcks.RemoveAt(0,PendingCommandAcks.Num()-256,EAllowShrinking::No);
    TGuardValue<bool> Scope(bDispatchingTaskReceipt,Ack.CommandKind==EGuLiCommandKind::Move);
    OnCommandAckChanged.Broadcast(Ack);
}
bool UGuLiCommanderNetSyncComponent::SubmitHybridTask(FGuLiUnitTaskCommand Command)
{
    auto* World=GetWorld(); auto* Hybrid=World?World->GetSubsystem<UGuLiHybridWorldSubsystem>():nullptr;
    auto* Player=GetBattlePlayerState(); auto* Tasks=World?World->GetSubsystem<UGuLiUnitTaskSubsystem>():nullptr;
    auto Reject=[&](EGuLiCommandAckResult Result,const TCHAR* Why)
    { FGuLiCommandAck Ack; InitializeAck(Ack,Command.CommandId,EGuLiCommandKind::Move); Ack.Result=Result; CompleteHybridCommand(Ack,Why); return false; };
    if (!Hybrid || !Player || !Tasks || !Player->IsCommander() || !IsSoldierStreamReady() || Hybrid->IsBattlePaused())
        return Reject(EGuLiCommandAckResult::Unauthorized,TEXT("Hybrid battle is not ready for commands"));
    TickHybridInputs();
    PruneSelection();
    Command.SelectionRevision=SelectionState.SelectionRevision;
    Tasks->BuildContextCommand(SelectionState,Command);
    if (!Command.IsWellFormed() || !bOrderedSelectionValid) return Reject(EGuLiCommandAckResult::InvalidRequest,TEXT("Invalid task or selection"));
    const auto* Special=GuLiCommanderBehavior::FindPolicyByWireId(*World,Command.SpecialTaskId);
    const FName Intent=Command.TargetIntent;
    if (!(Intent.IsNone() || (Intent==TEXT("Move") && Command.Kind==EGuLiUnitTaskKind::Move)
        || (Intent==TEXT("Mine") && Command.Kind==EGuLiUnitTaskKind::Special && Special && Special->Behavior==EGuLiCommanderBehavior::Mining)
        || (Intent==TEXT("Construct") && Command.Kind==EGuLiUnitTaskKind::Special && Special && Special->Behavior==EGuLiCommanderBehavior::Construction)
        || (Intent==TEXT("Return") && Command.Kind==EGuLiUnitTaskKind::ReturnToFactory)
        || (Intent==TEXT("Transit") && Command.Kind==EGuLiUnitTaskKind::Transit)))
        return Reject(EGuLiCommandAckResult::InvalidTarget,TEXT("Requested target mode does not match the authored task"));
    if (const auto* Previous=HybridSubmittedCommands.Find(Command.CommandId))
    {
        if (!FGuLiUnitTaskCommand::StaticStruct()->CompareScriptStruct(Previous,&Command,0))
            return Reject(EGuLiCommandAckResult::InvalidRequest,TEXT("Command identity was reused with different arguments"));
        const FGuid Event=FGuLiHybridManifest::MakeStableKey(World->GetOutermost()->GetName(),EGuLiHybridObjectKind::Soldier,
            Player->GetPlayerGuid().ToString(EGuidFormats::Digits)+FString::Printf(TEXT("/command/%u"),Command.CommandId)).StableId;
        if (const auto* Ack=HybridCompletedCommands.Find(Event)) CompleteHybridCommand(*Ack,LastTaskFeedback);
        return true;
    }
    if (HybridInputs.Num()>=32 || HybridPendingCommands.Num()>=2048 || HybridSubmittedCommands.Num()>=16384)
        return Reject(EGuLiCommandAckResult::RateLimited,TEXT("Hybrid command queue is full"));
    FHybridInput Input; Input.Command=Command; Input.Selection=SelectionState; Input.Frozen.Operation=TEXT("UnitTask");
    Input.Frozen.Event=FGuLiHybridManifest::MakeStableKey(World->GetOutermost()->GetName(),EGuLiHybridObjectKind::Soldier,
        Player->GetPlayerGuid().ToString(EGuidFormats::Digits)+FString::Printf(TEXT("/command/%u"),Command.CommandId)).StableId;
    TSet<uint32> Soldiers,Actors;
    for (const auto& Cohort : Input.Selection.Cohorts) for (const auto Id : Cohort.MemberIds) Soldiers.Add(Id.Value);
    for (const auto Id : Input.Selection.ActorIds) Actors.Add(Id.Value);
    TArray<FGuLiHybridObjectState> Objects; Hybrid->GetObjects(Objects);
    Objects.Sort([](const auto& A,const auto& B){ if (A.Key.Kind!=B.Key.Kind) return A.Key.Kind<B.Key.Kind; return A.Ordinal<B.Ordinal; });
    auto* Authority=World->GetSubsystem<UGuLiBattleAuthoritySubsystem>();
    Input.Intent=Authority?Authority->CreateSharedMoveIntent(Command.Target,Command.CommandId):nullptr;
    if (!Input.Intent) return Reject(EGuLiCommandAckResult::InvalidRequest,TEXT("Movement planner is unavailable"));
    const auto* Data=World->GetSubsystem<UGuLiCommanderDataSubsystem>();
    for (const auto& O : Objects)
    {
        const bool Soldier=O.Key.Kind==EGuLiHybridObjectKind::Soldier;
        const bool Selected=Input.Selection.Objects.Contains(O.Key) || (Soldier?Soldiers.Contains(O.Ordinal):Actors.Contains(O.Ordinal)
            && (O.Key.Kind==EGuLiHybridObjectKind::Engineer || O.Key.Kind==EGuLiHybridObjectKind::Miner));
        if (!Selected) continue;
        if (!Hybrid->HasControl(O.Key) || O.Team!=Player->GetTeam()) return Reject(EGuLiCommandAckResult::Unauthorized,TEXT("Selected object is not controlled by this player"));
        auto& M=Input.Frozen.Members.AddDefaulted_GetRef(); M.Key=O.Key; M.ControlEpoch=O.ControlEpoch; M.OwnerEpoch=O.OwnerEpoch;
        M.Slot=Input.Frozen.Members.Num()-1; M.Destination=O.Location;
        // Slot identity is independent of legacy SoldierId/ActorId namespaces.
        Input.Intent->UnassignedMembers.Add(M.Slot+1);
        uint32 Type=0; const FString Definition=O.Definition.ToString();
        if (Definition.StartsWith(TEXT("unit/"))) LexTryParseString(Type,*Definition.Mid(5));
        const auto* Unit=Data?Data->FindSoldierDefinition(static_cast<uint16>(Type)):nullptr;
        const auto* Actor=Hybrid->FindBoundActor(O.Key);
        if (!Unit && !Actor) return Reject(EGuLiCommandAckResult::InvalidRequest,TEXT("Selected object has no command footprint"));
        const auto Bounds=Unit?Unit->GetModelBoundsCentimeters():Actor->GetComponentsBoundingBox(true);
        Input.Intent->Pitch=FMath::Max(Input.Intent->Pitch,FMath::Max(1.f,static_cast<float>(FMath::Max(Bounds.GetSize().X,Bounds.GetSize().Y))));
    }
    if (Input.Frozen.Members.Num()!=Soldiers.Num()+Actors.Num()+Input.Selection.Objects.Num() || Input.Frozen.Members.IsEmpty())
        return Reject(EGuLiCommandAckResult::InvalidRequest,TEXT("Frozen selection has missing objects"));
    Input.Intent->WantedPoints=Input.Frozen.Members.Num(); Input.Intent->bMetadataReady=true;
    HybridSubmittedCommands.Add(Command.CommandId,Command); HybridInputs.Add(MoveTemp(Input));
    return true;
}
void UGuLiCommanderNetSyncComponent::TickHybridInputs()
{
    auto* Hybrid=GetWorld()?GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>():nullptr;
    const auto* Battle=GetBattleGameState(); if (!Hybrid || !Battle) return;
    if (HybridCommandWorld.Get()!=Hybrid)
    {
        if (auto* Old=HybridCommandWorld.Get()) Old->OnCommandCompleted.RemoveAll(this);
        HybridCommandWorld=Hybrid; Hybrid->OnCommandCompleted.AddUObject(this,&ThisClass::HandleHybridCommandResult);
    }
    if (HybridInputEpoch!=Battle->GetMatchEpoch())
    {
        for (const auto& [Event,Pending] : HybridPendingCommands)
        { FGuLiCommandAck Ack; InitializeAck(Ack,Pending.Command.CommandId,EGuLiCommandKind::Move); Ack.Result=EGuLiCommandAckResult::Cancelled; CompleteHybridCommand(Ack,TEXT("The room session changed before this command completed")); }
        HybridInputs.Reset(); HybridPendingCommands.Reset(); HybridCompletedCommands.Reset(); HybridSubmittedCommands.Reset();
        HybridInputEpoch=Battle->GetMatchEpoch();
    }
    // Membership changes may pause input. Ordinary handoffs keep the command identity and membership.
    if (!Hybrid->CanPublishFacts() || HybridInputs.IsEmpty()) return;
    auto& Input=HybridInputs[0];
    const bool NeedsSlots=Input.Command.Kind==EGuLiUnitTaskKind::Move && Input.Command.Disposition!=EGuLiTaskDisposition::Stop;
    if (NeedsSlots)
    {
        auto* Navigation=FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
        const auto* Nav=Navigation?Navigation->GetNavDataForAgentName(TEXT("CommanderSoldier")):nullptr;
        if (!Nav)
        {
            FGuLiCommandAck Ack;InitializeAck(Ack,Input.Command.CommandId,EGuLiCommandKind::Move);
            Ack.Result=EGuLiCommandAckResult::InvalidTarget;
            CompleteHybridCommand(Ack,TEXT("Navigation is unavailable for this move"));HybridInputs.RemoveAt(0);return;
        }
        FGuLiNavigationWorkBudget Budget; Budget.Reset(1.,64,0); FGuLiNavigationWorkBudget::FScope Scope(Budget);
        Input.Intent->Generate(*Nav,1,Budget);
        if (!Input.Intent->UnassignedMembers.IsEmpty() && !Input.Intent->bExhausted) return;
        if (!Input.Intent->UnassignedMembers.IsEmpty())
        { FGuLiCommandAck Ack; InitializeAck(Ack,Input.Command.CommandId,EGuLiCommandKind::Move); Ack.Result=EGuLiCommandAckResult::InvalidTarget; CompleteHybridCommand(Ack,TEXT("The full selection cannot fit on navigable formation slots")); HybridInputs.RemoveAt(0); return; }
    }
    for (auto& M : Input.Frozen.Members)
    {
        FGuLiHybridObjectState O;
        if (!Hybrid->GetObject(M.Key,O) || !Hybrid->HasControl(M.Key) || O.ControlEpoch!=M.ControlEpoch)
        { FGuLiCommandAck Ack; InitializeAck(Ack,Input.Command.CommandId,EGuLiCommandKind::Move); Ack.Result=EGuLiCommandAckResult::Cancelled; CompleteHybridCommand(Ack,TEXT("Authorization changed while preparing the frozen command")); HybridInputs.RemoveAt(0); return; }
        M.Destination=NeedsSlots?Input.Intent->AssignedPoints.FindChecked(M.Slot+1).Location:FVector(Input.Command.Target);
        M.OwnerEpoch=O.OwnerEpoch;
    }
    if (!GuLiHybridState::EncodeTaskCommand(Input.Command,Input.Intent->Pitch,Input.Intent->GeneratedRadius,Input.Frozen.Arguments))
    {
        FGuLiCommandAck Ack;InitializeAck(Ack,Input.Command.CommandId,EGuLiCommandKind::Move);
        Ack.Result=EGuLiCommandAckResult::InvalidRequest;
        CompleteHybridCommand(Ack,TEXT("Task arguments cannot be encoded"));HybridInputs.RemoveAt(0);return;
    }
    Input.Frozen.LogicalTick=Hybrid->GetLogicalTick();
    FHybridPendingCommand Pending; Pending.Command=Input.Command; Pending.Selection=Input.Selection;
    for (const auto& M : Input.Frozen.Members) Pending.Results.Add(M.Key,{});
    if (!HybridPendingCommands.Contains(Input.Frozen.Event)) HybridPendingCommands.Add(Input.Frozen.Event,MoveTemp(Pending));
    FGuLiMoveRequest Preview; Preview.ClientCommandId=Input.Command.CommandId; Preview.Target=Input.Command.Target;
    if (!Input.bPreviewPublished && Input.Command.Disposition!=EGuLiTaskDisposition::Stop)
        OnMoveReadyToSend.Broadcast(Preview,Input.Selection);
    Input.bPreviewPublished=true;
    // Backpressure only delays this command. Reusing its event identity makes a partial send safe to retry.
    if (!Hybrid->RouteCommand(Input.Frozen)) return;
    HybridInputs.RemoveAt(0);
}
void UGuLiCommanderNetSyncComponent::HandleHybridCommandResult(const FGuLiHybridCommandResult& Result)
{
    auto* Pending=HybridPendingCommands.Find(Result.Event); if (!Pending) return;
    for (const auto& M : Result.Members) if (auto* Value=Pending->Results.Find(M.Key))
    { if (Value->IsSet() && Value->GetValue()!=M.bAccepted) { HybridCommandWorld->Suspend(TEXT("Conflicting Hybrid command receipt")); return; } *Value=M.bAccepted; }
    for (const auto& [Key,Value] : Pending->Results) if (!Value.IsSet()) return;
    FGuLiCommandAck Ack; InitializeAck(Ack,Pending->Command.CommandId,EGuLiCommandKind::Move);
    int32 Accepted=0,Rejected=0;
    for (const auto& Cohort : Pending->Selection.Cohorts)
    {
        auto& Row=Ack.CohortResults.AddDefaulted_GetRef(); Row.CohortId=Cohort.CohortId; Row.MemberCount=Cohort.MemberIds.Num();
        for (int32 I=0; I<Cohort.MemberIds.Num(); ++I)
        {
            bool Success=false;
            for (const auto& [Key,Value] : Pending->Results)
            { FGuLiHybridObjectState O; if (Key.Kind==EGuLiHybridObjectKind::Soldier && HybridCommandWorld->GetObject(Key,O) && O.Ordinal==Cohort.MemberIds[I].Value) { Success=Value.GetValue(); break; } }
            if (Success) { Row.AcceptedMemberMask|=1u<<I; ++Accepted; } else ++Rejected;
        }
        Row.EligibleMemberMask=Row.AcceptedMemberMask;
        Row.Result=!Row.AcceptedMemberMask?EGuLiCommandAckResult::InvalidTarget
            :Row.GetAcceptedMemberCount()==Row.MemberCount?EGuLiCommandAckResult::Accepted:EGuLiCommandAckResult::PartiallyAccepted;
    }
    for (const auto Id : Pending->Selection.ActorIds)
    {
        bool Success=false;
        for (const auto& [Key,Value] : Pending->Results) { FGuLiHybridObjectState O; if (Key.Kind!=EGuLiHybridObjectKind::Soldier && HybridCommandWorld->GetObject(Key,O) && O.Ordinal==Id.Value) { Success=Value.GetValue(); break; } }
        Ack.EngineeringResults.Add({Id,Success?EGuLiTransitOrderResult::Accepted:EGuLiTransitOrderResult::InvalidTarget}); Success?++Accepted:++Rejected;
    }
    for (const auto& K:Pending->Selection.Objects)
        if (const auto* Value=Pending->Results.Find(K);Value && Value->GetValue()) ++Accepted;else ++Rejected;
    Ack.Result=Rejected?(Accepted?EGuLiCommandAckResult::PartiallyAccepted:EGuLiCommandAckResult::InvalidTarget):EGuLiCommandAckResult::Accepted;
    HybridCompletedCommands.Add(Result.Event,Ack); HybridPendingCommands.Remove(Result.Event);
    CompleteHybridCommand(Ack,FString::Printf(TEXT("%d accepted, %d rejected by simulation owners"),Accepted,Rejected));
}
