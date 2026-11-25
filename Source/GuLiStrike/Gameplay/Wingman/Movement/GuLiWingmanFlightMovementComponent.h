// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PawnMovementComponent.h"
#include "Gameplay/Wingman/Movement/GuLiWingmanSteering.h"
#include "Gameplay/Wingman/Movement/GuLiWingmanFlightCollision.h"
#include "GuLiWingmanFlightMovementComponent.generated.h"

class AGuLiWingmanPawn;
class AActor;

/**
 * Sole owner-simulation writer for a Wingman Pawn's Transform and velocity.
 * Component ticking is disabled; UGuLiWingmanSimulationSubsystem invokes it
 * at 30 Hz in complete-member identity order.
 */
UCLASS(ClassGroup=(Movement), meta=(BlueprintSpawnableComponent))
class GULISTRIKE_API UGuLiWingmanFlightMovementComponent final
	: public UPawnMovementComponent
{
	GENERATED_BODY()
    friend class UGuLiWingmanSimulationSubsystem;

public:
	UGuLiWingmanFlightMovementComponent();

	void InitializeForOwner(AGuLiWingmanPawn& InPawn);
	void AdvanceFixedSteps(float FrameDeltaSeconds);
	/** Compatibility-only application of an explicit v13 server rebase response. */
	void ApplyAuthorityRebase(const FTransform& Transform, const FVector& InitialVelocity);
	void ResetRecovery();

	FVector GetFlightVelocity() const;
	uint32 GetCaptureSimulationTick() const;
	bool IsInEmergencyRecovery() const;
	bool IsAwaitingAuthorityRebase() const;

private:
	void SimulateFixedStep(float StepSeconds);
	FVector BuildRequestedVelocity() const;
	bool TryMoveAlong(const FVector& Direction, float Speed, float StepSeconds,
		FHitResult& OutHit);
	void ApplyReposition(const FTransform& Transform, const FVector& InitialVelocity);
	void UpdateOwnerPresentation(float Alpha);
	TWeakObjectPtr<AGuLiWingmanPawn> WingmanPawn;
	TArray<GuLiWingmanFlightCollision::FHull> Hulls;
	FTransform PreviousSimulationTransform = FTransform::Identity;
	FTransform CurrentSimulationTransform = FTransform::Identity;
};
