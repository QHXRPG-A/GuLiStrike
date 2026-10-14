#include "Gameplay/CommanderSkills/GuLiCommanderSkillComponent.h"
#include "Battle/Framework/GuLiBattlePlayerState.h"
#include "Battle/Framework/GuLiBattleGameState.h"
#include "Commander/Framework/GuLiCommanderPlayerController.h"
#include "Network/GuLiHybridWorldSubsystem.h"
#include "Network/GuLiNetworkSettings.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
TArray<FGuLiActiveSkillRuntime> UGuLiCommanderSkillComponent::GetGlobalSkills() const
{
    auto Values=GlobalRuntime;
    if (UGuLiNetworkSettings::IsHybrid(GetWorld())&&Catalog) for (auto& V:Values)
        if (const auto* D=Catalog->FindSkill(V.SkillId);D&&D->ExecutorClass)
            D->ExecutorClass->GetDefaultObject<UGuLiCommanderSkillExecutor>()->ReadHybridRuntime(*GetWorld(),PlayerState().GetTeam(),V.SkillId,V);
    return Values;
}
FGuLiActiveSkillReply UGuLiCommanderSkillComponent::RequestHybridGlobal(const FGuLiActiveSkillRequest& R)
{
    FGuLiActiveSkillReply Reply;Reply.RequestId=R.RequestId;Reply.MatchEpoch=R.MatchEpoch;
    auto* PC=Commander();auto* H=GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>();
    if (!H||!PC||!Catalog||!PC->IsLocalController()||!PC->CanIssueCommanderOrders()||!R.RequestId.IsValid()||R.GroundPoint.ContainsNaN()
        ||R.Command>EGuLiActiveSkillCommand::Cancel||!SynchronizeEpoch()||R.MatchEpoch!=RuntimeEpoch||!Catalog->GlobalSkills.Contains(R.GlobalSkillId))
    {Reply.Error=TEXT("Invalid global ability controller or epoch");return Reply;}
    if (const auto* Pending=HybridGlobalRequests.Find(R.RequestId))
    {if (Pending->Request.HasSameContent(R)) return Pending->Reply;Reply.Error=TEXT("Conflicting global ability identity");return Reply;}
    if (const auto* Previous=Receipts.Find(R.RequestId))
    {if (Previous->Request.HasSameContent(R)) return Previous->Reply;Reply.Error=TEXT("Conflicting global ability identity");return Reply;}
    const auto* D=Catalog->FindSkill(R.GlobalSkillId);const auto* E=D&&D->ExecutorClass?D->ExecutorClass->GetDefaultObject<UGuLiCommanderSkillExecutor>():nullptr;
    FGuLiActiveSkillRuntime Runtime;
    if (!E||!E->UsesHybridAsyncGlobal()||!E->ReadHybridRuntime(*GetWorld(),PlayerState().GetTeam(),R.GlobalSkillId,Runtime)||HybridGlobalRequests.Num()>=32)
    {Reply.Error=TEXT("Global ability has no ready asynchronous capability");return Reply;}
    FVector Point=FVector::ZeroVector;
    if (R.Command!=EGuLiActiveSkillCommand::Cancel && D->TargetMode!=EGuLiActiveSkillTargetMode::Self
        && (!R.bHasGroundPoint||!E->ResolveGroundInput(*GetWorld(),R.GroundPoint,Point)))
    {Reply.Code=EGuLiActiveSkillResultCode::InvalidGround;return Reply;}
    if (D->RangeCentimeters>0&&PC->GetPawn()&&R.Command!=EGuLiActiveSkillCommand::Cancel
        &&FVector::DistSquared(PC->GetPawn()->GetActorLocation(),Point)>FMath::Square(D->RangeCentimeters))
    {Reply.Code=EGuLiActiveSkillResultCode::OutOfRange;return Reply;}
    FGuLiActiveSkillExecutionContext C;C.Commander=&PlayerState();C.ExecutionWorld=GetWorld();C.ExecutionTeam=PlayerState().GetTeam();C.ControllerIdentity=PlayerState().GetPlayerGuid();
    C.Scope=EGuLiActiveSkillScope::Global;C.SkillId=R.GlobalSkillId;C.Level=Runtime.Level;C.RequestId=R.RequestId;C.CastId=R.CastId;C.Command=R.Command;C.GroundPoint=Point;
    Reply.Global=E->Execute(C,D->Configuration);Reply.Error=Reply.Global.Error;
    if (!Reply.Global.bPendingCommit||Reply.Global.EffectId!=R.RequestId) {Reply.Code=EGuLiActiveSkillResultCode::ExecutionFailed;return Reply;}
    Reply.Code=EGuLiActiveSkillResultCode::PendingCommit;HybridGlobalRequests.Add(R.RequestId,{R,Reply});
    H->OnCommandCompleted.RemoveAll(this);H->OnCommandCompleted.AddUObject(this,&ThisClass::HandleHybridSkillResult);
    return Reply;
}
void UGuLiCommanderSkillComponent::CompleteHybridGlobal(const FGuLiHybridTransactionResult& R)
{
    if (R.Status==EGuLiHybridTransactionStatus::Deferred||R.Status==EGuLiHybridTransactionStatus::Paused) return;
    const auto* P=HybridGlobalRequests.Find(R.Event);if (!P) return;auto Receipt=*P;
    Receipt.Reply.Global.bPendingCommit=false;Receipt.Reply.Global.bSucceeded=R.IsCommitted();
    Receipt.Reply.Code=R.IsCommitted()?EGuLiActiveSkillResultCode::Succeeded:EGuLiActiveSkillResultCode::ExecutionFailed;
    if (!R.IsCommitted()) Receipt.Reply.Error=TEXT("Global ability request was finally rejected by the ledger");
    if (const auto* B=GetWorld()->GetGameState<AGuLiBattleGameState>()) Receipt.Reply.MatchEpoch=B->GetMatchEpoch();
    HybridGlobalRequests.Remove(R.Event);Receipts.Add(R.Event,Receipt);ClientReceiveReply(Receipt.Reply);
}
void UGuLiCommanderSkillComponent::RejectHybridGlobalCommand(const FGuLiHybridCommandResult& R)
{
    if (!HybridGlobalRequests.Contains(R.Event)||!R.Members.ContainsByPredicate([](const auto& M){return !M.bAccepted;})) return;
    FGuLiHybridTransactionResult Rejected;Rejected.Event=R.Event;Rejected.Status=EGuLiHybridTransactionStatus::BadRequest;CompleteHybridGlobal(Rejected);
}
void UGuLiCommanderSkillComponent::RebindHybridGlobals()
{
    auto* H=GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>();if (!H) return;
    TArray<FGuid> Events;HybridGlobalRequests.GetKeys(Events);
    for (auto Event:Events)
    {
        FGuLiHybridTransactionResult R;if (H->FindTransactionResult(Event,R)) {CompleteHybridGlobal(R);continue;}
        FGuLiHybridCommandResult C;if (H->FindCommandResult(Event,C)) RejectHybridGlobalCommand(C);
        // Keep listening across ownership changes; only the final result completes the request.
    }
    GlobalRuntime=GetGlobalSkills();
}
