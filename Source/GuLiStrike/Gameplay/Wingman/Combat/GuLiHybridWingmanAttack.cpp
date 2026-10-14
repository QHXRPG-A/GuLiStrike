#include "Gameplay/Wingman/GuLiWingmanSimulationSubsystem.h"
#include "Gameplay/Wingman/GuLiWingmanPawn.h"
#include "Gameplay/Wingman/Combat/GuLiWingmanTargeting.h"
#include "Gameplay/Ship/GuLiStrikeShip.h"
#include "Gameplay/Ship/Capabilities/GuLiShipHangarCapabilityComponent.h"
#include "Gameplay/Data/Generated/GuLiStrikeShipTableRows.h"
#include "Gameplay/Data/GuLiSpellFieldDataSubsystem.h"
#include "Gameplay/CombatEffects/GuLiCombatEffectRuntimeSubsystem.h"
#include "Gameplay/CombatEffects/GuLiGroundWarningSubsystem.h"
#include "Network/GuLiHybridActorComponent.h"
#include "Network/GuLiHybridActorWorldSubsystem.h"
#include "Network/GuLiHybridManifest.h"
#include "Engine/World.h"

namespace
{
void AdvanceRevision(uint32& Revision) { if (++Revision==0) ++Revision; }
FGuLiWingmanAttackTarget ManualTarget(UWorld& World,UGuLiDamageLedgerSubsystem& Ledger,
    const FGuLiWingmanAttackTarget& Requested,EGuLiTeam Team,double Now)
{
    FGuLiCombatTargetSnapshot Target;
    if (!Requested.Target.IsValid() || !Ledger.TryGetTargetSnapshot(Requested.Target,Target)
        || !Target.bAlive || Target.Team==Team || Target.Team==EGuLiTeam::Unassigned) return {};
    auto Result=Requested;Result.Location=Target.Location;Result.Radius=FMath::Max(0.f,Target.CollisionRadius);
    Result.ServerTime=Now;Result.bSpecified=true;
    Result.bGround=Target.Handle.Kind==EGuLiTargetKind::CommanderSoldier || Target.Handle.Kind==EGuLiTargetKind::GroundActor;
    if (!Result.Revision) Result.Revision=1;
    if (Result.bGround)
    {
        FHitResult Hit;FCollisionQueryParams Query(SCENE_QUERY_STAT(GuLiHybridWingmanGroundTarget),false);
        if (Target.CollisionActor.IsValid()) Query.AddIgnoredActor(Target.CollisionActor.Get());
        if (!World.LineTraceSingleByObjectType(Hit,Target.Location+FVector(0,0,5000),Target.Location-FVector(0,0,500000),
            FCollisionObjectQueryParams(ECC_WorldStatic),Query)) return {};
        Result.Location=Hit.ImpactPoint;
    }
    return Result;
}
}

void UGuLiWingmanSimulationSubsystem::AdvanceHybridAttack(FGuLiWingmanLocalGroupRuntime& Runtime,
    uint32 OwnerVersion,uint64 Tick,double Now)
{
    auto* W=GetWorld();auto* H=W->GetSubsystem<UGuLiHybridWorldSubsystem>();
    auto* Ledger=W->GetSubsystem<UGuLiDamageLedgerSubsystem>();
    auto* Effects=W->GetSubsystem<UGuLiCombatEffectRuntimeSubsystem>();
    auto* Pawn=Runtime.Pawns.Num()==1?Runtime.Pawns[0].Get():nullptr;
    auto* Ship=Cast<AGuLiStrikeShip>(Runtime.CarrierActor.Get());
    if (!H || !Ledger || !Effects || !Pawn || !Ship || !UGuLiHybridActorComponent::HasSimulation(Pawn)
        || !UGuLiHybridActorComponent::HasSimulation(Ship)) return;
    const auto Member=Pawn->GetWingmanHandle();
    const auto& Configuration=Runtime.AbilityConfig;
    if (Now>=Runtime.NextHybridTargetScan)
    {
        FGuLiWingmanTargetingTuning Tuning;
        if (const auto* Row=Ship->WingmanTargetingRow.GetRow<FGuLiStrikeShipWingmanTargetingRow>(TEXT("HybridWingmanTargeting")))
        {
            Tuning.AcquireRadiusCentimeters=Row->AcquireRadiusCentimeters;Tuning.ReleaseRadiusCentimeters=Row->ReleaseRadiusCentimeters;
            Tuning.GuardRejoinFraction=Row->GuardRejoinFraction;Tuning.ScanIntervalSeconds=Row->ScanIntervalSeconds;
        }
        if (!Tuning.IsWellFormed()) {H->Suspend(TEXT("Wingman targeting configuration is invalid"));return;}
        TArray<TSharedPtr<FGuLiWingmanLocalGroupRuntime>> Group;
        TArray<FGuLiWingmanTargetMember> Members;TArray<FGuLiWingmanAutoTargetAssignment> Previous,Assignments;
        for (const auto& Entry:IndependentRuntimes)
        {
            auto Other=Entry.Value.Pin();auto* OtherPawn=Other && Other->Pawns.Num()==1?Other->Pawns[0].Get():nullptr;
            if (Entry.Key.Flight.Group!=Member.Flight.Group || !OtherPawn || !UGuLiHybridActorComponent::HasSimulation(OtherPawn)
                || !OtherPawn->GetRuntimeState().Dynamics.bAlive) continue;
            const auto* Old=GuLiWingmanTargeting::ResolveTargetForEmitter(Other->HybridAttack,Entry.Key);
            Members.Add({Entry.Key,OtherPawn->GetActorLocation(),Old?Old->Target:FGuLiTargetHandle{},Old && Old->IsValid()});
            Previous.Append(Other->HybridAttack.AutomaticTargets);Group.Add(Other);
        }
        FGuLiCombatTargetSnapshot Source;
        const auto* Binding=Ship->FindComponentByClass<UGuLiHybridActorComponent>();
        FGuLiTargetHandle SourceHandle;
        if (!Binding || !H->ResolveObjectTarget(Binding->GetObjectKey(),SourceHandle) || !Ledger->TryGetSourceSnapshot(SourceHandle,Source)) return;
        const auto Manual=ManualTarget(*W,*Ledger,Ship->GetWingmanAttackTarget(),Source.Team,Now);
        if (!Manual.IsValid())
        {
            TArray<FGuLiCombatTargetSnapshot> Targets;Ledger->GetTargetSnapshots(Targets);
            // This cache is derived terrain data. Private target revisions are retained by the members.
            TMap<FGuLiTargetHandle,FGuLiWingmanGroundCorridorCache> Corridors;
            if (!GuLiWingmanTargeting::SelectAutomaticTargets(W,Source,Configuration,Tuning,Now,Members,Targets,Previous,Corridors,Assignments))
            {H->Suspend(TEXT("Wingman group target selection failed"));return;}
        }
        for (const auto& Other:Group)
        {
            const auto OtherMember=Other->Pawns[0]->GetWingmanHandle();auto& State=Other->HybridAttack;
            const auto* Before=GuLiWingmanTargeting::ResolveTargetForEmitter(State,OtherMember);
            const auto BeforeHandle=Before?Before->Target:FGuLiTargetHandle{};
            uint32 Revision=Before?Before->Revision:State.Revision;
            const auto* Assigned=Assignments.FindByPredicate([&](const auto& A){return A.Emitter==OtherMember;});
            const auto AfterHandle=Manual.IsValid()?Manual.Target:(Assigned?Assigned->Target.Target:FGuLiTargetHandle{});
            if (BeforeHandle!=AfterHandle || !Revision) AdvanceRevision(Revision);
            State.Target=Manual;State.AutomaticTargets.Reset();
            if (Assigned) {auto Copy=*Assigned;Copy.Target.Revision=Revision;State.AutomaticTargets.Add(Copy);}
            if (State.Target.IsValid()) State.Target.Revision=Revision;
            State.Revision=Revision;Other->NextHybridTargetScan=Now+Tuning.ScanIntervalSeconds;
            if (BeforeHandle!=AfterHandle) Effects->CancelWingmanGunBurst(OtherMember);
        }
    }
    TickIndependentAttackRuns(Runtime,Runtime.HybridAttack,OwnerVersion,uint32(Tick),Now,
        !Runtime.bExternalActionsLocked && Pawn->GetRuntimeState().Dynamics.bAlive);
    if (Runtime.PendingAttackShots.IsEmpty()) return;
    const auto* Actors=W->GetSubsystem<UGuLiHybridActorWorldSubsystem>();
    FGuLiHybridObjectKey Key;FGuLiHybridObjectState Object;
    if (!Actors || !H->ResolveTarget(GuLiCombatTargets::MakeWingmanTargetHandle(Member),Key) || !H->GetObject(Key,Object)) return;
    const auto* Path=Actors->GetManifest().Strings.Find(Object.Definition.ToString()+TEXT("/configuration"));
    const auto* Definition=Path?LoadObject<UGuLiShipHangarDefinition>(nullptr,**Path):nullptr;
    TArray<FGuLiShipAbilityGrant> Grants;FString Error;
    if (!Definition || !Definition->AbilitySet || !Definition->AbilitySet->ResolveLoadout(Definition->Loadout,Grants,&Error))
    {H->Suspend(TEXT("Wingman attack profile cannot resolve its admitted definition"));return;}
    const auto Shots=MoveTemp(Runtime.PendingAttackShots);Runtime.PendingAttackShots.Reset();
    for (const auto& Entry:Shots)
    {
        const auto& Shot=Entry.Value;
        const auto* Channel=Configuration.WeaponChannels.FindByPredicate([&](const auto& C){return C.bEnabled && C.Binding.SlotId==Shot.SlotId;});
        if (!Channel || Shot.MemberIndex!=Member.MemberIndex || Entry.Key!=Member.Flight.FlightIndex
            || Shot.ProfileRevision!=Channel->ProfileRevision || Shot.LoadoutRevision!=Configuration.LoadoutRevision) continue;
        const auto& Weapon=Channel->Runtime;const auto& Profile=Weapon.Attack;
        const bool Ground=Profile.Pattern==EGuLiWingmanAttackPattern::GroundDive;
        auto* Last=Runtime.HybridAttack.Checkpoints.FindByPredicate([&](const auto& C){return C.Emitter==Member && C.SlotId==Shot.SlotId;});
        if (Last && (Shot.RunId<Last->RunId || (Shot.RunId==Last->RunId && int32(Shot.ShotIndex)<=Last->LastShotIndex))) continue;
        FGuLiCombatTargetSnapshot LiveTarget;
        if (!Ledger->TryGetTargetSnapshot(Shot.Target.Target,LiveTarget) || !LiveTarget.bAlive
            || (!Ground && Effects->IsHybridWingmanBurstBusy(Member,Shot.SlotId))) continue;
        FGuLiCombatAttackRequest Request;Request.ExecutorId=Profile.ExecutorId;Request.SourceTransform=Pawn->GetActorTransform();
        Request.TargetLocation=Shot.Target.Location;Request.MuzzleOffset=Profile.Muzzle;Request.ShotOrdinal=Shot.ShotIndex;
        if (Ground)
        {
            FGuLiWingmanGroundRunPath Run;
            if (!GuLiWingmanAttack::BuildGroundPath(Shot.Target.Location,Shot.ApproachDirection,Profile,
                Configuration.FormationRuntime.MaximumTurnRateDegreesPerSecond,Run)) continue;
            Request.TargetLocation=GuLiWingmanAttack::StripPoint(Run,Profile.StripLength,Shot.ShotIndex,Profile.MissileCount);
            const auto* Grant=Grants.FindByPredicate([&](const auto& G){return G.GetEffectiveWeaponSlotId()==Shot.SlotId;});
            const auto* Fields=W->GetSubsystem<UGuLiSpellFieldDataSubsystem>();
            const auto* Field=Fields?Fields->FindCombatField(Weapon.EffectConfigId):nullptr;
            if (!Grant || !Grant->WeaponDefinition || !Field) continue;
            Request.Projectile=Grant->WeaponDefinition->ResolveAttackProjectile();
            Request.GroundWarningStyle=Request.Projectile?Request.Projectile->GroundWarningStyle.LoadSynchronous():nullptr;
            Request.FrozenField=*Field;Request.FrozenField.Damage=Weapon.Damage;Request.Context.EffectConfigId=Weapon.EffectConfigId;
        }
        Request.Motion.Speed=Weapon.ProjectileSpeedCentimetersPerSecond;Request.Motion.MaximumLifetime=Weapon.ProjectileLifetimeSeconds;
        Request.Motion.SweepRadius=Weapon.SweepRadiusCentimeters;Request.MaximumTravelDistance=Weapon.RangeCentimeters;
        // The member is the admitted source; carrier and member have the same simulator.
        Request.Context.Source=GuLiCombatTargets::MakeWingmanTargetHandle(Member);Request.Context.Emitter=Member;
        Request.Context.Target=Shot.Target.Target;Request.Context.WeaponBinding=Channel->Binding;Request.Context.SkillId=Channel->SkillId;
        Request.Context.ProfileRevision=Channel->ProfileRevision;Request.Context.LoadoutRevision=Configuration.LoadoutRevision;
        Request.Context.MatchEpoch=Configuration.MatchEpoch;Request.Context.Damage=Weapon.Damage;
        Request.Context.ShotId=FGuLiHybridManifest::MakeStableKey(TEXT("/Game/GuLiStrike/WingmanShots"),EGuLiHybridObjectKind::Field,
            Key.StableId.ToString(EGuidFormats::Digits)+FString::Printf(TEXT("/%u/%s/%u/%u"),Key.LifeGeneration,*Shot.SlotId.ToString(),Shot.RunId,Shot.ShotIndex)).StableId;
        Request.Context.RootEventId=Request.Context.ShotId;
        const bool Accepted=Ground?Effects->ExecuteWingmanAttack(Request):Effects->StartWingmanGunBurst(Request,Weapon.CooldownSeconds,
            Profile.AirBurstDurationSeconds,Profile.AirFireStopDistance,Configuration.FormationRuntime.SwarmOrbit.OuterSoftRadiusCentimeters,
            Profile.AirOrbitCooldownSeconds).IsValid();
        if (!Accepted) {H->Suspend(TEXT("Wingman authoritative attack could not enter the effect runtime"));return;}
        FGuLiWingmanAttackCheckpoint Checkpoint;Checkpoint.Emitter=Member;Checkpoint.SlotId=Shot.SlotId;Checkpoint.SkillId=Channel->SkillId;
        Checkpoint.DefinitionChecksum=Channel->DefinitionChecksum;Checkpoint.ProfileRevision=Channel->ProfileRevision;
        Checkpoint.RunId=Shot.RunId;Checkpoint.LeaseEpoch=OwnerVersion;Checkpoint.LastShotIndex=Shot.ShotIndex;
        Checkpoint.StartTime=Pawn->GetRuntimeState().Attack.StartTime;Checkpoint.FrozenTarget=Shot.Target.Location;
        Checkpoint.FrozenTargetHandle=Shot.Target.Target;Checkpoint.ApproachDirection=Shot.ApproachDirection;
        Checkpoint.NextFireTime=Last && Last->RunId==Shot.RunId?Last->NextFireTime:Now+Weapon.CooldownSeconds;
        if (Last) *Last=Checkpoint;else Runtime.HybridAttack.Checkpoints.Add(Checkpoint);
    }
}
