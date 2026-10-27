#pragma once

#include "CoreMinimal.h"
#include "Gameplay/Navigation/GuLiDynamicObstacleRegistry.h"
#include "Gameplay/Navigation/GuLiGroundMassCollisionTypes.h"
#include "Gameplay/Units/GuLiExternalCharacterMovementComponent.h"
#include "GuLiGroundMechMovementComponent.generated.h"

class UGuLiGroundMassContactSubsystem;
class UGuLiGroundMechRocketComponent;
namespace GuLiGroundMechMovement
{
inline constexpr uint8 MassSupportCustomMode = 1u;
}

struct FGuLiMassSupportState
{
	FGuLiSoldierId SoldierId;
	uint32 Epoch = 0u;
	uint32 DisplacementRevision = 0u;
	FVector BodyLocation = FVector::ZeroVector;
	FVector RelativeLocation = FVector::ZeroVector;
	float TopZ = 0.0f;
	float Radius = 0.0f;
	double SimulationSeconds = 0.0;
	// Platform reference retained by physical-state recovery and collision replay.
	FVector SourceLocation = FVector::ZeroVector;
	float SourceTopZ = 0.0f;
	bool bHasSourceReference = false;
	bool IsValid() const { return SoldierId.IsValid(); }
};

struct FGuLiRocketMoveState
{
	float Fuel = 0.f;
	float RecoveryElapsed = 0.f;
};

/** Fixed-clock world collision with explicit timed sweeps against Mass query data. */
UCLASS()
class GULISTRIKE_API UGuLiGroundMechMovementComponent final : public UGuLiExternalCharacterMovementComponent
{
	GENERATED_BODY()
  public:
	UGuLiGroundMechMovementComponent(const FObjectInitializer &ObjectInitializer = FObjectInitializer::Get());
	virtual ~UGuLiGroundMechMovementComponent() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
							   FActorComponentTickFunction *ThisTickFunction) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	void AdvanceHybrid(float DeltaTime,FVector2D Axis,bool bSprint);
	bool FindBlinkDestination(const FVector& Direction,float Distance,float SearchStep,float ProbeHeight,FVector& Destination);
	FVector TeleportBlink(const FVector& Destination);
    void AdvanceDormantRecovery(float DeltaTime);
	virtual bool ShouldUsePackedMovementRPCs() const override { return false; }
	virtual float GetMaxSpeed() const override;
	virtual float GetMaxBrakingDeceleration() const override;
	virtual FRotator ComputeOrientToMovementRotation(const FRotator& CurrentRotation, float DeltaTime, FRotator& DeltaRotation) const override;
	UFUNCTION(BlueprintPure, Category="Mech|Movement")
	bool IsGroundedForAnimation() const { return IsMovingOnGround() || IsMassSupportMode(); }
	virtual void ApplyExternalDisplacement(const FTransform &Transform) override;
	virtual void OnTeleported() override;
	virtual void StartNewPhysics(float DeltaTime, int32 Iterations) override;
	using Super::SafeMoveUpdatedComponent;
	virtual bool SafeMoveUpdatedComponent(const FVector &Delta, const FQuat &Rotation, bool bSweep, FHitResult &OutHit,
										  ETeleportType Teleport = ETeleportType::None) override;
	UFUNCTION(BlueprintPure, Category = "Mech|Mass Collision")
	FGuLiSoldierId GetMassSupportSoldierId() const { return SupportState.SoldierId; }
	void InitializeRocketFuel(float Fuel) { RocketState.Fuel = Fuel; RocketState.RecoveryElapsed = 0.f; }
	void ClearRocketInput() { bRocketHeld = bRocketRequested = bRocketThrusting = bRocketAllowedThisMove = false; }

#if WITH_DEV_AUTOMATION_TESTS
	FGuLiMassSupportState TestOnly_GetSupport() const { return SupportState; }
	FGuLiGroundMassMoveContext TestOnly_GetMoveContext() const { return LastMoveContext; }
	void TestOnly_Replay(const FGuLiGroundMassMoveContext &Context, float Duration);
	void TestOnly_ApplySupportBaseline(const FGuLiMassSupportState &State) { SupportState = State; }
#endif
  protected:
	virtual void PerformMovement(float DeltaTime) override;
	virtual void MoveAlongFloor(const FVector &InVelocity, float DeltaSeconds,
								FStepDownResult *OutStepDownResult) override;
	virtual void PhysFalling(float DeltaTime, int32 Iterations) override;
	virtual void PhysFlying(float DeltaTime, int32 Iterations) override;
	virtual void PhysCustom(float DeltaTime, int32 Iterations) override;
	virtual bool StepUp(const FVector &GravDir, const FVector &Delta, const FHitResult &Hit,
						FStepDownResult *OutStepDownResult) override;
	virtual bool CanStepUp(const FHitResult &Hit) const override;
	virtual bool IsValidLandingSpot(const FVector &CapsuleLocation, const FHitResult &Hit) const override;
	virtual void SetPostLandedPhysics(const FHitResult &Hit) override;
	virtual void OnMovementModeChanged(EMovementMode PreviousMovementMode, uint8 PreviousCustomMode) override;
	virtual void HandleImpact(const FHitResult &Hit, float TimeSlice = 0.0f,
							  const FVector &MoveDelta = FVector::ZeroVector) override;



  private:
	friend class FGuLiHybridGroundAdapter;
	void RefreshCollisionContext();
	bool IsMassSupportMode() const;
	bool IsMassHit(const FHitResult &Hit) const;
	UGuLiGroundMassContactSubsystem *GetMassContactSubsystem() const;
	FGuLiGroundMassMoveContext CaptureCollisionMove(float Duration);
	void BeginMoveContext(float Duration);
	void EndMoveContext();
	void BeginSweepStep(float Duration);
	void EndSweepStep();
	void SetMassSupport(const FGuLiGroundMassContact &Contact);
	void ClearMassSupport(bool bEnterFalling);
	void ResetMassSupportState();
	void UpdateGroundMechObstacle(float DeltaTime);
	void UnregisterGroundMechObstacle();
	void MarkPresentationContacts();
	UGuLiGroundMechRocketComponent* Rocket() const;
	FVector GetAirTargetVelocity(bool bThrust) const;
	FGuLiRocketMoveState RocketState;
	bool bRocketHeld = false, bRocketRequested = false, bRocketThrusting = false;
	bool bRocketAllowedThisMove = false;
	UPROPERTY()
	FGuLiSoldierId MassSupportSoldierId;
	FGuLiMassSupportState SupportState;
	FGuLiGroundMassMoveContext ActiveMove, LastMoveContext, PreparedMove;
	FGuLiGroundMassContact PendingMassContact;
	double SweepStartSeconds = 0.0;
	float MoveElapsedSeconds = 0.0f, SweepRemainingSeconds = 0.0f, StepDuration = 0.0f, StepStartElapsed = 0.0f;
	float DepenetrationUsed = 0.0f;
	int32 ContactsThisStep = 0;
	bool bMoveContextActive = false, bSweepStepActive = false, bPreparedMove = false;
	bool bReplayingMove = false, bTouchedMass = false;
	uint32 PredictionCacheGeneration = 0u;
	float ObstacleUpdateAccumulator = 0.0f;
	FGuLiDynamicObstacleHandle GroundMechObstacleHandle;
	EGuLiTeam RegisteredObstacleTeam = EGuLiTeam::Unassigned;
	TArray<FGuLiGroundMassBody> CandidateBodies;
};
