// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Gameplay/Units/GuLiUnitMovementLayer.h"
#include "Battle/Network/GuLiBattleTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "GuLiDynamicObstacleRegistry.generated.h"

class ANavigationData;

enum class EGuLiDynamicObstacleKind : uint8
{
	StaticWorld,
	GroundMech
};

enum class EGuLiObstacleUpdateResult : uint8
{
	Updated,
	Unchanged,
	NotFound,
	InvalidData
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
    bool IgnoresLayer(EGuLiUnitMovementLayer Layer) const {return Kind==EGuLiDynamicObstacleKind::GroundMech && Layer==EGuLiUnitMovementLayer::LowAir;}
	EGuLiTeam Team = EGuLiTeam::Unassigned;
	float HalfHeight = 0.0f;
};

/** Initial convex collision footprint; shared by navigation, avoidance and placement. */
struct GULISTRIKE_API FGuLiOreNodeObstacle
{
	uint32 NodeId = 0;
	uint16 ClusterId = 0;
	FGuLiDynamicObstacle Obstacle;
	float HalfHeight = 0;
};

struct FGuLiOreObstacleChunk
{
	TArray<FGuLiOreNodeObstacle> Nodes;
	uint32 AliveMask = 0;
};

struct FGuLiOreObstacleTopology
{
	TMap<FIntPoint,TArray<int32>> Cells;
	TArray<FIntPoint> NodeAddresses;
};

/** Immutable static layer. Only ore changes copy affected cluster chunks; motion shares it unchanged. */
struct GULISTRIKE_API FGuLiOreObstacleIndex
{
	uint64 Revision = 0;
	int32 AliveCount = 0;
	TSharedPtr<const FGuLiOreObstacleTopology,ESPMode::ThreadSafe> Topology;
	TArray<TSharedPtr<const FGuLiOreObstacleChunk,ESPMode::ThreadSafe>> Chunks;
	const FGuLiOreNodeObstacle* Find(uint32 NodeId) const;
	void Query(const FVector& Position,float Radius,TArray<uint32>& Out) const;
};

/** Exact local dependencies, including empty cells an obstacle can subsequently enter. */
struct FGuLiObstacleRegionStamp
{
	TMap<FIntPoint, uint64> Cells;
	uint32 SnapshotRevision = 0;
	bool bComplete = true;
};

/** Immutable game-thread publication shared by movement and predictive avoidance. */
struct GULISTRIKE_API FGuLiDynamicObstacleSnapshot
{
	static constexpr float CellSize=600.f;
	uint32 Revision=0;
	TArray<FGuLiDynamicObstacle> Obstacles;
	TMap<FIntPoint,TArray<int32>> Cells;
	TMap<uint32,int32> ByHandle;
	TMap<FIntPoint,uint64> CellVersions;
	TSharedPtr<const FGuLiOreObstacleIndex,ESPMode::ThreadSafe> Ore;
	/** Query indices encode static ore separately; consumers must use these accessors. */
	const FGuLiDynamicObstacle& GetObstacle(int32 QueryIndex) const;
	const FGuLiDynamicObstacle* FindObstacle(uint32 Handle) const;
	FGuLiObstacleRegionStamp CaptureRegion(const FVector& From, const FVector& To, float Radius) const;
	bool IsRegionCurrent(const FGuLiObstacleRegionStamp& Stamp) const;
	static FIntPoint Cell(const FVector& P) { return {FMath::FloorToInt32(P.X/CellSize),FMath::FloorToInt32(P.Y/CellSize)}; }
	void Query(const FVector& Position,float Radius,TArray<int32>& Out) const;
	bool IsSegmentClear(const FVector& From,const FVector& To,float Radius,bool bAllowEscape=false,EGuLiUnitMovementLayer Layer=EGuLiUnitMovementLayer::Ground) const;
};

DECLARE_MULTICAST_DELEGATE_OneParam(FGuLiDynamicObstaclesChanged, uint32 /* Revision */);
DECLARE_MULTICAST_DELEGATE_OneParam(FGuLiStaticObstacleRegionChanged, const FBox&);

/** Registration and query seam shared by world obstacles and avoidance consumers. */
class GULISTRIKE_API IGuLiDynamicObstacleRegistry
{
public:
	virtual ~IGuLiDynamicObstacleRegistry() = default;
	virtual FGuLiDynamicObstacleHandle RegisterObstacle(const FVector& Location, float RadiusCentimeters) = 0;
	virtual FGuLiDynamicObstacleHandle RegisterObstacle(const FGuLiDynamicObstacle& Obstacle) = 0;
	virtual EGuLiObstacleUpdateResult UpdateObstacle(FGuLiDynamicObstacleHandle Handle, const FGuLiDynamicObstacle& Obstacle) = 0;
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
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;

	virtual FGuLiDynamicObstacleHandle RegisterObstacle(
		const FVector& Location,
		float RadiusCentimeters) override;
	virtual FGuLiDynamicObstacleHandle RegisterObstacle(const FGuLiDynamicObstacle& Obstacle) override;
	virtual EGuLiObstacleUpdateResult UpdateObstacle(
		FGuLiDynamicObstacleHandle Handle,
		const FGuLiDynamicObstacle& Obstacle) override;
	virtual void UnregisterObstacle(FGuLiDynamicObstacleHandle Handle) override;
	virtual TConstArrayView<FGuLiDynamicObstacle> GetObstacles() const override { return Obstacles; }
	virtual uint32 GetRevision() const override { return Revision; }
	TSharedRef<const FGuLiDynamicObstacleSnapshot,ESPMode::ThreadSafe> GetSnapshot();
	virtual FGuLiDynamicObstaclesChanged& OnObstaclesChanged() override { return ObstaclesChanged; }
	FGuLiStaticObstacleRegionChanged StaticRegionChanged;
	uint32 GetStaticRevision() const { return StaticRevision; }
	bool HasStaticChangesSince(uint32 Since, const FBox& Bounds) const;
	/** Invalidate nearby route/work-position caches when existing world geometry changes. */
	void NotifyStaticGeometryChanged(const FBox& Bounds);
	void InitializeOreObstacles(TConstArrayView<FGuLiOreNodeObstacle> Nodes,TConstArrayView<uint8> Remaining);
	void RemoveOreObstacles(TConstArrayView<uint32> NodeIds);
	void ClearOreObstacles();
	int32 GetAliveOreCount() const { return OreIndex ? OreIndex->AliveCount : 0; }
	uint64 GetOrePublicationCount() const { return OrePublications; }
	bool IsOpeningStaticRegion() const { return bOpeningStaticRegion; }
	void FlushOreNavigationNotifications();

private:
	TSharedPtr<const FGuLiDynamicObstacleSnapshot,ESPMode::ThreadSafe> Snapshot;
	TSharedPtr<const FGuLiOreObstacleIndex,ESPMode::ThreadSafe> OreIndex;
	uint64 OrePublications = 0;
	bool bOpeningStaticRegion=false;
	bool bOreNavigationFinished=false;
	TMap<int32,FBox> PendingOreNavigationRegions;
	void NotifyStaticGeometryOpened(const FBox& Bounds);
	UFUNCTION() void HandleOreNavigationFinished(ANavigationData* NavigationData);
	TArray<FGuLiDynamicObstacle> Obstacles;
	TMap<uint32, int32> IndexByHandle;
	FGuLiDynamicObstaclesChanged ObstaclesChanged;
	uint32 NextHandle = 1u;
	uint32 Revision = 0u;
	uint32 StaticRevision = 0;
	uint64 NextCellVersion = 0;
	TMap<FIntPoint,uint64> CellVersions;
	void DirtyObstacleCells(const FGuLiDynamicObstacle& Obstacle);
	struct FRegionChange { uint32 Revision; FBox Bounds; };
	TArray<FRegionChange> RegionChanges;
	void PublishStaticRegion(const FGuLiDynamicObstacle& Obstacle);

	void PublishChange();
};
