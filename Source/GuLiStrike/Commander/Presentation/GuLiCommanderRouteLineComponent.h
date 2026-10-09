#pragma once

#include "CoreMinimal.h"
#include "Commander/UI/GuLiSceneUITypes.h"
#include "Components/PrimitiveComponent.h"
#include "Commander/Network/GuLiCommanderTypes.h"
#include "Commander/Network/GuLiMoveLatency.h"
#include "GuLiCommanderRouteLineComponent.generated.h"

class UGuLiCommanderNetSyncComponent;
class AGuLiSoldierStateReplicator;
class AGuLiCommanderPresentationActor;
class AGuLiCommanderPlayerController;
class UMaterialInterface;
struct FGuLiSoldierRosterDelta;

/** Per-view selected route cache consumed by the final Slate layer. */
UCLASS(Config=Game)
class GULISTRIKE_API UGuLiCommanderRouteLineComponent final : public UPrimitiveComponent
{
	GENERATED_BODY()
public:
	UGuLiCommanderRouteLineComponent();
	virtual void OnRegister() override;
	virtual void BeginPlay() override;
	virtual void TickComponent(float Dt, ELevelTick Tick, FActorComponentTickFunction* Function) override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual FPrimitiveSceneProxy* CreateSceneProxy() override;
	virtual void GetUsedMaterials(TArray<UMaterialInterface*>& Out, bool bGetDebugMaterials=false) const override;
	virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;
	virtual void SendRenderDynamicData_Concurrent() override;
	int32 GetPendingLineCount() const { return Queued.Num() + AwaitingPresentation.Num(); }
	int32 GetVisibleLineCount() const { return Slots.Num(); }
	int32 CountInvalidDisplayedLines() const;
	void GatherSceneUILines(TArray<FGuLiSceneUILine>& Out) const;
	void InitializeForController(AGuLiCommanderPlayerController* Viewer, AGuLiCommanderPresentationActor* Source);
	void RecordSceneUISubmission() const;
private:
	/** Translucent overlay material with depth testing disabled, configured in DefaultGame.ini. */
	UPROPERTY(Config, EditDefaultsOnly, Category="Commander|RouteLine")
	TSoftObjectPtr<UMaterialInterface> OverlayMaterial;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> ResolvedOverlayMaterial;
	static constexpr int32 LinesPerChunk=128;
	struct FLine { FVector Start=FVector::ZeroVector, End=FVector::ZeroVector; uint32 Order=0, Revision=0; bool bVisible=false; GuLiMoveLatency::FContext Trace; };
	struct FChunk { TArray<FLine> Lines; FChunk() { Lines.SetNum(LinesPerChunk); } };
	TArray<FChunk> Chunks;
	TSet<int32> DirtyChunks;
	TMap<FGuLiSoldierId,int32> Slots;
	TArray<int32> FreeSlots;
	TSet<FGuLiSoldierId> Selected, Queued, AwaitingPresentation;
	TArray<FGuLiSoldierId> Queue;
	int32 QueueCursor=0;
	double NextDiagnostic=0;
	TWeakObjectPtr<UGuLiCommanderNetSyncComponent> NetSync;
	TWeakObjectPtr<AGuLiSoldierStateReplicator> Roster;
	TWeakObjectPtr<AGuLiCommanderPresentationActor> Presentation;
	TWeakObjectPtr<AGuLiCommanderPlayerController> LocalController;
	mutable TSet<uint64> SubmittedBatches;
	bool bWasActive=false;
	void Unbind();
	void ClearLines();
	void Hide(FGuLiSoldierId Id);
	void ReleaseSlot(int32 Slot);
	void Enqueue(FGuLiSoldierId Id);
	bool CanShow(FGuLiSoldierId Id) const;
	void OnSelection(const FGuLiCommanderSelectionState& Selection);
	void OnEndpoints(TConstArrayView<FGuLiSoldierId> Ids, bool bReset);
	void OnRoster(const FGuLiSoldierRosterDelta& Delta);
};
