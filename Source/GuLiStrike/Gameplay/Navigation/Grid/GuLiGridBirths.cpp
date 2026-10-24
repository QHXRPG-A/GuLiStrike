#include "Gameplay/Navigation/Grid/GuLiGridBirths.h"
#include "Gameplay/Navigation/Grid/GuLiGridQueries.h"
#include "Gameplay/Navigation/Grid/GuLiGridNetwork.h"
#include "Network/GuLiHybridManifest.h"
#include "Network/Private/GuLiHybridObjectConversions.h"
#include "Network/Private/GuLiHybridStateArchive.h"
#include "GuLiNetObjectBatch.h"

FGuid GuLiGridBirths::EventId(const FGuid& Request,const FGuLiHybridObjectKey& Source)
{
    return FGuLiHybridManifest::MakeStableKey(TEXT("/Game/GuLiStrike/Summon"),EGuLiHybridObjectKind::BusinessWork,
        Request.ToString(EGuidFormats::Digits)+TEXT("/")+Source.StableId.ToString(EGuidFormats::Digits)).StableId;
}
FGuLiHybridObjectKey GuLiGridBirths::Target(const FGuLiGridBirthBatch& B,int32 Index)
{
    return GuLiNetBridge::ToUE(GuLiNet::ObjectBatch::Child(GuLiNetBridge::ToCore(B.Source),GuLiNetBridge::ToCore(B.Event),
        GuLiNet::ObjectKind::Soldier,uint32(Index)));
}
EGuLiGridPermit GuLiGridBirths::Prepare(UGuLiGroundGridSubsystem& G,const FGuLiHybridObjectState& O,const FGuid& Request,
    FVector Position,int32 CasterSize,int32 Size,int32 Count,double Clearance,int32 Rings,int32 Samples)
{
    if (!Request.IsValid() || Count<1 || Count>32 || Size<1 || Size>64 || CasterSize<1 || CasterSize>64
        || !FMath::IsFinite(Clearance) || Clearance<0 || Clearance>19200 || Rings<1 || Rings>3 || Samples<1 || Samples>36
        || !G.Network() || !G.Network()->IsLocal(O.SimulationOwner)) return EGuLiGridPermit::Blocked;
    auto* Batch=G.BirthPlans.Find(O.Key);
    if (!Batch)
    {
        if (G.BirthPlans.Num()>=1024) return EGuLiGridPermit::Blocked;
        Batch=&G.BirthPlans.Add(O.Key);G.BirthKeys.Add(O.Key);
        Batch->Source=O.Key;Batch->Request=Request;Batch->Event=EventId(Request,O.Key);
        Batch->Size=Size;Batch->Count=Count;Batch->CasterSize=CasterSize;Batch->Gap=FMath::CeilToInt32(Clearance/300.);
        Batch->Origin=G.Snapshot()->AnchorAt(Position,CasterSize);Batch->Rings=Rings;Batch->Samples=Samples;
    }
    auto& B=*Batch;if (B.Request!=Request || B.bSubmitted) return EGuLiGridPermit::Blocked;
    auto& Work=G.FrameBudget();FGuLiNavigationWorkBudget::FScope Scope(Work);
    TGuardValue<double> Slice(Work.LimitSeconds,FMath::Min(Work.LimitSeconds,Work.Elapsed()+.0002));
    const auto S=G.Snapshot();const int32 Limit=Count+Rings*Samples;
    while (B.Positions.Num()<Count && B.Cursor<Limit && Work.TakeProjection())
    {
        const int32 I=B.Cursor++,Ring=I<Count?0:1+(I-Count)/Samples;
        const int32 Local=I<Count?I:(I-Count)%Samples,PerimeterSamples=I<Count?Count:Samples;
        const int32 Low=-(Size+B.Gap)*(Ring+1),High=CasterSize+B.Gap+Ring*(Size+B.Gap),Span=High-Low;
        const int32 Along=FMath::FloorToInt32(double(Local)*4*Span/PerimeterSamples),Side=Along/Span,Offset=Along%Span;
        const FIntPoint P=B.Origin+(Side==0?FIntPoint(Low+Offset,Low):Side==1?FIntPoint(High,Low+Offset):
            Side==2?FIntPoint(High-Offset,High):FIntPoint(Low,High-Offset));
        const FVector Center=S->Center(P,Size);
        if (!GuLiGrid::Unoccupied(G,Center,Size)) continue;
        const auto Key=FGuLiGridNetwork::PlacementKey(Target(B,B.Positions.Num()));
        if (G.Traffic().Admit(Key,O.SimulationOwner,O.OwnerEpoch,uint8(O.Team),GuLiGrid::FRect::At(P,Size),{},Size)) B.Positions.Add(Center);
    }
    if (B.Positions.Num()<Count)
    {
        if (B.Cursor<Limit) return EGuLiGridPermit::Pending;
        Cancel(G,O.Key,Request);return EGuLiGridPermit::Blocked;
    }
    const uint64 Generation=G.Network()->PlanningGeneration();
    if (B.CheckGeneration!=Generation) {B.CheckGeneration=Generation;B.CheckCursor=0;B.bAwaitingGrant=false;}
    while (B.CheckCursor<B.Positions.Num())
    {
        if (!Work.TakeProjection()) return EGuLiGridPermit::Pending;
        const int32 I=B.CheckCursor++;
        const auto Key=Target(B,I);const auto Place=FGuLiGridNetwork::PlacementKey(Key);
        if (!GuLiGrid::Unoccupied(G,B.Positions[I],Size,Place)) {Cancel(G,O.Key,Request);return EGuLiGridPermit::Blocked;}
        const auto Permit=G.Network()->PermitPlacement(O.Key,Key,GuLiGrid::FRect::At(S->AnchorAt(B.Positions[I],Size),Size),Size,uint8(O.Team));
        if (Permit==EGuLiGridPermit::Blocked) {Cancel(G,O.Key,Request);return Permit;}
        B.bAwaitingGrant|=Permit==EGuLiGridPermit::Pending;
    }
    const bool Pending=B.bAwaitingGrant;B.CheckCursor=0;B.bAwaitingGrant=false;
    return Pending?EGuLiGridPermit::Pending:EGuLiGridPermit::Granted;
}
void GuLiGridBirths::Cancel(UGuLiGroundGridSubsystem& G,const FGuLiHybridObjectKey& Source,const FGuid& Request,bool Detach)
{
    const auto* B=G.BirthPlans.Find(Source);if (!B || B->Request!=Request || B->bSubmitted && !Detach) return;
    for (int32 I=0;I<B->Positions.Num();++I)
    {
        const auto K=Target(*B,I),P=FGuLiGridNetwork::PlacementKey(K);
        if (const auto* R=G.Traffic().Find(P)) G.Traffic().Remove(P,R->Owner,R->OwnerEpoch);
        if (!B->bSubmitted && G.Network())
        {
            FGuLiHybridObjectState Child;
            if (!G.GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>()->GetObjectView(K,Child)) G.Network()->Withdraw(K);
        }
    }
    G.BirthPlans.Remove(Source);G.BirthKeys.RemoveSwap(Source,EAllowShrinking::No);
}
void GuLiGridBirths::Recover(UGuLiGroundGridSubsystem& G,const FGuLiHybridObjectState& Source)
{
    const auto* B=G.BirthPlans.Find(Source.Key);
    if (!B || !B->bSubmitted || !G.Network() || !G.Network()->IsLocal(Source.SimulationOwner)) return;
    auto* H=G.GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>();
    for (int32 I=0;I<B->Positions.Num();++I)
    {
        const auto Key=Target(*B,I);FGuLiHybridObjectState Born;if (H->GetObjectView(Key,Born) && Born.LifeState!=0) continue;
        const auto R=GuLiGrid::FRect::At(G.Snapshot()->AnchorAt(B->Positions[I],B->Size),B->Size);
        G.Traffic().Admit(FGuLiGridNetwork::PlacementKey(Key),Source.SimulationOwner,Source.OwnerEpoch,uint8(Source.Team),R,{},B->Size);
        G.Network()->RecoverPlacement(Source,Key,R,B->Size);
    }
}
void GuLiGridBirths::Advance(UGuLiGroundGridSubsystem& G)
{
    auto& Work=G.FrameBudget();if (G.BirthKeys.IsEmpty() || !Work.CanWork()) return;
    FGuLiNavigationWorkBudget::FScope Scope(Work);
    TGuardValue<double> Slice(Work.LimitSeconds,FMath::Min(Work.LimitSeconds,Work.Elapsed()+.0001));
    auto* H=G.GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>();
    for (int32 I=0;I<4 && !G.BirthKeys.IsEmpty() && Work.CanWork();++I)
    {
        G.BirthCursor%=G.BirthKeys.Num();const auto K=G.BirthKeys[G.BirthCursor++];auto& B=G.BirthPlans.FindChecked(K);
        if (!B.bSubmitted) continue;
        FGuLiHybridObjectState O;
        if (!H->GetObjectView(K,O) || !G.Network()->IsLocal(O.SimulationOwner)) {Cancel(G,K,B.Request,true);continue;}
        FGuLiHybridTransactionResult Result;if (!H->FindTransactionResult(B.Event,Result)) continue;
        if (Result.Status==EGuLiHybridTransactionStatus::Deferred || Result.Status==EGuLiHybridTransactionStatus::Paused) continue;
        if (Result.IsCommitted() || Result.Status==EGuLiHybridTransactionStatus::Interrupted)
        {
            bool Born=true,Registered=false;
            for (int32 J=0;J<B.Positions.Num();++J)
            {FGuLiHybridObjectState Child;const bool Found=H->GetObjectView(Target(B,J),Child);Registered|=Found;Born&=Found && Child.LifeState!=0;}
            if (!Born && (Result.IsCommitted() || Registered)) continue;
        }
        B.bSubmitted=false;Cancel(G,K,B.Request);
    }
}
void GuLiGridBirths::Serialize(FGuLiHybridStateArchive& A,FGuLiGridBirthBatch& B)
{
    A.Value(B.Request);A.Value(B.Event);A.Value(B.Source);A.Value(B.Size);A.Value(B.Count);A.Value(B.bSubmitted);
    A.Array(B.Positions,32,[](auto& Ar,auto& P){Ar.Value(P);});
    if (!B.Source.IsValid() || !B.Request.IsValid() || B.Event!=EventId(B.Request,B.Source) || B.Size<1 || B.Size>64
        || B.Count<1 || B.Count>32 || !B.bSubmitted || B.Positions.Num()!=B.Count) A.Reject();
}
