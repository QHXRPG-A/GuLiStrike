#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Gameplay/CommanderSkills/GuLiCommanderSkillDefinition.h"
#include "GuLiCommanderSkillComponent.generated.h"

class AGuLiBattlePlayerState;
class AGuLiCommanderPlayerController;
class UGuLiCommanderNetSyncComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FGuLiActiveSkillReplyEvent, const FGuLiActiveSkillReply&, Reply);

/** Fixed PlayerState entry: RPC intent, request receipts and commander-global runtime. */
UCLASS(ClassGroup=(Commander))
class GULISTRIKE_API UGuLiCommanderSkillComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UGuLiCommanderSkillComponent();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	/** Called by Q after capturing this frame's ground trace. All-no-skill selections send nothing. */
	void ActivateSelectedUnits(bool bHasGroundPoint, FVector GroundPoint);
	UFUNCTION(BlueprintCallable, Category="Commander|Skills")
	void ActivateGlobalSkill(FName SkillId, bool bHasGroundPoint, FVector GroundPoint);
	UFUNCTION(BlueprintPure, Category="Commander|Skills") TArray<FGuLiActiveSkillRuntime> GetGlobalSkills() const { return GlobalRuntime; }
	UFUNCTION(BlueprintPure, Category="Commander|Skills") TArray<FGuLiUnitSkillRuntimeView> GetSelectedUnitSkills() const;
	UFUNCTION(BlueprintPure, Category="Commander|Skills") FGuLiActiveSkillReply GetLastReply() const { return LastReply; }
	const UGuLiCommanderSkillCatalog* GetSkillCatalog() const { return Catalog; }
	UFUNCTION(BlueprintPure, Category="Commander|Skills") int32 GetGlobalSkillLevel(FName SkillId) const;
	bool SetServerGlobalSkillLevel(FName SkillId, int32 Level);
	void CopyMatchStateFrom(const UGuLiCommanderSkillComponent& Other);
	/** Shared server-local entry also used by the existing two-point tactical UI. */
	FGuLiActiveSkillReply ExecuteServerRequest(const FGuLiActiveSkillRequest& Request);
	UFUNCTION(Server, Reliable) void ServerRequestSkill(const FGuLiActiveSkillRequest& Request);
	UPROPERTY(BlueprintAssignable, Category="Commander|Skills") FGuLiActiveSkillReplyEvent OnSkillReply;
protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
	UFUNCTION() void HandlePlayerStateChanged();
	void RefreshSelectedUnitSkills(const FGuLiCommanderSelectionState& Selection);
	UFUNCTION(Client, Reliable) void ClientReceiveReply(const FGuLiActiveSkillReply& Reply);
	AGuLiBattlePlayerState& PlayerState() const;
	AGuLiCommanderPlayerController* Commander() const;
	bool InitializeCatalog();
	bool SynchronizeEpoch();
	FGuLiActiveSkillRequest MakeLocalRequest(bool bHasGroundPoint, FVector GroundPoint) const;
	UPROPERTY(Replicated) TObjectPtr<UGuLiCommanderSkillCatalog> Catalog;
	UPROPERTY(Replicated) TArray<FGuLiActiveSkillRuntime> GlobalRuntime;
	UPROPERTY(Replicated) TArray<FGuLiUnitSkillRuntimeView> SelectedUnitRuntime;
	UPROPERTY(Replicated) uint32 RuntimeSelectionRevision = 0;
	UPROPERTY(Transient) FGuLiActiveSkillReply LastReply;
	uint32 RuntimeEpoch = 0;
	TWeakObjectPtr<UGuLiCommanderNetSyncComponent> BoundSelection;
	struct FReceipt { FGuLiActiveSkillRequest Request; FGuLiActiveSkillReply Reply; };
	TMap<FGuid, FReceipt> Receipts;
	bool bExecuting = false;
};
