#include "Gameplay/Buffs/GuLiBuffPresentation.h"
#include "Gameplay/Buffs/GuLiBuffDefinition.h"
#include "Gameplay/Data/Generated/GuLiStrikeBuffsTableRows.h"
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
#include "Engine/DataTable.h"
#include "Subsystems/SubsystemCollection.h"

bool UGuLiBuffPresentation::ShouldCreateSubsystem(UObject* Outer) const
{const auto* W=Cast<UWorld>(Outer);return Super::ShouldCreateSubsystem(Outer)&&W&&W->GetNetMode()!=NM_DedicatedServer&&(W->WorldType==EWorldType::Game||W->WorldType==EWorldType::PIE);}
void UGuLiBuffPresentation::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    auto* Registry=Collection.InitializeDependency<UGuLiVfxRegistrySubsystem>();
    const auto* Table=GetDefault<UGuLiBuffSettings>()->Buffs.LoadSynchronous();
    if(!Registry||!Table||Table->GetRowStruct()!=FGuLiStrikeBuffsBuffsRow::StaticStruct())
    {UE_LOG(LogTemp,Error,TEXT("Buff presentation preparation failed: Buff catalog or VFX registry unavailable."));return;}
    // Preparation owns synchronous loading. Both status and one-shot playback are cache-only.
    for(const auto& Pair:Table->GetRowMap())
    {
        const auto& Row=*reinterpret_cast<const FGuLiStrikeBuffsBuffsRow*>(Pair.Value);
        GuLiNet::Buffs::Definition Definition;FString Error;
        if(!UGuLiBuffHandler::Load(Row.Id,Definition,Error))
        {UE_LOG(LogTemp,Error,TEXT("Buff %d presentation preparation failed: %s"),Row.Id,*Error);continue;}
        for(uint32 Id:{Definition.Vfx,Definition.BulletVfx})
        {
            if(!Id||PreparedVfx.Contains(int32(Id)))continue;
            bool Ready=true;
            for(int32 Tier=0;Tier<3;++Tier)
                Ready=Registry->LoadNiagaraForLOD(int32(Id),EGuLiCommanderLODLevel(Tier),false,true)!=nullptr&&Ready;
            if(Ready)PreparedVfx.Add(int32(Id));
            else UE_LOG(LogTemp,Error,TEXT("Buff %d VfxId %u could not be prepared; check its resource and LOD references."),Row.Id,Id);
            if(auto* System=Registry->LoadNiagaraForLOD(int32(Id),EGuLiCommanderLODLevel::Full,false,false))
            {
                const float Expand=Registry->NiagaraFloat(System,TEXT("User.ExpandSeconds"),0);
                const float Fade=Registry->NiagaraFloat(System,TEXT("User.FadeSeconds"),0);
                if(FMath::IsFinite(Expand)&&FMath::IsFinite(Fade)&&Expand>0&&Fade>0&&Expand+Fade<=10)
                    BurstLifetimes.Add(int32(Id),Expand+Fade);
            }
        }
    }
}
FString UGuLiBuffPresentation::GetDiagnostics() const
{
    return FString::Printf(TEXT("{\"epoch\":%u,\"prepared_vfx\":%d,\"active_status\":%d,\"tails\":%d,\"burst_receipts\":%llu,\"burst_accepted\":%llu,\"burst_suppressed\":%llu,\"burst_rejected\":%llu}"),
        Epoch,PreparedVfx.Num(),ByKey.Num(),Tails.Num(),BurstReceipts,BurstAccepted,BurstSuppressed,BurstRejected);
}
TStatId UGuLiBuffPresentation::GetStatId() const {RETURN_QUICK_DECLARE_CYCLE_STAT(UGuLiBuffPresentation,STATGROUP_Tickables);}
void UGuLiBuffPresentation::Stop(FGuLiBuffVisualSlot& V)
{if(V.Component){V.Component->Deactivate();Tails.Add(V.Component);V.Component=nullptr;}}
void UGuLiBuffPresentation::Remove(const FGuLiHybridObjectKey& K,bool ForgetEvents)
{if(const auto* H=ByKey.Find(K)){const auto Handle=*H;if(Pool.IsLive(Handle)){Stop(Slots[Handle.Index]);Pool.Release(Handle);}ByKey.Remove(K);}if(ForgetEvents)LastBursts.Remove(K);}
void UGuLiBuffPresentation::Apply(const FGuLiHybridObjectKey& K,const GuLiNet::Buffs::State& S,bool Bootstrap,bool SyncEvents)
{
    auto* H=GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>();const auto* B=GetWorld()->GetGameState<AGuLiBattleGameState>();if(!H||!B)return;
    if(Epoch!=B->GetMatchEpoch()){for(int32 I:Pool.ActiveIndices())Stop(Slots[I]);Pool.Reset();ByKey.Reset();LastBursts.Reset();Epoch=B->GetMatchEpoch();BurstReceipts=BurstAccepted=BurstSuppressed=BurstRejected=0;}
    auto* Found=ByKey.Find(K);FGuLiPresentationSlotHandle Handle;
    if(!Found){Handle=Pool.Acquire();if(Slots.Num()<Pool.Capacity())Slots.SetNum(Pool.Capacity());auto& V=Slots[Handle.Index];V={};V.Handle=Handle;V.Key=K;ByKey.Add(K,Handle);}
    else Handle=*Found;
    auto& V=Slots[Handle.Index];V.Host=GuLiNetBridge::ToUE(S.Host);V.Until=H->GetLocalLogicalSeconds()+double(int64(S.Expires)-int64(H->GetObjectLogicalTick(K)))/GuLiNet::TicksPerSecond;
    for(const auto& Slot:S.Slots)if(Slot.Frozen.Id==S.Root){if(V.Vfx!=int32(Slot.Frozen.Vfx))Stop(V);V.Vfx=int32(Slot.Frozen.Vfx);}
    const auto Burst=GuLiNetBridge::ToUE(S.LastBurst);
    // Ordinary state fragments arrive before their live commit callback. They
    // update attached flames, but must not consume the commit's one-shot event.
    if(SyncEvents&&Burst.IsValid()&&LastBursts.FindRef(K)!=Burst)
    {
        ++BurstReceipts;
        const double Age=FMath::Max(0.,double(int64(H->GetObjectLogicalTick(K))-int64(S.BurstAt))/GuLiNet::TicksPerSecond);
        const float* Lifetime=BurstLifetimes.Find(int32(S.BurstVfx));
        if(Bootstrap||(Lifetime&&Age>=*Lifetime))++BurstSuppressed;
        else if(!Lifetime)
        {
            ++BurstRejected;
            if(!MissingStatusVfx.Contains(int32(S.BurstVfx)))
            {MissingStatusVfx.Add(int32(S.BurstVfx));UE_LOG(LogTemp,Error,TEXT("Buff burst VfxId %u has no prepared expansion/fade timing."),S.BurstVfx);}
        }
        else if(auto* P=GetWorld()->GetSubsystem<UGuLiCombatEffectPresentationSubsystem>())
        {
            FGuLiImpactEvent Event;Event.Identity={Burst,Epoch,1,6};Event.VfxId=int32(S.BurstVfx);
            Event.Position=FVector(S.BurstX,S.BurstY,S.BurstZ);Event.Seed=int32(GetTypeHash(Burst));
            Event.bRadialPulse=true;Event.RadiusCentimeters=S.BurstRadius;Event.StartRadiusCentimeters=S.BurstStartRadius;
            // Source age above rejects stale events. An accepted live receipt plays
            // the complete expand/fade locally; transport delay must not cut it short.
            Event.Lifetime=*Lifetime;Event.OccurredWorldSeconds=GetWorld()->GetTimeSeconds();
            if(P->EnqueueImpact(Event))++BurstAccepted;else ++BurstRejected;
        }
    }
    if(SyncEvents)LastBursts.Add(K,Burst);
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
            auto* System=GuLiVfx::Load<UNiagaraSystem>(this,V.Vfx,false);
            if(!System)
            {if(!MissingStatusVfx.Contains(V.Vfx)){MissingStatusVfx.Add(V.Vfx);UE_LOG(LogTemp,Error,TEXT("Buff status VfxId %d was not prepared; playback skipped without hot-path loading."),V.Vfx);}continue;}
            V.Component=UNiagaraFunctionLibrary::SpawnSystemAtLocation(GetWorld(),System,Center,T.Rotation,GuLiVfx::Scale(this,V.Vfx,FVector(Radius/100.f)),false,false,ENCPoolMethod::ManualRelease,true);
            if(V.Component){V.Component->SetVariableVec3(TEXT("User.TargetExtent"),FVector(Radius));V.Component->SetVariableFloat(TEXT("User.TargetRadius"),Radius);V.Component->Activate(true);}
        }
        if(V.Component){V.Component->SetWorldLocationAndRotation(Center,T.Rotation);V.Component->SetWorldScale3D(GuLiVfx::Scale(this,V.Vfx,FVector(Radius/100.f)));V.Component->SetPaused(H->IsBattlePaused());}
    }
    for(const auto& K:Ended)Remove(K);
    for(int32 I=Tails.Num()-1;I>=0;--I)if(!Tails[I]||Tails[I]->IsComplete()){if(Tails[I])Tails[I]->ReleaseToPool();Tails.RemoveAtSwap(I);}
}
void UGuLiBuffPresentation::Deinitialize()
{for(int32 I:Pool.ActiveIndices())Stop(Slots[I]);for(const auto& Entry:Tails)if(auto* C=Entry.Get()){C->DeactivateImmediate();C->ReleaseToPool();}Tails.Reset();Slots.Reset();ByKey.Reset();LastBursts.Reset();Pool.Reset();PreparedVfx.Reset();BurstLifetimes.Reset();MissingStatusVfx.Reset();Super::Deinitialize();}
