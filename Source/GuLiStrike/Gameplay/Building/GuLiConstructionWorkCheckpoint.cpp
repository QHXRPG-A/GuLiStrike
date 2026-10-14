#include "Gameplay/Building/GuLiConstructionWorkComponent.h"
#include "Gameplay/Building/GuLiBuildingLifecycleComponent.h"
#include "Gameplay/Building/GuLiBuildingRegistrySubsystem.h"
#include "Gameplay/Navigation/GuLiDynamicObstacleRegistry.h"
#include "Network/GuLiHybridActorComponent.h"
#include "Network/Private/GuLiHybridStateArchive.h"
#include "Engine/World.h"

namespace GuLiConstructionCheckpoint
{
constexpr uint32 Version=1;
struct FRejected {int32 Slot=INDEX_NONE; double RetryAt=0; uint32 Generation=0; FVector Start=FVector::ZeroVector; FGuLiHybridObjectKey Building;};
struct FState
{
    FGuLiHybridObjectKey Target,Contributing;
    FVector Position=FVector::ZeroVector;
    bool Moving=false,Applying=false,Failed=false,Cancelled=false,Automatic=false,Acknowledged=false;
    uint32 Task=0;
    FGuLiConstructionSlotReservation Reservation;
    FGuLiConstructionSlotIntent Intent;
    FString Reason;
    double CheckAt=0,RequestedAt=0;
    TArray<FRejected> Rejected;
};
void Serialize(FGuLiHybridStateArchive& A,FState& V)
{
    uint32 Schema=Version; A.Value(Schema); if (Schema!=Version) A.Reject();
    A.Value(V.Target); A.Value(V.Contributing); A.Value(V.Position);
    A.Value(V.Moving); A.Value(V.Applying); A.Value(V.Failed); A.Value(V.Cancelled); A.Value(V.Automatic); A.Value(V.Acknowledged);
    A.Value(V.Task); A.String(V.Reason,4096); A.Value(V.CheckAt); A.Value(V.RequestedAt);
    auto& R=V.Reservation; A.Value(R.Building); A.Value(R.Slot); A.Value(R.Generation); A.Value(R.Task);
    auto& I=V.Intent; A.Value(I.Building); A.Value(I.Slot); A.Value(I.Generation);
    A.Array(V.Rejected,64,[](auto& Ar,FRejected& F)
    {Ar.Value(F.Slot); Ar.Value(F.RetryAt); Ar.Value(F.Generation); Ar.Value(F.Start); Ar.Value(F.Building);
     if (F.Slot<0 || F.Slot>=4 || F.RetryAt<0 || !F.Generation || !F.Building.IsValid() || F.Building.Kind!=EGuLiHybridObjectKind::Building) Ar.Reject();});
    if (V.CheckAt<0 || V.RequestedAt<0 || (V.Target.IsValid() && V.Target.Kind!=EGuLiHybridObjectKind::Building)
        || (V.Contributing.IsValid() && V.Contributing!=V.Target)
        || (V.Applying && (!V.Task || !V.Target.IsValid() || !R.IsValid() || R.Task!=V.Task))
        || I.Slot>=4 || R.Slot>=4 || (I.IsValid() && !I.Generation) || (R.IsValid() && !R.Generation)) A.Reject();
}
bool Decode(TConstArrayView<uint8> Bytes,FState& V)
{FGuLiHybridStateArchive A(Bytes,65536); Serialize(A,V); return A.Done();}
}

bool UGuLiConstructionWorkComponent::CaptureHybridWork(TArray<uint8>& Bytes) const
{
    using namespace GuLiConstructionCheckpoint;
    auto Key=[](const UGuLiBuildingLifecycleComponent* Life,FGuLiHybridObjectKey& K)
    {if (!Life) return true; const auto* Binding=UGuLiHybridActorComponent::Find(Life->GetOwner()); if (!Binding) return false; K=Binding->GetObjectKey(); return true;};
    FState V; if (!Key(Target.Get(),V.Target) || !Key(ContributingTo.Get(),V.Contributing)) return false;
    V.Position=WorkPosition; V.Moving=bMoveRequested; V.Applying=bApplyingWork; V.Failed=bMoveFailed; V.Cancelled=bOrderCancelled;
    V.Automatic=bAutomaticOrder; V.Acknowledged=bHybridSlotAcknowledged; V.Task=TaskVersion; V.Reservation=Reservation; V.Intent=Intent;
    V.Reason=OrderReason; V.CheckAt=NextPositionCheck; V.RequestedAt=HybridSlotRequestedAt;
    const auto* Registry=GetWorld()->GetSubsystem<UGuLiBuildingRegistrySubsystem>();
    for (const auto& R:Rejected)
    {
        // Old terminal building failures no longer constrain a future order.
        const auto* Life=Registry?Registry->Find(R.Building):nullptr;
        if (!Life || !IsRejectionCurrent(R,*Life)) continue;
        GuLiConstructionCheckpoint::FRejected F; F.Slot=R.Slot; F.RetryAt=R.RetryAt; F.Generation=R.Generation; F.Start=R.Start;
        if (!Key(Life,F.Building)) return false; V.Rejected.Add(F);
    }
    FGuLiHybridStateArchive A(65536); GuLiConstructionCheckpoint::Serialize(A,V); return A.Finish(Bytes);
}
bool UGuLiConstructionWorkComponent::ValidateHybridWork(TConstArrayView<uint8> Bytes) const
{
    using namespace GuLiConstructionCheckpoint;
    FState V; if (!Decode(Bytes,V)) return false;
    const auto* H=GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>(); const auto* Source=UGuLiHybridActorComponent::Find(GetOwner());
    auto Reference=[&](const FGuLiHybridObjectKey& K,uint32 Ordinal=0)
    {if (!K.IsValid()) return Ordinal==0; FGuLiHybridObjectState O; return H && Source && H->GetObject(K,O)
        && O.Team==Source->GetState().Team && (!Ordinal || O.Ordinal==Ordinal);};
    if (!Reference(V.Target,V.Intent.Building) || !Reference(V.Contributing)) return false;
    if (V.Reservation.IsValid() && (!Reference(V.Target,V.Reservation.Building) || !V.Target.IsValid())) return false;
    for (const auto& R:V.Rejected) if (!Reference(R.Building)) return false;
    return true;
}
bool UGuLiConstructionWorkComponent::RestoreHybridWork(TConstArrayView<uint8> Bytes)
{
    using namespace GuLiConstructionCheckpoint;
    FState V; if (!ValidateHybridWork(Bytes) || !Decode(Bytes,V)) return false;
    auto* H=GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>();
    auto Life=[&](const FGuLiHybridObjectKey& K)->UGuLiBuildingLifecycleComponent*
    {auto* Actor=H->FindBoundActor(K); return Actor?Actor->FindComponentByClass<UGuLiBuildingLifecycleComponent>():nullptr;};
    Target=Life(V.Target); ContributingTo=Life(V.Contributing);
    if ((V.Target.IsValid() && !Target.IsValid()) || (V.Contributing.IsValid() && !ContributingTo.IsValid())) return false;
    WorkPosition=V.Position; bMoveRequested=V.Moving; bApplyingWork=V.Applying; bMoveFailed=V.Failed; bOrderCancelled=V.Cancelled;
    bAutomaticOrder=V.Automatic; bHybridSlotAcknowledged=V.Acknowledged; TaskVersion=V.Task; Reservation=V.Reservation; Intent=V.Intent;
    OrderReason=V.Reason; NextPositionCheck=V.CheckAt; HybridSlotRequestedAt=V.RequestedAt;
    Rejected.Reset(); const auto* Obstacles=GetWorld()->GetSubsystem<UGuLiDynamicObstacleRegistrySubsystem>();
    for (const auto& F:V.Rejected)
    {FGuLiHybridObjectState O; if (!H->GetObject(F.Building,O)) return false;
     Rejected.Add({F.Slot,F.RetryAt,Obstacles?Obstacles->GetStaticRevision():0,F.Start,F.Generation,O.Ordinal});}
    ObservedHybridFact={}; ObservedFactReceivedTick=0; bObservedFactReceived=false;
    SetComponentTickEnabled(false); return true;
}
