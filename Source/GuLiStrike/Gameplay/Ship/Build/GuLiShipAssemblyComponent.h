#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Gameplay/Ship/Build/GuLiShipBuildCatalog.h"
#include "Gameplay/Ship/Build/GuLiShipBuildComponent.h"
#include "Gameplay/Ship/Capabilities/GuLiShipCapabilityComponent.h"
#include "GuLiShipAssemblyComponent.generated.h"

class AGuLiStrikeShip;
struct FGuLiHybridObjectState;
struct FGuLiHybridManifestData;
struct FGuLiHybridTransactionResult;
enum class EGuLiHybridRestoreStage : uint8;

USTRUCT(BlueprintType)
struct FGuLiShipCapabilityActivationResult
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly) FGuid RequestId;
	UPROPERTY(BlueprintReadOnly) bool bAccepted = false;
	UPROPERTY(BlueprintReadOnly) FString Reason;
};
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FGuLiShipCapabilityActivationResolved, const FGuLiShipCapabilityActivationResult&, Result);

USTRUCT()
struct FGuLiShipAssembledGroup
{
	GENERATED_BODY()
	UPROPERTY() FName ConfigurationId;
	UPROPERTY() FName GroupId;
	UPROPERTY() TArray<TObjectPtr<UGuLiStrikeShipPartComponent>> Parts;
	UPROPERTY() TArray<TObjectPtr<UGuLiShipCapabilityComponent>> Capabilities;
};

USTRUCT()
struct FGuLiShipAssemblyManifest
{
	GENERATED_BODY()
	UPROPERTY() TObjectPtr<UGuLiShipBuildCatalog> Catalog;
	UPROPERTY() int32 CatalogRevision = 0;
	UPROPERTY() FGuLiShipRunBuildState Build;
};

USTRUCT()
struct FGuLiShipAssemblyRuntime
{
	GENERATED_BODY()
	UPROPERTY() int64 MatchEpoch = 0;
	UPROPERTY() int64 BuildRevision = 0;
	UPROPERTY() TArray<FGuLiShipCapabilityRuntimeView> Capabilities;
	UPROPERTY() TArray<FName> MissingSockets;
};

/** Owns the assembly transaction. Components are local runtime instances, never RPC endpoints. */
UCLASS(ClassGroup=(Ship), meta=(BlueprintSpawnableComponent))
class GULISTRIKE_API UGuLiShipAssemblyComponent : public UActorComponent
{
	GENERATED_BODY()
	friend class FGuLiHybridShipAdapter;
public:
	UGuLiShipAssemblyComponent();
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Ship|Build") TObjectPtr<UGuLiShipBuildCatalog> Catalog;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Ship|Build") FName UpgradeDomain=TEXT("ship");
    UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Ship|Build") FName UpgradeRule=TEXT("upgrade/ship");

	/** Synchronous staging leaves current instances intact; commit cannot fail after preparation. */
	bool PrepareBuild(const FGuLiShipRunBuildState& Build, FString& Error);
	void CommitPreparedBuild();
	void DiscardPreparedBuild();
	int64 GetAppliedBuildRevision() const { return AppliedBuild.BuildRevision; }
    const FGuLiShipRunBuildState& GetAppliedBuild() const { return AppliedBuild; }
	int64 GetAppliedMatchEpoch() const { return AppliedBuild.MatchEpoch; }
	bool OwnsSocket(FName Socket) const;
	bool IsAvailableForChoice() const;
	bool OwnsPart(const UGuLiStrikeShipPartComponent* Part) const;
	bool NotifyPartDestroyed(UGuLiStrikeShipPartComponent* Part);
	UFUNCTION(BlueprintPure, Category="Ship|Build") UGuLiShipCapabilityComponent* FindCapability(FName GroupId, FName CapabilityId) const;
	UFUNCTION(BlueprintCallable, Category="Ship|Build")
	void ServerRequestActivation(FGuid RequestId, FName GroupId, FName CapabilityId, FName ActionId, int64 MatchEpoch, int64 BuildRevision);
	UPROPERTY(BlueprintAssignable, Category="Ship|Build") FGuLiShipCapabilityActivationResolved OnActivationResolved;
	void ReleaseBuild();
	bool CaptureHybridState(TArray<uint8>& Bytes) const;
	bool ValidateHybridState(TConstArrayView<uint8> Bytes) const;
	bool PrepareHybridRestore(TConstArrayView<uint8> Bytes);
	bool RestoreHybridState(TConstArrayView<uint8> Bytes,EGuLiHybridRestoreStage Stage);
	bool ApplyHybridLedger(const FGuLiHybridObjectState& State);
	bool ExportHybridCapabilityContracts(FGuLiHybridManifestData& Data,FString& Error) const;
	bool ValidateHybridCapabilityContracts(const FGuLiHybridManifestData& Data) const;
	bool IsRestoringPrivateState() const { return bRestoringPrivate; }
    FGuid RequestHybridChoice(FGuid RequestId,FName NodeId,int64 MatchEpoch,int64 ExpectedBuildRevision);
    bool FindHybridChoiceResult(FGuid RequestId,FGuLiShipChoiceResult& Result) const;
    void AdvanceHybridChoices();
    void AdvanceHybridCapabilities(uint64 LogicalTick,double DeltaSeconds);
protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
	UFUNCTION() void OnRep_Manifest();
	UFUNCTION() void OnRep_Runtime();
	UFUNCTION() void ClientResolveActivation(const FGuLiShipCapabilityActivationResult& Result);
    UFUNCTION() void HandleHybridTransaction(const FGuLiHybridTransactionResult& Result);
	void EvaluateRuntimeRequirements();
	void PublishRuntime();
	AGuLiStrikeShip& Ship() const;
	UPROPERTY() FGuLiShipAssemblyManifest Manifest;
	UPROPERTY() FGuLiShipAssemblyRuntime Runtime;
	UPROPERTY(Transient) TArray<FGuLiShipAssembledGroup> Groups;
	UPROPERTY(Transient) TArray<FGuLiShipAssembledGroup> StagedGroups;
	FGuLiShipRunBuildState AppliedBuild, PreparedBuild;
	FGuLiShipResolvedBuild AppliedResolved, PreparedResolved;
	FGuLiShipCompiledBuildRules Rules;
	TWeakObjectPtr<UGuLiShipBuildCatalog> CompiledCatalog;
	TWeakObjectPtr<UGuLiShipBuildCatalog> AppliedCatalog;
	TSet<FName> PreparedKeepConfigurations;
	int32 AppliedCatalogRevision = 0;
	int32 CompiledRevision = 0;
	bool bPrepared = false;
	bool bCommitting = false;
	bool bRestoringPrivate = false;
	struct FActivationReceipt
	{
		FName GroupId, CapabilityId, ActionId;
		int64 MatchEpoch = 0, BuildRevision = 0;
		FGuLiShipCapabilityActivationResult Result;
	};
	TMap<FGuid, FActivationReceipt> ActivationReceipts;
    struct FChoiceReceipt
    {
        FName Node;FGuid Requester;int64 Epoch=0,Revision=0;bool Resolved=false,Notified=false;FGuLiShipChoiceResult Result;
    };
    TMap<FGuid,FChoiceReceipt> ChoiceReceipts;
    bool bAdvancingChoices=false;
};
