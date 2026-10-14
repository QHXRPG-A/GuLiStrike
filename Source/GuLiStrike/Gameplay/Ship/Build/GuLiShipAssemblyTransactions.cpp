#include "Gameplay/Ship/Build/GuLiShipAssemblyComponent.h"
#include "Gameplay/Ship/GuLiStrikeShip.h"
#include "Battle/Framework/GuLiBattleGameState.h"
#include "Battle/Framework/GuLiBattlePlayerState.h"
#include "Network/GuLiHybridActorComponent.h"
#include "Network/GuLiHybridWorldSubsystem.h"
#include "Engine/World.h"

FGuid UGuLiShipAssemblyComponent::RequestHybridChoice(FGuid RequestId,FName NodeId,int64 MatchEpoch,int64 ExpectedBuildRevision)
{
    const auto* Binding=UGuLiHybridActorComponent::Find(GetOwner());
    auto* Hybrid=GetWorld()?GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>():nullptr;
    if (!RequestId.IsValid() || NodeId.IsNone() || !Binding || !Binding->CanControl() || !Binding->CanSimulate() || !Hybrid) return {};
    if (const auto* Previous=ChoiceReceipts.Find(RequestId))
        return Previous->Node==NodeId && Previous->Epoch==MatchEpoch && Previous->Revision==ExpectedBuildRevision
            && Previous->Requester==Binding->GetState().Controller?RequestId:FGuid{};
    const auto* Battle=GetWorld()->GetGameState<AGuLiBattleGameState>();
    if (ChoiceReceipts.Num()>=1024 || !Catalog || bPrepared || bCommitting || bRestoringPrivate
        || !IsAvailableForChoice() || !Hybrid->CanPublishFacts() || !Battle || MatchEpoch!=Battle->GetMatchEpoch()
        || MatchEpoch!=AppliedBuild.MatchEpoch || ExpectedBuildRevision!=AppliedBuild.BuildRevision) return {};
    FGuLiShipCompiledBuildRules Compiled;FString Error;
    if (!Catalog->Compile(Compiled) || !Catalog->CanChoose(Compiled,AppliedBuild,NodeId,Error)) return {};
    auto Candidate=AppliedBuild;Candidate.ChosenNodeIds.Add(NodeId);++Candidate.BuildRevision;
    if (!PrepareBuild(Candidate,Error)) return {};
    DiscardPreparedBuild();
    FGuLiHybridUpgradeRequest Request;Request.Source=Binding->GetObjectKey();Request.Event=RequestId;
    Request.Rule=UpgradeRule;Request.Node=NodeId;Request.ExpectedSelectionRevision=ExpectedBuildRevision-1;
    FChoiceReceipt Receipt;Receipt.Node=NodeId;Receipt.Requester=Binding->GetState().Controller;
    Receipt.Epoch=MatchEpoch;Receipt.Revision=ExpectedBuildRevision;Receipt.Result.RequestId=RequestId;
    Receipt.Result.BuildRevision=ExpectedBuildRevision;
    ChoiceReceipts.Add(RequestId,MoveTemp(Receipt));
    if (Hybrid->SubmitUpgrade(Request)!=RequestId) {ChoiceReceipts.Remove(RequestId);return {};}
    return RequestId;
}
bool UGuLiShipAssemblyComponent::FindHybridChoiceResult(FGuid RequestId,FGuLiShipChoiceResult& Result) const
{
    const auto* Receipt=ChoiceReceipts.Find(RequestId);if (!Receipt || !Receipt->Resolved) return false;
    Result=Receipt->Result;return true;
}
void UGuLiShipAssemblyComponent::HandleHybridTransaction(const FGuLiHybridTransactionResult& Result)
{
    auto* Receipt=ChoiceReceipts.Find(Result.Event);
    if (!Receipt || Receipt->Resolved || Result.Status==EGuLiHybridTransactionStatus::Deferred
        || Result.Status==EGuLiHybridTransactionStatus::Paused || Result.LedgerRevision<=0) return;
    if (Result.IsCommitted())
    {
        const auto* Binding=UGuLiHybridActorComponent::Find(GetOwner());
        auto* Hybrid=GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>();FGuLiHybridObjectState Object;
        if (!Binding || !Hybrid || Result.Target!=Binding->GetObjectKey() || !Hybrid->GetObject(Result.Target,Object))
        {if (Hybrid) Hybrid->Suspend(TEXT("Committed equipment choice has no matching object"));return;}
        const auto* Selection=Object.Upgrades.Find(UpgradeDomain);
        if (!Selection || Selection->Revision<uint64(Receipt->Revision) || !Selection->Chosen.IsValidIndex(Receipt->Revision-1)
            || Selection->Chosen[Receipt->Revision-1]!=Receipt->Node || AppliedBuild.BuildRevision<int64(Selection->Revision)+1)
        {Hybrid->Suspend(TEXT("Committed equipment choice was not applied from the ledger"));return;}
    }
    Receipt->Resolved=true;Receipt->Result.bCommitted=Result.IsCommitted();Receipt->Result.bBusinessEffects=Result.bBusinessEffects;
    Receipt->Result.BuildRevision=AppliedBuild.BuildRevision;
    Receipt->Result.Reason=Result.IsCommitted()?FString{}:FString::Printf(TEXT("Coordinator rejected choice (%u)."),uint8(Result.Status));
    AdvanceHybridChoices();
}
void UGuLiShipAssemblyComponent::AdvanceHybridChoices()
{
    if (bRestoringPrivate || bPrepared || bCommitting || bAdvancingChoices) return;
    TGuardValue<bool> Advancing(bAdvancingChoices,true);
    auto* Hybrid=GetWorld()?GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>():nullptr;
    if (!Hybrid) return;
    TArray<FGuid> Events;ChoiceReceipts.GenerateKeyArray(Events);
    for (const auto Event:Events)
    {
        auto* Receipt=ChoiceReceipts.Find(Event);
        if (!Receipt->Resolved)
        {
            FGuLiHybridTransactionResult Result;
            if (Hybrid->FindTransactionResult(Event,Result)) HandleHybridTransaction(Result);
            Receipt=ChoiceReceipts.Find(Event);
        }
        if (!Receipt || !Receipt->Resolved || Receipt->Notified) continue;
        Receipt->Notified=true;
        const auto* Binding=UGuLiHybridActorComponent::Find(GetOwner());
        if (!Binding || !Binding->CanControl() || Binding->GetState().Controller!=Receipt->Requester) continue;
        const auto Result=Receipt->Result;
        if (auto* Player=Ship().GetPlayerState<AGuLiBattlePlayerState>()) Player->GetShipBuild()->NotifyHybridChoiceResolved(*this,Result);
    }
}
