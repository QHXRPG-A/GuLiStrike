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
    if (HybridMemberGroup.ShipInstanceId != Source.Key.StableId || HybridMemberGroup.ShipGeneration != Source.Key.LifeGeneration
        || HybridMemberProfile != Profile)
    {
        HybridMemberGroup = GuLiHybridWingman::Group(Manifest, Source.Key, Profile);
        HybridMemberProfile = Profile;
        HybridMemberKeys.Reset(GULI_WINGMAN_GROUP_SIZE);
        for (uint8 Slot = 0; Slot < GULI_WINGMAN_GROUP_SIZE; ++Slot)
            HybridMemberKeys.Add(GuLiHybridWingman::MemberKey(Manifest, Source.Key, Profile, Slot));
    }
    const auto& Group = HybridMemberGroup;
    const auto* Definition=Cast<UGuLiShipHangarDefinition>(CapabilityConfiguration);
    if (!Definition || !Definition->AbilitySet) return;
    auto Context=ProjectionContext;Context.ShipInstanceId=Group.ShipInstanceId;Context.ShipGeneration=Group.ShipGeneration;
    Context.GroupGeneration=Group.GroupGeneration;Context.MatchEpoch=GetWorld()->GetGameState<AGuLiBattleGameState>()->GetMatchEpoch();
    Context.OwnerPlayerGuid=Source.Controller;Context.Team=Source.Team;Context.WingmanTypeId=Definition->AbilitySet->WingmanTypeId;
    Context.FormationCommandRevision=FMath::Max(1u,Context.FormationCommandRevision);Context.EffectiveClientSimTick=FMath::Max(1u,Context.EffectiveClientSimTick);
    SetProjectionContext(Context);FGuLiGroupAbilityConfigSnapshot Configuration;if (!BuildGroupAbilityConfigSnapshot(Configuration) || !Configuration.IsUsableByLeaseOwner())
    {HybridMemberStatus=TEXT("incompatible_hangar_projection");H->Suspend(TEXT("Committed Hangar has an invalid member projection"));return;}
    TArray<uint8> Finished;
    for (const auto& [Slot,Pending]:HybridSpawns)
    {
        auto Key = HybridMemberKeys[Slot];
        Key.LifeGeneration = Pending.Generation;
        FGuLiHybridObjectState Member;
        FGuLiHybridTransactionResult Result;
        if (H->GetObjectView(Key,Member) && Member.Parent==Source.Key && Member.SpawnEvent==Pending.Event) Finished.Add(Slot);
        else if (H->FindTransactionResult(Pending.Event,Result) && Result.LedgerRevision>0 && !Result.IsCommitted())
        {Finished.Add(Slot);NextHybridSpawnTick=Tick+60;}
    }
    for (const auto Slot:Finished) HybridSpawns.Remove(Slot);
    if (Tick<NextHybridSpawnTick) return;
    for (uint8 Slot=0;Slot<GULI_WINGMAN_GROUP_SIZE;++Slot)
    {
        if (HybridSpawns.Contains(Slot)) continue;
        const auto& Stable = HybridMemberKeys[Slot];
        uint8 LatestState = 0;
        const uint32 LatestLife = H->FindLatestObjectGeneration(Stable, LatestState);
        const uint32 RetiredLife = H->GetLatestRetiredLife(Stable);
        if (LatestLife > RetiredLife && LatestState < 2) {HybridReplenishment.Remove(Slot);continue;}
        uint32 Generation=1;
        if (RetiredLife && RetiredLife >= LatestLife)
        {
            if (RetiredLife==MAX_uint32) {H->Suspend(TEXT("Wingman life generation exhausted"));return;}
            Generation=RetiredLife+1;
        }
        else if (LatestLife)
        {
            if (LatestLife==MAX_uint32) {H->Suspend(TEXT("Wingman life generation exhausted"));return;}
            if (LatestState!=3)
            {
                auto* Schedule=HybridReplenishment.Find(Slot);
                if (!Schedule || Schedule->Generation!=LatestLife)
                {HybridReplenishment.Add(Slot,{LatestLife,Tick+900});continue;}
                if (Tick<Schedule->DueAt) continue;
            }
            // A vacant Air seat retired the whole group. Takeover regenerates
            // every slot immediately, with a new life and no old inputs.
            Generation=LatestLife+1;
        }
        if (Tick < ClearanceRetryTick.FindRef(Slot)) continue;
        const int32 SpawnSlot=Slot;
        TArray<FVector> Positions;
        if (!UGuLiWingmanSimulationSubsystem::PlanExternalFormationPositions(GetWorld(),GetOwner()->GetActorTransform(),Configuration.FormationRuntime,Positions,MakeArrayView(&SpawnSlot,1),&HybridMemberStatus)
            || !Positions.IsValidIndex(Slot)) {ClearanceRetryTick.Add(Slot,Tick+30);continue;}
        ClearanceRetryTick.Remove(Slot);
        FGuLiHybridObjectRequest Request;Request.Source=Source.Key;Request.Target=Stable;Request.Target.LifeGeneration=Generation;
        Request.Related=Source.Key;Request.Event=FGuid::NewGuid();Request.Rule=FName(*Rule);Request.Operation=EGuLiHybridObjectOperation::Spawn;
        Request.Location=Positions[Slot];Request.FacingYaw=GetOwner()->GetActorRotation().Yaw;Request.StartedAt=Tick;
        HybridMemberStatus=TEXT("member_admission_pending");
        HybridSpawns.Add(Slot,{Slot,Generation,Request.Event,Tick,Request.Location,Request.FacingYaw});
        if (H->SubmitObjectOperation(Request)!=Request.Event) {HybridSpawns.Remove(Slot);NextHybridSpawnTick=Tick+6;}
        return; // Each proposal reserves cost; do not create a process-local squad.
    }
    HybridMemberStatus=TEXT("all_members_registered_or_waiting_replenishment");
}
