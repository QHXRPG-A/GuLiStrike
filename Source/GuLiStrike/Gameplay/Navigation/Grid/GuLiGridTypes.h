#pragma once

#include "CoreMinimal.h"

namespace GuLiGrid
{
inline constexpr double CellSize = 300.0;
inline constexpr int32 ChunkSide = 64;
// Existing forced-landing timeout; ordinary Mass movement uses its own recovery policy.
inline constexpr double BlockedTimeout = 3.0;

/** Quantization is based on the authored collision diameter, never the visual bounds. */
inline int32 FootprintFromDiameter(double Diameter)
{
    return FMath::IsFinite(Diameter) && Diameter > 0
        ? FMath::Max(1, FMath::FloorToInt32(Diameter / CellSize + 1.e-6)) : 1;
}
inline double QuantizedDiameter(double Diameter) { return FootprintFromDiameter(Diameter) * CellSize; }
inline double ScaleFromDiameter(double Diameter)
{
    return FMath::IsFinite(Diameter) && Diameter > 0 ? QuantizedDiameter(Diameter) / Diameter : 1.0;
}

enum class EMoveState : uint8 { Parked, Planning, Moving, Waiting, Yielding, PausedWork };
enum class ETrafficClass : uint8 { Automatic, Commander, DirectPlayer };

/** The accepted command sequence survives retries and ownership transfers. */
struct FTrafficPriority
{
    ETrafficClass Class = ETrafficClass::Automatic;
    uint64 AcceptedSequence = 0;
    FGuid Command;

    int32 Compare(const FTrafficPriority& Other) const
    {
        if (Class != Other.Class) return Class > Other.Class ? 1 : -1;
        if (AcceptedSequence != Other.AcceptedSequence) return AcceptedSequence > Other.AcceptedSequence ? 1 : -1;
        if (Command == Other.Command) return 0;
        return Command < Other.Command ? 1 : -1;
    }
};

/** Legacy serialized movement state. Current movement and blockage are owned by Mass. */
struct FMoveState
{
    EMoveState State = EMoveState::Parked;
    FIntPoint Anchor = FIntPoint::ZeroValue;
    FIntPoint Target = FIntPoint::ZeroValue;
    FIntPoint StepTarget = FIntPoint::ZeroValue;
    int32 Footprint = 1;
    double BlockedSeconds = 0;
    double RetryAt = 0;
    bool bStepCommitted = false;
    bool bUserStopped = false;
    FTrafficPriority Priority;

};

/** Half-open occupied rectangle. Odd and even footprints share the same cell boundaries. */
struct FRect
{
    FIntPoint Min = FIntPoint::ZeroValue;
    FIntPoint Max = FIntPoint::ZeroValue;
    bool Overlaps(const FRect& B) const
    {
        return Min.X < B.Max.X && B.Min.X < Max.X && Min.Y < B.Max.Y && B.Min.Y < Max.Y;
    }
    bool Contains(const FRect& B) const { return Min.X<=B.Min.X && Min.Y<=B.Min.Y && Max.X>=B.Max.X && Max.Y>=B.Max.Y; }
    static FRect Union(const FRect& A,const FRect& B)
    { return {{FMath::Min(A.Min.X,B.Min.X),FMath::Min(A.Min.Y,B.Min.Y)},
        {FMath::Max(A.Max.X,B.Max.X),FMath::Max(A.Max.Y,B.Max.Y)}}; }
    static FRect At(FIntPoint Anchor, int32 Size) { return {Anchor, Anchor + FIntPoint(Size, Size)}; }
    static FRect Sweep(FIntPoint A, FIntPoint B, int32 Size)
    {
        return {{FMath::Min(A.X,B.X),FMath::Min(A.Y,B.Y)},
            {FMath::Max(A.X,B.X)+Size,FMath::Max(A.Y,B.Y)+Size}};
    }
};
}
