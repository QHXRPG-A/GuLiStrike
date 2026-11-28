#include "Gameplay/Ship/Aiming/GuLiXuntianGunnery.h"
#include "Gameplay/Ship/Capabilities/GuLiXuntianBellyBattery.h"
#include "Gameplay/Ship/Build/GuLiShipAssemblyComponent.h"
#include "Gameplay/Ship/GuLiStrikeShip.h"
#include "Gameplay/CombatEffects/GuLiCombatEffectRuntimeSubsystem.h"
#include "Gameplay/CombatEffects/GuLiGroundWarningSubsystem.h"
#include "Gameplay/Data/GuLiSpellFieldDataSubsystem.h"
#include "Battle/Framework/GuLiBattlePlayerState.h"
#include "Engine/World.h"

int32 UGuLiXuntianGunnery::GetBellyLevel() const
{
    const auto* Battery = Cast<UGuLiXuntianBellyBattery>(Ship()->GetShipAssembly()->FindCapability(TEXT("Turret"), TEXT("Turret")));
    return Battery && Battery->GetShell() ? Battery->GetShell()->Level : 0;
}

void UGuLiXuntianGunnery::AdvanceBelly(double Delta, bool bAllowFire)
{
    auto* Battery = Cast<UGuLiXuntianBellyBattery>(Ship()->GetShipAssembly()->FindCapability(TEXT("Turret"), TEXT("Turret")));
    auto* Shell = Battery && Battery->IsCapabilityEnabled() ? Battery->GetShell() : nullptr;
    for (int32 Index : BellyMounts)
    {
        auto& Gun = Mounts[Index];
        auto& Clock = Gun.Salvo;
        Clock.RoundCooldown = FMath::Max(0., Clock.RoundCooldown - Delta);
        if (!Shell || !bGround || !bAllowFire || !Gun.bReachable)
        {
            Clock.Remaining = 0;
            Clock.bScheduled = false;
            continue;
        }
        if (!Clock.Remaining && !bTrigger) { Clock.bScheduled = false; continue; }
        if (!Clock.Remaining && !Clock.bScheduled)
        {
            Clock.bScheduled = true;
            Clock.ShotDelay = FMath::FRandRange(0.f, 1.5f);
        }
        Clock.ShotDelay = FMath::Max(0., Clock.ShotDelay - Delta);
        if (Clock.ShotDelay > UE_KINDA_SMALL_NUMBER || (!Clock.Remaining && Clock.RoundCooldown > UE_KINDA_SMALL_NUMBER)) continue;
        if (!FireShell(Gun, *Shell)) continue;
        Gun.Barrel = (Gun.Barrel + 1) % Gun.Bores.Num();
        if (Clock.Remaining) --Clock.Remaining;
        else { Clock.RoundCooldown = Shell->RoundSeconds; Clock.Remaining = Shell->Level - 1; }
        Clock.ShotDelay = Clock.Remaining ? Shell->BurstInterval : 0.;
    }
}

bool UGuLiXuntianGunnery::FireShell(FGuLiXuntianMount& Gun, UGuLiXuntianShellDefinition& Shell)
{
    UWorld* World = GetWorld();
    auto* Vessel = Ship();
    const auto* Pilot = Vessel->GetPlayerState<AGuLiBattlePlayerState>();
    const auto* Health = Vessel->GetCombatHealthComponent();
    const auto* Fields = World->GetSubsystem<UGuLiSpellFieldDataSubsystem>();
    const auto* Field = Fields ? Fields->FindCombatField(Shell.FieldRow) : nullptr;
    auto* Runtime = World->GetSubsystem<UGuLiCombatEffectRuntimeSubsystem>();
    auto* Ledger = World->GetSubsystem<UGuLiDamageLedgerSubsystem>();
    if (!Pilot || !Field || !Runtime || !Ledger || !Health->IsAlive() || !Vessel->CanAcceptServerIntent()) return false;

    // Freeze the actual bore's aim. During a fast camera turn it can still differ from the reticle.
    const double Distance = bAimingAtSurface ? Reach : FMath::Clamp(Gun.BoreDistance, 1., double(Reach));
    FVector End = Gun.Muzzle + Gun.Direction * Distance;
    FCollisionQueryParams Query(SCENE_QUERY_STAT(XuntianShellAim), false, Vessel);
    FHitResult Hit;
    if (World->LineTraceSingleByObjectType(Hit, Gun.Muzzle, End, FCollisionObjectQueryParams(ECC_WorldStatic), Query))
        End = Hit.ImpactPoint;

    FGuLiCombatAttackRequest Shot;
    Shot.SourceTransform = FTransform(Gun.Direction.Rotation(), Gun.Muzzle);
    Shot.TargetLocation = End;
    Shot.Projectile = &Shell;
    Shot.Motion = Shell.Motion;
    Shot.MaximumTravelDistance = Reach;
    Shot.FrozenField = *Field;
    Shot.GroundWarningStyle = Shell.GroundWarningStyle.LoadSynchronous();
    FVector Ground;
    if (SurfaceAt(End, Ground))
    {
        Shot.WarningGroundPoint = Ground;
    }
    auto& Context = Shot.Context;
    Context.Source = Health->GetTargetHandle(); Context.Target = Focus;
    Context.SkillId = Context.EffectConfigId = Shell.FieldRow;
    Context.Damage = Field->Damage;
    Context.MatchEpoch = Ledger->GetMatchEpoch();
    Context.LoadoutRevision = Vessel->GetLoadoutRevision();
    Context.WeaponBinding.MatchEpoch = Context.MatchEpoch;
    Context.WeaponBinding.Team = Health->GetCombatTeam();
    Context.WeaponBinding.OwnerPlayerGuid = Pilot->GetPlayerGuid();
    Context.WeaponBinding.Domain = EGuLiWeaponDomain::ShipMounted;
    Context.WeaponBinding.SubjectId = TEXT("5002");
    Context.WeaponBinding.SlotId = Shell.FieldRow;
    Context.RootEventId = Context.ShotId = FGuid::NewGuid();
    return Runtime->LaunchPointProjectile(Shot).IsValid();
}
