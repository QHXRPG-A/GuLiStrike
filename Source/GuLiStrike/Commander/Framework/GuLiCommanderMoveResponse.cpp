#include "Commander/Framework/GuLiCommanderNetSyncComponent.h"
#include "Commander/Mass/GuLiBattleAuthoritySubsystem.h"
#include "Commander/Network/GuLiMoveLatency.h"
#include "Commander/Orders/GuLiUnitTaskSubsystem.h"
#include "Battle/Framework/GuLiBattleGameState.h"
#include "Gameplay/Units/GuLiEngineeringTravelComponent.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

void UGuLiCommanderNetSyncComponent::RecordMoveInput(uint32 CommandId, double InputSeconds)
{
    MoveInputCommand=CommandId;
    MoveInputAt=InputSeconds;
}

void UGuLiCommanderNetSyncComponent::BeginMoveResponse(const FGuLiUnitTaskCommand& Command)
{
    if (Command.Kind!=EGuLiUnitTaskKind::Move || Command.Disposition==EGuLiTaskDisposition::Stop
        || Command.CommandId==MoveResponseCommand) return;
    MoveResponseCommand=Command.CommandId;
    MoveResponseInputAt=MoveInputCommand==Command.CommandId ? MoveInputAt : FPlatformTime::Seconds();
    const auto* Battle=GetBattleGameState();
    MoveResponseEpoch=Battle ? Battle->GetMatchEpoch() : 0;
    MoveResponsePending.Reset();
    MoveResponseTotal=MoveResponseStarted=MoveResponseFailed=0;
    MoveResponseLastStartAt=MoveResponseInputAt;
    bMoveResponseFrozen=bMoveResponseRemote=false;
    MoveResponseTextAt=0;
    RefreshMoveResponseText(FPlatformTime::Seconds());
}

void UGuLiCommanderNetSyncComponent::FreezeMoveResponse(const FGuLiUnitTaskCommand& Command,
    TConstArrayView<FGuLiHybridObjectState> Objects)
{
    const bool Move=Command.Kind==EGuLiUnitTaskKind::Move && Command.Disposition!=EGuLiTaskDisposition::Stop;
    if (!Move)
    {
        // Context picking may turn a right click into mining or another work order.
        if (MoveResponseCommand==Command.CommandId)
        { MoveResponseCommand=0; MoveResponsePending.Reset(); MoveResponseText.Reset(); return; }
        if (Command.Disposition!=EGuLiTaskDisposition::Append)
            for (const auto& O:Objects) if (MoveResponsePending.Remove(O.Key)) ++MoveResponseFailed;
        MoveResponseTextAt=0;
        return;
    }
    if (MoveResponseCommand!=Command.CommandId || bMoveResponseFrozen) return;
    auto* Hybrid=GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>();
    const auto* Authority=GetWorld()->GetSubsystem<UGuLiBattleAuthoritySubsystem>();
    MoveResponsePending.Reserve(Objects.Num());
    for (const auto& O:Objects)
    {
        auto& Member=MoveResponsePending.Add(O.Key);
        Member.Unit.Id=O.Ordinal; Member.Unit.Generation=O.Key.LifeGeneration;
        Member.Unit.bActor=O.Key.Kind!=EGuLiHybridObjectKind::Soldier;
        Member.Start=O.Location;
        FGuLiSoldierId MassId{O.Ordinal};
        if (Member.Unit.bActor)
        {
            const auto* Actor=Hybrid ? Hybrid->FindBoundActor(O.Key) : nullptr;
            const auto* Travel=Actor ? Actor->FindComponentByClass<UGuLiEngineeringTravelComponent>() : nullptr;
            MassId=FGuLiSoldierId(Travel ? uint32(Travel->GetMassEntityId()) : 0);
            if (Actor) Member.Start=Actor->GetActorLocation();
        }
        GuLiMoveLatency::FMovementSample Sample;
        if (Authority && Authority->TryGetMoveResponseSample(MassId,O.Key,Sample))
        { Member.Start=Sample.Location; Member.PreviousOrder=Sample.Order; }
    }
    MoveResponseTotal=MoveResponsePending.Num();
    bMoveResponseFrozen=true;
    MoveResponseTextAt=0;
    RefreshMoveResponseText(FPlatformTime::Seconds());
}

void UGuLiCommanderNetSyncComponent::RejectMoveResponse(uint32 CommandId)
{
    if (!CommandId || CommandId!=MoveResponseCommand) return;
    MoveResponseFailed+=MoveResponsePending.Num();
    MoveResponsePending.Reset(); bMoveResponseFrozen=true;
    MoveResponseTextAt=0;
    RefreshMoveResponseText(FPlatformTime::Seconds());
}

void UGuLiCommanderNetSyncComponent::TickMoveResponse()
{
    if (!MoveResponseCommand) return;
    const double Now=FPlatformTime::Seconds();
    const auto* Battle=GetBattleGameState();
    if (!Battle || Battle->GetMatchEpoch()!=MoveResponseEpoch || !IsSoldierStreamReady())
    { MoveResponseCommand=0; MoveResponsePending.Reset(); MoveResponseText.Reset(); return; }
    if (!MoveResponsePending.IsEmpty())
    {
        TRACE_CPUPROFILER_EVENT_SCOPE(GuLiCommander_MoveResponse);
        auto* Hybrid=GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>();
        const auto* Authority=GetWorld()->GetSubsystem<UGuLiBattleAuthoritySubsystem>();
        const auto* Tasks=GetWorld()->GetSubsystem<UGuLiUnitTaskSubsystem>();
        if (Hybrid && Authority) for (auto It=MoveResponsePending.CreateIterator();It;++It)
        {
            const auto& Key=It.Key(); auto& Member=It.Value();
            FGuid Owner; uint8 Life=0;
            if (!Hybrid->GetObjectLifecycle(Key,Owner,Life) || Life!=1 || !Hybrid->HasControl(Key))
            { ++MoveResponseFailed; It.RemoveCurrent(); continue; }
            const auto* Actor=Member.Unit.bActor ? Hybrid->FindBoundActor(Key) : nullptr;
            const auto* Travel=Actor ? Actor->FindComponentByClass<UGuLiEngineeringTravelComponent>() : nullptr;
            const FGuLiSoldierId Id(Member.Unit.bActor ? (Travel ? uint32(Travel->GetMassEntityId()) : 0) : Member.Unit.Id);
            GuLiMoveLatency::FMovementSample Sample;
            const bool HasSample=Authority->TryGetMoveResponseSample(Id,Key,Sample);
            const bool Local=Hybrid->HasSimulation(Key);
            double StartedAt=0;
            if (Local)
            {
                const auto* State=Tasks ? Tasks->FindState(Member.Unit) : nullptr;
                const bool MatchingTask=State && State->Active && !State->Active->bAutomatic
                    && State->Active->Command.CommandId==MoveResponseCommand;
                // Only a newly committed command can contribute. Existing movement,
                // planning and passive yielding must not make an idle member pass.
                if (HasSample && Sample.bLocal && Sample.AcceptedAt>=MoveResponseInputAt
                    && Sample.FirstMotionAt>=MoveResponseInputAt
                    && Sample.Command==MoveResponseCommand)
                    StartedAt=Sample.FirstMotionAt;
                else if ((MatchingTask && (State->Active->Status==EGuLiTaskStatus::Failed
                    || State->Active->Status==EGuLiTaskStatus::Completed || State->Active->Status==EGuLiTaskStatus::Stopped))
                    || (HasSample && Sample.Command==MoveResponseCommand && !Sample.Order && Member.bAccepted))
                { ++MoveResponseFailed; It.RemoveCurrent(); continue; }
            }
            else if (Member.bAccepted)
            {
                // Remote owners' clocks cannot be subtracted from this machine's input
                // clock. Observe their already-received motion locally, without an RPC.
                // Arm after acceptance so earlier work cannot supply the baseline delta.
                if (!HasSample && !Actor) continue; // A handoff can temporarily detach a host.
                const FVector Position=Member.Unit.bActor && Actor ? Actor->GetActorLocation() : Sample.Location;
                if (!Member.bRemoteArmed)
                { Member.Start=Position; Member.bRemoteArmed=true; continue; }
                if (FVector::DistSquared2D(Position,Member.Start)>1.
                    && (Member.Unit.bActor || (Sample.Order && Sample.Order!=Member.PreviousOrder)))
                { StartedAt=Now; bMoveResponseRemote=true; }
            }
            if (StartedAt>0)
            {
                ++MoveResponseStarted;
                MoveResponseLastStartAt=FMath::Max(MoveResponseLastStartAt,StartedAt);
                It.RemoveCurrent();
            }
        }
        if (MoveResponsePending.IsEmpty()) MoveResponseTextAt=0;
    }
    RefreshMoveResponseText(Now);
}

void UGuLiCommanderNetSyncComponent::RefreshMoveResponseText(double Now)
{
    // Formatting is bounded to 10 Hz while waiting; completion refreshes immediately.
    if (MoveResponseTextAt>0 && ((MoveResponsePending.IsEmpty() && bMoveResponseFrozen)
        || Now-MoveResponseTextAt<.1)) return;
    MoveResponseTextAt=Now;
    if (!bMoveResponseFrozen)
        MoveResponseText=TEXT("行进响应：正在接纳指令…");
    else if (!MoveResponseTotal)
        MoveResponseText=TEXT("行进响应：指令未接受");
    else if (!MoveResponsePending.IsEmpty())
        MoveResponseText=FString::Printf(TEXT("行进响应：%.0f ms | 已起步 %d/%d | 未执行 %d"),
            (Now-MoveResponseInputAt)*1000.,MoveResponseStarted,MoveResponseTotal,MoveResponseFailed);
    else if (MoveResponseFailed)
        MoveResponseText=FString::Printf(TEXT("行进响应：未全员起步 | 已起步 %d/%d | 未执行 %d"),
            MoveResponseStarted,MoveResponseTotal,MoveResponseFailed);
    else
        MoveResponseText=FString::Printf(TEXT("全员起步：%.1f ms | %d/%d%s"),
            (MoveResponseLastStartAt-MoveResponseInputAt)*1000.,MoveResponseStarted,MoveResponseTotal,
            bMoveResponseRemote ? TEXT("（含远端本机观测）") : TEXT(""));
}
