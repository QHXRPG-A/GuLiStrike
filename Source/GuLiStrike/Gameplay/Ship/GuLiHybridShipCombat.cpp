#include "Gameplay/Ship/GuLiStrikeShip.h"
#include "Gameplay/Ship/Capabilities/GuLiShipHangarCapabilityComponent.h"
#include "Gameplay/Wingman/Combat/GuLiWingmanTargetAcquisition.h"
#include "Gameplay/Wingman/Private/GuLiHybridWingmanIdentity.h"
#include "Gameplay/CombatEffects/GuLiCombatEffectRuntimeSubsystem.h"
#include "Battle/Combat/GuLiLogicalMissileSubsystem.h"
#include "Network/GuLiHybridActorComponent.h"
#include "Network/GuLiHybridActorWorldSubsystem.h"
#include "Engine/World.h"

FGuLiWingmanMissileSalvoResult AGuLiStrikeShip::ActivateHybridMissileSalvo(const FGuLiWingmanMissileSalvoRequest& Request)
{
    FGuLiWingmanMissileSalvoResult Result;Result.RejectReason=EGuLiWingmanRejectReason::InactiveGroup;
    auto* W=GetWorld();auto* H=W?W->GetSubsystem<UGuLiHybridWorldSubsystem>():nullptr;
    auto* Ledger=W?W->GetSubsystem<UGuLiDamageLedgerSubsystem>():nullptr;
    auto* Effects=W?W->GetSubsystem<UGuLiCombatEffectRuntimeSubsystem>():nullptr;
    auto* Actors=W?W->GetSubsystem<UGuLiHybridActorWorldSubsystem>():nullptr;
    const auto* Binding=FindComponentByClass<UGuLiHybridActorComponent>();
    if (!H || !Ledger || !Effects || !Actors || !Binding || !Binding->CanSimulate() || !Binding->CanControl()
        || !HangarCapability || !HangarCapability->IsActiveAbilityInputEnabled()) return Result;
    const auto* Channel=GroupAbilityConfig.FindWeaponChannel(Request.Binding);
    if (!Request.IsWellFormed() || !Channel || !Channel->bEnabled || Channel->Kind!=EGuLiWingmanWeaponKind::Missile
        || Channel->SkillId!=Request.SkillId || Channel->AbilityId!=Request.MissileAbilityId
        || Channel->ProfileRevision!=Request.ProfileRevision || Channel->DefinitionRevision!=Request.MissileDefinitionRevision
        || GroupAbilityConfig.AbilitySetRevision!=Request.AbilitySetRevision || GroupAbilityConfig.LoadoutRevision!=Request.LoadoutRevision)
    {Result.RejectReason=EGuLiWingmanRejectReason::WeaponDefinitionMismatch;return Result;}
    if (HangarCapability->IsWeaponCooldownActive(Channel->CooldownGroupId))
    {Result.RejectReason=EGuLiWingmanRejectReason::CooldownActive;return Result;}
    FVector Aim;FGuLiTargetHandle ShipHandle;
    if (!GuLiWingmanMissileAim::Decode(Request.AimDirectionMilli,Aim) || !H->ResolveObjectTarget(Binding->GetObjectKey(),ShipHandle))
    {Result.RejectReason=EGuLiWingmanRejectReason::InvalidTarget;return Result;}
    TArray<FGuLiCombatTargetSnapshot> Targets;Ledger->GetTargetSnapshots(Targets);
    Targets.RemoveAll([&](const auto& Target){FGuLiHybridObjectKey Key;return !H->ResolveTarget(Target.Handle,Key);});
    auto Visible=[&](const FVector& From,const FGuLiCombatTargetSnapshot& Target)
    {
        FCollisionQueryParams Query(SCENE_QUERY_STAT(GuLiHybridMissileLock),false,this);FHitResult Hit;
        return !W->LineTraceSingleByChannel(Hit,From,Target.Location,ECC_Visibility,Query)
            || (Target.CollisionActor.IsValid() && Hit.GetActor()==Target.CollisionActor.Get());
    };
    FGuLiCombatTargetSnapshot Target;
    Result.RejectReason=GuLiWingmanMissilePolicy::SelectTarget(GetActorLocation(),Aim,Channel->Runtime,ShipHandle,
        Binding->GetState().Team,Targets,Visible,Target);
    if (Result.RejectReason!=EGuLiWingmanRejectReason::None) return Result;
    Result.SelectedTarget=Target.Handle;
    TArray<FGuLiHybridObjectState> Objects;H->GetObjects(Objects);
    TArray<FGuLiLogicalMissileLaunchRequest> Launches;
    bool Living=false,InRange=false;
    for (uint8 Flight=0;Flight<GULI_WINGMAN_FLIGHT_COUNT && Launches.IsEmpty();++Flight)
    {
        for (uint8 Slot=0;Slot<GULI_WINGMAN_MEMBERS_PER_FLIGHT;++Slot)
        {
            for (const auto& O:Objects)
            {
                if (O.Key.Kind!=EGuLiHybridObjectKind::Wingman || O.Parent!=Binding->GetObjectKey()
                    || O.Health.bDead || O.LifeState!=1 || !H->HasSimulation(O.Key)) continue;
                FGuLiWingmanHandle Member;
                if (!GuLiHybridWingman::Resolve(Actors->GetManifest(),O,Member) || Member.Flight.FlightIndex!=Flight || Member.MemberIndex!=Slot) continue;
                auto* Pawn=H->FindBoundActor(O.Key);if (!Pawn) continue;Living=true;
                const FVector Position=Pawn->GetActorLocation();
                if (FVector::DistSquared(Position,Target.Location)>FMath::Square(double(Channel->Runtime.RangeCentimeters)+FMath::Max(0.f,Target.CollisionRadius))) continue;
                InRange=true;if (Channel->Runtime.bRequiresLineOfSight && !Visible(Position,Target)) continue;
                FGuLiLogicalMissileLaunchRequest R;R.MatchEpoch=GroupAbilityConfig.MatchEpoch;R.Emitter=Member;R.Source=ShipHandle;
                const FString Identity=Request.ActivationId.ToString(EGuidFormats::Digits)+TEXT("/")+O.Key.StableId.ToString(EGuidFormats::Digits)+TEXT("/")+LexToString(O.Key.LifeGeneration);
                R.MissileId=FGuLiHybridManifest::MakeStableKey(TEXT("/Game/GuLiStrike/WingmanSalvos"),EGuLiHybridObjectKind::Field,Identity).StableId;
                R.ShotId=R.MissileId;R.RootEventId=Request.ActivationId;R.WeaponBinding=Request.Binding;R.SkillId=Request.SkillId;
                R.LoadoutRevision=Request.LoadoutRevision;R.ProfileRevision=Request.ProfileRevision;R.Target=Target.Handle;
                R.LaunchPosition=Position;R.LaunchDirection=(Target.Location-Position).GetSafeNormal();
                R.SpeedCentimetersPerSecond=Channel->Runtime.ProjectileSpeedCentimetersPerSecond;
                R.TurnRateDegreesPerSecond=Channel->Runtime.MaximumHomingTurnRateDegreesPerSecond;
                R.SweepRadiusCentimeters=Channel->Runtime.SweepRadiusCentimeters;R.MaximumLifetimeSeconds=Channel->Runtime.ProjectileLifetimeSeconds;
                R.Damage=Channel->Runtime.Damage;Launches.Add(R);break;
            }
        }
        if (!Launches.IsEmpty()) Result.FlightIndex=Flight;
    }
    if (Launches.IsEmpty())
    {Result.RejectReason=!Living?EGuLiWingmanRejectReason::EmitterDead:!InRange?EGuLiWingmanRejectReason::OutOfRange:EGuLiWingmanRejectReason::NoLineOfSight;return Result;}
    if (!Effects->CanLaunchHomingSalvo(Launches)) {Result.RejectReason=EGuLiWingmanRejectReason::InvalidTarget;return Result;}
    if (!HangarCapability->ServerTryReserveWeaponCooldown(Channel->CooldownGroupId,Channel->Runtime.CooldownSeconds,Request.ActivationId))
    {Result.RejectReason=EGuLiWingmanRejectReason::CooldownActive;return Result;}
    if (!Effects->LaunchHomingSalvo(Launches,Result.LaunchedCount))
    {
        if (!Result.LaunchedCount) HangarCapability->ServerRollbackWeaponCooldown(Channel->CooldownGroupId,Request.ActivationId);
        Result.RejectReason=EGuLiWingmanRejectReason::InvalidTarget;return Result;
    }
    Result.bSharedCooldownStarted=true;Result.RejectReason=EGuLiWingmanRejectReason::None;return Result;
}
