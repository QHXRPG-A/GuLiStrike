#include "Gameplay/Stronghold/GuLiStrongholdFacilitiesComponent.h"
#include "Gameplay/Stronghold/GuLiHybridControlPoint.h"
#include "Gameplay/Building/GuLiBuildingCatalog.h"
#include "Gameplay/Building/GuLiBuildingSpawner.h"
#include "Gameplay/Building/GuLiBuildingLifecycleComponent.h"
#include "Network/GuLiHybridActorComponent.h"
#include "Network/GuLiHybridManifest.h"
#include "Network/Private/GuLiHybridComponentState.h"
#include "Network/Private/GuLiHybridStateArchive.h"
#include "Gameplay/Performance/GuLiHotspotOptions.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "Gameplay/Navigation/Grid/GuLiGridQueries.h"
#include "Gameplay/Navigation/Grid/GuLiGridNetwork.h"

namespace
{
void ReleaseGift(UGuLiGroundGridSubsystem& G,const FGuLiHybridObjectKey& Target)
{
    const auto Place=FGuLiGridNetwork::PlacementKey(Target);
    if (const auto* R=G.Traffic().Find(Place)) G.Traffic().Remove(Place,R->Owner,R->OwnerEpoch);
    FGuLiHybridObjectState Born;
    if (!G.GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>()->GetObjectView(Target,Born) && G.Network()) G.Network()->Withdraw(Target);
}
}
void UGuLiStrongholdFacilitiesComponent::RecoverGridGifts()
{
    auto* G=GetWorld()->GetSubsystem<UGuLiGroundGridSubsystem>();const auto* Binding=UGuLiHybridActorComponent::Find(GetOwner());
    const auto* Catalog=UGuLiBuildingCatalog::LoadDefaultCatalog();FGuLiHybridObjectState Source;
    if (!G || !G->IsReady() || !G->Network() || !Binding || !Catalog
        || !GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>()->GetObjectView(Binding->GetObjectKey(),Source)
        || !G->Network()->IsLocal(Source.SimulationOwner)) return;
    for (int32 I=0;I<HybridJobs.Num() && I<Slots.Num();++I) if (HybridJobs[I].Submitted)
    {
        const auto& J=HybridJobs[I];const auto* D=Catalog->FindById(Slots[I].DefinitionId);FGuLiHybridObjectState Born;
        if (!D || (GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>()->GetObjectView(J.Target,Born) && Born.LifeState!=0)) continue;
        const auto R=GuLiGrid::FRect::At(G->Snapshot()->AnchorAt(J.Placement.GetLocation(),D->FootprintCells),D->FootprintCells);
        G->Traffic().Admit(FGuLiGridNetwork::PlacementKey(J.Target),Source.SimulationOwner,Source.OwnerEpoch,uint8(Source.Team),R,{},D->FootprintCells);
        G->Network()->RecoverPlacement(Source,J.Target,R,D->FootprintCells);
    }
}

void UGuLiStrongholdFacilitiesComponent::ReleaseGridGifts()
{
    auto* G=GetWorld()?GetWorld()->GetSubsystem<UGuLiGroundGridSubsystem>():nullptr;
    if (!G || !G->IsReady()) return;
    for (const auto& J:HybridJobs)
    {
        const auto Key=FGuLiGridNetwork::PlacementKey(J.Target);
        if (const auto* R=G->Traffic().Find(Key)) G->Traffic().Remove(Key,R->Owner,R->OwnerEpoch);
        if (!J.Submitted && G->Network()) G->Network()->Withdraw(J.Target);
    }
}
void UGuLiStrongholdFacilitiesComponent::EndPlay(const EEndPlayReason::Type Reason)
{
    ReleaseGridGifts();Super::EndPlay(Reason);
}

struct FGuLiHybridFacilities
{
    static constexpr uint32 Version=2;
    struct FSlot {int32 Definition=0,Cursor=0;bool Delivered=false;FGuLiHybridGiftJob Job;};
    struct FState {bool Captured=false;uint64 Next=0;TArray<FSlot> Slots;};
    static void Codec(FGuLiHybridStateArchive& A,FState& V)
    {
        uint32 Schema=Version;A.Value(Schema);A.Value(V.Captured);A.Value(V.Next);
        A.Array(V.Slots,4,[](auto& Ar,FSlot& S)
        {
            Ar.Value(S.Definition);Ar.Value(S.Cursor);Ar.Value(S.Delivered);Ar.Value(S.Job.Target);
            Ar.Value(S.Job.Event);Ar.Value(S.Job.Placement);Ar.Value(S.Job.Attempt);Ar.Value(S.Job.Submitted);
            if (S.Definition<=0 || S.Cursor<0 || S.Cursor>160 || (S.Job.Submitted && (!S.Job.Event.IsValid() || !S.Job.Target.IsValid()))) Ar.Reject();
        });
        if (Schema!=Version) A.Reject();
    }
    static bool Decode(const UActorComponent& Component,TConstArrayView<uint8> Bytes,FState& V)
    {
        FGuLiHybridStateArchive A(Bytes,16384);Codec(A,V);if (!A.Done()) return false;
        const auto* Life=Component.GetOwner()->FindComponentByClass<UGuLiBuildingLifecycleComponent>();
        if (!Life || Life->GetDefinition().FirstCaptureGiftIds.Num()!=V.Slots.Num()) return false;
        for (int32 I=0;I<V.Slots.Num();++I) if (V.Slots[I].Definition!=Life->GetDefinition().FirstCaptureGiftIds[I]) return false;
        return true;
    }
    static bool Capture(const UActorComponent& Component,TArray<uint8>& Bytes)
    {
        const auto& C=*CastChecked<UGuLiStrongholdFacilitiesComponent>(&Component);
        if (C.Slots.Num()!=C.HybridJobs.Num()) return false;
        FState V;V.Captured=C.bFirstCaptured;V.Next=C.HybridNextAttempt;
        for (int32 I=0;I<C.Slots.Num();++I) V.Slots.Add({C.Slots[I].DefinitionId,C.PlacementSearchCursor.FindRef(I),C.Slots[I].bDelivered,C.HybridJobs[I]});
        FGuLiHybridStateArchive A(16384);Codec(A,V);return A.Finish(Bytes);
    }
    static bool Restore(UActorComponent& Component,TConstArrayView<uint8> Bytes,EGuLiHybridRestoreStage Stage)
    {
        FState V;if (!Decode(Component,Bytes,V)) return false;
        if (Stage==EGuLiHybridRestoreStage::Detach) CastChecked<UGuLiStrongholdFacilitiesComponent>(&Component)->ReleaseGridGifts();
        if (Stage!=EGuLiHybridRestoreStage::Link) return true;
        auto& C=*CastChecked<UGuLiStrongholdFacilitiesComponent>(&Component);C.bFirstCaptured=V.Captured;C.HybridNextAttempt=V.Next;
        C.Slots.Reset();C.HybridJobs.Reset();C.PlacementSearchCursor.Reset();C.LastPlacementFailure.Reset();
        for (int32 I=0;I<V.Slots.Num();++I)
        {
            const auto& S=V.Slots[I];auto& Slot=C.Slots.AddDefaulted_GetRef();Slot.DefinitionId=S.Definition;Slot.bDelivered=S.Delivered;
            C.HybridJobs.Add(S.Job);C.PlacementSearchCursor.Add(I,S.Cursor);
        }
        C.RecoverGridGifts();C.SetComponentTickEnabled(false);return true;
    }
    static void Advance(UActorComponent& Component,uint64 Tick,double)
    {
        auto& C=*CastChecked<UGuLiStrongholdFacilitiesComponent>(&Component);
        auto* W=C.GetWorld();auto* H=W->GetSubsystem<UGuLiHybridWorldSubsystem>();const auto* Binding=UGuLiHybridActorComponent::Find(C.GetOwner());
        if (!Binding || !H || !H->HasSimulation(Binding->GetObjectKey()) || !H->CanPublishFacts() || Tick<C.HybridNextAttempt) return;
        FGuLiHybridObjectState Source;if (!H->GetObjectView(Binding->GetObjectKey(),Source)) return;
        auto* Grid=W->GetSubsystem<UGuLiGroundGridSubsystem>();if (Grid && !Grid->IsReady()) Grid=nullptr;
        FGuLiNavigationWorkBudget Legacy;auto& Work=Grid?Grid->FrameBudget():Legacy;
        FGuLiNavigationWorkBudget::FScope Scope(Work);
        TGuardValue<double> Slice(Work.LimitSeconds,FMath::Min(Work.LimitSeconds,Work.Elapsed()+.0002));
        const auto Point=GuLiHybridControlPoint::Read(*H,Source.Key);if (Point.Index==INDEX_NONE) {H->Suspend(TEXT("Gift facility lost its registered control point capability"));return;}
        C.bFirstCaptured=Point.FirstCapture.IsValid();C.HybridNextAttempt=Tick+12;
        if (!C.bFirstCaptured || !GuLiResources::IsPlayableTeam(Point.Owner)) return;
        const auto* Life=C.GetOwner()->FindComponentByClass<UGuLiBuildingLifecycleComponent>();const auto* Catalog=UGuLiBuildingCatalog::LoadDefaultCatalog();
        if (!Life || !Catalog || C.HybridJobs.Num()!=C.Slots.Num()) {H->Suspend(TEXT("Gift facility state does not match the building definition"));return;}
        for (int32 Index=0;Index<C.Slots.Num() && Work.CanWork();++Index)
        {
            auto& Slot=C.Slots[Index];auto& Job=C.HybridJobs[Index];
            if (!Job.Target.IsValid()) Job.Target=FGuLiHybridManifest::MakeStableKey(TEXT("/Game/GuLiStrike/StrongholdGifts"),EGuLiHybridObjectKind::Building,
                Source.Key.StableId.ToString(EGuidFormats::Digits)+FString::Printf(TEXT("/%u/%d"),Source.Key.LifeGeneration,Index));
            FGuLiHybridObjectState Object;
            if (H->GetObjectView(Job.Target,Object))
            {
                if (Object.Parent!=Source.Key || Object.Definition!=FName(*FString::Printf(TEXT("building/%d"),Slot.DefinitionId)))
                {H->Suspend(TEXT("Gift slot resolved to an unrelated object"));return;}
                if (Object.LifeState==0) continue;
                Slot.bDelivered=true;Job.Submitted=false;Job.Prepared=false;Job.Event.Invalidate();
                if (Grid) ReleaseGift(*Grid,Job.Target);
                FGuLiTargetHandle Handle;FGuLiCombatTargetSnapshot Snapshot;
                if (H->ResolveObjectTarget(Job.Target,Handle) && W->GetSubsystem<UGuLiDamageLedgerSubsystem>()->TryGetTargetSnapshot(Handle,Snapshot)) Slot.Building=Snapshot.CollisionActor.Get();
                continue;
            }
            if (Slot.bDelivered) {H->Suspend(TEXT("Delivered gift is absent from the restored directory"));return;}
            if (Job.Submitted)
            {
                FGuLiHybridTransactionResult Result;if (!H->FindTransactionResult(Job.Event,Result)) continue;
                if (Result.IsCommitted()) continue; // The directory commit precedes the receipt; assignment may still be preparing.
                if (Result.Status==EGuLiHybridTransactionStatus::Deferred || Result.Status==EGuLiHybridTransactionStatus::Paused) continue;
                Job.Submitted=false;Job.Prepared=false;if (Grid) ReleaseGift(*Grid,Job.Target);
                Job.Event.Invalidate();if (Job.Attempt==UINT32_MAX) {H->Suspend(TEXT("Gift attempt sequence exhausted"));return;}++Job.Attempt;
            }
            const auto* D=Catalog->FindById(Slot.DefinitionId);if (!D) {H->Suspend(TEXT("Gift references a missing data definition"));return;}
            const auto Candidates=GuLiBuildings::GetGiftPlacementCandidates(Life->GetGroundLocation(),Index);
            int32& Cursor=C.PlacementSearchCursor.FindOrAdd(Index);const int32 End=FMath::Min(Cursor+4,Candidates.Num());
            bool Found=Job.Prepared;
            for (;!Found && Cursor<End && Work.TakeProjection();++Cursor)
            {
                AActor* Support=nullptr;FTransform Ground;FString Why;
                if (!GuLiBuildings::ProjectPlacementCandidate(*W,Candidates[Cursor],C.GetOwner(),Ground,Support,D->FootprintCells)
                    || !GuLiBuildings::ValidateGroundPlacement(*W,*D,Ground,D->PresentationClass.Get(),C.GetOwner(),Why)) continue;
                if (!Grid)
                {
                    FCollisionQueryParams Clearance(SCENE_QUERY_STAT(GuLiHybridGiftPlacement),false,Support);Clearance.AddIgnoredActor(C.GetOwner());
                    for (TActorIterator<APawn> It(W);It;++It) Clearance.AddIgnoredActor(*It);
                    if (W->OverlapBlockingTestByChannel(Ground.GetLocation()+FVector(0,0,D->CollisionExtent.Z+4),Ground.GetRotation(),ECC_Pawn,
                        FCollisionShape::MakeBox(D->CollisionExtent),Clearance)) continue;
                }
                else if (!Grid->Traffic().Admit(FGuLiGridNetwork::PlacementKey(Job.Target),Source.SimulationOwner,Source.OwnerEpoch,uint8(Source.Team),
                    GuLiGrid::FRect::At(Grid->Snapshot()->AnchorAt(Ground.GetLocation(),D->FootprintCells),D->FootprintCells),{},D->FootprintCells)) continue;
                Job.Placement=Ground;Found=true;Job.Prepared=true;break;
            }
            if (!Found) {if (Cursor>=Candidates.Num()) {Cursor=0;C.HybridNextAttempt=Tick+300+GetTypeHash(Source.Key)%60;}continue;}
            if (Grid)
            {
                const auto Place=FGuLiGridNetwork::PlacementKey(Job.Target);
                const bool Clear=GuLiGrid::Unoccupied(*Grid,Job.Placement.GetLocation(),D->FootprintCells,Place);
                const auto Permit=Clear && Grid->Network()?Grid->Network()->PermitPlacement(Source.Key,Job.Target,
                    GuLiGrid::FRect::At(Grid->Snapshot()->AnchorAt(Job.Placement.GetLocation(),D->FootprintCells),D->FootprintCells),D->FootprintCells,uint8(Source.Team)):EGuLiGridPermit::Blocked;
                if (Permit==EGuLiGridPermit::Pending) continue;
                if (Permit==EGuLiGridPermit::Blocked)
                {ReleaseGift(*Grid,Job.Target);Job.Prepared=false;++Cursor;C.HybridNextAttempt=Tick+300+GetTypeHash(Source.Key)%60;continue;}
            }
            FGuLiHybridObjectRequest Request;Request.Operation=EGuLiHybridObjectOperation::Spawn;Request.Source=Source.Key;Request.Target=Job.Target;
            Request.Event=FGuLiHybridManifest::MakeStableKey(TEXT("/Game/GuLiStrike/GiftAdmissions"),EGuLiHybridObjectKind::Building,
                Job.Target.StableId.ToString(EGuidFormats::Digits)+FString::Printf(TEXT("/%u"),Job.Attempt)).StableId;
            Request.Rule=FName(*(TEXT("gift/")+Source.Definition.ToString()+TEXT("/")+FString::Printf(TEXT("building/%d"),Slot.DefinitionId)));
            Request.Location=Job.Placement.GetLocation();Request.FacingYaw=Job.Placement.Rotator().Yaw;
            const FGuid Accepted=H->SubmitObjectOperation(Request);if (Accepted.IsValid()) {Job.Event=Accepted;Job.Submitted=true;}
        }
    }
    static bool NeedsAdvance(const UActorComponent& Component,uint64 Tick)
    {return Tick>=CastChecked<UGuLiStrongholdFacilitiesComponent>(&Component)->HybridNextAttempt;}
};
static const FGuLiHybridComponentCapabilityRegistration FacilitiesCapability({TEXT("StrongholdFacilities"),FGuLiHybridFacilities::Version,
    []()->const UClass* {return UGuLiStrongholdFacilitiesComponent::StaticClass();},FGuLiHybridFacilities::Capture,
    [](const UActorComponent& C,TConstArrayView<uint8> Bytes) {FGuLiHybridFacilities::FState V;return FGuLiHybridFacilities::Decode(C,Bytes,V);},
    FGuLiHybridFacilities::Restore,nullptr,FGuLiHybridFacilities::Advance,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,
    FGuLiHybridFacilities::NeedsAdvance});
