#pragma once

#include "CoreMinimal.h"
#include "Gameplay/Resources/GuLiResourceTypes.h"
#include "GameFramework/Character.h"
#include "Gameplay/Units/GuLiEngineeringAIController.h"
#include "Gameplay/Units/GuLiEngineeringTravelComponent.h"
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
	bool IssuePlayerCommand(const FGuLiMiningCommand& Command, EGuLiTeam RequestingTeam);
	/** A placement displacement invalidates the old path/task, retaining cargo and ownership. */
	void CancelTaskForExternalDisplacement();
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Resources|Transit")
	virtual EGuLiTransitOrderResult IssueStrongholdTransit(const FGuLiStrongholdTransitOrder& Order, EGuLiTeam RequestingTeam) override;
	UFUNCTION(BlueprintPure, Category="Resources|Transit") EGuLiTransitOrderResult GetLastTransitResult() const { return LastTransitResult; }
	/** Reflected convenience entry that still runs the same authority validation/state machine. */
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
	UFUNCTION(BlueprintPure, Category = "Resources|Mining")
	float GetMiningRange() const { return MiningDistanceCentimeters; }
	UFUNCTION(BlueprintPure, Category = "Resources|Mining")
	bool IsFactoryManeuverActive() const;
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
	float MiningDistanceCentimeters = 1080.0f;
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
	float ManeuverSpeed = 300.0f;
	FVector MiningTarget = FVector::ZeroVector;
	bool bDockAligned = false;
	FBox TravelBounds = FBox(ForceInit);
	uint32 TargetNodeId = 0;
	struct FPendingEngineeringCommand
	{
		FGuLiMiningCommand Mining;
		FGuLiStrongholdTransitOrder Transit;
		FPendingEngineeringCommand(const FGuLiMiningCommand& In) : Mining(In) {}
		FPendingEngineeringCommand(const FGuLiStrongholdTransitOrder& In) : Transit(In) {}
	};
	TOptional<FPendingEngineeringCommand> PendingCommand;
	UPROPERTY(Replicated) EGuLiTransitOrderResult LastTransitResult = EGuLiTransitOrderResult::InvalidRequest;
	void ExecuteTransit(const FGuLiStrongholdTransitOrder& Order, const FGuLiPreparedTransit& Prepared);
	bool bPendingAutomatic = false;
	bool bTaskManaged = false;
	bool bManagedTaskComplete = false;
	bool bManagedTaskFailed = false;

	UGuLiResourceWorldSubsystem* GetResourceSubsystem() const;
	int32 GetCargoTotal() const { return Cargo.Blue + Cargo.Red; }
	bool BeginMoveTo(const FVector& Target, float AcceptanceRadius);
	void TickAutomatic();
	void TickMovingToCluster();
	void TickMining(float DeltaSeconds);
	void TickReturningToFactory();
	void TickDocking(float DeltaSeconds);
	void TickPlayerMoving();
	void BeginReturnToFactory(bool bFromManualOrder);
	void BeginGrace(bool bSuccess = false);
	void FinishCurrentTarget();
	bool SelectMiningTarget();
	void SetMiningVisual(bool bActive);
	void TickFactoryManeuver(float DeltaSeconds);
	bool MoveFactoryStep(const FVector& LocalTarget, float LocalYaw, float DeltaSeconds);
	void FinishFactoryManeuver();
	void ExecutePlayerCommand(const FGuLiMiningCommand& Command);
};
