#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Gameplay/Ship/Build/GuLiShipBuildCatalog.h"
#include "GuLiShipBuildComponent.generated.h"

class UGuLiShipAssemblyComponent;

USTRUCT(BlueprintType)
struct FGuLiShipChoiceAvailability
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly) FName NodeId;
	UPROPERTY(BlueprintReadOnly) bool bAvailable = false;
	UPROPERTY(BlueprintReadOnly) FString Reason;
};

USTRUCT(BlueprintType)
struct FGuLiShipChoiceResult
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly) FGuid RequestId;
	UPROPERTY(BlueprintReadOnly) bool bCommitted = false;
    UPROPERTY(BlueprintReadOnly) bool bBusinessEffects = false;
	UPROPERTY(BlueprintReadOnly) int64 BuildRevision = 0;
	UPROPERTY(BlueprintReadOnly) FString Reason;
};
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FGuLiShipChoiceResolved,const FGuLiShipChoiceResult&,Result);

/** Match facts belong to PlayerState, while spawned Ship instances are replaceable. */
UCLASS(ClassGroup=(Ship))
class GULISTRIKE_API UGuLiShipBuildComponent : public UActorComponent
{
	GENERATED_BODY()
	friend class FGuLiHybridShipAdapter;
public:
	UGuLiShipBuildComponent();

	UFUNCTION(BlueprintPure, Category="Ship|Build") const FGuLiShipRunBuildState& GetBuildState() const { return State; }
	UFUNCTION(BlueprintPure, Category="Ship|Build") TArray<FGuLiShipChoiceAvailability> QueryChoices() const;
	/** Server-local reward integration. Caller has already confirmed the offered choice; this is deliberately not an RPC. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Ship|Build")
	FGuLiShipChoiceResult CommitConfirmedChoice(FGuid RequestId, FName NodeId, int64 MatchEpoch, int64 ExpectedBuildRevision);
    /** A valid identity means the request is retained. Only OnChoiceResolved confirms the outcome. */
    UFUNCTION(BlueprintCallable,BlueprintAuthorityOnly,Category="Ship|Build")
    FGuid RequestConfirmedChoice(FGuid RequestId,FName NodeId,int64 MatchEpoch,int64 ExpectedBuildRevision);
    UFUNCTION(BlueprintPure,Category="Ship|Build") bool FindChoiceResult(FGuid RequestId,FGuLiShipChoiceResult& Result) const;
    UPROPERTY(BlueprintAssignable,Category="Ship|Build") FGuLiShipChoiceResolved OnChoiceResolved;
    bool NotifyHybridChoiceResolved(const UGuLiShipAssemblyComponent& Assembly,const FGuLiShipChoiceResult& Result);
	bool RestoreShip(UGuLiShipAssemblyComponent& Assembly, FString& Error);
	void CopyMatchStateFrom(const UGuLiShipBuildComponent& Other);
    bool AdoptExistingShip(UGuLiShipAssemblyComponent& Assembly);
private:
	UPROPERTY() FGuLiShipRunBuildState State;
	UPROPERTY() TObjectPtr<UGuLiShipBuildCatalog> Catalog;
	TWeakObjectPtr<UGuLiShipAssemblyComponent> CurrentAssembly;
	struct FRequest { FName NodeId; int64 Epoch = 0, Revision = 0; FGuLiShipChoiceResult Result; };
	TMap<FGuid, FRequest> Requests;
	const FGuLiShipCompiledBuildRules& GetRules() const;
	mutable FGuLiShipCompiledBuildRules Rules;
	mutable TWeakObjectPtr<UGuLiShipBuildCatalog> CompiledCatalog;
	mutable int32 CompiledRevision = 0;
	bool bCommitting = false;
};
