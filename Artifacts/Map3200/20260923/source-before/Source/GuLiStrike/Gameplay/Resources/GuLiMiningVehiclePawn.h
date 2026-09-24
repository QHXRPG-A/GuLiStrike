#pragma once

#include "CoreMinimal.h"
#include "Gameplay/Resources/GuLiResourceTypes.h"
#include "Gameplay/Resources/GuLiMiningSlots.h"
#include "GameFramework/Character.h"
#include "Gameplay/Units/GuLiEngineeringAIController.h"
#include "Gameplay/Units/GuLiEngineeringTravelComponent.h"
#include "Commander/Behavior/GuLiCommanderWorkTypes.h"
#include "GuLiMiningVehiclePawn.generated.h"

class UChildActorComponent;
class UGuLiResourceEconomyConfig;
class UGuLiResourceWorldSubsystem;
class AGuLiResourceFactoryActor;
class UGuLiMiningPresentationComponent;
struct FGuLiSoldierDefinition;
namespace GuLiOrderBusinessProbe { struct FRun; }

USTRUCT()
struct FGuLiMiningPresentationDefinition
{
	GENERATED_BODY()
	UPROPERTY() TSubclassOf<AActor> ActorClass;
	UPROPERTY() float Scale = 1.0f;
};
UCLASS(NotPlaceable)
class GULISTRIKE_API AGuLiMiningVehicleAIController final : public AGuLiEngineeringAIController
{
	GENERATED_BODY()
};

/** Replicated, server-driven mining vehicle. Player orders temporarily override automatic scheduling. */
UCLASS(NotPlaceable)
class GULISTRIKE_API AGuLiMiningVehiclePawn final : public ACharacter, public IGuLiEngineeringVehicle
{
	GENERATED_BODY()

public:
	AGuLiMiningVehiclePawn(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	void InitializeVehicle(
		EGuLiTeam InTeam,
		FGuLiControllableActorId InStableActorId,
		const FGuLiSoldierDefinition& Unit,
		const UGuLiResourceEconomyConfig& Config,
		AGuLiResourceFactoryActor& InFactory);
	bool FindReachableMiningApproach(uint32 NodeId, FVector& OutApproach, float& OutPathLength) const;
	UFUNCTION(BlueprintPure, Category="Resources|Mining")
	virtual int32 GetUnitTypeId() const override { return UnitTypeId; }
	bool StartManagedTask(const FGuLiMiningCommand& Command, bool bAutomatic);
	bool StartPreparedManagedMove(const FGuLiMiningCommand& Command, const FGuLiPreparedGroundMove& Prepared);
	bool StopManagedTask();
	bool IsManagedTaskComplete() const { return bManagedTaskComplete; }
	bool DidManagedTaskFail() const { return bManagedTaskFailed; }
	bool HasManagedTaskOwner() const { return bTaskManaged; }
	/** The tree chooses the action; Tick only advances the selected physical/resource operation. */
	void ExecuteBehaviorAction(EGuLiMiningBehaviorAction Action, uint16 RequestedCluster);
	EGuLiCommanderWorkPhase GetBehaviorPhase() const;
	EGuLiCommanderWorkResult GetBehaviorResult() const { return BehaviorResult; }
	bool IsBehaviorRetryReady() const;
	bool NeedsReturnBeforeMining() const { return bManualReturnOrder || (ControlMode == EGuLiMiningControlMode::Auto && GetCargoTotal() > 0); }
	bool IssuePlayerCommand(const FGuLiMiningCommand& Command, EGuLiTeam RequestingTeam);
	/** A placement displacement invalidates the old path/task, retaining cargo and ownership. */
	void CancelTaskForExternalDisplacement();
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Resources|Transit")
	virtual EGuLiTransitOrderResult IssueStrongholdTransit(const FGuLiStrongholdTransitOrder& Order, EGuLiTeam RequestingTeam) override;
	UFUNCTION(BlueprintPure, Category="Resources|Transit") EGuLiTransitOrderResult GetLastTransitResult() const { return LastTransitResult; }
	/** Reflected convenience entry that still runs the same authority validation and StateTree task queue. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Resources|Mining")
	bool IssuePlayerCommandByValue(
		int32 RequestId,
		EGuLiMiningOrderType Type,
		FVector Target,
		int32 ClusterId,
		int32 SelectionRevision,
		EGuLiTeam RequestingTeam);
	UFUNCTION(BlueprintCallable, Category = "Resources|Mining")
	void ForceAutomaticControl();

	UFUNCTION(BlueprintPure, Category = "Resources|Mining")
	EGuLiTeam GetTeam() const { return Team; }
	FGuLiControllableActorId GetStableActorId() const { return StableActorId; }
	virtual float GetEngineeringBaseSpeed() const override { return SpeedCentimetersPerSecond; }
	virtual void SetEngineeringPresentationVisible(bool bVisible) override;
	virtual FBox GetEngineeringTravelBounds() const override { return TravelBounds; }
	UFUNCTION(BlueprintPure, Category = "Resources|Mining")
	FGuLiResourceAmounts GetCargo() const { return Cargo; }
	UFUNCTION(BlueprintPure, Category = "Resources|Mining")
	EGuLiMiningControlMode GetControlMode() const { return ControlMode; }
	UFUNCTION(BlueprintPure, Category = "Resources|Mining")
	EGuLiMiningTaskState GetTaskState() const { return TaskState; }
	UFUNCTION(BlueprintPure, Category = "Resources|Mining")
	float GetGraceSecondsRemaining() const;
	UFUNCTION(BlueprintPure, Category = "Resources|Mining")
	int32 GetTargetClusterId() const { return TargetClusterId; }
	float GetGraceEndServerTime() const { return GraceEndServerTime; }
	uint32 GetActivePlayerRequestId() const { return ActivePlayerRequestId; }
	FGuLiMiningVehiclePrivateState MakePrivateState() const;
	UFUNCTION(BlueprintPure, Category = "Resources|Mining")
	bool CanMineTargetFrom(const FVector& Target, const FTransform& Pose) const;
	UFUNCTION(BlueprintPure, Category = "Resources|Mining")
	bool IsMiningLaserActive() const { return MiningVisual.bActive; }
	UFUNCTION(BlueprintPure, Category = "Resources|Mining")
	int32 GetMiningNodeId() const { return MiningVisual.NodeId; }
	UFUNCTION(BlueprintPure, Category = "Resources|Mining")
	FVector GetMiningTarget() const { return MiningVisual.Target; }
	/** Zero means unlimited beam distance; mining still requires a reserved, occupied work position. */
	UFUNCTION(BlueprintPure, Category = "Resources|Mining")
	float GetMiningRange() const { return 0.0f; }
	UFUNCTION(BlueprintPure, Category = "Resources|Mining", meta=(DeprecatedFunction, DeprecationMessage="Factory manoeuvres were removed. Read TaskState Docking for unloading."))
	bool IsFactoryManeuverActive() const { return TaskState == EGuLiMiningTaskState::Docking; }
	UFUNCTION(BlueprintPure, Category = "Resources|Mining")
	FBox GetTravelBounds() const { return TravelBounds; }

private:
	friend struct GuLiOrderBusinessProbe::FRun;
	UFUNCTION()
	void OnRep_Presentation();
	UFUNCTION()
	void OnRep_MiningVisual();

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UChildActorComponent> VehiclePresentation;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UGuLiMiningPresentationComponent> MiningPresentation;
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<class UGuLiCombatHealthComponent> CombatHealth;

	UPROPERTY(ReplicatedUsing = OnRep_Presentation)
	FGuLiMiningPresentationDefinition PresentationDefinition;
	UPROPERTY(ReplicatedUsing = OnRep_MiningVisual)
	FGuLiMiningVisualState MiningVisual;

	UPROPERTY(Replicated)
	EGuLiTeam Team = EGuLiTeam::Unassigned;

	UPROPERTY(Replicated)
	FGuLiControllableActorId StableActorId;
	UPROPERTY(Replicated) int32 UnitTypeId = 0;

	UPROPERTY()
	FGuLiResourceAmounts Cargo;

	UPROPERTY()
	EGuLiMiningControlMode ControlMode = EGuLiMiningControlMode::Auto;

	UPROPERTY()
	EGuLiMiningTaskState TaskState = EGuLiMiningTaskState::Idle;

	UPROPERTY()
	uint16 TargetClusterId = 0u;

	UPROPERTY()
	float GraceEndServerTime = 0.0f;

	UPROPERTY()
	uint32 ActivePlayerRequestId = 0u;

	UPROPERTY()
	TObjectPtr<AGuLiResourceFactoryActor> Factory;

	float SpeedCentimetersPerSecond = 900.0f;
	float MiningRatePerSecond = 1.0f;
	float DockingSeconds = 1.0f;
	float GraceSeconds = 3.0f;
	float AutoRetrySeconds = 1.0f;
	int32 CargoCapacity = 10;
	float MiningAccumulator = 0.0f;
	float DockingAccumulator = 0.0f;
	float NextAutoRetryServerTime = 0.0f;
	FVector PlayerMoveTarget = FVector::ZeroVector;
	bool bManualReturnOrder = false;
	FVector MiningTarget = FVector::ZeroVector;
	// Task-local route candidates only; factories and unload points have no occupancy state.
	TArray<TWeakObjectPtr<AGuLiResourceFactoryActor>> ReturnFactories;
	int32 NextReturnFactory = 0;
	int32 UnloadPointAttempts = 0;
	FBox TravelBounds = FBox(ForceInit);
	uint32 TargetNodeId = 0;
	uint32 LastCompatibilityRequestId = 0;
	UPROPERTY(Replicated) EGuLiTransitOrderResult LastTransitResult = EGuLiTransitOrderResult::InvalidRequest;
	bool bTaskManaged = false;
	bool bManagedTaskComplete = false;
	bool bManagedTaskFailed = false;
	EGuLiCommanderWorkResult BehaviorResult = EGuLiCommanderWorkResult::None;
	FVector BehaviorApproach = FVector::ZeroVector;
	FGuLiMiningSlotReservation MiningSlot;
	uint32 MiningTaskVersion = 1;
	bool bWaitingForMiningSlot = false;

	UGuLiResourceWorldSubsystem* GetResourceSubsystem() const;
	int32 GetCargoTotal() const { return Cargo.Blue + Cargo.Red; }
	bool BeginMoveTo(const FVector& Target, float AcceptanceRadius);
	void TickMovingToCluster();
	void TickMining(float DeltaSeconds);
	void TickReturningToFactory();
	void TickDocking(float DeltaSeconds);
	void TickPlayerMoving();
	void BeginGrace(bool bSuccess = false);
	void FinishCurrentTarget();
	bool SelectMiningTarget();
	bool IsAtReservedMiningSlot() const;
	void SetMiningVisual(bool bActive);
};
