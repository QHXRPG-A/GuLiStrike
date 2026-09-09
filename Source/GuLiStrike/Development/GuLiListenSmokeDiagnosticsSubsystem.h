// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Battle/Contracts/GuLiWingmanProtocolTypes.h"
#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "GuLiListenSmokeDiagnosticsSubsystem.generated.h"

class AGuLiStrikeShip;

struct FGuLiListenSmokeMemberObservation
{
	FVector LowDisplacementAnchor = FVector::ZeroVector;
	double LowDisplacementStartWallSeconds = 0.0;
	double MaximumLowDisplacementSeconds = 0.0;
	bool bHasLowDisplacementAnchor = false;
	bool bCurrentStallRecorded = false;
};

struct FGuLiListenSmokeSlotAttackObservation
{
	FGuLiWingmanHandle LastObservedMember;
	uint32 LastObservedAttackRunId = 0u;
	uint32 ObservedAttackRunCount = 0u;
};

/**
 * Explicit-command-line, non-Shipping diagnostics for a real two-process Listen smoke.
 *
 * The subsystem is inert unless -GuLiListenSmoke is present. It samples
 * networking/Wingman state once per second and asks the engine to exit after the
 * requested duration. An explicit -GuLiListenSmokeMoveShip flag feeds ordinary
 * local Ship movement inputs for the moving-carrier gate. It never submits Relay
 * traffic and never writes a Wingman transform.
 */
UCLASS()
class GULISTRIKE_API UGuLiListenSmokeDiagnosticsSubsystem final : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

private:
	void DriveLocalShip(float DeltaSeconds);
	void UpdateShipMotionObservation();
	void EmitSample(bool bFinalSample);

	double StartedWallSeconds = 0.0;
	double NextSampleWallSeconds = 0.0;
	double DurationSeconds = 30.0;
	FString RequestedRole;
	FString RunId;
	TWeakObjectPtr<AGuLiStrikeShip> DrivenShip;
	FTransform DrivenShipStartTransform = FTransform::Identity;
	TMap<FGuLiWingmanHandle, FGuLiListenSmokeMemberObservation> MemberObservations;
	TMap<uint8, FGuLiListenSmokeSlotAttackObservation> SlotAttackObservations;
	uint32 MotionStallViolationCount = 0u;
	double MaximumLowDisplacementSeconds = 0.0;
	float ShipTranslationFromStartCentimeters = 0.0f;
	float MaximumShipTranslationCentimeters = 0.0f;
	float ShipRotationFromStartDegrees = 0.0f;
	float MaximumShipRotationDegrees = 0.0f;
	bool bShipMotionDriverEnabled = false;
	bool bBoundedShipMotionDriverEnabled = false;
	bool bShipMotionDriverActive = false;
	bool bHasDrivenShipStartTransform = false;
	bool bFinalSampleEmitted = false;
};
