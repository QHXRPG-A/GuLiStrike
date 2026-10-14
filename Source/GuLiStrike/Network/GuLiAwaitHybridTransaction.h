#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintAsyncActionBase.h"
#include "Network/GuLiHybridWorldSubsystem.h"
#include "GuLiAwaitHybridTransaction.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FGuLiHybridAwaitResult,const FGuLiHybridTransactionResult&,Result);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FGuLiHybridAwaitAbandoned,FGuid,Request,const FString&,Reason);

/** UI subscription only. A waiter never submits, retries or owns a transaction. */
UCLASS(meta=(ExposedAsyncProxy=AsyncAction))
class GULISTRIKE_API UGuLiAwaitHybridTransaction final : public UBlueprintAsyncActionBase
{
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintAssignable) FGuLiHybridAwaitResult Committed;
    UPROPERTY(BlueprintAssignable) FGuLiHybridAwaitResult Rejected;
    UPROPERTY(BlueprintAssignable) FGuLiHybridAwaitAbandoned Abandoned;
    UFUNCTION(BlueprintCallable,meta=(BlueprintInternalUseOnly="true",WorldContext="WorldContextObject"),Category="Hybrid|Transactions")
    static UGuLiAwaitHybridTransaction* AwaitHybridTransaction(UObject* WorldContextObject,FGuid Request);
    virtual void Activate() override;
    virtual void BeginDestroy() override;
    /** Removes this UI subscription; does not cancel an admitted operation. */
    UFUNCTION(BlueprintCallable,Category="Hybrid|Transactions") void StopWaiting();
private:
    TWeakObjectPtr<UGuLiHybridWorldSubsystem> Hybrid;
    TWeakObjectPtr<UWorld> AwaitWorld;
    FGuid Event;
    FDelegateHandle WorldCleanup;
    bool Finished=false,Activated=false;
    UFUNCTION() void Receive(const FGuLiHybridTransactionResult& Result);
    void Rebind();
    void WorldEnded(UWorld* World,bool SessionEnded,bool CleanupResources);
    void Abandon(const FString& Reason);
    void Detach();
};
