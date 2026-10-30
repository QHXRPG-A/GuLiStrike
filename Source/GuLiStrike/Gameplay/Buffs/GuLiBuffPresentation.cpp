#include "Gameplay/Buffs/GuLiBuffPresentation.h"
#include "Gameplay/CombatEffects/GuLiCombatEffectPresentationSubsystem.h"
#include "Gameplay/CombatEffects/GuLiImpactBatchPresentation.h"
#include "Gameplay/Vfx/GuLiVfxRegistrySubsystem.h"
#include "Gameplay/Vfx/GuLiClientPresentationPolicy.h"
#include "Commander/Presentation/GuLiCommanderLODSubsystem.h"
#include "Battle/Framework/GuLiBattleGameState.h"
#include "Battle/Combat/GuLiCombatDamageLedger.h"
#include "Network/GuLiHybridWorldSubsystem.h"
#include "Network/Private/GuLiHybridObjectConversions.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "Engine/World.h"

bool UGuLiBuffPresentation::ShouldCreateSubsystem(UObject* Outer) const
{const auto* W=Cast<UWorld>(Outer);return Super::ShouldCreateSubsystem(Outer)&&W&&W->GetNetMode()!=NM_DedicatedServer&&(W->WorldType==EWorldType::Game||W->WorldType==EWorldType::PIE);}
TStatId UGuLiBuffPresentation::GetStatId() const {RETURN_QUICK_DECLARE_CYCLE_STAT(UGuLiBuffPresentation,STATGROUP_Tickables);}
void UGuLiBuffPresentation::Stop(FGuLiBuffVisualSlot& V)
{if(V.Component){V.Component->Deactivate();Tails.Add(V.Component);V.Component=nullptr;}}
void UGuLiBuffPresentation::Remove(const FGuLiHybridObjectKey& K)
{if(const auto* H=ByKey.Find(K)){const auto Handle=*H;if(Pool.IsLive(Handle)){Stop(Slots[Handle.Index]);Pool.Release(Handle);}ByKey.Remove(K);}}
void UGuLiBuffPresentation::Apply(const FGuLiHybridObjectKey& K,const GuLiNet::Buffs::State& S,bool Bootstrap)
{
    auto* H=GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>();const auto* B=GetWorld()->GetGameState<AGuLiBattleGameState>();if(!H||!B)return;
    if(Epoch!=B->GetMatchEpoch()){for(int32 I:Pool.ActiveIndices())Stop(Slots[I]);Pool.Reset();ByKey.Reset();Epoch=B->GetMatchEpoch();}
    auto* Found=ByKey.Find(K);FGuLiPresentationSlotHandle Handle;
    if(!Found){Handle=Pool.Acquire();if(Slots.Num()<Pool.Capacity())Slots.SetNum(Pool.Capacity());auto& V=Slots[Handle.Index];V={};V.Handle=Handle;V.Key=K;ByKey.Add(K,Handle);}
    else Handle=*Found;
    auto& V=Slots[Handle.Index];V.Host=GuLiNetBridge::ToUE(S.Host);V.Until=H->GetLocalLogicalSeconds()+double(int64(S.Expires)-int64(H->GetObjectLogicalTick(K)))/GuLiNet::TicksPerSecond;
    for(const auto& Slot:S.Slots)if(Slot.Frozen.Id==S.Root){if(V.Vfx!=int32(Slot.Frozen.Vfx))Stop(V);V.Vfx=int32(Slot.Frozen.Vfx);}
    const auto Burst=GuLiNetBridge::ToUE(S.LastBurst);
    if(!Bootstrap&&Burst.IsValid()&&V.LastBurst!=Burst)
    {
        if(auto* P=GetWorld()->GetSubsystem<UGuLiCombatEffectPresentationSubsystem>())
        {FGuLiImpactEvent Event;Event.Identity={Burst,Epoch,1,6};Event.VfxId=int32(S.BurstVfx);Event.Position=FVector(S.BurstX,S.BurstY,S.BurstZ);Event.Scale=FVector(FMath::Max(.01f,S.BurstRadius/100.f));Event.Seed=int32(GetTypeHash(Burst));P->EnqueueImpact(Event);}
    }
    V.LastBurst=Burst;
    if(!S.Active)Remove(K);
}
void UGuLiBuffPresentation::Tick(float)
{
    auto* H=GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>();auto* D=GetWorld()->GetSubsystem<UGuLiDamageLedgerSubsystem>();auto* LOD=GetWorld()->GetSubsystem<UGuLiCommanderLODSubsystem>();
    if(!H||!D)return;const double Now=H->GetLocalLogicalSeconds();TArray<FGuLiHybridObjectKey> Ended;
    for(int32 I:Pool.ActiveIndices())
    {
        auto& V=Slots[I];FGuLiTargetHandle Host;FGuLiCombatTargetSnapshot T;
        if(Now>V.Until||!H->ResolveObjectTarget(V.Host,Host)||!D->TryGetTargetSnapshot(Host,T)||!T.bAlive){Ended.Add(V.Key);continue;}
        const FVector Center=T.GetHitCenter();const float Radius=FMath::Max(1.f,T.CollisionRadius);FGuLiCommanderLODQuery Q;Q.Bounds=FBox::BuildAABB(Center,FVector(FMath::Max(100.f,Radius)*3));
        const bool Visible=GuLiClientPresentation::CanPresent(GetWorld())&&(!LOD||LOD->EvaluateWorldEffectBounds(Q,20000).bVisible);
        if(!Visible)
        {if(V.Offscreen<0)V.Offscreen=Now;if(Now-V.Offscreen>=GuLiClientPresentation::OffscreenGraceSeconds)Stop(V);continue;}
        V.Offscreen=-1;
        if(!V.Component)
        {
            auto* System=GuLiVfx::Load<UNiagaraSystem>(this,V.Vfx);if(!System)continue;
            V.Component=UNiagaraFunctionLibrary::SpawnSystemAtLocation(GetWorld(),System,Center,T.Rotation,GuLiVfx::Scale(this,V.Vfx,FVector(Radius/100.f)),false,false,ENCPoolMethod::ManualRelease,true);
            if(V.Component){V.Component->SetVariableVec3(TEXT("User.TargetExtent"),FVector(Radius));V.Component->SetVariableFloat(TEXT("User.TargetRadius"),Radius);V.Component->Activate(true);}
        }
        if(V.Component){V.Component->SetWorldLocationAndRotation(Center,T.Rotation);V.Component->SetWorldScale3D(GuLiVfx::Scale(this,V.Vfx,FVector(Radius/100.f)));V.Component->SetPaused(H->IsBattlePaused());}
    }
    for(const auto& K:Ended)Remove(K);
    for(int32 I=Tails.Num()-1;I>=0;--I)if(!Tails[I]||Tails[I]->IsComplete()){if(Tails[I])Tails[I]->ReleaseToPool();Tails.RemoveAtSwap(I);}
}
void UGuLiBuffPresentation::Deinitialize()
{for(int32 I:Pool.ActiveIndices())Stop(Slots[I]);for(const auto& Entry:Tails)if(auto* C=Entry.Get()){C->DeactivateImmediate();C->ReleaseToPool();}Tails.Reset();Slots.Reset();ByKey.Reset();Pool.Reset();Super::Deinitialize();}
