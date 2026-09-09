// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Battle/Contracts/GuLiWingmanProtocolTypes.h"
#include "GameFramework/Pawn.h"
#include "Gameplay/Wingman/Behavior/GuLiWingmanMemberBehavior.h"
#include "Gameplay/Wingman/GuLiWingmanRuntimeTypes.h"
#include "GuLiWingmanPawn.generated.h"

class UGuLiWingmanFlightMovementComponent;
class USphereComponent;
class UStateTree;
class UStateTreeComponent;
class UStaticMesh;
class UStaticMeshComponent;

UENUM(BlueprintType)
enum class EGuLiWingmanPawnMode : uint8
{
	OwnerSimulation = 0,
	RemotePresentation
};

/** One non-replicated client Wingman. Dedicated Servers never spawn this class. */
UCLASS(Transient, NotPlaceable, Config=Game)
class GULISTRIKE_API AGuLiWingmanPawn final : public APawn
{
	GENERATED_BODY()

public:
	AGuLiWingmanPawn();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual UPawnMovementComponent* GetMovementComponent() const override;

	bool InitializeOwnerSimulation(const FGuLiWingmanRuntimeState& InitialState,
		UStateTree* BehaviorStateTree, UStaticMesh* Mesh);
	bool InitializeRemotePresentation(const FGuLiWingmanHandle& Handle, UStaticMesh* Mesh);
	void ResetForPool();

	void UpdateBehaviorObservation(float DeltaSeconds);
	EGuLiWingmanMemberBehavior EvaluateDesiredBehavior() const;
	bool ShouldSelectBehavior(EGuLiWingmanMemberBehavior Behavior) const;
	bool ApplyStateTreeBehavior(EGuLiWingmanMemberBehavior Behavior);
	EGuLiWingmanMemberBehavior GetSelectedBehavior() const { return SelectedBehavior; }

	void ApplyRemotePresentation(const FTransform& Transform, float Opacity,
		bool bInteractable, bool bAuthorityRebase);
	void ApplyAuthorityRebase(const FTransform& Transform, const FVector& InitialVelocity);
	void MarkRebaseRejected(bool bNoSafePoint, double RetryAfterServerTimeSeconds);
	void QueueStaleRebaseRetry(double EstimatedServerTimeSeconds);
	void RequestEmergencyRebase(EGuLiWingmanEmergencyRebaseReason Reason);
	bool ConsumeEmergencyRebaseRequest(EGuLiWingmanEmergencyRebaseReason& OutReason);
	void CancelFrozenAttackForRecovery();
	void SetAlive(bool bAlive);
	void ConfigureMesh(UStaticMesh* Mesh);

	const FGuLiWingmanHandle& GetWingmanHandle() const { return Runtime.Identity.Handle; }
	EGuLiWingmanPawnMode GetPawnMode() const { return PawnMode; }
	bool IsOwnerSimulationPawn() const { return PawnMode == EGuLiWingmanPawnMode::OwnerSimulation; }
	bool IsOwnerSimulationActive() const { return bOwnerSimulationActive; }
	bool IsPresentationInteractable() const { return bPresentationInteractable; }
	bool IsStateTreeRunning() const { return bUsingStateTree; }
	FGuLiWingmanRuntimeState& GetMutableRuntimeState() { return Runtime; }
	const FGuLiWingmanRuntimeState& GetRuntimeState() const { return Runtime; }
	UGuLiWingmanFlightMovementComponent* GetFlightMovement() const { return FlightMovement; }

#if WITH_DEV_AUTOMATION_TESTS
	/** Drives the native component in transient Worlds that do not run a World tick. */
	void TickStateTreeForTests(float DeltaSeconds);
#endif

private:
	void StopStateTree(const TCHAR* Reason);

	UPROPERTY(VisibleAnywhere, Category="Wingman")
	TObjectPtr<USphereComponent> CollisionRoot;

	UPROPERTY(VisibleAnywhere, Category="Wingman")
	TObjectPtr<UStaticMeshComponent> VisualMesh;

	UPROPERTY(VisibleAnywhere, Category="Wingman")
	TObjectPtr<UGuLiWingmanFlightMovementComponent> FlightMovement;

	UPROPERTY(VisibleAnywhere, Category="Wingman|Behavior")
	TObjectPtr<UStateTreeComponent> StateTreeComponent;

	FGuLiWingmanRuntimeState Runtime;
	EGuLiWingmanPawnMode PawnMode = EGuLiWingmanPawnMode::RemotePresentation;
	EGuLiWingmanMemberBehavior ObservedBehavior = EGuLiWingmanMemberBehavior::EscortOrbit;
	EGuLiWingmanMemberBehavior SelectedBehavior = EGuLiWingmanMemberBehavior::EscortOrbit;
	float BehaviorObservationAccumulator = 0.0f;
	double RebaseRetryAfterServerTimeSeconds = 0.0;
	EGuLiWingmanEmergencyRebaseReason PendingRebaseReason =
		EGuLiWingmanEmergencyRebaseReason::MovementDeadlock;
	bool bUsingStateTree = false;
	bool bOwnerSimulationActive = false;
	bool bPresentationInteractable = false;
	bool bEmergencyRebasePending = false;
};
