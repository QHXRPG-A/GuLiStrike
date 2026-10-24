#pragma once

#include "CoreMinimal.h"
#include "Gameplay/Navigation/Grid/GuLiGridTypes.h"
#include "Gameplay/Navigation/Grid/GuLiGroundGridData.h"

class UGuLiGroundGridData;
struct FGuLiNavigationWorkBudget;

/** Immutable placement obstacle chunk; only static obstacle changes replace it. */
struct FGuLiGridObstacleChunk
{
    uint64 Rows[GuLiGrid::ChunkSide]{};
    uint32 Revision=1;
};

/** Shared terrain data for footprints, square slots and placement queries. */
struct FGuLiGridTerrain
{
    TArray<float> Heights;
    TArray<int32> Clearance;
};
struct GULISTRIKE_API FGuLiGridSnapshot
{
    FIntPoint Cells=FIntPoint::ZeroValue, Chunks=FIntPoint::ZeroValue;
    FVector2D Origin=FVector2D::ZeroVector;
    TSharedPtr<const FGuLiGridTerrain,ESPMode::ThreadSafe> Terrain;
    TConstArrayView<float> Heights;
    TConstArrayView<int32> Clearance;
    uint32 TerrainRevision=1,ObstacleRevision=1;
    FString SourceHash;
    TArray<TSharedPtr<const FGuLiGridObstacleChunk,ESPMode::ThreadSafe>> Obstacles;
    static TSharedPtr<const FGuLiGridSnapshot,ESPMode::ThreadSafe> FromData(const UGuLiGroundGridData& Data);

    bool Contains(FIntPoint P) const;
    int32 Index(FIntPoint P) const { return P.Y*Cells.X+P.X; }
    FIntPoint Point(int32 I) const { return {I%Cells.X,I/Cells.X}; }
    FIntPoint AnchorAt(const FVector& Position,int32 Size) const;
    FVector Center(FIntPoint Anchor,int32 Size) const;
    float HeightAt(double X,double Y) const;
    bool Fits(FIntPoint Anchor,int32 Size) const;
    bool CanStep(FIntPoint A,FIntPoint B,int32 Size) const;
    bool RectangleClear(const GuLiGrid::FRect& Rect) const;
    bool CoveredTerrainClear(const GuLiGrid::FRect& Rect) const;
    bool Project(const FVector& Point,int32 Size,const FVector& Extent,FVector& Out) const;
    bool SegmentClear(const FVector& From,const FVector& To,int32 Size) const;
};
