#pragma once

#include "CoreMinimal.h"
#include "Navigation/CrowdManager.h"
#include "Navigation/CrowdAgentInterface.h"
#include "GuLiGroundCrowdManager.generated.h"

/** Read-only external bodies. IDs are stable within one snapshot producer. */
struct FGuLiGroundAvoidanceBody
{
	uint32 Id = 0;
	FVector Location = FVector::ZeroVector;
	FVector Velocity = FVector::ZeroVector;
	float Radius = 0;
};

/** Detour observes these bodies but never moves them. */
UCLASS()
class UGuLiGroundCrowdObstacle : public UObject, public ICrowdAgentInterface
{
	GENERATED_BODY()
public:
	FGuLiGroundAvoidanceBody Body;
	virtual FVector GetCrowdAgentLocation() const override { return Body.Location; }
	virtual FVector GetCrowdAgentVelocity() const override { return Body.Velocity; }
	virtual void GetCrowdAgentCollisions(float& Radius, float& HalfHeight) const override;
};

/** Uses the same large-agent NavMesh as engineering MoveTo, not the default 34 cm mesh. */
UCLASS(Config=Engine)
class GULISTRIKE_API UGuLiGroundCrowdManager : public UCrowdManager
{
	GENERATED_BODY()
public:
	UGuLiGroundCrowdManager(const FObjectInitializer& Initializer = FObjectInitializer::Get());
	virtual bool IsSuitableNavData(const ANavigationData& NavData) const override;
	void SetGroundObstacles(TConstArrayView<FGuLiGroundAvoidanceBody> Bodies);
	UFUNCTION(BlueprintPure, Category="Engineering|Navigation") int32 GetGroundObstacleCount() const { return Obstacles.Num(); }
private:
	UPROPERTY(Transient) TMap<uint32, TObjectPtr<UGuLiGroundCrowdObstacle>> Obstacles;
};
