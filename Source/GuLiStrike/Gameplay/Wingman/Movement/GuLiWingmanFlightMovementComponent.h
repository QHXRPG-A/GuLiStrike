// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PawnMovementComponent.h"
#include "Gameplay/Wingman/Movement/GuLiWingmanSteering.h"
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

public:
	UGuLiWingmanFlightMovementComponent();

	void InitializeForOwner(AGuLiWingmanPawn& InPawn);
	/** Keeps local collision prediction and SafeMove aligned with the server's carrier ignore. */
	void SetCarrierActor(AActor* InCarrierActor);
	void AdvanceFixedSteps(float FrameDeltaSeconds, bool bBypassNavigationForTests);
	/** Compatibility-only application of an explicit v13 server rebase response. */
	void ApplyAuthorityRebase(const FTransform& Transform, const FVector& InitialVelocity);
	void ResetRecovery();

	FVector GetFlightVelocity() const;
	uint32 GetCaptureSimulationTick() const;
	bool IsInEmergencyRecovery() const;
	bool IsAwaitingAuthorityRebase() const;

#if WITH_DEV_AUTOMATION_TESTS
	/** Test seam for the client-only failsafe; production enters it after 0.5 s. */
	bool PerformLocalDeadlockRecoveryForTests(bool bBypassNavigationForTests = true)
	{
		return PerformLocalDeadlockRecovery(bBypassNavigationForTests);
	}
#endif

private:
	void SimulateFixedStep(float StepSeconds, bool bBypassNavigationForTests);
	FVector BuildRequestedVelocity() const;
	GuLiWingmanSteering::FHeadingProbeResult ProbeHeading(
		const FVector& Start, const FVector& Direction, float Distance,
		float AgentRadius, bool bBypassNavigationForTests) const;
	bool TryMoveAlong(const FVector& Direction, float Speed, float StepSeconds,
		FHitResult& OutHit);
	bool PerformLocalDeadlockRecovery(bool bBypassNavigationForTests);
	void ApplyReposition(const FTransform& Transform, const FVector& InitialVelocity);
	void UpdateOwnerPresentation(float Alpha);
	void UpdateProgressState(float StepSeconds, float MovedDistance,
		bool bHadSafeHeading, bool bBlockedByCarrierBoundary);

	TWeakObjectPtr<AGuLiWingmanPawn> WingmanPawn;
	TWeakObjectPtr<AActor> CarrierActor;
	FTransform PreviousSimulationTransform = FTransform::Identity;
	FTransform CurrentSimulationTransform = FTransform::Identity;
};
