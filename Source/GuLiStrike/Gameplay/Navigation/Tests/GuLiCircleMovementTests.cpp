#include "Gameplay/Navigation/GuLiCircleAvoidance.h"
#include "Gameplay/Navigation/GuLiOreCircleClustering.h"
#include "Gameplay/Navigation/GuLiStaticCircleStore.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
using namespace GuLiCircleAvoidance;
namespace
{
FCircle Body(uint64 Id,FVector Position,float Radius=10,uint8 Layer=1)
{FCircle C;C.Id=Id;C.Center=Position;C.Radius=Radius;C.LayerMask=Layer;C.MinimumZ=0;C.MaximumZ=100;return C;}
FRequest Move(uint64 Id,FVector Position,FVector Direction,float Speed=100)
{FRequest R;R.SelfId=Id;R.Position=Position;R.Direction=Direction;R.Speed=Speed;R.MinimumZ=0;R.MaximumZ=100;return R;}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiCircleSweepContract,"GuLiStrike.Navigation.Circles.SweepAndLayers",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FGuLiCircleSweepContract::RunTest(const FString&)
{
    FIndex Static,Dynamic;FQueryScratch Scratch;FCounters Count;
    Static.Upsert(Body(99,FVector(100,0,0)));
    auto R=Move(1,FVector::ZeroVector,FVector::ForwardVector);
    auto H=Sweep(Static,Dynamic,R,FVector(200,0,0),10,false,Scratch,Count);
    TestTrue(TEXT("Fast sweep cannot tunnel"),H.Blocked);
    TestTrue(TEXT("Contact uses summed radii"),FMath::IsNearlyEqual(H.Fraction,.4,1.e-6));
    R.LayerMask=2;
    TestFalse(TEXT("Ground does not block low air"),Sweep(Static,Dynamic,R,FVector(200,0,0),10,false,Scratch,Count).Blocked);
    auto Both=Body(99,FVector(100,0,0),10,3);Static.Upsert(Both);
    TestTrue(TEXT("Building blocks both layers"),Sweep(Static,Dynamic,R,FVector(200,0,0),10,false,Scratch,Count).Blocked);
    R.Position=FVector(105,0,0);R.AllowInitialEscape=false;
    TestTrue(TEXT("Placement rejects overlap even with outward segment"),Sweep(Static,Dynamic,R,FVector(10,0,0),10,false,Scratch,Count).Blocked);
    R.AllowInitialEscape=true;
    TestFalse(TEXT("Existing overlap may escape monotonically"),Sweep(Static,Dynamic,R,FVector(10,0,0),10,false,Scratch,Count).Blocked);
    TestTrue(TEXT("Overlap cannot cross through center"),Sweep(Static,Dynamic,R,FVector(-30,0,0),10,false,Scratch,Count).Blocked);
    Static.Rebuild({});
    auto SlopeUnit=Body(2,FVector(100,0,60));SlopeUnit.MinimumZ=60;SlopeUnit.MaximumZ=88.8f;
    Dynamic.Upsert(SlopeUnit);R=Move(1,FVector::ZeroVector,FVector::ForwardVector);R.MaximumZ=28.8f;
    TestTrue(TEXT("Same-layer units still block across different slope elevations"),
        Sweep(Static,Dynamic,R,FVector(200,0,0),10,false,Scratch,Count).Blocked);
    TestTrue(TEXT("Relative observation keeps the same two-dimensional unit layer rule"),
        Sweep(Static,Dynamic,R,FVector(200,0,0),10,true,Scratch,Count).Blocked);
    Dynamic.Rebuild({});Static.Upsert(SlopeUnit);
    TestFalse(TEXT("Static geometry retains its height range for floors and passages"),
        Sweep(Static,Dynamic,R,FVector(200,0,0),10,false,Scratch,Count).Blocked);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiCircleSolverLimits,"GuLiStrike.Navigation.Circles.BoundedSides",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FGuLiCircleSolverLimits::RunTest(const FString&)
{
    FIndex Static,Dynamic;FQueryScratch Scratch;FCounters Count;FState State;FConfig Config;
    Config.Radius=10;Config.ContactMargin=0;Config.LookAheadSeconds=.2f;
    Static.Upsert(Body(99,FVector(25,0,0)));
    auto R=Move(1,FVector::ZeroVector,FVector::ForwardVector);
    const auto Result=Solve(Static,Dynamic,Config,R,State,Scratch,Count);
    TestTrue(TEXT("At most seven angles per side plus forward and slide"),Count.DirectionProbes<=16 && Count.SideProbes<=14);
    TestTrue(TEXT("Chosen side clears the obstacle in this case"),!Sweep(Static,Dynamic,R,Result.Delta,10,false,Scratch,Count).Blocked);
    Static.Rebuild({});
    Count={};State.Side=-1;
    Dynamic.Rebuild({});Solve(Static,Dynamic,Config,R,State,Scratch,Count);
    TestEqual(TEXT("Full lookahead clearance resets side preference"),State.Side,int8(0));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiCircleLocalPublication,"GuLiStrike.Navigation.Circles.LocalPublicationAndClustering",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FGuLiCircleLocalPublication::RunTest(const FString&)
{
    FStaticCircleStore Store;FBox Changed;TArray<FCircle> A{Body(1,FVector::ZeroVector)};
    TArray<FCircle> B{Body(2,FVector(1000,0,0))};
    TestTrue(TEXT("Publish first source"),Store.Replace(10,A,Changed));
    TestTrue(TEXT("Publish independent source"),Store.Replace(20,B,Changed));
    A[0].Radius=20;Store.Replace(10,A,Changed);
    TestTrue(TEXT("Local update leaves other geometry untouched"),Store.GetIndex().Find(2)->Radius==10 && Changed.Max.X<1000);
    TArray<FCircle> Members{Body(10,FVector::ZeroVector),Body(11,FVector(20,0,0)),Body(12,FVector(100,0,0))};
    TArray<FOreAggregate> Aggregates;FClusteringCounters Counts;
    BuildOreCluster(Members,{},2,1.25f,Aggregates,Counts);
    // Two tangent equal discs require a radius-20 enclosure whose area exceeds
    // 1.25 times their combined area. They must remain separate.
    TestEqual(TEXT("Area limit prevents excessive gap filling"),Aggregates.Num(),3);
    Members[1].Center=FVector(1,0,0);BuildOreCluster(Members,{},2,1.25f,Aggregates,Counts);
    TestEqual(TEXT("Only the overlapping local pair merges"),Aggregates.Num(),2);
    return true;
}
#endif
