#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Gameplay/CommanderSkills/GuLiCommanderSkillDefinition.h"
#include "GuLiCommanderSkillComponent.generated.h"

class AGuLiBattlePlayerState;
class AGuLiCommanderPlayerController;
class UGuLiCommanderNetSyncComponent;
struct FGuLiHybridCommandResult;
struct FGuLiHybridTransactionResult;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FGuLiActiveSkillReplyEvent, const FGuLiActiveSkillReply&, Reply);

/** Stable PlayerState input entry: owner commands, asynchronous receipts and UI state. */
UCLASS(ClassGroup=(Commander))
class GULISTRIKE_API UGuLiCommanderSkillComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UGuLiCommanderSkillComponent();

	/** Called by Q after capturing this frame's ground trace. All-no-skill selections send nothing. */
	void ActivateSelectedUnits(bool bHasGroundPoint, FVector GroundPoint);
    /** Returns an input identity; completion is delivered through OnSkillReply. */
    UFUNCTION(BlueprintCallable,Category="Commander|Skills") FGuid RequestHybridUnitSkills(bool bHasGroundPoint,FVector GroundPoint);
	UFUNCTION(BlueprintCallable, Category="Commander|Skills")
	void ActivateGlobalSkill(FName SkillId, bool bHasGroundPoint, FVector GroundPoint);
    UFUNCTION(BlueprintCallable,Category="Commander|Skills") FGuid RequestGlobalSkill(FName SkillId,bool bHasGroundPoint,FVector GroundPoint);
	UFUNCTION(BlueprintPure, Category="Commander|Skills") TArray<FGuLiActiveSkillRuntime> GetGlobalSkills() const;
	UFUNCTION(BlueprintPure, Category="Commander|Skills") TArray<FGuLiUnitSkillRuntimeView> GetSelectedUnitSkills() const;
	UFUNCTION(BlueprintPure, Category="Commander|Skills") FGuLiActiveSkillReply GetLastReply() const { return LastReply; }
	const UGuLiCommanderSkillCatalog* GetSkillCatalog() const { return Catalog; }
	UFUNCTION(BlueprintPure, Category="Commander|Skills") int32 GetGlobalSkillLevel(FName SkillId) const;
	bool SetServerGlobalSkillLevel(FName SkillId, int32 Level);
	void CopyMatchStateFrom(const UGuLiCommanderSkillComponent& Other);
    TArray<FGuid> GetPendingHybridRequests() const;
    void RebindHybridRequests();
	/** Retains the reflected entry name; routes locally through object-owner commands. */

	UFUNCTION() void ServerRequestSkill(const FGuLiActiveSkillRequest& Request);
#if WITH_EDITOR
	/** Read-only acceptance support: replay the complete receipt, including Q's ground trace. */
	bool CopyServerRequestReceipt(FGuid RequestId, FGuLiActiveSkillRequest& OutRequest) const;
#endif
	UPROPERTY(BlueprintAssignable, Category="Commander|Skills") FGuLiActiveSkillReplyEvent OnSkillReply;
protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
	UFUNCTION() void HandlePlayerStateChanged();
	void RefreshSelectedUnitSkills(const FGuLiCommanderSelectionState& Selection);
	void HandleWeaponProfilesChanged(uint32 Revision);
	UFUNCTION() void ClientReceiveReply(const FGuLiActiveSkillReply& Reply);
	AGuLiBattlePlayerState& PlayerState() const;
	AGuLiCommanderPlayerController* Commander() const;
	bool InitializeCatalog();
	bool SynchronizeEpoch();
	FGuLiActiveSkillRequest MakeLocalRequest(bool bHasGroundPoint, FVector GroundPoint) const;
	UPROPERTY() TObjectPtr<UGuLiCommanderSkillCatalog> Catalog;
	UPROPERTY() TArray<FGuLiActiveSkillRuntime> GlobalRuntime;
	UPROPERTY() TArray<FGuLiUnitSkillRuntimeView> SelectedUnitRuntime;
	UPROPERTY() uint32 RuntimeSelectionRevision = 0;
	UPROPERTY(Transient) FGuLiActiveSkillReply LastReply;
	uint32 RuntimeEpoch = 0;
	TWeakObjectPtr<UGuLiCommanderNetSyncComponent> BoundSelection;
	struct FReceipt { FGuLiActiveSkillRequest Request; FGuLiActiveSkillReply Reply; };
	TMap<FGuid, FReceipt> Receipts;
    struct FHybridPendingSkill {FGuLiActiveSkillRequest Request;TMap<FGuid,FGuLiActiveSkillUnitResult> Units;TSet<FGuid> Waiting;};
    TMap<FGuid,FHybridPendingSkill> HybridPending;
    FGuid RouteHybridUnitSkillRequest(const FGuLiActiveSkillRequest& Request);
    void HandleHybridSkillResult(const FGuLiHybridCommandResult&);
    TMap<FGuid,FReceipt> HybridGlobalRequests;
    FGuLiActiveSkillReply RequestHybridGlobal(const FGuLiActiveSkillRequest& Request);
    UFUNCTION() void CompleteHybridGlobal(const FGuLiHybridTransactionResult& Result);
    void RebindHybridGlobals();
    void RejectHybridGlobalCommand(const FGuLiHybridCommandResult& Result);
};
