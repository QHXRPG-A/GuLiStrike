#pragma once
#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Gameplay/Navigation/Grid/GuLiGridNavigation.h"
#include "Gameplay/Navigation/Grid/GuLiGridTraffic.h"
#include "Gameplay/Navigation/Grid/GuLiGridBirths.h"
#include "Commander/Mass/Navigation/GuLiNavigationWorkBudget.h"
#include "GuLiGroundGridSubsystem.generated.h"

class UGuLiGroundGridData;
class FGuLiGridNetwork;
class FGuLiGridSlots;

/** World footprint service: static snapshot, square slots, placement occupancy and shared planning budget. */
UCLASS()
class GULISTRIKE_API UGuLiGroundGridSubsystem final : public UWorldSubsystem
{
    GENERATED_BODY()
public:
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;
    bool IsReady() const { return Current.IsValid(); }
    TSharedPtr<const FGuLiGridSnapshot,ESPMode::ThreadSafe> Snapshot() const { return Current; }
    bool TryGetAuthoredDesignBounds(FBox2D& OutBounds) const;
    FGuLiNavigationWorkBudget& FrameBudget();
    void Advance(FGuLiNavigationWorkBudget& Budget);
    TSharedPtr<FGuLiGridSlots> CreateSlotWorkspace();
    void SetStaticObstacle(const FGuLiHybridObjectKey& Key,const GuLiGrid::FRect& Rect);
    void RemoveStaticObstacle(const FGuLiHybridObjectKey& Key);
    /** Coalesce contribution changes into one immutable placement snapshot. */
    struct FScopedObstacleBatch
    {
        explicit FScopedObstacleBatch(UGuLiGroundGridSubsystem* InGrid);
        ~FScopedObstacleBatch();
        FScopedObstacleBatch(const FScopedObstacleBatch&) = delete;
        FScopedObstacleBatch& operator=(const FScopedObstacleBatch&) = delete;
        UGuLiGroundGridSubsystem* Grid;
    };
    FGuLiGridTraffic& Traffic() { return TrafficLedger; }
    TSharedPtr<FGuLiGridNetwork> Network() const { return NetworkState; }
    TMap<FGuLiHybridObjectKey,FGuLiGridBirthBatch> BirthPlans;
    TArray<FGuLiHybridObjectKey> BirthKeys;
    int32 BirthCursor=0;
private:
    UPROPERTY() TObjectPtr<UGuLiGroundGridData> Data;
    TSharedPtr<const FGuLiGridSnapshot,ESPMode::ThreadSafe> Current;
    TMap<FGuLiHybridObjectKey,GuLiGrid::FRect> StaticObjects;
    TMap<int32,TArray<uint16>> ObstacleCounts;
    FGuLiGridTraffic TrafficLedger;
    TSharedPtr<FGuLiGridNetwork> NetworkState;
    FGuLiNavigationWorkBudget Budget;
    uint64 BudgetFrame=MAX_uint64;
    uint32 ObstacleRevision=1;
    int32 ObstacleBatchDepth=0;
    TSet<int32> BatchedObstacleChunks;
    TSharedRef<int32> SlotWorkspaceCount=MakeShared<int32>(0);
    void ChangeObstacle(const GuLiGrid::FRect& Rect,int32 Delta,TSet<int32>& Dirty);
    void PublishObstacles(const TSet<int32>& Dirty);
    void OnHybridObject(const FGuLiHybridObjectKey& Key);
};
