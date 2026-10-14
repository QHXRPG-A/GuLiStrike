#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Commander/Behavior/GuLiCommanderWorkTypes.h"
#include "Gameplay/Building/GuLiConstructionSlots.h"
#include "Network/GuLiHybridWorldSubsystem.h"
#include "GuLiConstructionWorkComponent.generated.h"
class UGuLiBuildingLifecycleComponent;
/** Produced by the engineer simulator; only the building simulator accepts slots/work. */
struct FGuLiHybridConstructionFact
{
    FGuLiHybridObjectKey Target;
    FGuLiConstructionSlotIntent Intent;
    uint32 Task=0;
    uint64 At=0;
    bool bOccupy=false,bReady=false;
};
UCLASS()
class GULISTRIKE_API UGuLiConstructionWorkComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UGuLiConstructionWorkComponent();
    void AdvanceHybrid(float DeltaSeconds);
    FGuLiHybridConstructionFact GetHybridWorkFact(uint64 LogicalTick) const;
    bool ApplyHybridWorkFact(const FGuLiHybridConstructionFact& Fact);
    bool CaptureHybridWork(TArray<uint8>& Bytes) const;
    bool ValidateHybridWork(TConstArrayView<uint8> Bytes) const;
    bool RestoreHybridWork(TConstArrayView<uint8> Bytes);
    bool GetObservedHybridFact(FGuLiHybridConstructionFact& Fact,uint64* ReceivedAge=nullptr) const;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	void SelectPosition();
	void RetryPosition();
	bool IsTerminalFailure() const;
	bool AssignBuilding(UGuLiBuildingLifecycleComponent& Building, bool bAutomatic = false);
	bool PrepareBuilding(const UGuLiBuildingLifecycleComponent& Building, FVector& OutPosition, float& OutPathLength) const;
	bool AreAllPositionsRejected(const UGuLiBuildingLifecycleComponent& Building) const;
	bool IsOrderCancelled() const { return bOrderCancelled; }
	const FString& GetOrderReason() const { return OrderReason; }
	UFUNCTION(BlueprintPure, Category="Construction") FString GetConstructionDebug() const;
	UFUNCTION() void StopWork();
	bool BeginBehaviorMove();
	bool BeginBehaviorConstruction();
	EGuLiCommanderWorkPhase GetBehaviorPhase() const;
	EGuLiCommanderWorkResult GetBehaviorResult() const;

	UGuLiBuildingLifecycleComponent* GetTarget() const { return Target.Get(); }
private:
    void AdvanceWork(float DeltaSeconds);
    bool bHybridSlotAcknowledged=false;
    double HybridSlotRequestedAt=0;
    FGuLiHybridConstructionFact ObservedHybridFact;
    // Receipt time belongs to this world, never to the remote engineer or building.
    uint64 ObservedFactReceivedTick=0;
    bool bObservedFactReceived=false;
	TWeakObjectPtr<UGuLiBuildingLifecycleComponent> Target;
	TWeakObjectPtr<UGuLiBuildingLifecycleComponent> ContributingTo;
	void PublishConstructionActivity(bool bActive);
	void SuspendConstructionEffects();
	FDelegateHandle ControlStateHandle;
	FVector WorkPosition = FVector::ZeroVector;
	bool bMoveRequested = false;
	bool bApplyingWork = false;
	bool bMoveFailed = false;
	bool bOrderCancelled = false;
	bool bAutomaticOrder = false;
	uint32 TaskVersion = 0;
	FGuLiConstructionSlotReservation Reservation;
	FGuLiConstructionSlotIntent Intent;
	FString OrderReason;
	double NextPositionCheck = 0;
	struct FRejected { int32 Slot; double RetryAt; uint32 Revision; FVector Start; uint32 Generation; uint32 Building; };
	TArray<FRejected> Rejected;
	bool IsRejectionCurrent(const FRejected& Entry, const UGuLiBuildingLifecycleComponent& Building) const;
	bool QueryPosition(const UGuLiBuildingLifecycleComponent& Building, FGuLiConstructionSlotIntent& Out, FVector& Position) const;
	void CancelOrder(const FString& Reason, bool bRejectPosition = false, bool bUnreachable = false);
	bool HasLocalReplacement() const;
};
