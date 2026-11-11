#include "Gameplay/Navigation/GuLiCircleAvoidance.h"

namespace GuLiCircleAvoidance
{
namespace
{
bool IsValid(const FCircle& C)
{
    return C.Id && !C.Center.ContainsNaN() && !C.StepDelta.ContainsNaN()
        && FMath::IsFinite(C.Radius) && C.Radius>0 && C.LayerMask
        && FMath::IsFinite(C.Clearance) && C.Clearance>=0 && FMath::IsFinite(C.ContactMargin) && C.ContactMargin>=0
        && FMath::IsFinite(C.MinimumZ) && FMath::IsFinite(C.MaximumZ) && C.MinimumZ<=C.MaximumZ;
}

// Exact ray/disc entry in XY. Existing overlap may escape only while its
// distance increases monotonically; a tangent cannot tunnel through the disc.
bool Entry(const FVector& RelativeStart, const FVector& RelativeDelta, double Radius, bool AllowEscape, double& Time)
{
    const double A=RelativeDelta.SizeSquared2D();
    const double B=RelativeStart.X*RelativeDelta.X+RelativeStart.Y*RelativeDelta.Y;
    const double C=RelativeStart.SizeSquared2D()-Radius*Radius;
    if (C<=0)
    {
        if (AllowEscape && B>=0 && A>UE_DOUBLE_SMALL_NUMBER) return false;
        Time=0.;return true;
    }
    if (A<=UE_DOUBLE_SMALL_NUMBER || B>=0) return false;
    const double Discriminant=B*B-A*C;
    if (Discriminant<=0) return false;
    Time=(-B-FMath::Sqrt(Discriminant))/A;
    return Time>=0. && Time<1.;
}

}

FIntPoint FIndex::Cell(const FVector& P) const
{ return {FMath::FloorToInt32(P.X/CellSize),FMath::FloorToInt32(P.Y/CellSize)}; }

void FIndex::AddToCells(int32 Index)
{
    const auto& C=Circles[Index];const float Support=C.Radius+C.Clearance+C.ContactMargin;const FVector R(Support,Support,0);
    const auto Min=Cell(C.Center-R),Max=Cell(C.Center+R);
    for (int32 X=Min.X;X<=Max.X;++X) for (int32 Y=Min.Y;Y<=Max.Y;++Y)
        Buckets.FindOrAdd({X,Y}).Add(Index);
}

void FIndex::RemoveFromCells(int32 Index)
{
    const auto& C=Circles[Index];const float Support=C.Radius+C.Clearance+C.ContactMargin;const FVector R(Support,Support,0);
    const auto Min=Cell(C.Center-R),Max=Cell(C.Center+R);
    for (int32 X=Min.X;X<=Max.X;++X) for (int32 Y=Min.Y;Y<=Max.Y;++Y)
        if (auto* Bucket=Buckets.Find({X,Y}))
        {
            Bucket->RemoveSingleSwap(Index,EAllowShrinking::No);
            if (Bucket->IsEmpty()) Buckets.Remove({X,Y});
        }
}

void FIndex::Rebuild(TConstArrayView<FCircle> Input)
{
    Circles.Reset(Input.Num());FreeSlots.Reset();ById.Reset();Buckets.Reset();MaximumStepDistance=0;
    for (const auto& C:Input) Upsert(C);
}

void FIndex::Upsert(const FCircle& C)
{
    if (!ensure(IsValid(C))) return;
    int32 Index;
    if (const int32* Existing=ById.Find(C.Id))
    {
        Index=*Existing;const auto& Previous=Circles[Index];
        const float OldSupport=Previous.Radius+Previous.Clearance+Previous.ContactMargin;
        const float NewSupport=C.Radius+C.Clearance+C.ContactMargin;
        const FVector OldExtent(OldSupport,OldSupport,0),NewExtent(NewSupport,NewSupport,0);
        // Circle positions still update immediately; unchanged bucket coverage
        // needs no remove/reinsert work and yields exactly the same candidates.
        if (Cell(Previous.Center-OldExtent)==Cell(C.Center-NewExtent)
            && Cell(Previous.Center+OldExtent)==Cell(C.Center+NewExtent))
        {
            Circles[Index]=C;
            MaximumStepDistance=FMath::Max(MaximumStepDistance,float(C.StepDelta.Size2D()));return;
        }
        RemoveFromCells(Index);Circles[Index]=C;
    }
    else
    {
        if (!FreeSlots.IsEmpty()) {Index=FreeSlots.Pop(EAllowShrinking::No);Circles[Index]=C;}
        else Index=Circles.Add(C);
        ById.Add(C.Id,Index);
    }
    MaximumStepDistance=FMath::Max(MaximumStepDistance,float(C.StepDelta.Size2D()));
    AddToCells(Index);
}

void FIndex::Remove(uint64 Id)
{
    if (const int32* Found=ById.Find(Id))
    {const int32 Index=*Found;RemoveFromCells(Index);ById.Remove(Id);Circles[Index]={};FreeSlots.Add(Index);}
}

void FIndex::MoveCircle(uint64 Id,const FVector& Center)
{
    const int32* Index=ById.Find(Id);if (!Index) return;
    FCircle Updated=Circles[*Index];
    const float HeightDelta=float(Center.Z-Updated.Center.Z);
    Updated.Center=Center;Updated.MinimumZ+=HeightDelta;Updated.MaximumZ+=HeightDelta;
    Upsert(Updated);
}

const FCircle* FIndex::Find(uint64 Id) const
{const int32* Index=ById.Find(Id);return Index?&Circles[*Index]:nullptr;}

void FIndex::Query(const FVector& From,const FVector& To,float Radius,FQueryScratch& Scratch,FCounters& Counters) const
{
    Scratch.Cells.Reset();Scratch.Indices.Reset();
    const auto Last=Cell(To);auto Current=Cell(From);
    const FVector D=To-From;
    const int32 SX=D.X>0?1:D.X<0?-1:0,SY=D.Y>0?1:D.Y<0?-1:0;
    const double TX=SX?CellSize/FMath::Abs(D.X):DBL_MAX,TY=SY?CellSize/FMath::Abs(D.Y):DBL_MAX;
    double NX=SX?((Current.X+(SX>0?1:0))*CellSize-From.X)/D.X:DBL_MAX;
    double NY=SY?((Current.Y+(SY>0?1:0))*CellSize-From.Y)/D.Y:DBL_MAX;
    const int32 Pad=FMath::CeilToInt(FMath::Max(0.f,Radius)/CellSize);
    // DDA visits the segment corridor, never a diagonal's entire bounding box.
    const int64 Limit=int64(FMath::Abs(Last.X-Current.X))+FMath::Abs(Last.Y-Current.Y)+1;
    for (int64 Step=0;Step<Limit;++Step)
    {
        for (int32 X=-Pad;X<=Pad;++X) for (int32 Y=-Pad;Y<=Pad;++Y)
        {
            const FIntPoint Key=Current+FIntPoint(X,Y);
            if (Scratch.Cells.Contains(Key)) continue;
            Scratch.Cells.Add(Key);++Counters.BucketVisits;
            if (const auto* Bucket=Buckets.Find(Key)) for (int32 Index:*Bucket) Scratch.Indices.Add(Index);
        }
        if (Current==Last) break;
        if (NX<=NY) {Current.X+=SX;NX+=TX;} else {Current.Y+=SY;NY+=TY;}
    }
}

SIZE_T FIndex::GetAllocatedSize() const
{
    SIZE_T Bytes=Circles.GetAllocatedSize()+FreeSlots.GetAllocatedSize()+ById.GetAllocatedSize()+Buckets.GetAllocatedSize();
    for (const auto& Pair:Buckets) Bytes+=Pair.Value.GetAllocatedSize();
    return Bytes;
}

FHit Sweep(const FIndex& Static,const FIndex& Dynamic,const FRequest& R,const FVector& Delta,
    float Radius,bool RelativeMotion,FQueryScratch& Scratch,FCounters& Counters)
{
    FHit Hit;
    auto Visit=[&](const FIndex& Index,bool DynamicIndex)
    {
        const bool Moving=DynamicIndex && RelativeMotion;
        Index.Query(R.Position,R.Position+Delta,Radius+(Moving?Index.GetMaximumStepDistance():0),Scratch,Counters);
        for (int32 I:Scratch.Indices)
        {
            const auto& C=Index.Get(I);
            if (C.Id==R.SelfId || (R.IgnoredSource && C.Source==R.IgnoredSource)
                || !(C.LayerMask&R.LayerMask)) continue;
            // Unit layers are explicitly 2D. Different ground elevations on a
            // slope must not let same-layer circles pass through one another.
            // Static wall/floor ranges still use the navigation body's height.
            if (!DynamicIndex && (C.MaximumZ<R.MinimumZ || C.MinimumZ>R.MaximumZ)) continue;
            ++Counters.CircleChecks;double Time;
            const double PairRadius=Radius+C.Radius+FMath::Max(0.f,C.Clearance-R.Clearance)
                +FMath::Max(0.f,C.ContactMargin-R.ContactMargin);
            if (!Entry(R.Position-C.Center,Delta-(Moving?C.StepDelta:FVector::ZeroVector),PairRadius,R.AllowInitialEscape,Time)) continue;
            if (!Hit.Blocked || Time<Hit.Fraction || (Time==Hit.Fraction && C.Id<Hit.Circle.Id))
            {Hit.Blocked=true;Hit.Fraction=Time;Hit.Circle=C;}
        }
    };
    Visit(Static,false);Visit(Dynamic,true);return Hit;
}

// Port of the web demo's steer + moveAndSlide. Only broad-phase lookup and
// reuse of the selected lookahead contact differ from the original algorithm.
FResult Solve(const FIndex& Static,const FIndex& Dynamic,const FConfig& Config,
    const FRequest& Request,FState& State,FQueryScratch& Scratch,FCounters& Counters)
{
    FRequest R=Request;R.Clearance=Config.Clearance;R.ContactMargin=Config.ContactMargin;
    FResult Result;
    const FVector Forward=R.Direction.GetSafeNormal2D();
    if (Forward.IsNearlyZero() || R.Speed<=0 || R.DeltaSeconds<=0) return Result;
    const double Distance=double(R.Speed)*R.DeltaSeconds;
    const double Reach=R.AllowSteering?FMath::Max(Distance,double(R.Speed)*Config.LookAheadSeconds):Distance;
    const double Scale=Config.Radius/8.5;
    const double ForwardRequired=FMath::Min(6*Scale,Reach);
    const double SideRequired=FMath::Min(9*Scale,Reach);
    const double Epsilon=.01/8.5*Config.Radius;
    const double Skin=.025/8.5*Config.Radius;
    const float Radius=Config.Radius+Config.Clearance+Config.ContactMargin;
    ++Counters.DirectionProbes;
    const FHit Direct=Sweep(Static,Dynamic,R,Forward*Reach,Radius,false,Scratch,Counters);
    const double Clear=Direct.Blocked?Reach*Direct.Fraction:Reach;
    Result.FullLookAheadClear=Clear+Epsilon>=Reach;
    FVector Direction=Forward;
    FHit Contact=Direct;
    if (!R.AllowSteering)
    {
        // Relay the existing movement sweep's first blocker, without another query.
        Result.BlockingCircleId=Direct.Circle.Id;
        Result.YieldDirection=Forward;
    }
    else if (Clear+Epsilon>=ForwardRequired)
    { if (Result.FullLookAheadClear) State.Side=0; }
    else
    {
        struct FChoice
        {
            FVector Direction=FVector::ZeroVector;
            FHit Hit;
            double Cost=0;
            int8 Side=0;
        };
        const int8 Preferred=State.Side?State.Side:(R.SelfId%2?-1:1);
        auto TestSide=[&](int8 Side)
        {
            FChoice Choice;
            for (const FTurn& Turn:Config.Turns)
            {
                const FVector Candidate(Forward.X*Turn.Cos-Forward.Y*Turn.Sin*Side,
                    Forward.Y*Turn.Cos+Forward.X*Turn.Sin*Side,0);
                ++Counters.DirectionProbes;++Counters.SideProbes;
                const FHit Hit=Sweep(Static,Dynamic,R,Candidate*Reach,Radius,false,Scratch,Counters);
                const double Available=Hit.Blocked?Reach*Hit.Fraction:Reach;
                if (Available+Epsilon<SideRequired) continue;
                Choice.Direction=Candidate;Choice.Hit=Hit;Choice.Side=Side;
                Choice.Cost=Turn.Radians-Available/Config.Radius*Config.ClearancePreference
                    -(Preferred==Side?Config.SidePreferenceRadians:0);
                break;
            }
            return Choice;
        };
        const FChoice Left=TestSide(-1),Right=TestSide(1);
        const FChoice& Selected=Left.Side && (!Right.Side || Left.Cost<=Right.Cost)?Left:Right;
        if (Selected.Side)
        {
            Direction=Selected.Direction;Contact=Selected.Hit;State.Side=Selected.Side;
        }
        // Like the Demo, a failed side search can still ask the blocker to yield.
        const int8 YieldSide=Selected.Side?Selected.Side:Preferred;
        Result.BlockingCircleId=Direct.Circle.Id;
        Result.YieldDirection=FVector(Forward.Y*YieldSide,-Forward.X*YieldSide,0);
    }

    // Reach covers this entire step, and no other unit moves during Solve:
    // the selected probe's first hit is also the movement sweep's first hit.
    const double ContactDistance=Contact.Blocked?Reach*Contact.Fraction:Reach;
    const double Travel=ContactDistance<Distance?FMath::Max(0.,ContactDistance-Skin):Distance;
    Result.Delta=Direction*Travel;
    const double Remaining=Distance-ContactDistance;
    if (Contact.Blocked && Remaining>.001*Scale)
    {
        const FVector Normal=(R.Position+Direction*ContactDistance-Contact.Circle.Center).GetSafeNormal2D();
        const double Inward=FVector::DotProduct(Direction,Normal);
        if (Inward<0)
        {
            const FVector Tangent=Direction-Inward*Normal;
            const double TangentLength=Tangent.Size2D();
            if (TangentLength>=.001)
            {
                const FVector SlideDirection=Tangent/TangentLength;
                const double SlideDistance=Remaining*TangentLength;
                FRequest Sliding=R;Sliding.Position+=Result.Delta;
                ++Counters.DirectionProbes;
                const FHit Slide=Sweep(Static,Dynamic,Sliding,SlideDirection*SlideDistance,Radius,false,Scratch,Counters);
                const double SlideClear=Slide.Blocked?SlideDistance*Slide.Fraction:SlideDistance;
                Result.Delta+=SlideDirection*FMath::Max(0.,SlideClear-Skin);
            }
        }
    }
    Result.Blocked=Result.Delta.Size2D()+Epsilon<Distance;
    return Result;
}
}
