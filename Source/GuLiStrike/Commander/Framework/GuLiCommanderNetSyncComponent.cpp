// Copyright Epic Games, Inc. All Rights Reserved.

#include "Commander/Framework/GuLiCommanderNetSyncComponent.h"
#include "Network/GuLiHybridWorldSubsystem.h"
#include "Network/GuLiHybridActorComponent.h"
#include "Gameplay/Units/GuLiEngineeringTravelComponent.h"

#include "Battle/Framework/GuLiBattleGameState.h"
#include "GameFramework/PlayerController.h"
#include "Battle/Framework/GuLiBattlePlayerState.h"
#include "Commander/Mass/GuLiBattleAuthoritySubsystem.h"
#include "Commander/Network/GuLiSoldierStateReplicator.h"
#include "GuLiStrike.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Commander/Network/GuLiCommanderPoseMetrics.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"

UGuLiCommanderNetSyncComponent::UGuLiCommanderNetSyncComponent()
{
    SetIsReplicatedByDefault(false);
    PrimaryComponentTick.bCanEverTick=true;
    PrimaryComponentTick.bStartWithTickEnabled=true;
}
void UGuLiCommanderNetSyncComponent::TickComponent(float DeltaTime,ELevelTick TickType,FActorComponentTickFunction* TickFunction)
{
    Super::TickComponent(DeltaTime,TickType,TickFunction);
    TickHybridInputs();
    TickOrderedCommands();
    TickMoveResponse();
}
void UGuLiCommanderNetSyncComponent::EndPlay(const EEndPlayReason::Type Reason)
{
    if (auto* H=HybridCommandWorld.Get()) H->OnCommandCompleted.RemoveAll(this);
    HybridInputs.Reset(); HybridPendingCommands.Reset(); HybridSubmittedCommands.Reset();
    HybridCompletedCommands.Reset(); HybridCommandWorld.Reset(); PendingPoseChunks.Reset();
    ClearLocalMoveEndpoints();
    MoveResponsePending.Reset(); MoveResponseText.Reset();
    Super::EndPlay(Reason);
}
bool UGuLiCommanderNetSyncComponent::IsSoldierStreamReady() const { return IsConnectionReady(); }
void UGuLiCommanderNetSyncComponent::SubmitSelectionRequest(const FGuLiSelectionRequest& Request) { SubmitOrderedSelection(Request); }
void UGuLiCommanderNetSyncComponent::SubmitMoveRequest(const FGuLiMoveRequest& Request)
{
    FGuLiUnitTaskCommand Command; Command.CommandId=Request.ClientCommandId; Command.Target=Request.Target;
    Command.SelectionRevision=SelectionState.SelectionRevision; Command.bGroundMoveOnly=true;
    SubmitHybridTask(Command);
}
bool UGuLiCommanderNetSyncComponent::IsMoveCommandPending(uint32 Id) const
{
    if (!Id) return false;
    for (const auto& Input:HybridInputs) if (Input.Command.CommandId==Id) return true;
    for (const auto& Pair:HybridPendingCommands) if (Pair.Value.Command.CommandId==Id) return true;
    return false;
}
bool UGuLiCommanderNetSyncComponent::CanProcessCommanderRequest(FGuLiCommandAck& Ack,const TCHAR*)
{
    const auto* PC=GetOwningPlayerController();const auto* Player=GetBattlePlayerState();
    if (!PC || !PC->IsLocalController() || !Player || !Player->IsCommander() || !IsSoldierStreamReady())
    { Ack.Result=EGuLiCommandAckResult::Unauthorized;return false; }
    return true;
}
bool UGuLiCommanderNetSyncComponent::ConsumeCommandRateLimit()
{ return GetWorld() && CommanderRequestWindow.Consume(GetWorld()->GetRealTimeSeconds(),10,1.0); }
uint64 UGuLiCommanderNetSyncComponent::GetPresentationQueueBytes() const
{
    uint64 Size=PendingPoseChunks.GetAllocatedSize();
    for (const auto& Chunk:PendingPoseChunks) Size+=Chunk.Samples.GetAllocatedSize();
    return Size;
}

void UGuLiCommanderNetSyncComponent::ConsumePendingPoseChunks(
	TArray<FGuLiSoldierPoseChunk>& OutChunks)
{
	OutChunks = MoveTemp(PendingPoseChunks);
	PendingPoseChunks.Reset();
}

void UGuLiCommanderNetSyncComponent::ConsumePendingCommandAcks(
	TArray<FGuLiCommandAck>& OutAcks)
{
	OutAcks = MoveTemp(PendingCommandAcks);
	PendingCommandAcks.Reset();
}

void UGuLiCommanderNetSyncComponent::ApplyHybridPoseChunk(const FGuLiSoldierPoseChunk& Chunk)
{
    if (!UGuLiNetworkSettings::IsHybrid(GetWorld()) || !UGuLiNetworkSettings::CanPresent(GetWorld())
        || Chunk.Samples.IsEmpty()) return;
#if !UE_BUILD_SHIPPING
    LastAcceptedPoseReceiveTimeSeconds=FPlatformTime::Seconds();
    if (!AcceptedPoseFrameCount || Chunk.FrameSequence!=LastAcceptedPoseFrameSequence)
    { ++AcceptedPoseFrameCount;LastAcceptedPoseFrameSequence=Chunk.FrameSequence; }
#endif
    for (auto& Pending : PendingPoseChunks)
        if (Pending.AuthorityEpoch==Chunk.AuthorityEpoch && Pending.FrameSequence==Chunk.FrameSequence
            && Pending.ServerSimTick==Chunk.ServerSimTick)
        { Pending.Samples.Append(Chunk.Samples); return; }
    if (PendingPoseChunks.Num()>=static_cast<int32>(GULI_MAX_POSE_CHUNKS_PER_FRAME)) PendingPoseChunks.RemoveAt(0,1,EAllowShrinking::No);
    PendingPoseChunks.Add(Chunk); OnPoseChunkReceived.Broadcast(Chunk);
}

void UGuLiCommanderNetSyncComponent::InitializeAck(
	FGuLiCommandAck& Ack,
	const uint32 ClientId,
	const EGuLiCommandKind CommandKind) const
{
	Ack.CommandKind = CommandKind;
	Ack.ClientCommandId = ClientId;
	Ack.BatchOrderId = 0;
	Ack.Result = EGuLiCommandAckResult::InvalidRequest;
	Ack.ServerSelectionRevision = SelectionState.SelectionRevision;
	Ack.CohortResults.Reset();
}

bool UGuLiCommanderNetSyncComponent::IsNewerSerial(uint32 Candidate, uint32 Baseline)
{
	return static_cast<int32>(Candidate - Baseline) > 0;
}

void UGuLiCommanderNetSyncComponent::NotifySelectionChanged()
{
    if (auto* H=GetWorld()?GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>():nullptr)
    {
        TArray<FGuLiHybridObjectKey> Selected=SelectionState.Objects;const auto* Authority=GetWorld()->GetSubsystem<UGuLiBattleAuthoritySubsystem>();
        TSet<FGuLiSoldierId> Tracked;
        for (const auto& Group:ControlGroups) for (const auto Id:Group.Soldiers) Tracked.Add(Id);
        if (Authority) for (const auto& Cohort:SelectionState.Cohorts) for (const auto Id:Cohort.MemberIds)
        {
            Tracked.Add(Id);FGuLiHybridObjectKey K;
            if (H->ResolveTarget(Authority->MakeSoldierTargetHandle(Id),K)) SelectionSoldierLives.Add(Id,K);
            if (const auto* Life=SelectionSoldierLives.Find(Id)) Selected.AddUnique(*Life);
        }
        for (auto It=SelectionSoldierLives.CreateIterator();It;++It) if (!Tracked.Contains(It.Key())) It.RemoveCurrent();
        TArray<FGuLiHybridObjectState> Objects;H->GetObjects(Objects);
        for (const auto& O:Objects) if (const auto* Vehicle=Cast<IGuLiEngineeringVehicle>(H->FindBoundActor(O.Key));
            Vehicle && SelectionState.ActorIds.Contains(Vehicle->GetStableActorId())) Selected.AddUnique(O.Key);
        H->SetCommanderSelection(Selected);
    }
	OnSelectionChanged.Broadcast(SelectionState);
    NextOrderedSummaryTime=0;
}

void UGuLiCommanderNetSyncComponent::SetLocalMoveEndpoint(FGuLiSoldierId Id, const FGuLiMoveEndpointItem* Endpoint)
{
	const int32* Found=MoveEndpointIndex.Find(Id);
	if (!Endpoint)
	{
		if (!Found) return;
		const int32 Index=*Found; MoveEndpointIndex.Remove(Id);
		MoveEndpoints.Items.RemoveAtSwap(Index,1,EAllowShrinking::No);
		if (MoveEndpoints.Items.IsValidIndex(Index)) MoveEndpointIndex.Add(MoveEndpoints.Items[Index].SoldierId,Index);
	}
	else
	{
		if (Found)
		{
			const auto& Previous=MoveEndpoints.Items[*Found];
			if (Previous.ActiveOrderId==Endpoint->ActiveOrderId && Previous.Revision==Endpoint->Revision
				&& Previous.CommandStart==Endpoint->CommandStart && Previous.FinalDestination==Endpoint->FinalDestination) return;
			MoveEndpoints.Items[*Found]=*Endpoint;
		}
		else MoveEndpointIndex.Add(Id,MoveEndpoints.Items.Add(*Endpoint));
	}
	OnMoveEndpointIdsChanged.Broadcast(MakeArrayView(&Id,1),false);
}

void UGuLiCommanderNetSyncComponent::ClearLocalMoveEndpoints()
{
	MoveEndpointIndex.Reset(); MoveEndpoints.Items.Reset();
	OnMoveEndpointIdsChanged.Broadcast({},true);
	OnMoveEndpointsChanged.Broadcast(MoveEndpoints);
}

void UGuLiCommanderNetSyncComponent::RefreshSelectedMoveEndpoints()
{
    const auto* A=GetWorld()->GetSubsystem<UGuLiBattleAuthoritySubsystem>();
    const auto* Player=GetBattlePlayerState(); TSet<FGuLiSoldierId> Visible;
    if (A && Player) for (const auto& Cohort:SelectionState.Cohorts) for (auto Id:Cohort.MemberIds)
    {
        const auto* E=A->FindActiveMoveEndpoint(Id);
        if (!E || E->Team!=Player->GetTeam()) continue;
        FGuLiMoveEndpointItem Value;Value.SoldierId=Id;Value.ActiveOrderId=E->ActiveOrderId;
        Value.CommandStart=E->CommandStart;Value.FinalDestination=E->FinalDestination;Value.Revision=E->Revision;
        Visible.Add(Id);SetLocalMoveEndpoint(Id,&Value);
    }
    TArray<FGuLiSoldierId> Previous;MoveEndpointIndex.GetKeys(Previous);
    for (auto Id:Previous) if (!Visible.Contains(Id)) SetLocalMoveEndpoint(Id,nullptr);
}
FString UGuLiCommanderNetSyncComponent::GetMoveResponseDiagnostics() const
{
    auto Row=GetStreamCaptureSnapshot();Row->SetStringField(TEXT("feedback"),LastTaskFeedback);
    Row->SetNumberField(TEXT("selection_revision"),SelectionState.SelectionRevision);
    FString Result;const auto Writer=TJsonWriterFactory<>::Create(&Result);FJsonSerializer::Serialize(Row,Writer);return Result;
}
