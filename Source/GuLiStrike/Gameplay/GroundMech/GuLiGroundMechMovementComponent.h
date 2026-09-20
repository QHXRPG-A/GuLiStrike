#pragma once

#include "CoreMinimal.h"
#include "Gameplay/Navigation/GuLiDynamicObstacleRegistry.h"
#include "Gameplay/Units/GuLiExternalCharacterMovementComponent.h"
#include "Commander/Network/GuLiCommanderTypes.h"
#include "GuLiGroundMechMovementComponent.generated.h"

class UGuLiGroundMassContactSubsystem;

namespace GuLiGroundMechMovement
{
	inline constexpr uint8 MassSupportCustomMode = 1u;
}

/** CharacterMovement extension that collides with data-only Mass cylinders. */
UCLASS()
class GULISTRIKE_API UGuLiGroundMechMovementComponent final
	: public UGuLiExternalCharacterMovementComponent
{
	GENERATED_BODY()

public:
	UGuLiGroundMechMovementComponent(
		const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
	virtual void TickComponent(
		float DeltaTime,
		ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void GetLifetimeReplicatedProps(
		TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void ApplyExternalDisplacement(const FTransform& Transform) override;
	virtual void OnTeleported() override;

	UFUNCTION(BlueprintPure, Category="Mech|Mass Collision")
	FGuLiSoldierId GetMassSupportSoldierId() const { return MassSupportSoldierId; }

protected:
	virtual bool MoveUpdatedComponentImpl(
		const FVector& Delta,
		const FQuat& NewRotation,
		bool bSweep,
		FHitResult* OutHit = nullptr,
		ETeleportType Teleport = ETeleportType::None) override;
	virtual void PhysCustom(float DeltaTime, int32 Iterations) override;
	virtual void OnMovementModeChanged(
		EMovementMode PreviousMovementMode,
		uint8 PreviousCustomMode) override;
	virtual void HandleImpact(
		const FHitResult& Hit,
		float TimeSlice = 0.0f,
		const FVector& MoveDelta = FVector::ZeroVector) override;

private:
	bool IsMassSupportMode() const;
	UGuLiGroundMassContactSubsystem* GetMassContactSubsystem() const;
	void SetMassSupport(const struct FGuLiGroundMassLandingResult& Landing);
	void ClearMassSupport(bool bEnterFalling);
	void ResetMassSupportState();
	void UpdateGroundMechObstacle(float DeltaTime);
	void UnregisterGroundMechObstacle();

	UFUNCTION()
	void OnRep_MassSupportSoldierId();

	UPROPERTY(ReplicatedUsing=OnRep_MassSupportSoldierId)
	FGuLiSoldierId MassSupportSoldierId;

	FVector LastSupportBodyLocation = FVector::ZeroVector;
	float ActiveMovementDeltaSeconds = 0.0f;
	float ObstacleUpdateAccumulator = 0.0f;
	FGuLiDynamicObstacleHandle GroundMechObstacleHandle;
	EGuLiTeam RegisteredObstacleTeam = EGuLiTeam::Unassigned;
	TArray<struct FGuLiGroundMassBody> CandidateBodies;
};
