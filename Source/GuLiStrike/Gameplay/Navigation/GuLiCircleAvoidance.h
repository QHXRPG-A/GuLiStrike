#pragma once

#include "CoreMinimal.h"

// Data-only geometry. World ownership, navigation and movement publication stay
// in their existing subsystems; this solver never moves another object.
namespace GuLiCircleAvoidance
{
struct FTurn
{
    explicit FTurn(float Degrees)
        : Radians(FMath::DegreesToRadians(double(Degrees))),
          Cos(FMath::Cos(Radians)), Sin(FMath::Sin(Radians)) {}
    double Radians;
    double Cos;
    double Sin;
};

struct FConfig
{
    float Radius = 150.f;
    float Clearance = 0.f;
    float ContactMargin = 2.f;
    float LookAheadSeconds = .2f;
    // Cached once when the unit catalog loads, never recomputed per movement step.
    FTurn Turns[7] = {FTurn(20),FTurn(37),FTurn(55),FTurn(73),FTurn(91),FTurn(111),FTurn(134)};
    float ClearancePreference = .0425f; // Demo .005 per pixel, normalized by its 8.5px radius.
    float SidePreferenceRadians = .38f;
    float YieldSeconds = .5f;
};

struct FCircle
{
    uint64 Source = 0;
    uint64 Id = 0;
    uint64 Version = 0;
    FVector Center = FVector::ZeroVector;
    float Radius = 0;
    float Clearance = 0;
    float ContactMargin = 0;
    float MinimumZ = -FLT_MAX;
    float MaximumZ = FLT_MAX;
    uint8 LayerMask = 3;
    // Optional movement trace used by the performance observer, not gameplay submission.
    FVector StepDelta = FVector::ZeroVector;
};

struct FCounters
{
    uint64 BucketVisits = 0;
    uint64 CircleChecks = 0;
    uint32 DirectionProbes = 0;
    uint32 SideProbes = 0;
};

struct FQueryScratch
{
    TSet<FIntPoint> Cells;
    TSet<int32> Indices;
};

class FIndex
{
public:
    explicit FIndex(float InCellSize = 600.f) : CellSize(FMath::Max(1.f, InCellSize)) {}
    void Reset(int32 ExpectedCircles = 0);
    void Rebuild(TConstArrayView<FCircle> Input);
    void Upsert(const FCircle& Circle);
    void Remove(uint64 Id);
    void MoveCircle(uint64 Id, const FVector& Center);
    const FCircle* Find(uint64 Id) const;
    void Query(const FVector& From, const FVector& To, float Radius,
        FQueryScratch& Scratch, FCounters& Counters) const;
    const FCircle& Get(int32 Index) const { return Circles[Index]; }
    SIZE_T GetAllocatedSize() const;
    float GetMaximumStepDistance() const { return MaximumStepDistance; }
    int32 Num() const { return ById.Num(); }
private:
    using FBucket = TArray<int32, TInlineAllocator<8>>;
    float CellSize;
    float MaximumStepDistance = 0;
    TArray<FCircle> Circles;
    TArray<int32> FreeSlots;
    TMap<uint64,int32> ById;
    TMap<FIntPoint,FBucket> Buckets;
    FIntPoint Cell(const FVector& P) const;
    void AddToCells(int32 Index);
    void RemoveFromCells(int32 Index);
};

struct FHit
{
    bool Blocked = false;
    double Fraction = 1.;
    FCircle Circle;
};

struct FState
{
    int8 Side = 0;
};

struct FRequest
{
    uint64 IgnoredSource = 0;
    uint64 SelfId = 0;
    FVector Position = FVector::ZeroVector;
    FVector Direction = FVector::ZeroVector;
    float Speed = 0;
    float DeltaSeconds = .1f;
    float MinimumZ = -FLT_MAX;
    float MaximumZ = FLT_MAX;
    uint8 LayerMask = 1;
    float Clearance = 0;
    float ContactMargin = 0;
    bool AllowInitialEscape = true;
    bool AllowSteering = true; // A yielding unit keeps its direction and only moves/slides.
};

struct FResult
{
    FVector Delta = FVector::ZeroVector;
    bool Blocked = false;
    bool FullLookAheadClear = false;
    uint64 BlockingCircleId = 0;
    uint64 SlideBlockingCircleId = 0; // Existing slide contact, for a yield relay's second candidate.
    FVector YieldDirection = FVector::ZeroVector;
};

FHit Sweep(const FIndex& Static, const FIndex& Dynamic, const FRequest& Request,
    const FVector& Delta, float Radius, bool RelativeMotion,
    FQueryScratch& Scratch, FCounters& Counters);

// Compare both sides on entry, then keep the chosen side while it is passable.
// Move and slide, publishing the NavMesh-constrained position before
// the next unit. The chosen lookahead hit also supplies the movement contact.
FResult Solve(const FIndex& Static, const FIndex& Dynamic, const FConfig& Config,
    const FRequest& Request, FState& State, FQueryScratch& Scratch, FCounters& Counters);

}
