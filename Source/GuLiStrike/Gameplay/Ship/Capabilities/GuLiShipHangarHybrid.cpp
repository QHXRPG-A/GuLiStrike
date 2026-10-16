#include "Gameplay/Ship/Capabilities/GuLiShipHangarCapabilityComponent.h"
#include "Gameplay/Ship/Build/GuLiShipAssemblyComponent.h"
#include "Gameplay/Wingman/GuLiWingmanSimulationSubsystem.h"
#include "Gameplay/Wingman/Private/GuLiHybridWingmanIdentity.h"
#include "Network/GuLiHybridActorComponent.h"
#include "Network/GuLiHybridActorWorldSubsystem.h"
#include "Battle/Framework/GuLiBattleGameState.h"
#include "Engine/World.h"

void UGuLiShipHangarCapabilityComponent::AdvanceHybrid(uint64 Tick,double)
{
    HybridAdvanceTick=Tick;
    const auto* Binding=UGuLiHybridActorComponent::Find(GetOwner());auto* H=GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>();
    const auto* Factories=GetWorld()->GetSubsystem<UGuLiHybridActorWorldSubsystem>();
    if (!Binding || !Binding->CanSimulate() || !H || !Factories || !H->CanPublishFacts() || !Binding->GetState().Controller.IsValid()
        || Binding->GetState().bDormant || Binding->GetState().Health.bDead || Tick<NextHybridSpawnTick)
    {if (Tick>=NextHybridSpawnTick) HybridMemberStatus=TEXT("waiting_for_source_authority");return;}
    const auto* Assembly=GetOwner()->FindComponentByClass<UGuLiShipAssemblyComponent>();if (!Assembly || !Assembly->Catalog) return;
    const auto* GroupDefinition=Assembly->Catalog->Groups.FindByPredicate([&](const auto& G)
    {return G.bExecutable && G.GroupId==GetSourceGroupId() && G.Capabilities.ContainsByPredicate([&](const auto& C){return C.CapabilityId==GetCapabilityId() && C.Configuration==CapabilityConfiguration;});});
    if (!GroupDefinition) {H->Suspend(TEXT("Active hangar is absent from the committed catalog"));return;}
    const auto& Manifest=Factories->GetManifest();const auto& Source=Binding->GetState();
    const FString Profile=GuLiHybridWingman::Profile(Assembly->UpgradeDomain,GroupDefinition->ConfigurationId,GetCapabilityId());
    const FString Rule=Manifest.Strings.FindRef(Profile+TEXT("/spawn_rule"));if (Rule.IsEmpty()) {H->Suspend(TEXT("Hangar has no admitted member factory"));return;}
    const auto Group=GuLiHybridWingman::Group(Manifest,Source.Key,Profile);const auto* Definition=Cast<UGuLiShipHangarDefinition>(CapabilityConfiguration);
    if (!Definition || !Definition->AbilitySet) return;
    auto Context=ProjectionContext;Context.ShipInstanceId=Group.ShipInstanceId;Context.ShipGeneration=Group.ShipGeneration;
    Context.GroupGeneration=Group.GroupGeneration;Context.MatchEpoch=GetWorld()->GetGameState<AGuLiBattleGameState>()->GetMatchEpoch();
    Context.OwnerPlayerGuid=Source.Controller;Context.Team=Source.Team;Context.WingmanTypeId=Definition->AbilitySet->WingmanTypeId;
    Context.FormationCommandRevision=FMath::Max(1u,Context.FormationCommandRevision);Context.EffectiveClientSimTick=FMath::Max(1u,Context.EffectiveClientSimTick);
    SetProjectionContext(Context);FGuLiGroupAbilityConfigSnapshot Configuration;if (!BuildGroupAbilityConfigSnapshot(Configuration) || !Configuration.IsUsableByLeaseOwner())
    {HybridMemberStatus=TEXT("incompatible_hangar_projection");H->Suspend(TEXT("Committed Hangar has an invalid member projection"));return;}
    TArray<FGuLiHybridObjectState> Directory;H->GetObjects(Directory);
    TArray<uint8> Finished;
    for (const auto& [Slot,Pending]:HybridSpawns)
    {
        const auto Key=GuLiHybridWingman::MemberKey(Manifest,Source.Key,Profile,Slot,Pending.Generation);FGuLiHybridObjectState Member;
        FGuLiHybridTransactionResult Result;
        if (H->GetObject(Key,Member) && Member.Parent==Source.Key && Member.SpawnEvent==Pending.Event) Finished.Add(Slot);
        else if (H->FindTransactionResult(Pending.Event,Result) && Result.LedgerRevision>0 && !Result.IsCommitted())
        {Finished.Add(Slot);NextHybridSpawnTick=Tick+60;}
    }
    for (const auto Slot:Finished) HybridSpawns.Remove(Slot);
    if (Tick<NextHybridSpawnTick) return;
    for (uint8 Slot=0;Slot<GULI_WINGMAN_GROUP_SIZE;++Slot)
    {
        if (HybridSpawns.Contains(Slot)) continue;
        const auto Stable=GuLiHybridWingman::MemberKey(Manifest,Source.Key,Profile,Slot);const FGuLiHybridObjectState* Latest=nullptr;
        for (const auto& O:Directory) if (O.Key.Kind==Stable.Kind && O.Key.StableId==Stable.StableId && (!Latest || O.Key.LifeGeneration>Latest->Key.LifeGeneration)) Latest=&O;
        const uint32 RetiredLife=H->GetLatestRetiredLife(Stable);
        if (Latest && Latest->LifeState<2 && Latest->Key.LifeGeneration>RetiredLife) {HybridReplenishment.Remove(Slot);continue;}
        uint32 Generation=1;
        if (RetiredLife && (!Latest || RetiredLife>=Latest->Key.LifeGeneration))
        {
            if (RetiredLife==MAX_uint32) {H->Suspend(TEXT("Wingman life generation exhausted"));return;}
            Generation=RetiredLife+1;
        }
        else if (Latest)
        {
            if (Latest->Key.LifeGeneration==MAX_uint32) {H->Suspend(TEXT("Wingman life generation exhausted"));return;}
            if (Latest->LifeState!=3)
            {
                auto* Schedule=HybridReplenishment.Find(Slot);
                if (!Schedule || Schedule->Generation!=Latest->Key.LifeGeneration)
                {HybridReplenishment.Add(Slot,{Latest->Key.LifeGeneration,Tick+900});continue;}
                if (Tick<Schedule->DueAt) continue;
            }
            // A vacant Air seat retired the whole group. Takeover regenerates
            // every slot immediately, with a new life and no old inputs.
            Generation=Latest->Key.LifeGeneration+1;
        }
        TArray<FVector> Positions;
        if (!UGuLiWingmanSimulationSubsystem::PlanExternalFormationPositions(GetWorld(),GetOwner()->GetActorTransform(),Configuration.FormationRuntime,Positions,{},&HybridMemberStatus)
            || !Positions.IsValidIndex(Slot)) {NextHybridSpawnTick=Tick+60;return;}
        FGuLiHybridObjectRequest Request;Request.Source=Source.Key;Request.Target=GuLiHybridWingman::MemberKey(Manifest,Source.Key,Profile,Slot,Generation);
        Request.Related=Source.Key;Request.Event=FGuid::NewGuid();Request.Rule=FName(*Rule);Request.Operation=EGuLiHybridObjectOperation::Spawn;
        Request.Location=Positions[Slot];Request.FacingYaw=GetOwner()->GetActorRotation().Yaw;Request.StartedAt=Tick;
        HybridMemberStatus=TEXT("member_admission_pending");
        HybridSpawns.Add(Slot,{Slot,Generation,Request.Event,Tick,Request.Location,Request.FacingYaw});
        if (H->SubmitObjectOperation(Request)!=Request.Event) {HybridSpawns.Remove(Slot);NextHybridSpawnTick=Tick+6;}
        return; // Each proposal reserves cost; do not create a process-local squad.
    }
    HybridMemberStatus=TEXT("all_members_registered_or_waiting_replenishment");
}
