#include "Gameplay/Building/GuLiBuildingProductionComponent.h"
#include "Gameplay/Building/GuLiBuildingLifecycleComponent.h"
#include "Gameplay/Data/GuLiCommanderDataSubsystem.h"
#include "Gameplay/Navigation/Grid/GuLiGridQueries.h"
#include "Gameplay/Navigation/Grid/GuLiGridNetwork.h"
#include "Network/GuLiHybridActorComponent.h"
#include "Network/GuLiHybridActorWorldSubsystem.h"
#include "Network/GuLiHybridAdapterRegistry.h"
#include "Network/GuLiHybridManifest.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/Pawn.h"

void UGuLiBuildingProductionComponent::RecoverGridBirths()
{
    if (HybridJobs.IsEmpty()) return;
    auto* G=GetWorld()->GetSubsystem<UGuLiGroundGridSubsystem>();
    const auto* Binding=UGuLiHybridActorComponent::Find(GetOwner());
    const auto* Life=GetOwner()->FindComponentByClass<UGuLiBuildingLifecycleComponent>();
    if (!G || !G->IsReady() || !G->Network() || !Binding || !Life) return;
    const auto* Data=GetWorld()->GetSubsystem<UGuLiCommanderDataSubsystem>();
    const auto* Unit=Data?Data->FindSoldierDefinition(Life->GetDefinition().ProductionUnitId):nullptr;
    if (!Unit) return;
    FGuLiHybridObjectState Source;
    auto* H=GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>();
    if (!H || !H->GetObjectView(Binding->GetObjectKey(),Source) || !G->Network()->IsLocal(Source.SimulationOwner)) return;
    for (const auto& J:HybridJobs) if (J.bSubmitted)
    {
        FGuLiHybridObjectState Born;if (H->GetObjectView(J.Target,Born) && Born.LifeState!=0) continue;
        const auto Rect=GuLiGrid::FRect::At(G->Snapshot()->AnchorAt(J.Location,Unit->FootprintCells),Unit->FootprintCells);
        G->Traffic().Admit(FGuLiGridNetwork::PlacementKey(J.Target),Source.SimulationOwner,Source.OwnerEpoch,uint8(Source.Team),Rect,{},Unit->FootprintCells,Unit->MovementLayer);
        G->Network()->RecoverPlacement(Source,J.Target,Rect,Unit->FootprintCells,false,Unit->MovementLayer);
    }
}

void UGuLiBuildingProductionComponent::ReleaseGridBirth(const FGuLiHybridObjectKey& Target,bool bWithdraw)
{
    auto* G=GetWorld()->GetSubsystem<UGuLiGroundGridSubsystem>();if (!G || !G->IsReady()) return;
    const auto Key=FGuLiGridNetwork::PlacementKey(Target);
    if (const auto* R=G->Traffic().Find(Key)) G->Traffic().Remove(Key,R->Owner,R->OwnerEpoch);
    // A completed birth has already replaced its placement with its own body record.
    FGuLiHybridObjectState O;auto* H=GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>();
    if (bWithdraw && (!H || !H->GetObjectView(Target,O)) && G->Network()) G->Network()->Withdraw(Target);
}
void UGuLiBuildingProductionComponent::AdvanceGridProduction(float Dt)
{
    auto& G=*GetWorld()->GetSubsystem<UGuLiGroundGridSubsystem>();auto& Work=G.FrameBudget();
    FGuLiNavigationWorkBudget::FScope Scope(Work);
    TGuardValue<double> Slice(Work.LimitSeconds,FMath::Min(Work.LimitSeconds,Work.Elapsed()+.0002));
    const auto* Binding=UGuLiHybridActorComponent::Find(GetOwner());const auto& Owner=Binding->GetState();
    const auto& Life=*GetOwner()->FindComponentByClass<UGuLiBuildingLifecycleComponent>();const auto& D=Life.GetDefinition();
    const auto* Unit=GetWorld()->GetSubsystem<UGuLiCommanderDataSubsystem>()->FindSoldierDefinition(D.ProductionUnitId);
    const auto* Host=GetWorld()->GetSubsystem<UGuLiHybridActorWorldSubsystem>();auto* H=GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>();
    if (!Unit || !Host || D.ProductionSeconds<=0 || D.ProductionCount<=0 || D.ProductionCount>256) return;
    const double Now=UGuLiHybridActorComponent::BattleSeconds(GetOwner());
    if (!GridRemaining && HybridJobs.IsEmpty())
    {
        CycleSeconds=FMath::Min(D.ProductionSeconds,CycleSeconds+Dt);
        if (CycleSeconds<D.ProductionSeconds) return;
        GridRemaining=D.ProductionCount;GridCandidate=0;CycleSeconds=0;
    }
    const auto* Pawn=Unit->ActorClass?Cast<APawn>(Unit->ActorClass->GetDefaultObject()):nullptr;
    const auto* Capsule=Pawn?Pawn->FindComponentByClass<UCapsuleComponent>():nullptr;
    const double HalfHeight=Capsule?Capsule->GetUnscaledCapsuleHalfHeight()*Unit->GridDimensionScale:0.;
    const auto S=G.Snapshot();FGuLiHybridAdapterFamily Family;FString Error;
    if (!FGuLiHybridAdapterRegistry::ResolveDefinition(Host->GetManifest(),FString::Printf(TEXT("unit/%d"),D.ProductionUnitId),Family,Error)) return;
    static const FIntPoint Directions[]={{1,0},{1,1},{0,1},{-1,1},{-1,0},{-1,-1},{0,-1},{1,-1}};
    int32 Attempts=0;
    while (GridRemaining>0 && HybridJobs.Num()<256 && Now>=GridRetryAt && Attempts++<8 && Work.TakeProjection())
    {
        const int32 Candidate=GridCandidate++;const auto Dir=Directions[Candidate%8];
        const double Offset=(D.FootprintCells+Unit->FootprintCells)*150.+(Candidate/8)*Unit->FootprintCells*300.;
        const FVector Desired=Life.GetGroundLocation()+FVector(Dir.X*Offset,Dir.Y*Offset,0);FVector P;
        if (S->Project(Desired,Unit->FootprintCells,FVector(300,300,5000),P) && GuLiGrid::Unoccupied(G,P,Unit->FootprintCells,{},Unit->MovementLayer))
        {
            FHybridJob J;J.Event=FGuid::NewGuid();J.Location=P+FVector(0,0,HalfHeight+Unit->AltitudeCentimeters);J.FacingYaw=GetOwner()->GetActorRotation().Yaw;
            J.Target=FGuLiHybridManifest::MakeStableKey(GetWorld()->GetOutermost()->GetName(),Family.Kind,TEXT("production/")+J.Event.ToString(EGuidFormats::Digits));
            if (G.Traffic().Admit(FGuLiGridNetwork::PlacementKey(J.Target),Owner.SimulationOwner,Owner.OwnerEpoch,uint8(Owner.Team),
                GuLiGrid::FRect::At(S->AnchorAt(P,Unit->FootprintCells),Unit->FootprintCells),{},Unit->FootprintCells,Unit->MovementLayer))
            {HybridJobs.Add(J);--GridRemaining;}
        }
        if (GridCandidate>=128)
        {GridCandidate=0;GridRetryAt=Now+5.+(GetTypeHash(Owner.Key)%60)/60.;break;}
    }
    int32 Remaining=FMath::Min(8,HybridJobs.Num());
    while (Remaining-- && !HybridJobs.IsEmpty() && Work.CanWork())
    {
        GridJobCursor%=HybridJobs.Num();auto& J=HybridJobs[GridJobCursor];
        if (J.bSubmitted)
        {
            FGuLiHybridObjectState Born;
            if (H->GetObjectView(J.Target,Born) && Born.LifeState!=0)
            {ReleaseGridBirth(J.Target);HybridJobs.RemoveAtSwap(GridJobCursor,1,EAllowShrinking::No);}
            else ++GridJobCursor;
            continue;
        }
        const auto Actual=GuLiGrid::FRect::At(S->AnchorAt(J.Location,Unit->FootprintCells),Unit->FootprintCells);
        const auto Reservation=FGuLiGridNetwork::PlacementKey(J.Target);
        // Recover the local reservation from the saved production job after an ownership handoff.
        if (!G.Traffic().Find(Reservation) && GuLiGrid::Unoccupied(G,J.Location,Unit->FootprintCells,{},Unit->MovementLayer))
            G.Traffic().Admit(Reservation,Owner.SimulationOwner,Owner.OwnerEpoch,uint8(Owner.Team),Actual,{},Unit->FootprintCells,Unit->MovementLayer);
        const bool Clear=GuLiGrid::Unoccupied(G,J.Location,Unit->FootprintCells,Reservation,Unit->MovementLayer);
        const auto Permit=Clear && G.Network()?G.Network()->PermitPlacement(Owner.Key,J.Target,Actual,Unit->FootprintCells,uint8(Owner.Team),Unit->MovementLayer):EGuLiGridPermit::Blocked;
        if (Permit==EGuLiGridPermit::Blocked)
        {
            ReleaseGridBirth(J.Target);HybridJobs.RemoveAtSwap(GridJobCursor,1,EAllowShrinking::No);++GridRemaining;
            GridRetryAt=Now+5.+(GetTypeHash(Owner.Key)%60)/60.;continue;
        }
        if (Permit==EGuLiGridPermit::Granted)
        {
            FGuLiHybridObjectRequest Request;Request.Source=Owner.Key;Request.Target=J.Target;Request.Event=J.Event;
            Request.Operation=EGuLiHybridObjectOperation::Spawn;Request.Location=J.Location;Request.FacingYaw=J.FacingYaw;
            Request.Rule=FName(*FString::Printf(TEXT("produce/building/%d"),D.DefinitionId));J.bSubmitted=H->SubmitObjectOperation(Request).IsValid();
        }
        ++GridJobCursor;
    }
}
