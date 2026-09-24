#pragma once

#include "CoreMinimal.h"
#include "Components/PrimitiveComponent.h"
#include "Commander/Network/GuLiCommanderTypes.h"
#include "Commander/Network/GuLiMoveLatency.h"
#include "GuLiCommanderRouteLineComponent.generated.h"

class UGuLiCommanderNetSyncComponent;
class AGuLiSoldierStateReplicator;
struct FGuLiSoldierRosterDelta;
class FGuLiRouteLineSceneProxy;

/** Local selected routes. Network application and rendering have independent cursors. */
UCLASS()
class GULISTRIKE_API UGuLiCommanderRouteLineComponent final : public UPrimitiveComponent
{
	GENERATED_BODY()
public:
	UGuLiCommanderRouteLineComponent();
	virtual void TickComponent(float Dt, ELevelTick Tick, FActorComponentTickFunction* Function) override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual FPrimitiveSceneProxy* CreateSceneProxy() override;
	virtual void GetUsedMaterials(TArray<UMaterialInterface*>& Out, bool bGetDebugMaterials=false) const override;
	virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;
	virtual void SendRenderDynamicData_Concurrent() override;
	int32 GetPendingLineCount() const { return Queued.Num(); }
	int32 GetVisibleLineCount() const { return Slots.Num(); }
	int32 CountInvalidDisplayedLines() const;
private:
	friend class FGuLiRouteLineSceneProxy;
	static constexpr int32 LinesPerChunk=128;
	struct FLine { FVector Start=FVector::ZeroVector, End=FVector::ZeroVector; uint32 Order=0, Revision=0; bool bVisible=false; GuLiMoveLatency::FContext Trace; };
	struct FChunk { TArray<FLine> Lines; FChunk() { Lines.SetNum(LinesPerChunk); } };
	TArray<FChunk> Chunks;
	TSet<int32> DirtyChunks;
	TMap<FGuLiSoldierId,int32> Slots;
	TArray<int32> FreeSlots;
	TSet<FGuLiSoldierId> Selected, Queued;
	TArray<FGuLiSoldierId> Queue;
	int32 QueueCursor=0;
	double NextDiagnostic=0;
	TWeakObjectPtr<UGuLiCommanderNetSyncComponent> NetSync;
	TWeakObjectPtr<AGuLiSoldierStateReplicator> Roster;
	bool bWasActive=false;
	void Unbind();
	void ClearLines();
	void Hide(FGuLiSoldierId Id);
	void Enqueue(FGuLiSoldierId Id);
	bool CanShow(FGuLiSoldierId Id) const;
	void OnSelection(const FGuLiCommanderSelectionState& Selection);
	void OnEndpoints(TConstArrayView<FGuLiSoldierId> Ids, bool bReset);
	void OnRoster(const FGuLiSoldierRosterDelta& Delta);
};
