#include "Gameplay/Navigation/Grid/GuLiGroundGridSubsystem.h"
#include "Gameplay/Navigation/Grid/GuLiGroundGridData.h"
#include "Gameplay/Navigation/Grid/GuLiGridNetwork.h"
#include "Gameplay/Navigation/Grid/GuLiGridSlots.h"
#include "Gameplay/Data/GuLiCommanderDataSubsystem.h"
#include "Gameplay/Building/GuLiBuildingCatalog.h"
#include "Gameplay/Navigation/Grid/GuLiGridMotion.h"
#include "Engine/World.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

bool UGuLiGroundGridSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
    const auto* W=Cast<UWorld>(Outer);return Super::ShouldCreateSubsystem(Outer) && W && W->IsGameWorld();
}
void UGuLiGroundGridSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    if (!GetWorld()->GetMapName().EndsWith(TEXT("LVL_CommanderMassPrototype"))) return;
    Data=LoadObject<UGuLiGroundGridData>(nullptr,TEXT("/Game/GuLiStrike/Data/Navigation/DA_CommanderGroundGrid.DA_CommanderGroundGrid"));
    if (!Data) return;
    FString Error;
    if (!Data->Validate(Error)) { UE_LOG(LogTemp,Error,TEXT("Ground grid rejected: %s"),*Error);return; }
    Current=FGuLiGridSnapshot::FromData(*Data);
    Collection.InitializeDependency<UGuLiHybridWorldSubsystem>();
    NetworkState=MakeShared<FGuLiGridNetwork>(*this);
    if (!GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>()->RegisterSharedStateSystem(FGuLiGridNetwork::System,NetworkState))
        GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>()->Suspend(TEXT("Ground grid traffic checkpoint registration failed"));
    GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>()->OnObjectChanged.AddUObject(this,&ThisClass::OnHybridObject);
}
void UGuLiGroundGridSubsystem::Deinitialize()
{
    StaticObjects.Empty();ObstacleCounts.Empty();Current.Reset();Data=nullptr;
    if (auto* H=GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>()) {H->OnObjectChanged.RemoveAll(this);H->UnregisterFactories(*this);}
    NetworkState.Reset();TrafficLedger={};BirthPlans.Empty();BirthKeys.Empty();Super::Deinitialize();
}
bool UGuLiGroundGridSubsystem::TryGetAuthoredDesignBounds(FBox2D& OutBounds) const
{
    if(!Data||!Data->bHasAuthoredDesignBounds||!Current)return false;
    OutBounds=FBox2D(Data->Origin,Data->Origin+FVector2D(Data->Cells)*Data->CellSizeCentimeters);
    return OutBounds.bIsValid&&OutBounds.GetSize().GetMin()>0;
}
void UGuLiGroundGridSubsystem::OnHybridObject(const FGuLiHybridObjectKey& Key)
{
    if (!Current) return;
    if (NetworkState) NetworkState->ObjectChanged(Key);
    const bool Ground=Key.Kind==EGuLiHybridObjectKind::Soldier || Key.Kind==EGuLiHybridObjectKind::Miner
        || Key.Kind==EGuLiHybridObjectKind::Engineer || Key.Kind==EGuLiHybridObjectKind::GroundPlayer;
    const bool Building=Key.Kind==EGuLiHybridObjectKind::Building || Key.Kind==EGuLiHybridObjectKind::Territory;
    if (!Ground && !Building) return;
    auto* H=GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>();FGuLiHybridObjectState O;
    if (!H || !H->GetObjectView(Key,O) || O.LifeState!=1 || O.bPhased || NetworkState && NetworkState->HoldsBody(Key))
    {
        if (const auto* R=TrafficLedger.Find(Key)) TrafficLedger.Remove(Key,R->Owner,R->OwnerEpoch);
        if (Building) RemoveStaticObstacle(Key);return;
    }
    GuLiGridBirths::Recover(*this,O);
    const FString Definition=O.Definition.ToString();int32 Slash=INDEX_NONE;Definition.FindLastChar('/',Slash);
    const int32 Id=Slash>=0?FCString::Atoi(*Definition.Mid(Slash+1)):0;
    if (Building)
    {
        if (const auto* Catalog=UGuLiBuildingCatalog::LoadDefaultCatalog())
            if (const auto* D=Catalog->FindById(Id)) SetStaticObstacle(Key,FGuLiGridMotion::Coverage(*Current,O.Location,D->FootprintCells));
        return;
    }
    auto* R=TrafficLedger.Find(Key);
    if (R && R->Owner==O.SimulationOwner && R->OwnerEpoch==O.OwnerEpoch) return;
    const auto* Catalog=GetWorld()->GetSubsystem<UGuLiCommanderDataSubsystem>();
    const auto* D=Catalog?Catalog->FindSoldierDefinition(Id):nullptr;
    const int32 Size=D?D->FootprintCells:1;
    GuLiGrid::FTrafficPriority Priority;
    if (Key.Kind==EGuLiHybridObjectKind::GroundPlayer) Priority.Class=GuLiGrid::ETrafficClass::DirectPlayer;
    if (TrafficLedger.Admit(Key,O.SimulationOwner,O.OwnerEpoch,uint8(O.Team),FGuLiGridMotion::Coverage(*Current,O.Location,Size),Priority,Size)
        && NetworkState) NetworkState->Observe(*TrafficLedger.Find(Key));
}
FGuLiNavigationWorkBudget& UGuLiGroundGridSubsystem::FrameBudget()
{
    if (BudgetFrame!=GFrameCounter)
    {
        BudgetFrame=GFrameCounter;Budget.Reset(2.,64,4);
        // Traffic gets a bounded first slice, independent of shared-system iteration order.
        if (NetworkState) NetworkState->Advance(0,0);
    }
    return Budget;
}
TSharedPtr<FGuLiGridSlots> UGuLiGroundGridSubsystem::CreateSlotWorkspace()
{
    // Eight workspaces, each <= 64 footprint bitmaps plus 32768 member IDs (< 3 MiB).
    // The shared counter outlives the world if a command still holds its frozen work.
    if (*SlotWorkspaceCount>=8) return {};
    return MakeShared<FGuLiGridSlots>(SlotWorkspaceCount);
}
void UGuLiGroundGridSubsystem::Advance(FGuLiNavigationWorkBudget& Work)
{
    check(IsInGameThread());if (!Current) return;
    TRACE_CPUPROFILER_EVENT_SCOPE(GuLiGrid_PlacementPlanning);
    FGuLiNavigationWorkBudget::FScope Scope(Work);
    // Grid search/route indexing is retired. Only footprint placement work runs here.
    GuLiGridBirths::Advance(*this);
}
void UGuLiGroundGridSubsystem::ChangeObstacle(const GuLiGrid::FRect& R,int32 Delta,TSet<int32>& Dirty)
{
    if (!Current) return;
    const FIntPoint Min(FMath::Clamp(R.Min.X,0,Current->Cells.X),FMath::Clamp(R.Min.Y,0,Current->Cells.Y));
    const FIntPoint Max(FMath::Clamp(R.Max.X,0,Current->Cells.X),FMath::Clamp(R.Max.Y,0,Current->Cells.Y));
    for (int32 Y=Min.Y;Y<Max.Y;++Y) for (int32 X=Min.X;X<Max.X;++X)
    {
        const int32 C=(Y/64)*Current->Chunks.X+X/64;auto& Counts=ObstacleCounts.FindOrAdd(C);
        if (Counts.IsEmpty()) Counts.SetNumZeroed(4096);
        auto& Count=Counts[(Y%64)*64+X%64];check(int32(Count)+Delta>=0 && int32(Count)+Delta<=MAX_uint16);
        Count=uint16(int32(Count)+Delta);Dirty.Add(C);
    }
}
void UGuLiGroundGridSubsystem::PublishObstacles(const TSet<int32>& Dirty)
{
    if (!Current || Dirty.IsEmpty()) return;
    if (ObstacleBatchDepth>0) { BatchedObstacleChunks.Append(Dirty); return; }
    auto Next=MakeShared<FGuLiGridSnapshot,ESPMode::ThreadSafe>(*Current);++ObstacleRevision;
    Next->ObstacleRevision=ObstacleRevision;
    for (int32 C:Dirty)
    {
        auto Chunk=MakeShared<FGuLiGridObstacleChunk,ESPMode::ThreadSafe>();Chunk->Revision=ObstacleRevision;
        const auto& Counts=ObstacleCounts.FindChecked(C);
        for (int32 Y=0;Y<64;++Y) for (int32 X=0;X<64;++X) if (Counts[Y*64+X]) Chunk->Rows[Y]|=uint64(1)<<X;
        Next->Obstacles[C]=Chunk;
    }
    // Height and clearance arrays remain shared. Only changed obstacle chunks are copied.
    Current=Next;
}
UGuLiGroundGridSubsystem::FScopedObstacleBatch::FScopedObstacleBatch(UGuLiGroundGridSubsystem* InGrid) : Grid(InGrid)
{
    check(IsInGameThread());
    if (Grid) ++Grid->ObstacleBatchDepth;
}
UGuLiGroundGridSubsystem::FScopedObstacleBatch::~FScopedObstacleBatch()
{
    if (Grid && --Grid->ObstacleBatchDepth==0)
    {
        Grid->PublishObstacles(Grid->BatchedObstacleChunks);
        Grid->BatchedObstacleChunks.Reset();
    }
}
void UGuLiGroundGridSubsystem::SetStaticObstacle(const FGuLiHybridObjectKey& Key,const GuLiGrid::FRect& Rect)
{
    check(IsInGameThread());if (!Key.IsValid() || !Current || Rect.Max.X<=Rect.Min.X || Rect.Max.Y<=Rect.Min.Y
        || int64(Rect.Max.X)-Rect.Min.X>128 || int64(Rect.Max.Y)-Rect.Min.Y>128
        || StaticObjects.Num()>=32768 && !StaticObjects.Contains(Key)) return;
    if (const auto* Old=StaticObjects.Find(Key);Old && Old->Min==Rect.Min && Old->Max==Rect.Max) return;
    TSet<int32> Dirty;
    if (const auto* Old=StaticObjects.Find(Key)) ChangeObstacle(*Old,-1,Dirty);
    StaticObjects.Add(Key,Rect);ChangeObstacle(Rect,1,Dirty);PublishObstacles(Dirty);
}
void UGuLiGroundGridSubsystem::RemoveStaticObstacle(const FGuLiHybridObjectKey& Key)
{
    check(IsInGameThread());const auto* Old=StaticObjects.Find(Key);if (!Old) return;
    TSet<int32> Dirty;ChangeObstacle(*Old,-1,Dirty);StaticObjects.Remove(Key);PublishObstacles(Dirty);
}
