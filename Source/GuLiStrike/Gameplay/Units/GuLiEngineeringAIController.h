#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "Navigation/CrowdFollowingComponent.h"
#include "GuLiEngineeringAIController.generated.h"

class ACharacter;

/** Ground steering only. Mining, construction and transit retain their own tasks. */
UCLASS(Config=Engine)
class GULISTRIKE_API UGuLiEngineeringCrowdFollowingComponent : public UCrowdFollowingComponent
{
	GENERATED_BODY()
public:
	UGuLiEngineeringCrowdFollowingComponent(const FObjectInitializer& Initializer = FObjectInitializer::Get());
	void ConfigureFootprint(const FBox& LocalBounds);
	void RefreshParticipation();
	virtual void GetCrowdAgentCollisions(float& Radius, float& HalfHeight) const override;
	virtual FVector GetCrowdAgentVelocity() const override;
	virtual void OnPathfindingQuery(FPathFindingQuery& Query) override;
	virtual void OnPathFinished(const FPathFollowingResult& Result) override;
	UFUNCTION(BlueprintPure, Category="Engineering|Navigation") FString GetAvoidanceDebug() const;
private:
	UPROPERTY(Config) float PredictionDistance = 2400;
	UPROPERTY(Config) float OptimizationDistance = 6000;
	UPROPERTY(Config) float VehicleSeparationWeight = 2;
	float FootprintRadius = 130;
	bool bPausedForControl = false;
	void ChangeParticipation(ECrowdSimulationState Desired);
};

/** Shared by both engineering vehicles; no resource or construction dependencies. */
UCLASS(NotPlaceable)
class GULISTRIKE_API AGuLiEngineeringAIController : public AAIController
{
	GENERATED_BODY()
public:
	AGuLiEngineeringAIController(const FObjectInitializer& Initializer = FObjectInitializer::Get());
	void ConfigureVehicleNavigation();
protected:
	virtual void OnPossess(APawn* InPawn) override;
	virtual void OnUnPossess() override;
private:
	void RefreshParticipation();
	UFUNCTION() void OnVehicleMovementModeChanged(ACharacter* VehicleCharacter, EMovementMode PreviousMode, uint8 PreviousCustomMode);
};
