#pragma once

#include "CoreMinimal.h"
#include "Gameplay/Navigation/GuLiDynamicObstacleRegistry.h"
#include "Gameplay/Navigation/GuLiGroundMassCollisionTypes.h"
#include "Gameplay/Units/GuLiExternalCharacterMovementComponent.h"
#include "GuLiGroundMechMovementComponent.generated.h"

class UGuLiGroundMassContactSubsystem;
struct FGuLiGroundMechNetworkStorage;
struct FGuLiGroundMechStorageDeleter
{
	void operator()(FGuLiGroundMechNetworkStorage *Storage) const;
};
class FGuLiGroundMechSavedMove;
struct FGuLiGroundMechMoveResponse;
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
	// Local prediction origin. Corrections rebase this against the acknowledged raw
	// sample so later ordinary moves preserve the authoritative platform offset.
	FVector SourceLocation = FVector::ZeroVector;
	float SourceTopZ = 0.0f;
	bool bHasSourceReference = false;
	bool IsValid() const { return SoldierId.IsValid(); }
	void Serialize(FArchive &Ar);
};

/** CMC prediction and world collision, with explicit timed sweeps against Mass data. */
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
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty> &OutLifetimeProps) const override;
	virtual FNetworkPredictionData_Client *GetPredictionData_Client() const override;
	virtual float GetMaxSpeed() const override;
	virtual float GetMaxBrakingDeceleration() const override;
	virtual void ApplyExternalDisplacement(const FTransform &Transform) override;
	virtual void OnTeleported() override;
	virtual void StartNewPhysics(float DeltaTime, int32 Iterations) override;
	using Super::SafeMoveUpdatedComponent;
	virtual bool SafeMoveUpdatedComponent(const FVector &Delta, const FQuat &Rotation, bool bSweep, FHitResult &OutHit,
										  ETeleportType Teleport = ETeleportType::None) override;
	UFUNCTION(BlueprintPure, Category = "Mech|Mass Collision")
	FGuLiSoldierId GetMassSupportSoldierId() const { return SupportState.SoldierId; }
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
	virtual void ServerMoveHandleClientError(float TimeStamp, float DeltaTime, const FVector &Accel,
											 const FVector &RelativeClientLocation,
											 UPrimitiveComponent *ClientMovementBase, FName ClientBaseBoneName,
											 uint8 ClientMovementMode) override;
	virtual bool ServerCheckClientError(float TimeStamp, float DeltaTime, const FVector &Accel,
										const FVector &ClientWorldLocation, const FVector &RelativeClientLocation,
										UPrimitiveComponent *ClientMovementBase, FName ClientBaseBoneName,
										uint8 ClientMovementMode) override;
	virtual void BeforeValidatedMoveResponse(const FCharacterMoveResponseDataContainer &Response) override;
	virtual void AfterValidatedMoveResponse(const FCharacterMoveResponseDataContainer &Response) override;

  private:
	friend class FGuLiGroundMechSavedMove;
	friend class FGuLiGroundMassNetworkMoveTest;
	friend struct FGuLiGroundMechMoveResponse;
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
	UPROPERTY(Replicated)
	FGuLiSoldierId MassSupportSoldierId;
	FGuLiMassSupportState SupportState;
	FGuLiMassSupportState PendingResponseSupport;
	float PendingResponseTimeStamp = -1.0f;
	FGuLiGroundMassMoveContext ActiveMove, LastMoveContext, PreparedMove;
	FGuLiGroundMassContact PendingMassContact;
	double SweepStartSeconds = 0.0;
	float MoveElapsedSeconds = 0.0f, SweepRemainingSeconds = 0.0f, StepDuration = 0.0f, StepStartElapsed = 0.0f;
	float DepenetrationUsed = 0.0f;
	int32 ContactsThisStep = 0;
	bool bMoveContextActive = false, bSweepStepActive = false, bPreparedMove = false;
	bool bReplayingMove = false, bApplyingCorrection = false, bTouchedMass = false;
	uint32 PredictionCacheGeneration = 0u;
	float ObstacleUpdateAccumulator = 0.0f;
	FGuLiDynamicObstacleHandle GroundMechObstacleHandle;
	EGuLiTeam RegisteredObstacleTeam = EGuLiTeam::Unassigned;
	TArray<FGuLiGroundMassBody> CandidateBodies;
	TUniquePtr<FGuLiGroundMechNetworkStorage, FGuLiGroundMechStorageDeleter> MechNetworkStorage;
};
