#include "Network/GuLiAwaitHybridTransaction.h"
#include "Engine/Engine.h"
#include "Engine/World.h"

UGuLiAwaitHybridTransaction* UGuLiAwaitHybridTransaction::AwaitHybridTransaction(UObject* Context,FGuid Request)
{
    auto* Action=NewObject<UGuLiAwaitHybridTransaction>();Action->Event=Request;
    auto* World=GEngine?GEngine->GetWorldFromContextObject(Context,EGetWorldErrorMode::ReturnNull):nullptr;
    if (World && World->IsGameWorld())
    {Action->AwaitWorld=World;Action->Hybrid=World->GetSubsystem<UGuLiHybridWorldSubsystem>();Action->RegisterWithGameInstance(Context);}
    return Action;
}
void UGuLiAwaitHybridTransaction::Activate()
{
    if (Finished || Activated) return;Activated=true;auto* H=Hybrid.Get();
    if (!H || !Event.IsValid()) {Abandon(TEXT("A valid World and submitted request identity are required"));return;}
    H->OnTransactionCompleted.AddUniqueDynamic(this,&ThisClass::Receive);
    H->OnOwnershipStateApplied.AddUObject(this,&ThisClass::Rebind);
    WorldCleanup=FWorldDelegates::OnWorldCleanup.AddUObject(this,&ThisClass::WorldEnded);
    FGuLiHybridTransactionResult R;if (H->FindTransactionResult(Event,R)) Receive(R);
}
void UGuLiAwaitHybridTransaction::Receive(const FGuLiHybridTransactionResult& R)
{
    if (Finished || R.Event!=Event || R.Status==EGuLiHybridTransactionStatus::Deferred || R.Status==EGuLiHybridTransactionStatus::Paused) return;
    Finished=true;Detach();
    if (R.IsCommitted()) Committed.Broadcast(R);else Rejected.Broadcast(R);
    SetReadyToDestroy();
}
void UGuLiAwaitHybridTransaction::Rebind()
{
    if (Finished) return;auto* H=Hybrid.Get();if (!H) {Abandon(TEXT("The transaction World was released"));return;}
    FGuLiHybridTransactionResult R;
    if (H->FindTransactionResult(Event,R)) {Receive(R);if (Finished) return;}
    // Pending execution may have moved to another object owner. Absence from this
    // client's execution queue is not a rejection; the World result subscription
    // remains valid until a final receipt, explicit UI cancellation or World cleanup.
}
void UGuLiAwaitHybridTransaction::WorldEnded(UWorld* World,bool,bool)
{if (AwaitWorld.Get()==World) Abandon(TEXT("The transaction World has ended"));}
void UGuLiAwaitHybridTransaction::Abandon(const FString& Reason)
{
    if (Finished) return;Finished=true;Detach();Abandoned.Broadcast(Event,Reason);SetReadyToDestroy();
}
void UGuLiAwaitHybridTransaction::Detach()
{
    if (auto* H=Hybrid.Get()) {H->OnTransactionCompleted.RemoveDynamic(this,&ThisClass::Receive);H->OnOwnershipStateApplied.RemoveAll(this);}
    FWorldDelegates::OnWorldCleanup.Remove(WorldCleanup);WorldCleanup.Reset();
}
void UGuLiAwaitHybridTransaction::StopWaiting() {Abandon(TEXT("UI subscription removed"));}
void UGuLiAwaitHybridTransaction::BeginDestroy() {Finished=true;Detach();Super::BeginDestroy();}
