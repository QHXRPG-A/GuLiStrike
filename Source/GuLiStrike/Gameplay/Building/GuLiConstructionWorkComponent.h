#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Commander/Behavior/GuLiCommanderWorkTypes.h"
#include "Gameplay/Building/GuLiConstructionSlots.h"
#include "GuLiConstructionWorkComponent.generated.h"
class UGuLiBuildingLifecycleComponent;
UCLASS()
class GULISTRIKE_API UGuLiConstructionWorkComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UGuLiConstructionWorkComponent();
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
	void StopWork();
	bool BeginBehaviorMove();
	bool BeginBehaviorConstruction();
	EGuLiCommanderWorkPhase GetBehaviorPhase() const;
	EGuLiCommanderWorkResult GetBehaviorResult() const;
	virtual void TickComponent(float Dt, ELevelTick TickType, FActorComponentTickFunction* Function) override;
	UGuLiBuildingLifecycleComponent* GetTarget() const { return Target.Get(); }
private:
	TWeakObjectPtr<UGuLiBuildingLifecycleComponent> Target;
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
