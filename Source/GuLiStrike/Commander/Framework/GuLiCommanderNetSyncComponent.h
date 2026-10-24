// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Battle/Network/GuLiPlayerNetSyncComponent.h"
#include "Commander/Network/GuLiCommanderTypes.h"
#include "Commander/Orders/GuLiUnitTaskTypes.h"
#include "Network/GuLiHybridWorldSubsystem.h"
#include "GuLiCommanderNetSyncComponent.generated.h"

class AGuLiBattlePlayerState;
class FJsonObject;
class AGuLiSoldierStateReplicator;
class UGuLiBattleAuthoritySubsystem;
struct FGuLiSoldierRosterDelta;
struct FGuLiMoveEndpointDelta;
DECLARE_MULTICAST_DELEGATE_TwoParams(FGuLiMoveEndpointIdsChanged, TConstArrayView<FGuLiSoldierId>, bool);

DECLARE_MULTICAST_DELEGATE_OneParam(
	FGuLiCommanderSelectionChangedSignature,
	const FGuLiCommanderSelectionState&);
DECLARE_MULTICAST_DELEGATE_OneParam(
	FGuLiCommanderCommandAckChangedSignature,
	const FGuLiCommandAck&);
DECLARE_MULTICAST_DELEGATE_OneParam(
	FGuLiSoldierPoseChunkReceivedSignature,
	const FGuLiSoldierPoseChunk&);
DECLARE_MULTICAST_DELEGATE_OneParam(
	FGuLiMoveEndpointsChangedSignature,
	const FGuLiMoveEndpointFastArray&);
DECLARE_MULTICAST_DELEGATE_TwoParams(FGuLiMoveReadyToSendSignature,
	const FGuLiMoveRequest&, const FGuLiCommanderSelectionState&);
/** Local intent only; admission and authoritative movement still complete separately. */
DECLARE_MULTICAST_DELEGATE_TwoParams(FGuLiLocalTaskSubmittedSignature,
	const FGuLiUnitTaskCommand&, const FGuLiCommanderSelectionState&);

/** Local input, selection and presentation view. P2P delivery belongs to the World session. */
UCLASS(ClassGroup=(GuLiStrike), meta=(BlueprintSpawnableComponent))
class GULISTRIKE_API UGuLiCommanderNetSyncComponent : public UGuLiPlayerNetSyncComponent
{
    GENERATED_BODY()
public:
    UGuLiCommanderNetSyncComponent();
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    UFUNCTION(BlueprintPure, Category="Commander|Network") bool IsSoldierStreamReady() const;
    void ApplyHybridPoseChunk(const FGuLiSoldierPoseChunk& Chunk);
    void ConsumePendingPoseChunks(TArray<FGuLiSoldierPoseChunk>& OutChunks);
    void ConsumePendingCommandAcks(TArray<FGuLiCommandAck>& OutAcks);
    void SubmitSelectionRequest(const FGuLiSelectionRequest& Request);
    void SubmitMoveRequest(const FGuLiMoveRequest& Request);
    void SubmitOrderedSelection(FGuLiSelectionRequest Request);
    bool SubmitPanelSelection(FGuLiPanelSelectionRequest Request);
    void SubmitOrderedTask(FGuLiUnitTaskCommand Command);
    void SubmitControlGroup(uint8 Slot, bool bSet, bool bAppend, bool bSteal, bool bFocus);
    bool HasUnresolvedSelectionIntent() const { return false; } // Selection is resolved synchronously in this local World.
    bool IsMoveCommandPending(uint32 CommandId) const;
    void RecordMoveInput(uint32 CommandId, double InputSeconds);
    const FString& GetMoveResponseText() const { return MoveResponseText; }
    const FGuLiCommanderSelectionState& GetSelectionState() const { return SelectionState; }
    const FGuLiCommandAck& GetLastCommandAck() const { return LastCommandAck; }
    const TArray<FGuLiUnitTaskSummary>& GetTaskSummaries() const;
    const TArray<int32>& GetControlGroupCounts() const { return ControlGroupCounts; }
    uint16 GetRelatedControlGroups() const { return DisplayedTaskSelectionRevision == SelectionState.SelectionRevision ? RelatedControlGroups : 0; }
    const FString& GetLastTaskFeedback() const { return LastTaskFeedback; }
    bool IsDispatchingTaskReceipt() const { return bDispatchingTaskReceipt; }
    uint32 GetSyncGeneration() const { return GetConnectionGeneration(); }
    const FGuLiMoveEndpointFastArray& GetMoveEndpoints() const { return MoveEndpoints; }
    const FGuLiMoveEndpointItem* FindMoveEndpoint(FGuLiSoldierId Id) const
    { const auto* I=MoveEndpointIndex.Find(Id); return I && MoveEndpoints.Items.IsValidIndex(*I) ? &MoveEndpoints.Items[*I] : nullptr; }
    uint64 GetPresentationQueueBytes() const;
    TSharedRef<FJsonObject> GetStreamCaptureSnapshot() const;
    UFUNCTION(BlueprintCallable, Category="Commander|Diagnostics", meta=(DevelopmentOnly)) FString GetMoveResponseDiagnostics() const;
    UFUNCTION(BlueprintCallable, Category="Commander|Diagnostics", meta=(DevelopmentOnly)) bool SubmitMoveResponseDiagnostic(FName Action, int32 CommandId, int32 SoldierSeed, FVector Position);
    bool DispatchMoveResponseDiagnostic(FName Action, int32 CommandId, int32 SoldierSeed, FVector Position);
    UFUNCTION() void ServerRequestRogueCards(FGuid Request);
    UFUNCTION() void ServerRerollRogueCards(FGuid Request, FGuid Session);
    UFUNCTION() void ServerConfirmRogueCard(FGuid Session, const FString& CardId);
    UFUNCTION() void ServerCancelRogueCards(FGuid Session);
    UFUNCTION() void ServerRogueCardPresentationClosed(FGuid Session);
    UFUNCTION() void ClientRogueCardOffer(FGuid Request, FGuid Session, uint32 Epoch, const TArray<FString>& Cards, const FString& Error);
    UFUNCTION() void ClientRogueCardResult(FGuid Session, bool bSuccess, const FString& Error);
    FGuLiCommanderSelectionChangedSignature OnSelectionChanged;
    FGuLiCommanderCommandAckChangedSignature OnCommandAckChanged;
    FGuLiSoldierPoseChunkReceivedSignature OnPoseChunkReceived;
    FGuLiMoveEndpointsChangedSignature OnMoveEndpointsChanged;
    FGuLiMoveEndpointIdsChanged OnMoveEndpointIdsChanged;
    FGuLiMoveReadyToSendSignature OnMoveReadyToSend;
    FGuLiLocalTaskSubmittedSignature OnLocalTaskSubmitted;
#if !UE_BUILD_SHIPPING
    bool PrepareReviewSoldierSelection(TConstArrayView<FGuLiSoldierId> Members, uint32 RequestId);
    uint64 GetAcceptedPoseFrameCount() const { return AcceptedPoseFrameCount; }
    double GetLastAcceptedPoseReceiveTimeSeconds() const { return LastAcceptedPoseReceiveTimeSeconds; }
    TSharedRef<FJsonObject> GetReviewCommandEvidence() const;
#endif
private:
    struct FMoveResponseMember
    {
        FGuLiTaskUnitId Unit;
        FVector Start = FVector::ZeroVector;
        uint32 PreviousOrder = 0;
        bool bAccepted = false, bRemoteArmed = false;
    };
    TMap<FGuLiHybridObjectKey,FMoveResponseMember> MoveResponsePending;
    uint32 MoveResponseCommand = 0, MoveInputCommand = 0, MoveResponseEpoch = 0;
    int32 MoveResponseTotal = 0, MoveResponseStarted = 0, MoveResponseFailed = 0;
    double MoveInputAt = 0, MoveResponseInputAt = 0, MoveResponseLastStartAt = 0, MoveResponseTextAt = 0;
    bool bMoveResponseFrozen = false, bMoveResponseRemote = false;
    FString MoveResponseText;
    void BeginMoveResponse(const FGuLiUnitTaskCommand& Command);
    void FreezeMoveResponse(const FGuLiUnitTaskCommand& Command, TConstArrayView<FGuLiHybridObjectState> Objects);
    void TickMoveResponse();
    void RefreshMoveResponseText(double Now);
    void RejectMoveResponse(uint32 CommandId);
    // Selection is attached to an exact life, not the temporary Mass entity
    // that is detached/recreated while that life changes simulation owner.
    mutable TMap<FGuLiSoldierId,FGuLiHybridObjectKey> SelectionSoldierLives;
    struct FHybridInput
    {
        FGuLiUnitTaskCommand Command;
        FGuLiCommanderSelectionState Selection;
        FGuLiHybridCommand Frozen;
        TSharedPtr<FGuLiSharedMoveIntent> Intent;
        bool bPreviewPublished=false;
    };
    struct FHybridPendingCommand
    {
        FGuLiUnitTaskCommand Command;
        FGuLiCommanderSelectionState Selection;
        TMap<FGuLiHybridObjectKey,TOptional<bool>> Results;
    };
    TArray<FHybridInput> HybridInputs;
    TMap<FGuid,FHybridPendingCommand> HybridPendingCommands;
    TMap<uint32,FGuLiUnitTaskCommand> HybridSubmittedCommands;
    TMap<FGuid,FGuLiCommandAck> HybridCompletedCommands;
    TWeakObjectPtr<UGuLiHybridWorldSubsystem> HybridCommandWorld;
    uint32 HybridInputEpoch=0;
    uint64 LastHybridInputPlanFrame=MAX_uint64;
    void TickHybridInputs();
    bool SubmitHybridTask(FGuLiUnitTaskCommand Command);
    void HandleHybridCommandResult(const FGuLiHybridCommandResult& Result);
    void CompleteHybridCommand(FGuLiCommandAck Ack,const FString& Message);

    void ApplyPanelSelection(FGuLiPanelSelectionRequest Request, uint32 Sequence, uint32 Generation);
    void ApplyControlGroup(uint8 Slot, bool bSet, bool bAppend, bool bSteal, bool bFocus, uint32 Sequence, uint32 Generation);
    void ApplySelection(const FGuLiCommanderSelectionState& State, uint32 Sequence, bool bFocus, uint32 Generation);
    void ApplyTaskReceipt(const FGuLiCommandAck& Ack, const FString& Message, uint32 Generation);
    bool AdmitOrderedSequence(uint32 Sequence, uint32 Generation);
    void TickOrderedCommands();
    void PruneSelection();
    void PruneControlGroup(FGuLiCommanderControlGroup& Group, EGuLiTeam Team) const;
    bool CanProcessCommanderRequest(FGuLiCommandAck& Ack, const TCHAR* Context);
    bool ConsumeCommandRateLimit();
    void InitializeAck(FGuLiCommandAck& Ack, uint32 ClientId, EGuLiCommandKind Kind) const;
    void NotifySelectionChanged();
    static bool IsNewerSerial(uint32 Candidate, uint32 Baseline);
    void SetLocalMoveEndpoint(FGuLiSoldierId Id, const FGuLiMoveEndpointItem* Endpoint);
    void ClearLocalMoveEndpoints();
    void RefreshSelectedMoveEndpoints();
    uint32 NextOrderedSequence=1, LastOrderedSequence=0, OrderedGeneration=0;
    bool bDispatchingTaskReceipt=false, bOrderedSelectionValid=true;
    double NextOrderedSummaryTime=0;
    uint32 DisplayedTaskSelectionRevision=0;
    uint16 RelatedControlGroups=0;
    TArray<FGuLiCommanderControlGroup> ControlGroups;
    TArray<FGuLiUnitTaskSummary> TaskSummaries;
    TArray<int32> ControlGroupCounts;
    FString LastTaskFeedback;
    FGuLiNetworkRequestWindow CommanderRequestWindow;
    FGuLiCommanderSelectionState SelectionState;
    FGuLiCommandAck LastCommandAck;
    TArray<FGuLiCommandAck> PendingCommandAcks;
    TArray<FGuLiSoldierPoseChunk> PendingPoseChunks;
    FGuLiMoveEndpointFastArray MoveEndpoints;
    TMap<FGuLiSoldierId,int32> MoveEndpointIndex;
#if !UE_BUILD_SHIPPING
    uint64 AcceptedPoseFrameCount=0;
    uint32 LastAcceptedPoseFrameSequence=0;
    double LastAcceptedPoseReceiveTimeSeconds=0;
#endif
};
