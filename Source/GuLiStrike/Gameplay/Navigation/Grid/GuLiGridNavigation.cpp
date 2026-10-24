#include "Gameplay/Navigation/Grid/GuLiGridNavigation.h"

bool FGuLiGridSnapshot::Contains(FIntPoint P) const
{
    return P.X>=0 && P.Y>=0 && P.X<Cells.X && P.Y<Cells.Y;
}
FIntPoint FGuLiGridSnapshot::AnchorAt(const FVector& P,int32 Size) const
{
    return {FMath::RoundToInt32((P.X-Origin.X)/GuLiGrid::CellSize-Size*.5),
        FMath::RoundToInt32((P.Y-Origin.Y)/GuLiGrid::CellSize-Size*.5)};
}
FVector FGuLiGridSnapshot::Center(FIntPoint P,int32 Size) const
{
    const double X=Origin.X+(P.X+Size*.5)*GuLiGrid::CellSize;
    const double Y=Origin.Y+(P.Y+Size*.5)*GuLiGrid::CellSize;
    return {X,Y,HeightAt(X,Y)};
}
float FGuLiGridSnapshot::HeightAt(double X,double Y) const
{
    if (Cells.X<=0 || Cells.Y<=0 || Heights.IsEmpty()) return 0.f;
    const double U=FMath::Clamp((X-Origin.X)/GuLiGrid::CellSize,0.,double(Cells.X));
    const double V=FMath::Clamp((Y-Origin.Y)/GuLiGrid::CellSize,0.,double(Cells.Y));
    const int32 IX=FMath::Min(FMath::FloorToInt32(U),Cells.X-1),IY=FMath::Min(FMath::FloorToInt32(V),Cells.Y-1);
    const int32 I=IY*(Cells.X+1)+IX;
    const double FX=U-IX,FY=V-IY;
    const double H00=Heights[I],H10=Heights[I+1],H01=Heights[I+Cells.X+1],H11=Heights[I+Cells.X+2];
    // Landscape triangulates each quad along 00 -> 11. Match its physical surface.
    return float(FX>=FY?H00+(H10-H00)*FX+(H11-H10)*FY:H00+(H11-H01)*FX+(H01-H00)*FY);
}
bool FGuLiGridSnapshot::RectangleClear(const GuLiGrid::FRect& R) const
{
    if (!Contains(R.Min) || !Contains(R.Max-FIntPoint(1,1)) || R.Max.X<=R.Min.X || R.Max.Y<=R.Min.Y) return false;
    const int32 LastX=(R.Max.X-1)/GuLiGrid::ChunkSide,LastY=(R.Max.Y-1)/GuLiGrid::ChunkSide;
    for (int32 CY=R.Min.Y/GuLiGrid::ChunkSide;CY<=LastY;++CY)
    for (int32 CX=R.Min.X/GuLiGrid::ChunkSide;CX<=LastX;++CX)
    {
        const int32 Chunk=CY*Chunks.X+CX;
        if (!Obstacles.IsValidIndex(Chunk) || !Obstacles[Chunk]) continue;
        const int32 X0=FMath::Max(0,R.Min.X-CX*64),X1=FMath::Min(64,R.Max.X-CX*64);
        const int32 Y0=FMath::Max(0,R.Min.Y-CY*64),Y1=FMath::Min(64,R.Max.Y-CY*64);
        const uint64 Mask=(~uint64(0)<<X0)&(X1==64?~uint64(0):(uint64(1)<<X1)-1);
        for (int32 Y=Y0;Y<Y1;++Y) if (Obstacles[Chunk]->Rows[Y]&Mask) return false;
    }
    return true;
}
bool FGuLiGridSnapshot::Fits(FIntPoint P,int32 Size) const
{
    return Size>0 && Contains(P) && Clearance[Index(P)]>=Size && RectangleClear(GuLiGrid::FRect::At(P,Size));
}
bool FGuLiGridSnapshot::CanStep(FIntPoint A,FIntPoint B,int32 Size) const
{
    const FIntPoint D=B-A;
    if (FMath::Abs(D.X)>1 || FMath::Abs(D.Y)>1 || !Fits(A,Size) || !Fits(B,Size)) return false;
    // Both orthogonal placements cover the entire diagonal sweep, including large bodies.
    return !D.X || !D.Y || (Fits({A.X,B.Y},Size) && Fits({B.X,A.Y},Size));
}
bool FGuLiGridSnapshot::CoveredTerrainClear(const GuLiGrid::FRect& Rect) const
{
    if (!Contains(Rect.Min) || Rect.Max.X>Cells.X || Rect.Max.Y>Cells.Y || !RectangleClear(Rect)) return false;
    for (int32 Y=Rect.Min.Y;Y<Rect.Max.Y;++Y) for (int32 X=Rect.Min.X;X<Rect.Max.X;++X)
        if (Clearance[Index({X,Y})]<1) return false;
    return true;
}
bool FGuLiGridSnapshot::Project(const FVector& P,int32 Size,const FVector& Extent,FVector& Out) const
{
    if (P.ContainsNaN() || Extent.ContainsNaN() || Size<1 || Cells.X<=0 || Cells.Y<=0) return false;
    const FIntPoint A=AnchorAt(P,Size);
    const int32 RX=FMath::Min(32,FMath::CeilToInt32(FMath::Max(0.,Extent.X)/GuLiGrid::CellSize));
    const int32 RY=FMath::Min(32,FMath::CeilToInt32(FMath::Max(0.,Extent.Y)/GuLiGrid::CellSize));
    double Best=TNumericLimits<double>::Max(); bool bFound=false;
    for (int32 Y=FMath::Max(0,A.Y-RY);Y<=FMath::Min(Cells.Y-Size,A.Y+RY);++Y)
    for (int32 X=FMath::Max(0,A.X-RX);X<=FMath::Min(Cells.X-Size,A.X+RX);++X)
    {
        const FVector Candidate=Center({X,Y},Size),D=Candidate-P;
        if (FMath::Abs(D.X)>FMath::Max(0.,Extent.X) || FMath::Abs(D.Y)>FMath::Max(0.,Extent.Y)
            || FMath::Abs(D.Z)>FMath::Max(0.,Extent.Z) || D.SizeSquared()>=Best || !Fits({X,Y},Size)) continue;
        Out=Candidate; Best=D.SizeSquared(); bFound=true;
    }
    return bFound;
}
bool FGuLiGridSnapshot::SegmentClear(const FVector& From,const FVector& To,int32 Size) const
{
    FIntPoint A=AnchorAt(From,Size); const FIntPoint End=AnchorAt(To,Size);
    if (!Fits(A,Size) || !Fits(End,Size)) return false;
    const int32 DX=FMath::Abs(End.X-A.X),DY=FMath::Abs(End.Y-A.Y);
    const int32 SX=A.X<End.X?1:-1,SY=A.Y<End.Y?1:-1;
    int32 Error=DX-DY;
    for (int32 Remaining=FMath::Max(DX,DY);Remaining>0;--Remaining)
    {
        FIntPoint B=A; const int32 Twice=2*Error;
        if (Twice>-DY) { Error-=DY; B.X+=SX; }
        if (Twice<DX) { Error+=DX; B.Y+=SY; }
        if (!CanStep(A,B,Size)) return false;
        A=B;
    }
    return A==End;
}
