#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Gameplay/Units/GuLiGroundCrowdManager.h"
#include "GuLiMassCrowdObstacleSubsystem.generated.h"

/** Bridges authoritative ground Soldiers into avoidance without taking movement ownership. */
UCLASS()
class UGuLiMassCrowdObstacleSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()
public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
private:
	TArray<FGuLiGroundAvoidanceBody> Bodies;
};
