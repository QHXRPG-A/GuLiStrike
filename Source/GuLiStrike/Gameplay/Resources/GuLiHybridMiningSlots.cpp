#include "Gameplay/Resources/GuLiMiningVehicleManager.h"
#include "Gameplay/Resources/GuLiMiningVehiclePawn.h"
#include "Gameplay/Resources/GuLiResourceWorldSubsystem.h"
#include "Gameplay/Resources/GuLiResourceMapDefinition.h"
#include "Gameplay/Units/GuLiEngineeringTravelComponent.h"
#include "Gameplay/Navigation/GuLiDynamicObstacleRegistry.h"
#include "Network/GuLiHybridActorComponent.h"
#include "Network/GuLiNetworkSettings.h"
#include "Network/Private/GuLiHybridStateArchive.h"
#include "Engine/World.h"

namespace
{
constexpr uint32 Version=2,MaximumOperations=2048,MaximumRejections=8192,MaximumBytes=4*1024*1024;
struct FOperation
{
    FGuLiHybridMiningRequest Request;
    bool bSubmitted=false,bFinished=false,bCancelled=false;
    EGuLiHybridTransactionStatus Status=EGuLiHybridTransactionStatus::BadRequest;
};
void Operation(FGuLiHybridStateArchive& A,FOperation& V)
{
    auto& R=V.Request;
    A.Value(R.Source); A.Value(R.Event); A.Value(R.Rule); A.Enum(R.Operation,uint8(EGuLiHybridMiningOperation::Release));
    A.Value(R.ClusterId); A.Value(R.Sample); A.Value(R.Task); A.Value(R.Token); A.Value(R.NodeId);
    A.Value(V.bSubmitted); A.Value(V.bFinished); A.Value(V.bCancelled); A.Enum(V.Status,uint8(EGuLiHybridTransactionStatus::Paused));
    if (!R.Source.IsValid() || R.Source.Kind!=EGuLiHybridObjectKind::Miner || !R.Event.IsValid() || R.Rule.IsNone()
        || R.Operation==EGuLiHybridMiningOperation::None || (V.bFinished && !V.bSubmitted)) A.Reject();
    if (R.Operation==EGuLiHybridMiningOperation::ClaimPosition
        ? R.ClusterId<=0 || R.Sample>=8 || R.Task<=0 || R.Task>MAX_uint32 || R.Token.IsValid() || R.NodeId
        : R.ClusterId || R.Sample || R.Task || !R.Token.IsValid()
            || (R.Operation==EGuLiHybridMiningOperation::ClaimNode ? R.NodeId<=0 : R.NodeId!=0)) A.Reject();
}
struct FRejectedRecipe
{
    FGuLiHybridObjectKey Source;
    FGuLiMiningSlotReservation Slot;
    FVector Start=FVector::ZeroVector;
    double RetryAt=0,NextVisibilityCheck=0;
    EGuLiMiningSlotFailure Failure=EGuLiMiningSlotFailure::Movement;
    int32 ClusterRemaining=0;
    FTransform FailedPose;
};
void Rejected(FGuLiHybridStateArchive& A,FRejectedRecipe& V)
{
    A.Value(V.Source);A.Value(V.Slot.Profile);A.Value(V.Slot.Cluster);A.Value(V.Slot.Task);A.Value(V.Slot.Sample);
    A.Value(V.Start);A.Value(V.RetryAt);A.Value(V.NextVisibilityCheck);A.Enum(V.Failure,uint8(EGuLiMiningSlotFailure::NoVisibleNodes));
    A.Value(V.ClusterRemaining);A.Value(V.FailedPose);
    if (!V.Source.IsValid() || V.Source.Kind!=EGuLiHybridObjectKind::Miner || V.Slot.Profile<=0 || !V.Slot.Cluster || !V.Slot.Task
        || V.Slot.Sample>=8 || V.RetryAt<0 || V.NextVisibilityCheck<0 || V.ClusterRemaining<0) A.Reject();
}
bool Encode(TArray<FOperation> Operations,TArray<FRejectedRecipe> Rejections,TArray<uint8>& Out)
{
    FGuLiHybridStateArchive A(MaximumBytes);uint32 Schema=Version;A.Value(Schema);
    A.Array(Operations,MaximumOperations,Operation);A.Array(Rejections,MaximumRejections,Rejected);return A.Finish(Out);
}
bool Decode(TConstArrayView<uint8> Bytes,TArray<FOperation>& Out,TArray<FRejectedRecipe>* OutRejected=nullptr)
{
    FGuLiHybridStateArchive A(Bytes,MaximumBytes); uint32 Schema=0; A.Value(Schema);
    A.Array(Out,MaximumOperations,Operation);
    TArray<FRejectedRecipe> Rejections;A.Array(Rejections,MaximumRejections,Rejected);
    TSet<FGuid> Events;
    for (const auto& V:Out) { if (Events.Contains(V.Request.Event)) A.Reject(); Events.Add(V.Request.Event); }
    if (Schema!=Version || !A.Done()) return false;
    if (OutRejected) *OutRejected=MoveTemp(Rejections);return true;
}
bool Committed(EGuLiHybridTransactionStatus S)
{ return S==EGuLiHybridTransactionStatus::Committed || S==EGuLiHybridTransactionStatus::Duplicate; }
class FMiningOperationsAdapter final : public IGuLiHybridSharedStateAdapter
{
    TWeakObjectPtr<UGuLiMiningVehicleManager> Owner;
public:
    explicit FMiningOperationsAdapter(UGuLiMiningVehicleManager& V):Owner(&V) {}
    UObject* LifetimeOwner() const override {return Owner.Get();}
    uint32 Version() const override {return ::Version;}
    void ResetResourceRequests(const TArray<FGuLiHybridObjectKey>& Objects,uint64) override
    {if (Owner.IsValid()) Owner->ResetHybridMiningRequests(Objects);}


    bool Capture(const TArray<FGuLiHybridObjectKey>& Objects,TArray<uint8>& Out) const override
    {return Owner.IsValid() && Owner->CaptureHybridMiningOperations(Objects,Out);}
    bool CaptureCurrent(const TArray<FGuLiHybridObjectKey>& Objects,TArray<uint8>& Out) const override
    {
        TArray<uint8> B;TArray<FOperation> Operations;TArray<FRejectedRecipe> Rejections;
        if (!Capture(Objects,B)||!Decode(B,Operations,&Rejections)) return false;
        return Encode({},MoveTemp(Rejections),Out);
    }
    bool Validate(const TArray<FGuLiHybridSharedStatePiece>& Pieces,const TArray<FGuLiHybridObjectKey>&) const override
    {for (const auto& P:Pieces) if (P.Version!=Version() || !Owner.IsValid() || !Owner->ValidateHybridMiningOperations(P.Bytes)) return false; return true;}
    bool Restore(const TArray<FGuLiHybridSharedStatePiece>& Pieces,const TArray<FGuLiHybridObjectKey>& Local,EGuLiHybridRestoreStage Stage) override
    {
        if (!Owner.IsValid()) return false;
        if (Stage!=EGuLiHybridRestoreStage::Link) return true;
        TArray<FOperation> All;
        TArray<FRejectedRecipe> Rejections;
        TSet<FGuid> Events;
        for (const auto& P:Pieces)
        {
            TArray<FOperation> Part;TArray<FRejectedRecipe> RejectedPart;
            if (P.Version!=Version() || !Decode(P.Bytes,Part,&RejectedPart)) return false;
            for (const auto& V:Part) if (Local.Contains(V.Request.Source))
            {
                if (Events.Contains(V.Request.Event)) return false;
                Events.Add(V.Request.Event); All.Add(V);
            }
            for (const auto& V:RejectedPart) if (Local.Contains(V.Source)) Rejections.Add(V);
        }
        TArray<uint8> Bytes;return Encode(MoveTemp(All),MoveTemp(Rejections),Bytes) && Owner->RestoreHybridMiningOperations(Bytes,Local);
    }
};
}
struct UGuLiMiningVehicleManager::FHybridState { TArray<FOperation> Operations; };
void UGuLiMiningVehicleManager::FHybridStateDeleter::operator()(FHybridState* Value) const {delete Value;}
UGuLiMiningVehicleManager::UGuLiMiningVehicleManager():HybridState(new FHybridState) {}
UGuLiMiningVehicleManager::~UGuLiMiningVehicleManager()=default;
void UGuLiMiningVehicleManager::ResetHybridMiningRequests(const TArray<FGuLiHybridObjectKey>& Objects)
{HybridState->Operations.RemoveAll([&](const FOperation& V){return Objects.Contains(V.Request.Source);});}
void UGuLiMiningVehicleManager::InitializeHybridOperations()
{
    if (auto* H=GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>())
        if (!H->RegisterSharedStateSystem(TEXT("MiningSlots"),MakeShared<FMiningOperationsAdapter>(*this))) H->Suspend(TEXT("Mining operations adapter registration failed"));
}
void UGuLiMiningVehicleManager::ClearHybridOperations()
{
    HybridState->Operations.Reset();
    if (auto* H=GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>()) H->UnregisterFactories(*this);
}
void UGuLiMiningVehicleManager::PollHybridOperations()
{
    auto* H=GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>(); if (!H) return;
    HybridState->Operations.RemoveAll([](const FOperation& V){return V.bCancelled && !V.bSubmitted;});
    for (auto& V:HybridState->Operations)
    {
        FGuLiHybridObjectState Source;
        if (!H->GetObject(V.Request.Source,Source)) continue;
        if (Source.LifeState>=2) {V.bCancelled=true; V.bFinished=true; continue;}
        if (!V.bSubmitted && H->CanPublishFacts() && H->HasSimulation(V.Request.Source))
            V.bSubmitted=H->SubmitMiningOperation(V.Request)==V.Request.Event;
        FGuLiHybridTransactionResult Result;
        if (V.bSubmitted && H->FindTransactionResult(V.Request.Event,Result)) {V.bFinished=true; V.Status=Result.Status;}
    }
    HybridState->Operations.RemoveAll([&](const FOperation& V)
    {
        if (!V.bFinished) return false;
        if (V.Request.Operation==EGuLiHybridMiningOperation::Release) return true;
        FGuLiHybridObjectState Source; return H->GetObject(V.Request.Source,Source) && Source.LifeState>=2;
    });
    // A cancelled prepare may have committed after local cancellation. Release
    // exactly that token; a newer task's token is never used for the cleanup.
    TArray<FOperation> Releases;
    for (const auto& V:HybridState->Operations) if (V.bCancelled && V.bFinished)
    {
        FGuLiHybridMiningLease Lease;
        if (H->GetMiningLease(V.Request.Source,Lease) && V.Request.Operation==EGuLiHybridMiningOperation::ClaimPosition
            && Lease.Token==V.Request.Event)
        {
            FOperation Release; Release.Request.Source=V.Request.Source; Release.Request.Event=FGuid::NewGuid();
            FGuLiHybridObjectState Source; if (!H->GetObject(V.Request.Source,Source)) continue;
            Release.Request.Rule=FName(TEXT("mining/release/")+Source.Definition.ToString().RightChop(5));
            Release.Request.Operation=EGuLiHybridMiningOperation::Release; Release.Request.Token=Lease.Token; Releases.Add(Release);
        }
    }
    HybridState->Operations.RemoveAll([](const FOperation& V){return V.bCancelled && V.bFinished;});
    if (HybridState->Operations.Num()+Releases.Num()>MaximumOperations) {H->Suspend(TEXT("Mining operation queue capacity exhausted")); return;}
    HybridState->Operations.Append(Releases);
}
bool UGuLiMiningVehicleManager::ValidateHybridReservation(const AGuLiMiningVehiclePawn& Vehicle,const FGuLiMiningSlotReservation& R) const
{
    const auto* Binding=UGuLiHybridActorComponent::Find(&Vehicle); const auto* H=GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>();
    FGuLiHybridMiningLease Lease;
    return Binding && H && R.Token.IsValid() && H->GetMiningLease(Binding->GetObjectKey(),Lease)
        && Lease.Token==R.Token && Lease.ClusterId==R.Cluster && Lease.Task==R.Task && Lease.Sample==R.Sample;
}
void UGuLiMiningVehicleManager::ReleaseHybridSlot(const AGuLiMiningVehiclePawn& Vehicle,const FGuLiMiningSlotReservation& R)
{
    const auto* Binding=UGuLiHybridActorComponent::Find(&Vehicle); auto* H=GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>();
    if (!Binding || !H || !H->HasSimulation(Binding->GetObjectKey())) return;
    const auto Source=Binding->GetObjectKey();
    for (auto& V:HybridState->Operations) if (V.Request.Source==Source && V.Request.Operation!=EGuLiHybridMiningOperation::Release) V.bCancelled=true;
    if (!R.Token.IsValid() || HybridState->Operations.ContainsByPredicate([&](const FOperation& V)
        {return V.Request.Source==Source && V.Request.Operation==EGuLiHybridMiningOperation::Release && V.Request.Token==R.Token;})) return;
    if (HybridState->Operations.Num()>=MaximumOperations) {H->Suspend(TEXT("Mining operation queue capacity exhausted")); return;}
    FOperation V; V.Request.Source=Source; V.Request.Event=FGuid::NewGuid(); V.Request.Token=R.Token;
    V.Request.Operation=EGuLiHybridMiningOperation::Release; V.Request.Rule=FName(FString::Printf(TEXT("mining/release/%d"),Vehicle.GetUnitTypeId()));
    HybridState->Operations.Add(V); PollHybridOperations();
}
EGuLiWorkPositionAvailability UGuLiMiningVehicleManager::TryReserveHybridSlot(AGuLiMiningVehiclePawn& Vehicle,uint16 Preferred,uint32 Task,FGuLiMiningSlotReservation& Out,FVector& Position)
{
    using Status=EGuLiWorkPositionAvailability;
    auto* H=GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>(); const auto* Binding=UGuLiHybridActorComponent::Find(&Vehicle);
    auto* Profile=SlotProfiles.Find(Vehicle.GetUnitTypeId());
    if (!H || !Binding || !Task || !H->HasSimulation(Binding->GetObjectKey())) return Status::NoValidPositions;
    if (!Profile) return Status::Pending;
    if (Preferred && Profile->Clusters.IsValidIndex(Preferred-1) && Profile->Clusters[Preferred-1].bPending)
        Profile->RequestedClusters.AddUnique(Preferred-1);
    const auto Source=Binding->GetObjectKey(); PollHybridOperations();
    if (const auto* Pending=HybridState->Operations.FindByPredicate([&](const FOperation& V)
        {return V.Request.Source==Source && V.Request.Operation==EGuLiHybridMiningOperation::ClaimPosition;}))
    {
        if (!Pending->bFinished) return Status::Pending;
        const bool Success=Committed(Pending->Status); const auto Event=Pending->Request.Event;
        HybridState->Operations.RemoveAll([&](const FOperation& V){return V.Request.Event==Event;});
        if (!Success) return Status::Occupied;
    }
    FGuLiHybridMiningLease Lease;
    if (H->GetMiningLease(Source,Lease) && Lease.Task==Task && (!Preferred || Lease.ClusterId==Preferred)
        && Profile->Clusters.IsValidIndex(Lease.ClusterId-1))
    {
        auto& Cluster=Profile->Clusters[Lease.ClusterId-1]; if (Cluster.bPending) return Status::Pending;
        const int32 Index=Cluster.Slots.IndexOfByPredicate([&](const FGuLiMiningSlot& S){return S.OriginalSample==Lease.Sample;});
        if (Index!=INDEX_NONE)
        {
            auto& Slot=Cluster.Slots[Index]; Slot.Owner=&Vehicle; Slot.Task=Task;
            Out={Vehicle.GetUnitTypeId(),static_cast<uint16>(Lease.ClusterId),Index,Cluster.Generation,Task,Lease.Sample,Lease.Token};
            Position=Slot.Pose.GetLocation(); return Status::Available;
        }
    }
    const auto* Resources=GetWorld()->GetSubsystem<UGuLiResourceWorldSubsystem>(); const auto* Map=Resources?Resources->GetMapDefinition():nullptr;
    if (!Map || !H->CanPublishFacts()) return Status::Pending;
    auto* Travel=Vehicle.FindComponentByClass<UGuLiEngineeringTravelComponent>();
    const int32 Current=Resources->FindTerritoryIndex(Vehicle.GetActorLocation()); const double Now=H->GetLogicalSeconds();
    auto& Rejections=RejectedSlots.FindOrAdd(&Vehicle);
    const auto* Obstacles=GetWorld()->GetSubsystem<UGuLiDynamicObstacleRegistrySubsystem>();int32 VisibilityChecks=0;
    Rejections.RemoveAll([&](FRejectedSlot& R)
    {
        FBox Dependency(R.Start,R.Start); Dependency+=R.FailedPose.GetLocation();
        const bool GeometryChanged=Obstacles->HasStaticChangesSince(R.NavRevision,Dependency.ExpandBy(3000.f));
        R.NavRevision=Obstacles->GetStaticRevision();
        if (R.RetryAt<=Now || R.Slot.Task!=Task || GeometryChanged
            || (R.Failure==EGuLiMiningSlotFailure::Movement && !R.Start.Equals(Vehicle.GetActorLocation(),100))
            || (R.Failure==EGuLiMiningSlotFailure::NoVisibleNodes && R.ClusterRemaining!=Resources->GetClusterRemainingRaw(R.Slot.Cluster))) return true;
        if (R.Failure==EGuLiMiningSlotFailure::NoVisibleNodes && R.NextVisibilityCheck<=Now && VisibilityChecks++<2
            && Profile->Clusters.IsValidIndex(R.Slot.Cluster-1))
        {
            R.NextVisibilityCheck=Now+1;
            const auto& Cluster=Profile->Clusters[R.Slot.Cluster-1];
            if (const auto* Slot=Cluster.Slots.FindByPredicate([&](const FGuLiMiningSlot& V){return V.OriginalSample==R.Slot.Sample;}))
                for (uint32 N:Slot->Nodes) {FVector Aim;if (Resources->GetNodeMiningTarget(N,Aim) && Vehicle.CanMineTargetFrom(Aim,R.FailedPose)) return true;}
        }
        return false;
    });
    for (int32 Pass=0;Pass<(Preferred?1:2);++Pass)
    {
        bool Pending=false,Occupied=false; double Best=TNumericLimits<double>::Max(); int32 BestCluster=INDEX_NONE,BestSlot=INDEX_NONE;
        for (int32 I=0;I<Profile->Clusters.Num();++I)
        {
            const auto& D=Map->Clusters[I]; auto& C=Profile->Clusters[I];
            if ((Preferred && D.ClusterId!=Preferred) || (!Preferred && (D.TerritoryIndex==Current)!=(Pass==0)) || !Resources->CanTeamMineAt(Vehicle.GetTeam(),D.ClusterId)) continue;
            if (C.bPending) {Pending=true; continue;}
            for (int32 S=0;S<C.Slots.Num();++S)
            {
                const auto& Slot=C.Slots[S];
                if (Rejections.ContainsByPredicate([&](const FRejectedSlot& R){return R.Slot.Cluster==D.ClusterId && R.Slot.Sample==Slot.OriginalSample && R.Slot.Task==Task;})) continue;
                if (!Slot.Nodes.ContainsByPredicate([&](uint32 N){return Resources->GetNodeRemainingRaw(N)>0;})) continue;
                if (!H->IsMiningPositionAvailable(Source,D.ClusterId,Slot.OriginalSample)
                    || !Slot.Nodes.ContainsByPredicate([&](uint32 N){return Resources->GetNodeRemainingRaw(N)>0 && H->IsMiningNodeAvailable(Source,N);})) {Occupied=true; continue;}
                const double Distance=Travel->EstimateWorkDistance(Slot.Pose.GetLocation());
                if (Distance<Best) {Best=Distance;BestCluster=I;BestSlot=S;}
            }
        }
        if (BestCluster!=INDEX_NONE)
        {
            if (HybridState->Operations.Num()>=MaximumOperations) {H->Suspend(TEXT("Mining operation queue capacity exhausted")); return Status::Pending;}
            FOperation V; V.Request.Source=Source; V.Request.Event=FGuid::NewGuid();
            V.Request.Rule=FName(FString::Printf(TEXT("mining/position/%d"),Vehicle.GetUnitTypeId()));
            V.Request.Operation=EGuLiHybridMiningOperation::ClaimPosition; V.Request.ClusterId=Map->Clusters[BestCluster].ClusterId;
            V.Request.Sample=Profile->Clusters[BestCluster].Slots[BestSlot].OriginalSample; V.Request.Task=Task;
            HybridState->Operations.Add(V); PollHybridOperations(); return Status::Pending;
        }
        if (Pending) return Status::Pending; if (Occupied) return Status::Occupied;
    }
    return Status::NoValidPositions;
}
EGuLiWorkPositionAvailability UGuLiMiningVehicleManager::AssignHybridNode(AGuLiMiningVehiclePawn& Vehicle,const FGuLiMiningSlotReservation& R,const FTransform& Pose,uint32& Node,FVector& Target)
{
    using Status=EGuLiWorkPositionAvailability;
    auto* H=GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>(); const auto* Binding=UGuLiHybridActorComponent::Find(&Vehicle);
    if (!H || !Binding || !H->HasSimulation(Binding->GetObjectKey()) || !ValidateReservation(Vehicle,R)) return Status::NoValidPositions;
    const auto Source=Binding->GetObjectKey(); PollHybridOperations();
    if (const auto* Pending=HybridState->Operations.FindByPredicate([&](const FOperation& V){return V.Request.Source==Source && V.Request.Operation==EGuLiHybridMiningOperation::ClaimNode;}))
    {
        if (!Pending->bFinished) return Status::Pending;
        const bool Success=Committed(Pending->Status); const auto Event=Pending->Request.Event;
        HybridState->Operations.RemoveAll([&](const FOperation& V){return V.Request.Event==Event;}); if (!Success) return Status::Occupied;
    }
    const auto* Resources=GetWorld()->GetSubsystem<UGuLiResourceWorldSubsystem>();
    const auto& Nodes=SlotProfiles.FindChecked(R.Profile).Clusters[R.Cluster-1].Slots[R.Slot].Nodes;
    FGuLiHybridMiningLease Lease;
    if (H->GetMiningLease(Source,Lease) && Lease.Token==R.Token && Lease.NodeId>0 && Nodes.Contains(uint32(Lease.NodeId))
        && Resources->GetNodeMiningTarget(uint32(Lease.NodeId),Target) && Vehicle.CanMineTargetFrom(Target,Pose))
    {Node=uint32(Lease.NodeId); return Status::Available;}
    Node=0; if (!H->CanPublishFacts()) return Status::Pending; bool Occupied=false;
    for (uint32 Id:Nodes)
    {
        FVector Aim; if (!Resources->GetNodeMiningTarget(Id,Aim) || !Vehicle.CanMineTargetFrom(Aim,Pose)) continue;
        if (!H->IsMiningNodeAvailable(Source,Id)) {Occupied=true; continue;}
        if (HybridState->Operations.Num()>=MaximumOperations) {H->Suspend(TEXT("Mining operation queue capacity exhausted")); return Status::Pending;}
        FOperation V; V.Request.Source=Source; V.Request.Event=FGuid::NewGuid(); V.Request.Token=R.Token; V.Request.NodeId=static_cast<int32>(Id);
        V.Request.Operation=EGuLiHybridMiningOperation::ClaimNode; V.Request.Rule=FName(FString::Printf(TEXT("mining/node/%d"),Vehicle.GetUnitTypeId()));
        HybridState->Operations.Add(V); PollHybridOperations(); return Status::Pending;
    }
    return Occupied?Status::Occupied:Status::NoValidPositions;
}
bool UGuLiMiningVehicleManager::CaptureHybridMiningOperations(const TArray<FGuLiHybridObjectKey>& Objects,TArray<uint8>& Bytes) const
{
    auto Copy=HybridState->Operations;Copy.RemoveAll([&](const auto& V){return !Objects.Contains(V.Request.Source);});const auto* H=GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>();
    for (auto& V:Copy)
    {
        FGuLiHybridTransactionResult Result;
        if (V.bSubmitted && H && H->FindTransactionResult(V.Request.Event,Result)) {V.bFinished=true;V.Status=Result.Status;}
    }
    TArray<FRejectedRecipe> Rejections;
    for (const auto& Pair:RejectedSlots)
    {
        const auto* Binding=UGuLiHybridActorComponent::Find(Pair.Key.Get());if (!Binding || !Objects.Contains(Binding->GetObjectKey())) continue;
        for (const auto& R:Pair.Value)
        {FRejectedRecipe V;V.Source=Binding->GetObjectKey();V.Slot=R.Slot;V.Start=R.Start;V.RetryAt=FMath::Max(0.,R.RetryAt-H->GetLogicalSeconds());V.NextVisibilityCheck=FMath::Max(0.,R.NextVisibilityCheck-H->GetLogicalSeconds());
            V.Failure=R.Failure;V.ClusterRemaining=R.ClusterRemaining;V.FailedPose=R.FailedPose;Rejections.Add(V);}
    }
    return Encode(MoveTemp(Copy),MoveTemp(Rejections),Bytes);
}
bool UGuLiMiningVehicleManager::ValidateHybridMiningOperations(TConstArrayView<uint8> Bytes) const
{
    TArray<FOperation> Values;TArray<FRejectedRecipe> Rejections;if (!Decode(Bytes,Values,&Rejections)) return false;
    const auto* H=GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>();
    for (const auto& V:Values) {FGuLiHybridObjectState O; if (!H || !H->GetObject(V.Request.Source,O)) return false;}
    const auto* Resources=GetWorld()->GetSubsystem<UGuLiResourceWorldSubsystem>();const auto* Map=Resources?Resources->GetMapDefinition():nullptr;
    for (const auto& V:Rejections)
    {FGuLiHybridObjectState O;if (!H || !Map || !H->GetObject(V.Source,O) || !Map->FindCluster(V.Slot.Cluster)
        || O.Definition!=FName(FString::Printf(TEXT("unit/%d"),V.Slot.Profile))) return false;}
    return true;
}
bool UGuLiMiningVehicleManager::RestoreHybridMiningOperations(TConstArrayView<uint8> Bytes,const TArray<FGuLiHybridObjectKey>& Objects)
{
    TArray<FOperation> Values;TArray<FRejectedRecipe> Rejections;
    if (!ValidateHybridMiningOperations(Bytes) || !Decode(Bytes,Values,&Rejections)) return false;
    HybridState->Operations.RemoveAll([&](const auto& V){return Objects.Contains(V.Request.Source);});
    for (const auto& V:Values) if (!Objects.Contains(V.Request.Source)) return false;
    HybridState->Operations.Append(MoveTemp(Values));
    for (auto It=RejectedSlots.CreateIterator();It;++It)
    {const auto* B=UGuLiHybridActorComponent::Find(It.Key().Get());if (B && Objects.Contains(B->GetObjectKey())) It.RemoveCurrent();}
    const auto* H=GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>();const auto* Obstacles=GetWorld()->GetSubsystem<UGuLiDynamicObstacleRegistrySubsystem>();
    for (const auto& V:Rejections)
    {
        auto* P=Cast<AGuLiMiningVehiclePawn>(H->FindBoundActor(V.Source));if (!P) return false;
        RejectedSlots.FindOrAdd(P).Add({V.Slot,V.Start,H->GetLogicalSeconds()+V.RetryAt,Obstacles->GetStaticRevision(),V.Failure,V.ClusterRemaining,V.FailedPose,H->GetLogicalSeconds()+V.NextVisibilityCheck});
    }
    return true;
}
