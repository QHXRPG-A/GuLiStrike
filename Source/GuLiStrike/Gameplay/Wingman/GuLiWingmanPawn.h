// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Battle/Contracts/GuLiWingmanProtocolTypes.h"
#include "GameFramework/Pawn.h"
#include "Gameplay/Wingman/Behavior/GuLiWingmanMemberBehavior.h"
#include "Gameplay/Wingman/GuLiWingmanRuntimeTypes.h"
#include "GuLiWingmanPawn.generated.h"

class UGuLiWingmanFlightMovementComponent;
class UGuLiTeamOutlineComponent;
class UNiagaraComponent;
class UNiagaraSystem;
class USceneComponent;
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
	void SetPhaseAppearance(bool bPhased);
	void MarkRebaseRejected(bool bNoSafePoint, double RetryAfterServerTimeSeconds);
	void QueueStaleRebaseRetry(double EstimatedServerTimeSeconds);
	void RequestEmergencyRebase(EGuLiWingmanEmergencyRebaseReason Reason);
	bool ConsumeEmergencyRebaseRequest(EGuLiWingmanEmergencyRebaseReason& OutReason);
	void CancelFrozenAttackForRecovery();
	void SetAlive(bool bAlive);
	void ConfigureMesh(UStaticMesh* Mesh);
	/** Applies a render-only world transform below the authoritative Pawn root. */
	void ApplyOwnerPresentationTransform(const FTransform& Transform);
	/** Model/nozzle space for local effects, never a collision or attack-authority input. */
	FTransform GetPresentationTransform() const;
	/** Called after movement by the existing owner/remote presentation update. */
	void UpdateFlightTrail(float Opacity = 1.0f, bool bResetTrail = false);

	UFUNCTION(BlueprintPure, Category="Wingman")
	const FGuLiWingmanHandle& GetWingmanHandle() const { return Runtime.Identity.Handle; }
	EGuLiWingmanPawnMode GetPawnMode() const { return PawnMode; }
	bool IsOwnerSimulationPawn() const { return PawnMode == EGuLiWingmanPawnMode::OwnerSimulation; }
	bool IsOwnerSimulationActive() const { return bOwnerSimulationActive; }
	bool IsPresentationInteractable() const { return bPresentationInteractable; }
	bool IsStateTreeRunning() const { return bUsingStateTree; }
	FGuLiWingmanRuntimeState& GetMutableRuntimeState() { return Runtime; }
	const FGuLiWingmanRuntimeState& GetRuntimeState() const { return Runtime; }
	UGuLiWingmanFlightMovementComponent* GetFlightMovement() const { return FlightMovement; }
	UGuLiTeamOutlineComponent* GetTeamOutline() const { return TeamOutline; }

#if WITH_DEV_AUTOMATION_TESTS
	/** Drives the native component in transient Worlds that do not run a World tick. */
	void TickStateTreeForTests(float DeltaSeconds);
#endif

private:
	void StopStateTree(const TCHAR* Reason);
	void StopFlightTrail();

	UPROPERTY(Config, EditDefaultsOnly, Category="Wingman|Flight VFX")
	int32 FlightTrailVfxId = 0;

	/** Mesh-local nozzle position; this mesh faces -X, so its tail is +X. */
	UPROPERTY(Config, EditDefaultsOnly, Category="Wingman|Flight VFX")
	FVector FlightTrailOffset = FVector(1258.0f, 0.0f, 300.0f);

	UPROPERTY(Config, EditDefaultsOnly, Category="Wingman|Flight VFX", meta=(ClampMin="0"))
	float FlightTrailCullDistance = 36000.0f;

	/** Reused with the Pawn pool; never replicated or allocated on a dedicated server. */
	UPROPERTY(Transient)
	TObjectPtr<UNiagaraComponent> FlightTrail;

	FVector LastFlightTrailLocation = FVector::ZeroVector;
	bool bHasFlightTrailLocation = false;
	bool bPreviousRemoteRebase = false;
	bool bHasRemoteVisualPose = false;
	double RemoteVisualTimeSeconds = 0.0;
	bool bFlightTrailRunning = false;
	bool bFlightTrailLoadFailed = false;

	UPROPERTY(VisibleAnywhere, Category="Wingman")
	TObjectPtr<USphereComponent> CollisionRoot;

	UPROPERTY(VisibleAnywhere, Category="Wingman|Presentation")
	TObjectPtr<USceneComponent> PresentationRoot;

	UPROPERTY(VisibleAnywhere, Category="Wingman")
	TObjectPtr<UStaticMeshComponent> VisualMesh;

	UPROPERTY(VisibleAnywhere, Category="Wingman|Presentation")
	TObjectPtr<UGuLiTeamOutlineComponent> TeamOutline;

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
