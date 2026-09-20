// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Battle/Network/GuLiBattleTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "GuLiDynamicObstacleRegistry.generated.h"

enum class EGuLiDynamicObstacleKind : uint8
{
	StaticWorld,
	GroundMech
};

struct GULISTRIKE_API FGuLiDynamicObstacleHandle
{
	uint32 Value = 0u;

	bool IsValid() const { return Value != 0u; }
	void Reset() { Value = 0u; }

	friend bool operator==(const FGuLiDynamicObstacleHandle Lhs, const FGuLiDynamicObstacleHandle Rhs)
	{
		return Lhs.Value == Rhs.Value;
	}
};

struct GULISTRIKE_API FGuLiDynamicObstacle
{
	FGuLiDynamicObstacleHandle Handle;
	FVector Location = FVector::ZeroVector;
	float RadiusCentimeters = 0.0f;
	EGuLiDynamicObstacleKind Kind = EGuLiDynamicObstacleKind::StaticWorld;
	EGuLiTeam Team = EGuLiTeam::Unassigned;
};

DECLARE_MULTICAST_DELEGATE_OneParam(FGuLiDynamicObstaclesChanged, uint32 /* Revision */);

/** Registration and query seam shared by world obstacles and avoidance consumers. */
class GULISTRIKE_API IGuLiDynamicObstacleRegistry
{
public:
	virtual ~IGuLiDynamicObstacleRegistry() = default;
	virtual FGuLiDynamicObstacleHandle RegisterObstacle(const FVector& Location, float RadiusCentimeters) = 0;
	virtual FGuLiDynamicObstacleHandle RegisterObstacle(const FGuLiDynamicObstacle& Obstacle) = 0;
	virtual bool UpdateObstacle(FGuLiDynamicObstacleHandle Handle, const FGuLiDynamicObstacle& Obstacle) = 0;
	virtual void UnregisterObstacle(FGuLiDynamicObstacleHandle Handle) = 0;
	virtual TConstArrayView<FGuLiDynamicObstacle> GetObstacles() const = 0;
	virtual uint32 GetRevision() const = 0;
	virtual FGuLiDynamicObstaclesChanged& OnObstaclesChanged() = 0;
};

/** World-local registry; obstacle producers and navigation consumers never depend on each other. */
UCLASS()
class GULISTRIKE_API UGuLiDynamicObstacleRegistrySubsystem final
	: public UWorldSubsystem
	, public IGuLiDynamicObstacleRegistry
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Deinitialize() override;

	virtual FGuLiDynamicObstacleHandle RegisterObstacle(
		const FVector& Location,
		float RadiusCentimeters) override;
	virtual FGuLiDynamicObstacleHandle RegisterObstacle(const FGuLiDynamicObstacle& Obstacle) override;
	virtual bool UpdateObstacle(
		FGuLiDynamicObstacleHandle Handle,
		const FGuLiDynamicObstacle& Obstacle) override;
	virtual void UnregisterObstacle(FGuLiDynamicObstacleHandle Handle) override;
	virtual TConstArrayView<FGuLiDynamicObstacle> GetObstacles() const override { return Obstacles; }
	virtual uint32 GetRevision() const override { return Revision; }
	virtual FGuLiDynamicObstaclesChanged& OnObstaclesChanged() override { return ObstaclesChanged; }

private:
	TArray<FGuLiDynamicObstacle> Obstacles;
	TMap<uint32, int32> IndexByHandle;
	FGuLiDynamicObstaclesChanged ObstaclesChanged;
	uint32 NextHandle = 1u;
	uint32 Revision = 0u;

	void PublishChange();
};
