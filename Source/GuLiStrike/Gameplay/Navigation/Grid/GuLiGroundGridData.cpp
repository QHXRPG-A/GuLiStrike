#include "Gameplay/Navigation/Grid/GuLiGroundGridData.h"
#include "Gameplay/Navigation/Grid/GuLiGridTypes.h"

bool UGuLiGroundGridData::Validate(FString& Error) const
{
    const int64 Count = int64(Cells.X) * Cells.Y;
    const int64 Vertices = (int64(Cells.X)+1) * (int64(Cells.Y)+1);
    if (SchemaVersion != 1 || Cells.X <= 0 || Cells.Y <= 0 || Count > 4*1024*1024
        || CellSizeCentimeters != GuLiGrid::CellSize || Origin.ContainsNaN() || SourceHash.IsEmpty()
        || Walkable.Num() != Count || Clearance.Num() != Count || VertexHeights.Num() != Vertices)
    { Error = TEXT("Invalid grid schema, dimensions, source hash or buffer lengths."); return false; }
    for (float H : VertexHeights) if (!FMath::IsFinite(H))
    { Error = TEXT("Grid contains a non-finite height."); return false; }
    for (int32 Y=Cells.Y-1; Y>=0; --Y) for (int32 X=Cells.X-1; X>=0; --X)
    {
        const int32 I=Y*Cells.X+X;
        const int32 Expected=!Walkable[I] ? 0 : X+1==Cells.X || Y+1==Cells.Y ? 1
            : 1+FMath::Min3(Clearance[I+1],Clearance[I+Cells.X],Clearance[I+Cells.X+1]);
        if (Walkable[I]>1 || Clearance[I]!=Expected)
        { Error=TEXT("Grid footprint clearance does not match walkability."); return false; }
    }
    TSet<int32> Sizes;
    for (const auto& H:Hierarchies)
    {
        const int32 N=H.EdgeOffsets.Num()-1;
        if (H.Footprint<1 || H.Footprint>64 || Sizes.Contains(H.Footprint) || H.Regions.Num()!=Count
            || N<=0 || N>Count || H.EdgeOffsets[0]!=0 || H.EdgeOffsets.Last()!=H.Neighbours.Num())
        { Error=TEXT("Invalid hierarchy dimensions.");return false; }
        Sizes.Add(H.Footprint);
        for (int32 I=0;I<Count;++I)
            if (H.Regions[I]<-1 || H.Regions[I]>=N || ((Clearance[I]>=H.Footprint)!=(H.Regions[I]>=0)))
            { Error=TEXT("Hierarchy does not match footprint clearance.");return false; }
        for (int32 I=0;I<N;++I)
            if (H.EdgeOffsets[I]>H.EdgeOffsets[I+1] || H.EdgeOffsets[I+1]-H.EdgeOffsets[I]>1024)
            { Error=TEXT("Invalid hierarchy edge range.");return false; }
        for (int32 R:H.Neighbours) if (R<0 || R>=N)
        { Error=TEXT("Invalid hierarchy edge reference.");return false; }
    }
    return true;
}
