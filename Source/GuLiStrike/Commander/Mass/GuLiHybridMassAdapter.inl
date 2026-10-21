#include "Battle/Combat/GuLiHybridTargetBinding.h"
// Included only by GuLiBattleAuthoritySubsystem.cpp, after its private runtime types.
#include "Commander/Mass/Private/GuLiHybridMoveIntent.h"
#include "Commander/Orders/Private/GuLiHybridTaskCommand.h"
#include "Network/GuLiHybridAdapterRegistry.h"
#include "Network/GuLiHybridActorWorldSubsystem.h"
#include "Gameplay/Units/GuLiExternalControlRegistry.h"
#include "Network/Private/GuLiHybridMessageConversions.h"
#include <deque>

namespace GuLiHybridMass
{
using namespace GuLiCommanderMassPrivate;
TAutoConsoleVariable<int32> PublishAfterMovement(TEXT("gs.Commander.PublishPoseAfterMovement"),1,
    TEXT("Sample due synchronous Mass poses after the shared movement step. Keeps 10 Hz publication and the existing wire format. 0 selects the comparison ordering."));
constexpr uint32 ObjectVersion=2,SystemVersion=3;
const FName SystemName(TEXT("CommanderMass"));
struct FPrivate
{
    FSoldierRuntime Soldier;
    FGuid CapturedOwner;
    double CapturedSeconds=0;
    TSharedPtr<FGuLiSharedMoveIntent> Intent;
};
struct FJob
{
    uint64 Handle=0;
    uint32 SourceCommand=0,Batch=0,Intent=0;
    FGuid Owner;
    EGuLiTeam Team=EGuLiTeam::Unassigned;
    FVector Target=FVector::ZeroVector;
    bool bAutomatic=false;
    TArray<uint32> Remaining;
    TArray<uint64> Versions;
    TArray<uint32> Members,Committed,Failed;
};
struct FSystem
{
    double Seconds=0;
    TArray<TSharedPtr<FGuLiSharedMoveIntent>> Intents;
    TArray<FJob> Jobs;
};
void Private(FGuLiHybridStateArchive& A,FPrivate& V)
{
    uint32 Version=ObjectVersion; A.Value(Version); if (Version!=ObjectVersion) A.Reject();
    auto& S=V.Soldier; A.Value(S.HybridKey); A.Value(S.SoldierId.Value); A.Value(V.CapturedOwner); A.Value(V.CapturedSeconds);
    bool HasIntent=V.Intent.IsValid();A.Value(HasIntent);
    if (HasIntent) GuLiHybridState::MoveIntent(A,V.Intent);else if (A.IsReading()) V.Intent.Reset();
    A.Enum(S.Team,uint8(EGuLiTeam::Blue)); A.Value(S.UnitTypeId); A.Value(S.AvoidanceRadiusCentimeters);
    A.Value(S.bMovementEvenArchetype); A.Value(S.Location); A.Value(S.Velocity); A.Value(S.LastMovementUpdateSimulationSeconds);
    A.Value(S.FacingYawDegrees); A.Value(S.FixedSpawnYawDegrees); A.Enum(S.FacingPolicy,uint8(EGuLiSoldierFacingPolicy::FixedSpawnYaw));
    auto& M=S.MechanicalPose; A.Value(M.UpperYawDegrees);
    for (auto& X : M.GunPitchDegrees) A.Value(X);
    for (auto& X : M.RecoilStartTime) A.Value(X);
    for (auto& X : M.RecoilFromCentimeters) A.Value(X);
    for (auto& X : M.WheelRadians) A.Value(X);
    A.Value(M.DiscTiltRadians); A.Value(M.DiscTiltFrom); A.Value(M.DiscTiltTarget); A.Value(M.DiscTiltElapsed);
    A.Value(M.HoverBlendStartMilliseconds); A.Value(M.HoverBlendFromWeight); A.Value(M.bHoverIdleTarget);
    A.Value(M.HoverStoppedSeconds); A.Value(M.bHoverInitialized); A.Value(M.HoverBobCentimeters);
    A.Value(M.HoverPitchRadians); A.Value(M.HoverRollRadians); A.Value(M.bInitialized);
    A.Value(S.VATPlayback.Clip); A.Value(S.VATPlayback.Phase); A.Value(S.VATPlayback.Rate); A.Value(S.VATPlayback.bInitialized);
    A.Value(S.NextMechanicalMuzzle); A.Value(S.Health); A.Value(S.MaxHealth); A.Value(S.Defense);
    A.Array(S.Weapons,64,[](auto& Ar,auto& W)
    {
        Ar.Value(W.SlotId); Ar.Value(W.ProfileIndex); Ar.Value(W.Attack.TargetId.Value); Ar.Value(W.Attack.NextFireSeconds);
        Ar.Value(W.Attack.PausedCooldownFraction); Ar.Value(W.Attack.bRatePaused); Ar.Value(W.Attack.ShotsFired);
        Ar.Enum(W.Attack.StopReason,uint8(EGuLiCombatStopReason::Fired));
    });
    A.Value(S.bAllowAutomaticFire); A.Value(S.ActiveSkill.SkillId); A.Value(S.ActiveSkill.ReadyAt);
    A.Value(S.ActiveSkill.Level); A.Value(S.ActiveSkill.ActiveCastId); A.Value(S.ActiveSkill.SuccessfulCasts);
    A.Value(S.StateRevision); A.Value(S.ActiveOrderId); A.Value(S.TaskGeneration); A.Value(S.RouteCursor); A.Value(S.bHasDockTarget);
    A.Value(S.bAutomaticAdvance); A.Value(S.bAttackMoveHolding); A.Value(S.LastValidNavLocation.Location); A.Value(S.FinalDestination.Location);
    A.Value(S.CommandStartLocation); A.Value(S.CurrentNavigationWaypoint);
    A.Enum(S.NavigationState,uint8(EGuLiSoldierNavigationState::Blocked)); A.Enum(S.NavigationFailure,uint8(EGuLiSoldierNavigationFailure::FinalSlotInvalidated));
    A.Value(S.LastCompletedOrderId); A.Value(S.LastFailedOrderId); A.Value(S.FinalDestinationNavigationGeneration);
    A.Value(S.bNavigationBudgetPending); A.Value(S.NoProgressSeconds); A.Value(S.FailureSimulationSeconds);
    A.Value(S.BestWaypointDistanceCentimeters); A.Value(S.LastProgressPathPointIndex); A.Value(S.ConsecutiveSurfaceFailures); A.Value(S.TotalSurfaceFailures);
    A.Array(S.PersonalPathPoints,4096,[](auto& Ar,auto& P){ Ar.Value(P); });
    A.Value(S.PersonalPathPointIndex); A.Value(S.PersonalPathRetries); A.Value(S.NextPersonalPathQuerySeconds);
    A.Value(S.bHasFinalDestination); A.Value(S.bForceMovementUpdate); A.Value(S.DeathSimulationSeconds); A.Value(S.bWreckExpired);
    A.Value(S.ExternalControlToken); A.Value(S.bPhased); A.Value(S.bExternalActionsLocked); A.Value(S.ContactObstacle);
    A.Value(S.BypassSide); A.Value(S.ContactClearSeconds); A.Value(S.FormationCorrectionAlpha); A.Value(S.EnvironmentSpeedScale);
    A.Value(S.bGroundMechYielding); A.Value(S.GroundMechYieldAnchor); A.Value(S.GroundMechYieldTarget); A.Value(S.GroundMechLastPressureSimulationSeconds);
    A.Value(S.DisplacementFrameFloor); A.Value(S.DisplacementLocation); A.Value(S.DisplacementSimulationTime); A.Value(S.ExternalLockSimulationTime);
    if (!S.HybridKey.IsValid() || !S.SoldierId.IsValid() || !V.CapturedOwner.IsValid() || S.Health<0 || S.MaxHealth<=0
        || S.Health>S.MaxHealth || S.AvoidanceRadiusCentimeters<=0 || S.RouteCursor<0 || S.ActiveSkill.Level<1 || S.NextMechanicalMuzzle>1) A.Reject();
}
bool Decode(TConstArrayView<uint8> Bytes,FPrivate& V)
{ FGuLiHybridStateArchive A(Bytes); Private(A,V); return A.Done(); }
void System(FGuLiHybridStateArchive& A,FSystem& V)
{
    uint32 Version=SystemVersion; A.Value(Version); if (Version!=SystemVersion) A.Reject();
    A.Value(V.Seconds);if (V.Seconds<0) A.Reject();
    A.Array(V.Intents,4096,[](auto& Ar,auto& I){ GuLiHybridState::MoveIntent(Ar,I); });
    A.Array(V.Jobs,4096,[](auto& Ar,FJob& J)
    {
        Ar.Value(J.Handle); Ar.Value(J.SourceCommand); Ar.Value(J.Batch); Ar.Value(J.Intent); Ar.Value(J.Owner);
        Ar.Enum(J.Team,uint8(EGuLiTeam::Blue)); Ar.Value(J.Target); Ar.Value(J.bAutomatic);
        Ar.Array(J.Remaining,10000,[](auto& B,auto& Id){ B.Value(Id); if (!Id) B.Reject(); });
        Ar.Array(J.Versions,10000,[](auto& B,auto& N){ B.Value(N); });
        Ar.Array(J.Members,10000,[](auto& B,auto& N){ B.Value(N); if (!N) B.Reject(); });
        Ar.Array(J.Committed,10000,[](auto& B,auto& N){ B.Value(N); if (!N) B.Reject(); });
        Ar.Array(J.Failed,10000,[](auto& B,auto& N){ B.Value(N); if (!N) B.Reject(); });
        TSet<uint32> Members(J.Members),States;
        for (const auto& List : {J.Remaining,J.Committed,J.Failed}) for (auto Id : List)
        { if (!Members.Contains(Id) || States.Contains(Id)) Ar.Reject(); States.Add(Id); }
        if (!J.Handle || (!J.Intent && !J.Remaining.IsEmpty()) || J.Remaining.Num()!=J.Versions.Num()
            || J.Members.IsEmpty() || Members.Num()!=J.Members.Num()) Ar.Reject();
    });
    for (const auto& J : V.Jobs) if (J.Intent>uint32(V.Intents.Num())) A.Reject();
}
bool Decode(TConstArrayView<uint8> Bytes,FSystem& V)
{ FGuLiHybridStateArchive A(Bytes,16*1024*1024); System(A,V); return A.Done(); }
AGuLiBattlePlayerState* Player(UWorld& World,const FGuid& Id)
{
    if (const auto* State=World.GetGameState<AGuLiBattleGameState>())
        for (APlayerState* Entry : State->PlayerArray)
            if (auto* PS=Cast<AGuLiBattlePlayerState>(Entry); PS && PS->GetPlayerGuid()==Id) return PS;
    return nullptr;
}
void Present(UWorld& World,const FGuLiSoldierPoseChunk& Chunk)
{
    if (auto* Controller=World.GetFirstPlayerController())
        if (auto* Net=Controller->FindComponentByClass<UGuLiCommanderNetSyncComponent>()) Net->ApplyHybridPoseChunk(Chunk);
}
struct FDecodedPose final:GuLiNet::IObjectMotionOutput
{FGuLiSoldierPoseChunk Chunk;TArray<uint8> Acknowledgment;};
struct FWorkerPoseCodec final:GuLiNet::IObjectMotionCodec
{
    uint32 Ordinal=0,ReceiveOwner=0,ReceiveEpoch=0,SendOwner=0,SendEpoch=0,Displacement=0;
    uint64 SendGeneration=0;
    TMap<FGuid,GuLiCommanderPoseCodec::FSender> Senders;
    GuLiCommanderPoseCodec::FReceiver Receiver;
    explicit FWorkerPoseCodec(uint32 Id):Ordinal(Id) {}
    void PrepareSend(uint32 Owner,uint32 Epoch,uint32 Floor)
    {
        if (SendOwner!=Owner||SendEpoch!=Epoch) {Senders.Reset();SendOwner=Owner;SendEpoch=Epoch;Displacement=Floor;++SendGeneration;}
        else if (Displacement!=Floor)
        {const FGuLiSoldierId Id(Ordinal);for (auto& S:Senders) S.Value.Forget(MakeArrayView(&Id,1));Displacement=Floor;++SendGeneration;}
    }
    std::shared_ptr<GuLiNet::IObjectMotionOutput> Decode(std::span<const uint8> Bytes,GuLiNet::Revision Owner,GuLiNet::Revision Epoch,bool& Valid) override
    {
        Valid=false;if (Owner>MAX_uint32||Epoch>MAX_uint32) return {};
        if (ReceiveOwner!=Owner||ReceiveEpoch!=Epoch) {Receiver.Reset(uint32(Owner),uint32(Epoch));ReceiveOwner=uint32(Owner);ReceiveEpoch=uint32(Epoch);}
        FGuLiEncodedPoseBlock Block;Block.Data.Append(Bytes.data(),int32(Bytes.size()));auto Out=std::make_shared<FDecodedPose>();
        const auto Result=Receiver.Decode(Block,Out->Chunk);
        if (Result==GuLiCommanderPoseCodec::EDecodeResult::Duplicate||Result==GuLiCommanderPoseCodec::EDecodeResult::TooOld||Result==GuLiCommanderPoseCodec::EDecodeResult::MissingBaseline)
        {Valid=true;return {};}
        if (Result!=GuLiCommanderPoseCodec::EDecodeResult::Decoded||Out->Chunk.Samples.Num()!=1||Out->Chunk.Samples[0].SoldierId.Value!=Ordinal) return {};
        FGuLiPoseAcknowledgment Ack;
        if (Receiver.BuildAcknowledgment(Ack))
        {FGuLiHybridStateArchive A;A.Value(Ack.ProtocolVersion);A.Value(Ack.SyncGeneration);A.Value(Ack.MatchEpoch);A.Value(Ack.LatestSequence);for (auto& B:Ack.ReceivedBits) A.Value(B);A.Finish(Out->Acknowledgment);}
        Valid=true;return Out;
    }
    bool Feedback(GuLiNet::Id Peer,std::span<const uint8> Bytes) override
    {
        auto* Sender=Senders.Find(GuLiNetBridge::ToUE(Peer));if (!Sender) return false;
        FGuLiPoseAcknowledgment Ack;FGuLiHybridStateArchive A({Bytes.data(),int32(Bytes.size())});
        A.Value(Ack.ProtocolVersion);A.Value(Ack.SyncGeneration);A.Value(Ack.MatchEpoch);A.Value(Ack.LatestSequence);
        for (auto& B:Ack.ReceivedBits) A.Value(B);return A.Done()&&Sender->Confirm(Ack);
    }
};
struct FPoseInput
{
    FGuLiHybridObjectKey Key;uint64 At=0,Owner=0,Control=0;
    uint32 Epoch=0,SimTick=0,Displacement=0;
    FVector Position,Velocity;float Facing=0,Upper=0,Pitch[2]{},VATPhase=0,VATRate=0;
    FGuLiQuantizedSoldierPose Value;TArray<FGuid> Peers;
    std::shared_ptr<FWorkerPoseCodec> Codec;
};
struct FPoseBatch final:GuLiNet::IGameplayCalculation
{
    struct FSend {FGuid Peer;GuLiCommanderPoseCodec::FPreparedBlock Prepared;uint64 Generation=0;bool Admitted=false;};
    struct FOutput {FGuLiSoldierPoseChunk Chunk;TArray<FSend> Sends;bool Valid=false;};
    TSet<FGuLiHybridObjectKey> Keys;TArray<FPoseInput> Inputs;TArray<FOutput> Outputs;
    void Compute() override
    {
        TRACE_CPUPROFILER_EVENT_SCOPE(GuLiHybrid_WorkerMassPoseEncode);
        Outputs.SetNum(Inputs.Num());
        for (int32 I=0;I<Inputs.Num();++I)
        {
            const auto& V=Inputs[I];auto& O=Outputs[I];auto& Chunk=O.Chunk;
            Chunk.AuthorityEpoch=V.Epoch;Chunk.FrameSequence=uint32(V.At/6);Chunk.ServerSimTick=V.SimTick;Chunk.ServerTimeSeconds=float(double(V.At)/60.);
            auto P=V.Value;if (!GuLiCommanderPoseCodec::Quantize(V.Position,V.Velocity,V.Facing,P)) continue;
            P.UpperYaw=FRotator::CompressAxisToShort(V.Upper);P.LeftGunPitch=int16(FMath::RoundToInt(V.Pitch[0]*100));P.RightGunPitch=int16(FMath::RoundToInt(V.Pitch[1]*100));
            P.VATPhase=uint16(FMath::RoundToInt(FMath::Clamp(V.VATPhase,0.f,1.f)*65535));P.VATRate=uint16(FMath::RoundToInt(FMath::Clamp(V.VATRate,0.f,255.f)*256));
            Chunk.Samples.Add(P);V.Codec->PrepareSend(uint32(V.Owner),V.Epoch,V.Displacement);
            for (const auto& Peer:V.Peers)
            {
                auto* Sender=V.Codec->Senders.Find(Peer);if (!Sender) {Sender=&V.Codec->Senders.Add(Peer);Sender->Reset(uint32(V.Owner),V.Epoch);}
                FSend Send;Send.Peer=Peer;Send.Generation=V.Codec->SendGeneration;
                if (Sender->Prepare(Chunk,700,Send.Prepared)) O.Sends.Add(MoveTemp(Send));
            }
            O.Valid=true;
        }
    }
};
struct FPoseConfirmation final:GuLiNet::IGameplayCalculation
{
    std::shared_ptr<FPoseBatch> Batch;
    void Compute() override
    {
        TRACE_CPUPROFILER_EVENT_SCOPE(GuLiHybrid_WorkerPoseSendConfirmation);
        for (int32 I=0;I<Batch->Inputs.Num();++I) for (const auto& S:Batch->Outputs[I].Sends)
        {
            auto& Codec=*Batch->Inputs[I].Codec;
            if (S.Admitted&&S.Generation==Codec.SendGeneration)
                if (auto* Sender=Codec.Senders.Find(S.Peer)) Sender->Commit(S.Prepared);
        }
    }
};
struct FPosePipeline
{
    std::deque<std::shared_ptr<FPoseBatch>> Pending;uint64 PendingBytes=0;
    bool Queue(FPoseInput Input)
    {
        const uint64 Bytes=2048+Input.Peers.Num()*2048;if (PendingBytes+Bytes>64ull*1024*1024) return false;
        // Newborns have different object ages. Group the ordered step by
        // unique object identity, not adjacent equal ages, or a mixed-life
        // army degenerates into hundreds of tiny queued batches.
        if (Pending.empty()||Pending.back()->Keys.Contains(Input.Key)) Pending.push_back(std::make_shared<FPoseBatch>());
        Pending.back()->Keys.Add(Input.Key);
        Pending.back()->Inputs.Add(MoveTemp(Input));PendingBytes+=Bytes;return true;
    }
};
}

class FGuLiHybridMassAdapter final : public IGuLiHybridSimulationAdapter, public TSharedFromThis<FGuLiHybridMassAdapter>
{
    TWeakObjectPtr<UGuLiBattleAuthoritySubsystem> Authority;
    FGuLiHybridObjectState Record;
    FGuLiTargetHandle RegisteredTarget;
    bool bLocal=false,bRestoringQuery=false;
    uint64 LastPublished=0;
#if !UE_BUILD_SHIPPING
    uint32 LastReportedMoveCommand=0;
#endif
    TMap<FGuid,GuLiCommanderPoseCodec::FSender> Senders;
    GuLiCommanderPoseCodec::FReceiver Receiver;
    std::shared_ptr<GuLiHybridMass::FWorkerPoseCodec> WorkerCodec;
    GuLiCommanderMassPrivate::FSoldierRuntime* Soldier() const
    {
        auto* Self=Authority.Get(); const auto* I=Self && Self->AuthorityState?Self->AuthorityState->SoldierIndexById.Find(Record.Ordinal):nullptr;
        if (!I) return nullptr; auto& S=Self->AuthorityState->Soldiers[*I]; return S.HybridKey==Record.Key?&S:nullptr;
    }
    void ReadModel()
    {
        auto* Self=Authority.Get(); if (!Self || !UGuLiNetworkSettings::CanPresent(Self->GetWorld())) return;
        TRACE_CPUPROFILER_EVENT_SCOPE(GuLiSim_MassReadModelPublish);
        auto* World=Self->GetWorld(); auto* Battle=World->GetGameState<AGuLiBattleGameState>(); if (!Battle || !Battle->GetMatchEpoch()) return;
        AGuLiSoldierStateReplicator* R=Self->AuthorityState?Self->AuthorityState->HybridReadModel.Get():nullptr;
        if (R && (R->GetWorld()!=World || R->IsActorBeingDestroyed())) R=nullptr;
        if (!R) for (TActorIterator<AGuLiSoldierStateReplicator> It(World); It; ++It)
            if (!It->IsActorBeingDestroyed()) {R=*It;break;}
        if (!R) { R=World->SpawnActor<AGuLiSoldierStateReplicator>(); if (R) R->SetReplicates(false); }
        if (!R) return;
        if (Self->AuthorityState) Self->AuthorityState->HybridReadModel=R;
        if (R->GetSnapshotMatchEpoch()!=Battle->GetMatchEpoch())
        { TArray<FGuLiSoldierStateItem> All; Self->BuildSoldierStateSnapshot(All); R->ApplyAuthoritySnapshot(All,Battle->GetMatchEpoch()); }
        else { FGuLiSoldierStateItem Item; if (Self->BuildSoldierState(FGuLiSoldierId(Record.Ordinal),Item)) R->ApplyAuthorityDelta(MakeArrayView(&Item,1),Battle->GetMatchEpoch()); }
    }
    bool Archetypes(UGuLiBattleAuthoritySubsystem& Self)
    {
        using namespace GuLiCommanderMassPrivate;
        auto& State=*Self.AuthorityState; if (State.RuntimeTuningEvenBaseArchetype.IsValid()) return true;
        auto* Mass=State.MassEntitySubsystem.Get(); if (!Mass) return false;
        auto& Manager=Mass->GetMutableEntityManager();
        TArray<const UScriptStruct*> Types={FTransformFragment::StaticStruct(),FAgentRadiusFragment::StaticStruct(),FMassVelocityFragment::StaticStruct(),
            FMassForceFragment::StaticStruct(),FMassMoveTargetFragment::StaticStruct(),FMassNavigationObstacleGridCellLocationFragment::StaticStruct(),
            FGuLiMassIdentityFragment::StaticStruct(),FGuLiCommanderStateTreeFragment::StaticStruct(),FGuLiMassHealthFragment::StaticStruct(),
            FGuLiMassSoldierStatsFragment::StaticStruct(),FGuLiMassOrderFragment::StaticStruct(),FGuLiMassSlotTargetFragment::StaticStruct(),
            FGuLiMassAvoidanceOutputFragment::StaticStruct(),FGuLiMassAvoidanceStateFragment::StaticStruct(),FGuLiMassAvoidanceParticipantTag::StaticStruct(),
            FGuLiServerAuthorityMassTag::StaticStruct(),FGuLiMassRuntimeTuningEvenTag::StaticStruct()};
        FMassArchetypeCreationParams Params; Params.DebugName=TEXT("GuLiHybridGrantedSoldiers");
        State.RuntimeTuningEvenBaseArchetype=Manager.CreateArchetype(Types,Params);
        Types.RemoveSingle(FGuLiMassRuntimeTuningEvenTag::StaticStruct()); Types.Add(FGuLiMassRuntimeTuningOddTag::StaticStruct());
        Params.DebugName=TEXT("GuLiHybridGrantedSoldiersOdd"); State.RuntimeTuningOddBaseArchetype=Manager.CreateArchetype(Types,Params);
        State.AuthorityArchetype=State.RuntimeTuningEvenBaseArchetype;
        State.bPopulationSpawned=State.RuntimeTuningEvenBaseArchetype.IsValid() && State.RuntimeTuningOddBaseArchetype.IsValid();
        if (auto* GI=Self.GetWorld()->GetGameInstance())
            if (const auto* Session=GI->GetSubsystem<UGuLiHybridSessionSubsystem>()) State.NextPlanId=(uint64(GetTypeHash(Session->GetLocalPlayerId())|1u)<<32)|1u;
        Self.bSoldierSimulationEnabled=State.bPopulationSpawned; return State.bPopulationSpawned;
    }
    void Publish(uint64 At)
    {
        auto* S=Soldier(); auto* Self=Authority.Get(); if (!S || !Self || At%6 || At==LastPublished) return;
        LastPublished=At; auto* World=Self->GetWorld(); auto* Hybrid=World->GetSubsystem<UGuLiHybridWorldSubsystem>();
        auto* Session=World->GetGameInstance()->GetSubsystem<UGuLiHybridSessionSubsystem>();
        if (!Hybrid || !Session || Hybrid->GetLogicalTick()/6>MAX_uint32) return;
        if (UGuLiHotspotOptions::Enabled(World,EGuLiHotspot::GameplayWorker))
        {
            if (!WorkerCodec) WorkerCodec=std::make_shared<GuLiHybridMass::FWorkerPoseCodec>(Record.Ordinal);
            auto& Pipeline=Self->AuthorityState->PosePipeline;if (!Pipeline) Pipeline=std::make_shared<GuLiHybridMass::FPosePipeline>();
            GuLiHybridMass::FPoseInput V;V.Codec=WorkerCodec;V.Key=Record.Key;V.At=At;V.Owner=Record.OwnerEpoch;V.Control=Record.ControlEpoch;
            V.Epoch=World->GetGameState<AGuLiBattleGameState>()->GetMatchEpoch();V.SimTick=Self->AuthorityState->ServerSimTick;V.Displacement=S->DisplacementFrameFloor;
            V.Position=S->Location;V.Velocity=S->Velocity;V.Facing=S->FacingYawDegrees;V.Upper=S->MechanicalPose.bInitialized?S->MechanicalPose.UpperYawDegrees:S->FacingYawDegrees;
            V.Pitch[0]=S->MechanicalPose.GunPitchDegrees[0];V.Pitch[1]=S->MechanicalPose.GunPitchDegrees[1];V.VATPhase=S->VATPlayback.Phase;V.VATRate=S->VATPlayback.Rate;
            auto& P=V.Value;P.SoldierId=S->SoldierId;P.ActiveOrderId=S->ActiveOrderId;P.HoverBlendStartMilliseconds=S->MechanicalPose.HoverBlendStartMilliseconds;
            P.HoverBlendFromWeight=S->MechanicalPose.HoverBlendFromWeight;P.bHoverIdleTarget=S->MechanicalPose.bHoverIdleTarget;P.VATClip=S->VATPlayback.Clip;
            P.State=S->IsAlive()?(S->ActiveOrderId?EGuLiSoldierPoseState::Moving:EGuLiSoldierPoseState::Idle):EGuLiSoldierPoseState::Destroyed;
            P.Flags=S->DisplacementFrameFloor&&int32(uint32(At/6)-S->DisplacementFrameFloor)<3?GULI_SOLDIER_POSE_FLAG_TELEPORT:0;
            for (const auto& M:Session->GetMembers()) if (M.bOnline&&M.bReady&&M.Player!=Session->GetLocalPlayerId()) V.Peers.Add(M.Player);
            if (!Pipeline->Queue(std::move(V))) LastPublished=0;ReadModel();return;
        }
        FGuLiSoldierPoseChunk Chunk; Chunk.AuthorityEpoch=World->GetGameState<AGuLiBattleGameState>()->GetMatchEpoch();
        Chunk.FrameSequence=static_cast<uint32>(At/6); Chunk.ServerSimTick=Self->AuthorityState->ServerSimTick;
        Chunk.ServerTimeSeconds=static_cast<float>(double(At)/60.);
        auto& P=Chunk.Samples.AddDefaulted_GetRef(); P.SoldierId=S->SoldierId;
        if (!GuLiCommanderPoseCodec::Quantize(S->Location,S->Velocity,S->FacingYawDegrees,P)) { Hybrid->Suspend(TEXT("Hybrid Mass pose exceeds quantization bounds")); return; }
        P.ActiveOrderId=S->ActiveOrderId; P.UpperYaw=FRotator::CompressAxisToShort(S->MechanicalPose.bInitialized?S->MechanicalPose.UpperYawDegrees:S->FacingYawDegrees);
        P.LeftGunPitch=int16(FMath::RoundToInt(S->MechanicalPose.GunPitchDegrees[0]*100)); P.RightGunPitch=int16(FMath::RoundToInt(S->MechanicalPose.GunPitchDegrees[1]*100));
        P.HoverBlendStartMilliseconds=S->MechanicalPose.HoverBlendStartMilliseconds; P.HoverBlendFromWeight=S->MechanicalPose.HoverBlendFromWeight; P.bHoverIdleTarget=S->MechanicalPose.bHoverIdleTarget;
        P.VATClip=S->VATPlayback.Clip; P.VATPhase=uint16(FMath::RoundToInt(FMath::Clamp(S->VATPlayback.Phase,0.f,1.f)*65535)); P.VATRate=uint16(FMath::RoundToInt(FMath::Clamp(S->VATPlayback.Rate,0.f,255.f)*256));
        P.State=S->IsAlive()?(S->ActiveOrderId?EGuLiSoldierPoseState::Moving:EGuLiSoldierPoseState::Idle):EGuLiSoldierPoseState::Destroyed;
        P.Flags=S->DisplacementFrameFloor && int32(Chunk.FrameSequence-S->DisplacementFrameFloor)<3?GULI_SOLDIER_POSE_FLAG_TELEPORT:0;
        for (const auto& Member : Session->GetMembers())
        {
            if (!Member.bOnline || !Member.bReady || Member.Player==Session->GetLocalPlayerId()) continue;
            auto* Sender=Senders.Find(Member.Player);
            if (!Sender) { Sender=&Senders.Add(Member.Player); Sender->Reset(static_cast<uint32>(Record.OwnerEpoch),Chunk.AuthorityEpoch); }
            GuLiCommanderPoseCodec::FPreparedBlock Prepared;
            // Leave room for the application envelope and transport fragmentation header.
            if (Sender->Prepare(Chunk,700,Prepared) && Hybrid->PublishMotionTo(Record.Key,Member.Player,Prepared.Block.Data)) Sender->Commit(Prepared);
        }
#if !UE_BUILD_SHIPPING
        if (GuLiMoveLatency::IsEnabled() && S->FirstDisplacementAt>0 && S->MoveTrace.Command
            && LastReportedMoveCommand!=S->MoveTrace.Command)
        {
            LastReportedMoveCommand=S->MoveTrace.Command;
            GuLiMoveLatency::Record(TEXT("pose-publish"),S->MoveTrace,1,
                *FString::Printf(TEXT("soldier=%u tick=%llu simTick=%u order=%u"),
                    S->SoldierId.Value,At,Chunk.ServerSimTick,P.ActiveOrderId));
        }
#endif
        GuLiHybridMass::Present(*World,Chunk); ReadModel();
    }
public:
    explicit FGuLiHybridMassAdapter(UGuLiBattleAuthoritySubsystem& Self) : Authority(&Self) {}
    UObject* LifetimeOwner() const override { return Authority.Get(); }
    uint32 Version() const override { return GuLiHybridMass::ObjectVersion; }
    TArray<FName> RequiredSharedStateSystems() const override { return {TEXT("CommanderTasks"),GuLiHybridMass::SystemName,TEXT("ArmyAdvance")}; }
    bool Prepare(const FGuLiHybridObjectState& R) override
    {
        auto* Self=Authority.Get(); if (!Self || R.Key.Kind!=EGuLiHybridObjectKind::Soldier || !R.Ordinal || R.OwnerEpoch>MAX_uint32) return false;
        Record=R; if (Soldier()) return true;
        const FString Definition=R.Definition.ToString(); if (!Definition.StartsWith(TEXT("unit/"))) return false;
        uint32 Type=0; if (!LexTryParseString(Type,*Definition.Mid(5)) || !Type || Type>MAX_uint16 || !Archetypes(*Self)) return false;
        // No local auto-spawn is admitted here: this scope installs an already registered object.
        TGuardValue<bool> Grant(Self->bInstallingHybridGrant,true); TGuardValue<uint32> Ordinal(Self->InstallingHybridOrdinal,R.Ordinal);
        FGuLiSoldierId Id;
        if (!Self->InstallGrantedSoldier(R.Team,static_cast<uint16>(Type),R.Location,Id,R.FacingYawDegrees,FMath::Max(1.f,R.Health.Health))) return false;
        auto& S=Self->AuthorityState->Soldiers[Self->AuthorityState->SoldierIndexById.FindChecked(Id.Value)]; S.HybridKey=R.Key; S.bLocalSimulation=false;
        Self->RegisterCombatLedgerTarget(Id);
        RegisteredTarget=Self->MakeSoldierTargetHandle(Id);
        auto* Hybrid=Self->GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>();
        if (!Hybrid || !Hybrid->BindTarget(Self->MakeSoldierTargetHandle(Id),R.Key)) return false;
        return Self->GetWorld()->GetSubsystem<UGuLiExternalControlRegistry>()->Bind(R.Key,*Self,
            [this](FGuLiExternalBody& Out)
            {
                const auto* V=Soldier();if (!V || !V->IsAlive()) return false;
                Out.Transform=FTransform(FRotator(0,V->FacingYawDegrees,0),V->Location);Out.Radius=Out.HalfHeight=V->AvoidanceRadiusCentimeters;
                Out.bGroundPivot=true;Out.bLocked=V->bExternalActionsLocked;return true;
            },[this](const FGuLiExternalControl& In)
            {
                auto* V=Soldier();auto* A=Authority.Get();if (!V || !A) return false;
                const FGuLiMassExternalUnit Unit{V->SoldierId,In.Transform,V->AvoidanceRadiusCentimeters};
                if (!A->ApplyExternalUnitState(MakeArrayView(&Unit,1),In.Token,In.bPhased,In.bLocked,In.bDisplace)) return false;
                if (In.bDisplace)
                {
                    // Hybrid pose sequences use logical ticks, never the legacy
                    // world's independently incremented frame counter.
                    V->DisplacementFrameFloor=uint32(FMath::Clamp<uint64>((In.LogicalTick+5)/6,1,MAX_uint32));
                    V->DisplacementSimulationTime=double(In.LogicalTick)/60.;
                    const auto Id=V->SoldierId;
                    for (auto& Sender:Senders) Sender.Value.Forget(MakeArrayView(&Id,1));
                    Receiver.Forget(MakeArrayView(&Id,1));LastPublished=0;
                }
                ReadModel();return true;
            });
    }
    void SetAuthority(const FGuLiHybridObjectState& R,bool Local) override
    {
        auto* Self=Authority.Get(); if (!Self) return;
        if (R.OwnerEpoch>MAX_uint32) { Self->GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>()->Suspend(TEXT("Hybrid Mass ownership exceeds pose codec generation")); return; }
        const bool Changed=Record.OwnerEpoch!=R.OwnerEpoch || bLocal!=Local; Record=R; bLocal=Local;
        if (auto* S=Soldier())
        {
            S->bLocalSimulation=Local;
            if (!Local) Self->AuthorityState->ImmediateMoveIds.Remove(S->SoldierId.Value);
            else if (Changed && S->ActiveOrderId && S->MoveIntent && !S->bAutomaticAdvance)
                Self->AuthorityState->ImmediateMoveIds.Add(S->SoldierId.Value);
        }
        if (Changed)
        {
            Senders.Reset(); Receiver.Reset(static_cast<uint32>(R.OwnerEpoch),Self->GetWorld()->GetGameState<AGuLiBattleGameState>()->GetMatchEpoch()); LastPublished=0;
#if !UE_BUILD_SHIPPING
            LastReportedMoveCommand=0;
#endif
        }
        if (Local && R.LifeState==1)
            if (auto* Tasks=Self->GetWorld()->GetSubsystem<UGuLiUnitTaskSubsystem>())
            { const FGuLiSoldierId Id(R.Ordinal); Tasks->RegisterSoldiers(R.Team,MakeArrayView(&Id,1)); }
    }
    void Advance(uint64 At,double Delta) override
    {
        if (!Authority.IsValid() || !bLocal) return;
        auto* Self=Authority.Get();
        if (Self->AuthorityState && GuLiHybridMass::PublishAfterMovement.GetValueOnGameThread()!=0
            && !UGuLiHotspotOptions::Enabled(Self->GetWorld(),EGuLiHotspot::GameplayWorker))
        {
            // Object callbacks precede shared-state callbacks. Only defer sampling
            // within this logical step, so its timestamp describes the new pose.
            Self->AuthorityState->PendingPosePublishes.Add({AsShared(),At,Record.OwnerEpoch,Record.ControlEpoch});
            return;
        }
        Publish(At);
    }
    void PublishCompletedMovement(uint64 At,uint64 OwnerEpoch,uint64 ControlEpoch)
    {
        if (bLocal && !bRestoringQuery && Record.OwnerEpoch==OwnerEpoch && Record.ControlEpoch==ControlEpoch)
            Publish(At);
    }
    uint32 AdvancePeriod() const override {return 6;} // Publish is due at the existing 10 Hz boundary.
    std::shared_ptr<GuLiNet::IObjectMotionCodec> CreateMotionCodec() override
    {if (!WorkerCodec) WorkerCodec=std::make_shared<GuLiHybridMass::FWorkerPoseCodec>(Record.Ordinal);return WorkerCodec;}
    bool Execute(const FGuLiHybridCommand& C,const FGuLiHybridCommandMember& M) override
    {
        auto* Self=Authority.Get(); auto* S=Soldier(); if (!Self || !S || !S->CanSimulate() || M.Key!=Record.Key) return false;
        auto* Owner=GuLiHybridMass::Player(*Self->GetWorld(),Record.Controller); auto* Tasks=Self->GetWorld()->GetSubsystem<UGuLiUnitTaskSubsystem>();
        if (!Owner || !Tasks) return false;
        FGuLiUnitTaskCommand Command; float Pitch=0,Radius=0;
        if (C.Operation==TEXT("UnitTask"))
        { if (!GuLiHybridState::DecodeTaskCommand(C.Arguments,Command,Pitch,Radius)) return false; }
        else if (C.Operation==TEXT("Move")) { Command.CommandId=GetTypeHash(C.Event)|1; Command.Kind=EGuLiUnitTaskKind::Move; Command.Target=M.Destination; }
        else return false;
        if (GuLiMoveLatency::IsEnabled())
            GuLiMoveLatency::Record(TEXT("owner-command"),GuLiMoveLatency::Context(Owner,Command.CommandId),1,
                *FString::Printf(TEXT("soldier=%u"),Record.Ordinal));
        auto Intent=Self->CreateSharedMoveIntent(Command.Target,GetTypeHash(C.Event));
        Intent->AssignedPoints.Add(Record.Ordinal,FNavLocation(M.Destination)); Intent->bMetadataReady=true; Intent->bExhausted=true; Intent->WantedPoints=1;
        Intent->Pitch=Pitch; Intent->GeneratedRadius=Radius;
        FGuLiCommanderSelectionState Selection; Selection.SelectionRevision=1;
        auto& Cohort=Selection.Cohorts.AddDefaulted_GetRef(); Cohort.CohortId=FGuLiControlCohortId(Record.Ordinal); Cohort.MemberIds.Add(FGuLiSoldierId(Record.Ordinal)); Cohort.AliveCount=1;
        Command.SelectionRevision=Selection.SelectionRevision; Command.SharedMoveIntent=Intent;
        FString Message; int32 Accepted=0,Rejected=0;EGuLiCommandAckResult Reason;
        const bool Submitted=Tasks->Submit(*Owner,Selection,Command,Message,Accepted,Rejected,Reason) && Accepted==1;
        if (!Submitted) UE_LOG(LogGuLiCommanderMass,Log,TEXT("Hybrid Mass command=%u object=%u reason=%u target=%s message=%s"),
            Command.CommandId,Record.Ordinal,uint8(Reason),*FVector(Command.Target).ToString(),*Message);
        return Submitted;
    }
    bool ApplyMotion(TConstArrayView<uint8> Bytes,uint64 At) override
    {
        auto* S=Soldier(); auto* Self=Authority.Get(); if (!S || !Self || bLocal) return false;
        FGuLiEncodedPoseBlock Block; Block.Data.Append(Bytes.GetData(),Bytes.Num()); FGuLiSoldierPoseChunk Chunk;
        const auto Result=Receiver.Decode(Block,Chunk);
        if (Result==GuLiCommanderPoseCodec::EDecodeResult::Duplicate || Result==GuLiCommanderPoseCodec::EDecodeResult::TooOld
            || Result==GuLiCommanderPoseCodec::EDecodeResult::MissingBaseline) return true;
        if (Result!=GuLiCommanderPoseCodec::EDecodeResult::Decoded || Chunk.Samples.Num()!=1 || Chunk.Samples[0].SoldierId.Value!=Record.Ordinal) return false;
        GuLiHybridMass::FDecodedPose Decoded;Decoded.Chunk=MoveTemp(Chunk);FGuLiPoseAcknowledgment Ack;
        if (Receiver.BuildAcknowledgment(Ack))
        {FGuLiHybridStateArchive A;A.Value(Ack.ProtocolVersion);A.Value(Ack.SyncGeneration);A.Value(Ack.MatchEpoch);A.Value(Ack.LatestSequence);for (auto& Bits:Ack.ReceivedBits) A.Value(Bits);A.Finish(Decoded.Acknowledgment);}
        return ApplyDecodedMotion(Decoded,At);
    }
    bool ApplyDecodedMotion(const GuLiNet::IObjectMotionOutput& Value,uint64) override
    {
        const auto& Decoded=static_cast<const GuLiHybridMass::FDecodedPose&>(Value);const auto& Chunk=Decoded.Chunk;
        auto* S=Soldier();auto* Self=Authority.Get();if (!S||!Self||bLocal||Chunk.Samples.Num()!=1||Chunk.Samples[0].SoldierId.Value!=Record.Ordinal) return false;
        const auto& P=Chunk.Samples[0]; S->Location=P.GetWorldLocationCentimeters(); S->Velocity=P.GetVelocityCentimetersPerSecond(); S->FacingYawDegrees=FRotator::DecompressAxisFromByte(P.FacingYaw);
        S->ActiveOrderId=P.ActiveOrderId; S->MechanicalPose.UpperYawDegrees=FRotator::DecompressAxisFromShort(P.UpperYaw);
        S->MechanicalPose.GunPitchDegrees[0]=P.LeftGunPitch*.01f; S->MechanicalPose.GunPitchDegrees[1]=P.RightGunPitch*.01f;
        S->MechanicalPose.HoverBlendStartMilliseconds=P.HoverBlendStartMilliseconds; S->MechanicalPose.HoverBlendFromWeight=P.HoverBlendFromWeight;
        S->MechanicalPose.bHoverIdleTarget=P.bHoverIdleTarget; S->MechanicalPose.bInitialized=true;
        S->VATPlayback.Clip=P.VATClip; S->VATPlayback.Phase=P.VATPhase/65535.f; S->VATPlayback.Rate=P.VATRate/256.f; S->VATPlayback.bInitialized=true;
        auto& Manager=Self->AuthorityState->MassEntitySubsystem->GetMutableEntityManager();
        if (Manager.IsEntityValid(S->Entity))
        { Manager.GetFragmentDataChecked<FTransformFragment>(S->Entity).SetTransform(FTransform(FRotator(0,S->FacingYawDegrees,0),S->Location)); Manager.GetFragmentDataChecked<FMassVelocityFragment>(S->Entity).Value=S->Velocity; }
        if (!Decoded.Acknowledgment.IsEmpty()) Self->GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>()->PublishMotionAcknowledgment(Record.Key,Decoded.Acknowledgment);
        GuLiHybridMass::Present(*Self->GetWorld(),Chunk); ReadModel(); return true;
    }
    bool ApplyMotionFeedback(const FGuid& Peer,TConstArrayView<uint8> Bytes) override
    {
        auto* Sender=Senders.Find(Peer); if (!Sender || !bLocal) return false;
        FGuLiPoseAcknowledgment Ack; FGuLiHybridStateArchive A(Bytes);
        A.Value(Ack.ProtocolVersion); A.Value(Ack.SyncGeneration); A.Value(Ack.MatchEpoch); A.Value(Ack.LatestSequence);
        for (auto& Bits : Ack.ReceivedBits) A.Value(Bits); return A.Done() && Sender->Confirm(Ack);
    }
    void ApplyLedger(const FGuLiHybridObjectState& R,uint64 Ledger,bool Effects) override
    {
        auto* Self=Authority.Get(); auto* S=Soldier(); if (!Self || !S) return; Record=R;
        if (R.Health.Health<S->Health)
        { TGuardValue<bool> Apply(Self->bApplyingHybridLedger,true); Self->ApplyDamage(S->SoldierId,S->Health-R.Health.Health); }
        S->Health=R.Health.Health; S->MaxHealth=R.Health.MaxHealth;
        auto& Manager=Self->AuthorityState->MassEntitySubsystem->GetMutableEntityManager();
        if (Manager.IsEntityValid(S->Entity)) { auto& Health=Manager.GetFragmentDataChecked<FGuLiMassHealthFragment>(S->Entity); Health.Health=S->Health; Health.bDead=S->Health<=0; }
        ReadModel();
    }
    bool Capture(TArray<uint8>& Bytes) const override
    {
        auto* S=Soldier(); auto* Self=Authority.Get(); if (!S || !Self) return false;
        GuLiHybridMass::FPrivate V; V.Soldier=*S; V.CapturedOwner=Record.SimulationOwner;
        V.CapturedSeconds=Self->AuthorityState->SimulationSeconds;
        if (S->MoveIntent)
        {
            V.Intent=MakeShared<FGuLiSharedMoveIntent>(*S->MoveIntent);
            V.Intent->Goal=MakeShared<FGuLiSharedRouteGoal>();V.Intent->Goal->Target=S->MoveIntent->Goal->Target;
            for (auto I=V.Intent->AssignedPoints.CreateIterator();I;++I) if (I.Key()!=S->SoldierId.Value) I.RemoveCurrent();
            V.Intent->UnassignedMembers.RemoveAll([&](uint32 Id){return Id!=S->SoldierId.Value;});
            if (V.Intent->UnassignedMembers.IsEmpty()) V.Intent->Points.Reset();
        }
        FGuLiHybridStateArchive A; GuLiHybridMass::Private(A,V); return A.Finish(Bytes);
    }
    bool Validate(TConstArrayView<uint8> Bytes) const override
    {
        GuLiHybridMass::FPrivate V;
        return GuLiHybridMass::Decode(Bytes,V) && V.Soldier.HybridKey==Record.Key && V.Soldier.SoldierId.Value==Record.Ordinal;
    }
    bool Restore(TConstArrayView<uint8> Bytes,EGuLiHybridRestoreStage Stage) override
    {
        auto* Self=Authority.Get(); if (!Self) return false; GuLiHybridMass::FPrivate V; if (!GuLiHybridMass::Decode(Bytes,V)) return false;
        if (Stage==EGuLiHybridRestoreStage::Detach) { Release(); return true; }
        if (Stage==EGuLiHybridRestoreStage::Create)
        {
            auto Create=Record; Create.Location=V.Soldier.Location; Create.FacingYawDegrees=V.Soldier.FacingYawDegrees; Create.Health.Health=FMath::Max(1.f,V.Soldier.Health);
            if (!Prepare(Create)) return false; Record.Location=V.Soldier.Location;
            auto* S=Soldier(); const auto Entity=S->Entity; const auto Profiles=S->WeaponProfiles;
            *S=MoveTemp(V.Soldier); S->Entity=Entity; S->WeaponProfiles=Profiles; S->bLocalSimulation=bLocal;
            S->MoveIntent=bRestoringQuery?nullptr:V.Intent;S->RouteGoal=S->MoveIntent?S->MoveIntent->Goal:nullptr;
            const double Offset=Self->AuthorityState->SimulationSeconds-V.CapturedSeconds;
            for (auto& Weapon:S->Weapons) Weapon.Attack.NextFireSeconds+=Offset;
            S->ActiveSkill.ReadyAt+=Offset;S->LastMovementUpdateSimulationSeconds+=Offset;
            S->NextPersonalPathQuerySeconds+=Offset;S->FailureSimulationSeconds+=Offset;
            if (S->Health<=0) S->DeathSimulationSeconds+=Offset;
            S->GroundMechLastPressureSimulationSeconds+=Offset;S->DisplacementSimulationTime+=Offset;S->ExternalLockSimulationTime+=Offset;
            return true;
        }
        if (Stage==EGuLiHybridRestoreStage::Link)
        {
            auto* S=Soldier(); auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(Self->GetWorld());
            const auto* Data=Nav?GuLiCommanderMassPrivate::GetCommanderNavigationData(*Nav):nullptr; if (!S) return false;
            const bool NeedsRoute=!bRestoringQuery && (S->MoveIntent.IsValid() || S->bHasFinalDestination);
            FNavLocation Ground(S->Location),Projected;
            const bool HasGround=Nav && Data
                && GuLiCommanderMassPrivate::ProjectPointToCommanderNavigation(*Nav,*Data,S->Location,FVector(150,150,5000),Projected)
                && FVector::DistSquared2D(S->Location,Projected.Location)<=FMath::Square(150.);
            // An idle newborn can legitimately occupy a retiring foundation's
            // nav cutout. Preserve its pose and invalid nav ref until ordinary
            // planning recovers; query copies never need a local route.
            if (!HasGround && NeedsRoute) return false;
            if (HasGround) Ground=Projected;
            S->LastValidNavLocation=Ground;
            if (NeedsRoute && S->bHasFinalDestination && !GuLiCommanderMassPrivate::ProjectPointToCommanderNavigation(*Nav,*Data,S->FinalDestination.Location,FVector(150,150,5000),S->FinalDestination)) return false;
            if (NeedsRoute && S->MoveIntent && S->RouteGoal)
                S->SharedRoute=Self->AuthorityState->SharedRoutes.Request(S->RouteGoal,Ground,S->bAutomaticAdvance,*Data,Self->AuthorityState->NavigationGeneration);
            S->DirectDock={}; S->ActiveNavigation.Reset(); S->FinalDestinationNavigationGeneration=Self->AuthorityState->NavigationGeneration;
            Self->RegisterCombatLedgerTarget(S->SoldierId);
            RegisteredTarget=Self->MakeSoldierTargetHandle(S->SoldierId);
            return Self->GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>()->BindTarget(Self->MakeSoldierTargetHandle(S->SoldierId),Record.Key);
        }
        if (Stage==EGuLiHybridRestoreStage::Activate)
        {
            Senders.Reset(); Receiver.Reset(static_cast<uint32>(Record.OwnerEpoch),Self->GetWorld()->GetGameState<AGuLiBattleGameState>()->GetMatchEpoch()); LastPublished=0;
            if (const auto* S=Soldier(); bLocal && !bRestoringQuery && S && S->ActiveOrderId
                && S->MoveIntent && !S->bAutomaticAdvance)
                Self->AuthorityState->ImmediateMoveIds.Add(S->SoldierId.Value);
            ReadModel();
        }
        return true;
    }
    bool RestoreQuery(TConstArrayView<uint8> Bytes,EGuLiHybridRestoreStage Stage) override
    { TGuardValue<bool> Query(bRestoringQuery,true);return !bLocal && Restore(Bytes,Stage); }
    void Release() override
    {
        auto* Self=Authority.Get(); auto* S=Soldier(); if (!Self || !S) return;
        Self->GetWorld()->GetSubsystem<UGuLiExternalControlRegistry>()->Unbind(Record.Key,*Self);
        const auto Id=S->SoldierId; const auto Entity=S->Entity;
        if (auto* Ledger=Self->GetWorld()->GetSubsystem<UGuLiDamageLedgerSubsystem>()) Ledger->UnregisterTarget(RegisteredTarget);
        Self->RegisteredCombatLedgerTargets.Remove(RegisteredTarget); RegisteredTarget={};
        auto& State=*Self->AuthorityState; auto& Manager=State.MassEntitySubsystem->GetMutableEntityManager();
        if (Manager.IsEntityValid(Entity)) Manager.DestroyEntity(Entity);
        const int32 Index=State.SoldierIndexById.FindChecked(Id.Value); State.Soldiers.RemoveAtSwap(Index,1,EAllowShrinking::No); State.SoldierIndexById.Remove(Id.Value);
        if (State.Soldiers.IsValidIndex(Index)) State.SoldierIndexById.FindChecked(State.Soldiers[Index].SoldierId.Value)=Index;
        State.MoveReservations.Remove(Id.Value); State.ImmediateMoveIds.Remove(Id.Value); Senders.Reset();
    }
};

class FGuLiHybridMassState final : public IGuLiHybridSharedStateAdapter
{
    TWeakObjectPtr<UGuLiBattleAuthoritySubsystem> Authority;
    bool Scope(const TArray<FGuLiHybridObjectKey>& Objects,TSet<uint32>& Ids) const
    {
        const auto* Self=Authority.Get();const auto* H=Self?Self->GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>():nullptr;
        if (!H) return false;
        for (const auto& K:Objects) {FGuLiHybridObjectState O;if (!H->GetObject(K,O)) return false;Ids.Add(O.Ordinal);}
        return true;
    }
public:
    explicit FGuLiHybridMassState(UGuLiBattleAuthoritySubsystem& V):Authority(&V) {}
    UObject* LifetimeOwner() const override {return Authority.Get();}
    uint32 Version() const override {return GuLiHybridMass::SystemVersion;}
    void Advance(uint64,double Delta) override
    {
        if (auto* Self=Authority.Get())
        {
            TGuardValue<bool> Scope(Self->bAdvancingHybrid,true);Self->Tick(static_cast<float>(Delta));
            if (Self->AuthorityState)
            {
                auto& State=*Self->AuthorityState;
                check(State.DispatchPosePublishes.IsEmpty());
                Swap(State.PendingPosePublishes,State.DispatchPosePublishes);
                for (const auto& Pending:State.DispatchPosePublishes)
                    if (const auto Adapter=Pending.Adapter.Pin())
                        Adapter->PublishCompletedMovement(Pending.Tick,Pending.OwnerEpoch,Pending.ControlEpoch);
                State.DispatchPosePublishes.Reset();
            }
            if (!UGuLiHotspotOptions::Enabled(Self->GetWorld(),EGuLiHotspot::GameplayWorker)||!Self->AuthorityState) return;
            auto* H=Self->GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>();auto& Pipeline=Self->AuthorityState->PosePipeline;
            if (!Pipeline||Pipeline->Pending.empty()||H->HasPendingGameplayCalculation(TEXT("MassPose"))) return;
            auto Batch=Pipeline->Pending.front();const uint64 Bytes=1024+Batch->Inputs.Num()*8192;
            const TWeakObjectPtr<UGuLiBattleAuthoritySubsystem> Weak(Self);
            if (H->SubmitGameplayCalculation(TEXT("MassPose"),Batch,Bytes,Bytes,[Weak,Batch,Bytes]
            {
                auto* A=Weak.Get();if (!A||!A->AuthorityState) return;auto* World=A->GetWorld();auto* Hybrid=World->GetSubsystem<UGuLiHybridWorldSubsystem>();
                TRACE_CPUPROFILER_EVENT_SCOPE(GuLiHybrid_MainMassPoseDelivery);
                for (int32 I=0;I<Batch->Inputs.Num();++I)
                {
                    const auto& V=Batch->Inputs[I];auto& O=Batch->Outputs[I];const auto* Index=A->AuthorityState->SoldierIndexById.Find(V.Value.SoldierId.Value);
                    FGuLiHybridObjectState R;
                    if (!Index||!Hybrid->GetObjectView(V.Key,R)||R.OwnerEpoch!=V.Owner||R.ControlEpoch!=V.Control||!Hybrid->HasSimulation(V.Key)) continue;
                    const auto& S=A->AuthorityState->Soldiers[*Index];
                    if (S.HybridKey!=V.Key||S.DisplacementFrameFloor!=V.Displacement||S.ActiveOrderId!=V.Value.ActiveOrderId||S.IsAlive()!=(V.Value.State!=EGuLiSoldierPoseState::Destroyed)) continue;
                    if (!O.Valid) {Hybrid->Suspend(TEXT("Hybrid Mass pose exceeds quantization bounds"));continue;}
                    for (auto& Send:O.Sends) Send.Admitted=Hybrid->PublishMotionTo(V.Key,Send.Peer,Send.Prepared.Block.Data,V.At);
                    GuLiHybridMass::Present(*World,O.Chunk);
                }
                auto Ack=std::make_shared<GuLiHybridMass::FPoseConfirmation>();Ack->Batch=Batch;
                if (!Hybrid->ConfirmGameplayCalculation(Ack,Bytes)) Hybrid->Suspend(TEXT("Pose send confirmation capacity exhausted"));
            }))
            {
                for (const auto& V:Batch->Inputs) Pipeline->PendingBytes-=2048+V.Peers.Num()*2048;
                Pipeline->Pending.pop_front();
            }
        }
    }
    bool Capture(const TArray<FGuLiHybridObjectKey>& Objects,TArray<uint8>& Bytes) const override
    {
        using namespace GuLiHybridMass;
        auto* Self=Authority.Get();TSet<uint32> Ids;if (!Self || !Self->AuthorityState || !Scope(Objects,Ids)) return false;
        const auto& S=*Self->AuthorityState;FSystem V;V.Seconds=S.SimulationSeconds;
        // Local frame accumulators and global allocators are not ownership state.
        for (const auto& Job:S.MovePlanningJobs)
        {
            if (!Job) continue;FJob J;
            for (const auto& Cohort:Job->FullSelection.Cohorts) for (const auto Id:Cohort.MemberIds) if (Ids.Contains(Id.Value))
            {
                J.Members.AddUnique(Id.Value);
                if (Job->Stage!=EMovePlanningStage::Completed && !Job->FinishedIds.Contains(Id.Value))
                {J.Remaining.Add(Id.Value);J.Versions.Add(Job->FrozenTaskGenerations.FindRef(Id.Value));}
            }
            if (J.Members.IsEmpty()) continue;
            J.Handle=Job->Handle.Value;J.SourceCommand=Job->SourceCommandId;J.Batch=Job->SharedBatchOrderId;
            J.Team=Job->Team;J.Target=Job->Request.Target;J.bAutomatic=Job->bAutomatic;
            if (Job->PlayerState.IsValid()) J.Owner=Job->PlayerState->GetPlayerGuid();
            if (Job->MoveIntent)
            {
                auto Copy=MakeShared<FGuLiSharedMoveIntent>(*Job->MoveIntent);
                Copy->Goal=MakeShared<FGuLiSharedRouteGoal>();Copy->Goal->Target=Job->MoveIntent->Goal->Target;
                for (auto I=Copy->AssignedPoints.CreateIterator();I;++I) if (!Ids.Contains(I.Key())) I.RemoveCurrent();
                Copy->UnassignedMembers.RemoveAll([&](uint32 Id){return !Ids.Contains(Id);});
                if (Copy->UnassignedMembers.IsEmpty()) Copy->Points.Reset();
                V.Intents.Add(Copy);J.Intent=V.Intents.Num();
            }
            for (auto Id:Job->Progress.Committed) if (Ids.Contains(Id.Value)) J.Committed.Add(Id.Value);
            for (auto Id:Job->Progress.Failed) if (Ids.Contains(Id.Value)) J.Failed.Add(Id.Value);
            V.Jobs.Add(MoveTemp(J));
        }
        FGuLiHybridStateArchive A(16*1024*1024);System(A,V);return A.Finish(Bytes);
    }
    bool Validate(const TArray<FGuLiHybridSharedStatePiece>& Pieces,const TArray<FGuLiHybridObjectKey>& Objects) const override
    {
        using namespace GuLiHybridMass;TSet<FGuLiHybridObjectKey> Claims;TSet<uint32> Ids;
        if (!Scope(Objects,Ids)) return false;
        for (const auto& P:Pieces)
        {
            FSystem V;if (P.Name!=SystemName || P.Version!=Version() || !Decode(P.Bytes,V)) return false;
            for (const auto& K:P.Objects) {if (Claims.Contains(K)) return false;Claims.Add(K);}
            for (const auto& J:V.Jobs) for (uint32 Id:J.Members) if (!Ids.Contains(Id)) return false;
        }
        for (const auto& K:Objects) if (!Claims.Contains(K)) return false;return true;
    }
    bool Restore(const TArray<FGuLiHybridSharedStatePiece>& Pieces,const TArray<FGuLiHybridObjectKey>& Objects,EGuLiHybridRestoreStage Stage) override
    {
        using namespace GuLiHybridMass;auto* Self=Authority.Get();TSet<uint32> Ids;
        if (!Self || !Self->AuthorityState || !Scope(Objects,Ids)) return false;auto& S=*Self->AuthorityState;
        if (Stage==EGuLiHybridRestoreStage::Detach)
        {
            for (uint32 Id:Ids)
            {S.Steering.Remove(Id);S.SteeringQueued.Remove(Id);S.RecoveryWork.Remove(Id);S.MoveReservations.Remove(Id);S.ActiveEndpoints.Remove(Id);}
            for (auto& Job:S.MovePlanningJobs) if (Job)
            {
                for (auto& C:Job->FullSelection.Cohorts) C.MemberIds.RemoveAll([&](auto Id){return Ids.Contains(Id.Value);});
                Job->FullSelection.Cohorts.RemoveAll([](const auto& C){return C.MemberIds.IsEmpty();});
                for (uint32 Id:Ids) {Job->FinishedIds.Add(Id);Job->FrozenTaskGenerations.Remove(Id);}
                Job->Progress.Committed.RemoveAll([&](auto Id){return Ids.Contains(Id.Value);});
                Job->Progress.Failed.RemoveAll([&](auto Id){return Ids.Contains(Id.Value);});
            }
            S.MovePlanningJobs.RemoveAll([](const auto& J){return !J || J->FullSelection.Cohorts.IsEmpty();});
            return true;
        }
        if (Stage==EGuLiHybridRestoreStage::Link)
        {
            for (const auto& P:Pieces)
            {
                FSystem V;if (!Decode(P.Bytes,V)) return false;
                for (const auto& J:V.Jobs)
                {
                    // Navigation handles belong to this process. The command and task
                    // identities travel; a foreign handle must never merge local jobs.
                    auto* Existing=static_cast<TUniquePtr<FMovePlanningJob>*>(nullptr);
                    {
                        if (!S.NextPlanId) ++S.NextPlanId;
                        auto New=MakeUnique<FMovePlanningJob>();New->Handle={S.NextPlanId++,Self->GetWorld()->GetGameState<AGuLiBattleGameState>()->GetMatchEpoch()};
                        New->Progress.Handle=New->Handle;New->bAutomatic=J.bAutomatic;New->Team=J.Team;New->Request.Target=J.Target;
                        New->Request.ClientCommandId=J.SourceCommand;New->SourceCommandId=J.SourceCommand;New->SharedBatchOrderId=J.Batch;
                        New->MoveIntent=J.Intent?V.Intents[J.Intent-1]:nullptr;New->Reservations=&S.MoveReservations;
                        New->PlayerState=Player(*Self->GetWorld(),J.Owner);New->AuthorityEpoch=New->Handle.Epoch;New->NavigationGeneration=S.NavigationGeneration;
                        New->MaximumMemberRadiusCentimeters=Self->MemberAgentRadiusCentimeters;New->PlanningStartedAt=New->LastProgressAt=FPlatformTime::Seconds();
                        S.MovePlanningJobs.Add(MoveTemp(New));Existing=&S.MovePlanningJobs.Last();
                    }
                    auto& Job=**Existing;bool Remaining=false;
                    for (uint32 Id:J.Members)
                    {
                        if (Job.FullSelection.Cohorts.IsEmpty() || Job.FullSelection.Cohorts.Last().MemberIds.Num()==25)
                            Job.FullSelection.Cohorts.AddDefaulted_GetRef().CohortId=FGuLiControlCohortId(AllocateNonZero(S.NextControlCohortId));
                        Job.FullSelection.Cohorts.Last().MemberIds.Add(FGuLiSoldierId(Id));
                        const int32 Index=J.Remaining.IndexOfByKey(Id);
                        if (Index!=INDEX_NONE) {Remaining=true;Job.FrozenTaskGenerations.Add(Id,J.Versions[Index]);Job.FinishedIds.Remove(Id);}
                        else Job.FinishedIds.Add(Id);
                    }
                    for (uint32 Id:J.Committed) Job.Progress.Committed.AddUnique(FGuLiSoldierId(Id));
                    for (uint32 Id:J.Failed) Job.Progress.Failed.AddUnique(FGuLiSoldierId(Id));
                    Job.Progress.BatchOrderId=J.Batch;
                    if (Remaining) {Job.Progress.bComplete=false;Job.Stage=EMovePlanningStage::PrepareBatch;}
                    else if (Job.FullSelection.Cohorts.Num()==1 && Job.FullSelection.Cohorts[0].MemberIds.Num()==J.Members.Num())
                    {Job.Progress.bComplete=true;Job.Stage=EMovePlanningStage::Completed;}
                }
            }
            for (const auto& Unit:S.Soldiers) if (Ids.Contains(Unit.SoldierId.Value)) S.MoveReservations.Update(Unit);
        }
        return true;
    }
};
void RegisterGuLiHybridMassAdapters(UGuLiBattleAuthoritySubsystem& Self)
{
    auto* Hybrid=Self.GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>();
    if (!Hybrid || !Hybrid->RegisterSharedStateSystem(GuLiHybridMass::SystemName,MakeShared<FGuLiHybridMassState>(Self)))
        if (Hybrid) Hybrid->Suspend(TEXT("Mass production adapter registration failed"));
}
namespace
{
const FGuLiHybridAdapterRegistration MassFamily({TEXT("Mass"),EGuLiHybridObjectKind::Soldier,GuLiHybridMass::ObjectVersion,
    [](UGuLiHybridActorWorldSubsystem& Owner,const FGuLiHybridObjectState&)->TSharedPtr<IGuLiHybridSimulationAdapter>
    {auto* Authority=Owner.GetWorld()->GetSubsystem<UGuLiBattleAuthoritySubsystem>();if (!Authority) return {};return MakeShared<FGuLiHybridMassAdapter>(*Authority);},nullptr,GuLiHybridTargets::Mass,EGuLiHybridOwnershipPolicy::CommanderActivity});
}
