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
	void ReservePosition();
	void RetryPosition();
	bool IsTerminalFailure() const;
	bool AssignBuilding(UGuLiBuildingLifecycleComponent& Building);
	bool PrepareBuilding(const UGuLiBuildingLifecycleComponent& Building, FVector& OutPosition, float& OutPathLength) const;
	bool AreAllPositionsRejected(const UGuLiBuildingLifecycleComponent& Building) const;
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
	bool bWaitingPosition = false;
	bool bNoPositions = false;
	uint32 TaskVersion = 0;
	FGuLiConstructionSlotReservation Reservation;
	double NextPositionCheck = 0;
	struct FRejected { int32 Slot; double RetryAt; uint32 Revision; FVector Start; uint32 Generation; uint32 Building; };
	TArray<FRejected> Rejected;
	bool IsRejectionCurrent(const FRejected& Entry, const UGuLiBuildingLifecycleComponent& Building) const;
};
