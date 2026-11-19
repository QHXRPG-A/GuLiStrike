#include "Gameplay/GroundMech/GuLiGroundMechCharacter.h"
#include "Gameplay/GroundMech/Movement/GuLiGroundMechMovementComponent.h"
#include "Gameplay/GroundMech/Abilities/GuLiGroundStrikeSurface.h"
#include "Gameplay/GroundMech/Rogue/GuLiGroundRogueRuntime.h"
#include "Gameplay/GroundMech/Rogue/GuLiGroundRoguePresentation.h"
#include "Gameplay/Cards/GuLiRogueCardSubsystem.h"
#include "Gameplay/GroundMech/Weapons/GuLiGroundMechWeaponComponent.h"
#include "Gameplay/CombatEffects/GuLiCombatEffectRuntimeSubsystem.h"
#include "Gameplay/CombatEffects/GuLiCombatEffectDefinition.h"
#include "Gameplay/Data/GuLiSpellFieldDataSubsystem.h"
#include "Network/GuLiHybridActorComponent.h"
#include "Network/GuLiHybridActorWorldSubsystem.h"
#include "Network/GuLiHybridWorldSubsystem.h"
#include "Network/GuLiHybridManifest.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Controller.h"

int32 AGuLiGroundMechCharacter::GetBarrageLevel() const
{
    if(ModelId!=4001)return 0;
    const auto* Identity=UGuLiHybridActorComponent::Find(this);
    const auto* Cards=GetWorld()->GetSubsystem<UGuLiRogueCardSubsystem>();
    const int32 Owned=Cards&&Identity ? Cards->GetAcquisitionCount(Identity->GetState().Controller,GuLiGroundRogue::BarrageCardId) : 0;
    return FMath::Max(Owned,bBarrageTrial?1:0);
}

bool AGuLiGroundMechCharacter::ResolveBarrageDefinition()
{
    if (!BarrageProjectile)
        BarrageProjectile = LoadObject<UGuLiProjectileEffectDefinition>(nullptr,
            TEXT("/Game/GuLiStrike/FX/GroundMech/NoSurvivors/DA_GroundNoSurvivorsMissile.DA_GroundNoSurvivorsMissile"));
    return BarrageProjectile && BarrageProjectile->IsValidDefinition() && BarrageProjectile->GetGroundStrikeFlight().IsEnabled();
}

bool AGuLiGroundMechCharacter::EnterBarrageRange(const FTransform& Start)
{
#if WITH_EDITOR
    if (GetWorld()->IsPlayInEditor() && IsLocallyControlled() && ModelId==4001 && !IsCarrierDormant()
        && UGuLiHybridActorComponent::HasSimulation(this) && Weapon->SourceHandle.IsValid() && ResolveBarrageDefinition())
    {
        auto* Movement=CastChecked<UGuLiGroundMechMovementComponent>(GetCharacterMovement());
        Movement->StopMovementImmediately();
        Movement->TeleportBlink(Start.GetLocation());
        if (!GetActorLocation().Equals(Start.GetLocation(),2.f)) return false;
        SetActorRotation(Start.GetRotation());
        Controller->SetControlRotation(Start.Rotator());
        ResetFacingForBlink(Start.Rotator().Yaw);
        ClearLocalControlInput();
        bBarrageTrial=true;
        return true;
    }
#endif
    return false;
}

float AGuLiGroundMechCharacter::GetBarrageClearance() const
{
    const FVector Feet=GetActorLocation()-FVector::UpVector*GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
    double Height;
    return GuLiGroundStrikeSurface::HeightBelow(*GetWorld(), Feet + FVector::UpVector, Height)
        ? FMath::Max(0.f, float(Feet.Z - Height)) : -1.f;
}

bool AGuLiGroundMechCharacter::GetBarrageAimPoint(FVector& Point) const
{
    if(!IsLocallyControlled())
    {
        if (UGuLiHybridActorComponent::HasSimulation(this) && SimulationInput.bHasAim)
        {
            Point=SimulationInput.Aim;
            return true;
        }
        Point=UpperAimState.TargetPoint;
        return UpperAimState.bHasAimPose;
    }
    if (bSemanticInput) { Point = HybridInput.Aim; return HybridInput.bHasAim; }
    FVector Origin, Direction;
    if (!GetReticleRay(Origin, Direction)) return false;
    double Distance = 500000.;
    if (BarrageDisplayHeight >= 0.f && Direction.Z < -UE_SMALL_NUMBER)
    {
        const double Floor = GetActorLocation().Z - GetCapsuleComponent()->GetScaledCapsuleHalfHeight() - BarrageDisplayHeight;
        if (Floor < Origin.Z) Distance = FMath::Min(Distance, (Floor - Origin.Z) / Direction.Z);
    }
    Point = Origin + Direction * Distance;
    return true;
}

namespace
{
FQuat BarrageFacing(const FVector& Direction, float FallbackYaw)
{
    FRotator Facing=Direction.Rotation();
    if (Direction.SizeSquared2D() < UE_KINDA_SMALL_NUMBER*UE_KINDA_SMALL_NUMBER) Facing.Yaw=FallbackYaw;
    return Facing.Quaternion();
}
}

bool AGuLiGroundMechCharacter::GetBarrageDisplayTransform(FRotator& Rotation,FVector& Translation) const
{
    FVector Aim;
    if (!GetBarrageAimPoint(Aim))
    {
        if (!UpperAimState.bHasAimPose) return false;
        Aim=UpperAimState.TargetPoint;
    }
    const FVector Direction=(Aim-GetActorLocation()).GetSafeNormal();
    if (Direction.IsNearlyZero()) return false;
    const float Yaw=GetActorRotation().Yaw;
    const FQuat WorldDelta=BarrageFacing(Direction,Yaw)*FRotator(0,Yaw,0).Quaternion().Inverse();
    const FTransform MeshTransform=GetMesh()->GetComponentTransform();
    const FQuat LocalDelta=(MeshTransform.GetRotation().Inverse()*WorldDelta*MeshTransform.GetRotation()).GetNormalized();
    const FVector Pivot=MeshTransform.InverseTransformPosition(GetActorLocation());
    Rotation=LocalDelta.Rotator();
    Translation=Pivot-LocalDelta.RotateVector(Pivot);
    return true;
}

void AGuLiGroundMechCharacter::RequestBarrage()
{
    if (!GetBarrageLevel() || IsFiringBarrage() || !IsShoulderLookActive()) return;
    const auto* Identity=UGuLiHybridActorComponent::Find(this);
    auto* Hybrid=GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>();
    if (!Identity || !Hybrid || !Identity->CanControl()) return;
    const auto& Object=Identity->GetState();
    FGuLiHybridCommand Command;
    Command.Event=FGuid::NewGuid();
    Command.Operation=TEXT("GroundBarrage");
    Command.LogicalTick=Hybrid->GetLogicalTick();
    auto& Member=Command.Members.AddDefaulted_GetRef();
    Member.Key=Object.Key; Member.ControlEpoch=Object.ControlEpoch; Member.OwnerEpoch=Object.OwnerEpoch; Member.Slot=Object.Ordinal;
    Hybrid->RouteCommand(Command);
}

void AGuLiGroundMechCharacter::StartBarrage(const FGuid& Event)
{
    if (!Event.IsValid() || Event==BarrageCast || IsFiringBarrage() || IsCarrierDormant() || (IsLocallyControlled() && !CanUseControls()) || ModelId!=4001
        || GetBlinkState().Phase || GetBarrageClearance()<BarrageMinimumHeight) return;
    const auto* Identity=UGuLiHybridActorComponent::Find(this);
    TMap<FString,int32> Counts;uint64 Revision;GuLiNet::Effects::Set Effects;
    if(!Identity || Identity->GetState().Health.bDead || !GuLiGroundRogue::ReadBuild(*GetWorld(),Identity->GetState().Controller,Counts,Revision,&Effects))return;
    GuLiNet::Effects::Barrage Profile;
    if(const auto* Owned=GuLiNet::Effects::FindBarrage(Effects))
    {
        if(!GuLiNet::Effects::DecodeBarrage(Owned->Parameters,Profile)||!Profile.Level)return;
    }
    else
    {
        FString Error;
        if(!bBarrageTrial || !GuLiGroundRogue::LoadBarrage(Profile,Error))return;
        Profile.Level=1;
    }
    const auto* Fields=GetWorld()->GetSubsystem<UGuLiSpellFieldDataSubsystem>();
    const auto* Field=Fields?Fields->FindCombatField(GuLiGroundRogue::BarrageFieldId):nullptr;
    const auto* Actors = GetWorld()->GetSubsystem<UGuLiHybridActorWorldSubsystem>();
    const double* Delay = Actors ? Actors->GetManifest().Numbers.Find(GuLiGroundRogue::BarrageWindupKey) : nullptr;
    if(!Field || !ResolveBarrageDefinition())return;
    const auto& Rank=Profile.Ranks[Profile.Level-1];
    BarrageCastLevel=Profile.Level;
    BarrageCount=Rank.ShotCount;
    BarrageIntervalTicks=Rank.IntervalTicks;
    BarrageDamage=Field->Damage*(1.f+Rank.DamageBonus);
    BarrageHalfAngle=Rank.ConeHalfAngleDegrees;
    BarrageCast=Event;
    // Legacy snapshot manifests scheduled their first shot immediately.
    const uint64 WindupTicks = Delay && *Delay > 0 ? uint64(*Delay) : 0;
    BarrageStartTick=GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>()->GetLogicalTick() + WindupTicks;
    BarrageNextShot=0;
    RoguePresentation->SetCast(BarrageStartTick);
    GetCharacterMovement()->StopMovementImmediately();
}

void AGuLiGroundMechCharacter::AdvanceBarrage()
{
    const uint64 Tick=GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>()->GetLogicalTick();
    if (Tick<BarrageStartTick+uint64(BarrageIntervalTicks)*BarrageNextShot) return;
    const auto* Fields=GetWorld()->GetSubsystem<UGuLiSpellFieldDataSubsystem>();
    const auto* Field=Fields ? Fields->FindCombatField(GuLiGroundRogue::BarrageFieldId) : nullptr;
    FGuLiCombatAttackRequest Request;
    if (!Field || !BarrageProjectile || !BarrageProjectile->ResolveMotionSettings(Request.Motion))
    {
        StopBarrage();
        UE_LOG(LogTemp,Error,TEXT("GroundNoSurvivors lacks its projectile or field configuration"));
        return;
    }
    const FGuid Shot=FGuLiHybridManifest::MakeStableKey(TEXT("/Game/GuLiStrike/GroundNoSurvivors"),EGuLiHybridObjectKind::Field,
        BarrageCast.ToString(EGuidFormats::Digits)+FString::Printf(TEXT("/%d"),BarrageNextShot)).StableId;
    // A4B2's jet outlets are fixed on the left/right rear armour. Raise the launch
    // point above each shoulder in the pose's horizontal plane rather than using the gun barrels.
    const FName Outlet=(BarrageNextShot%2==0) ? TEXT("Jet_Outlet_L") : TEXT("Jet_Outlet_R");
    const FVector Port=GetMesh()->GetSocketLocation(Outlet);
    const FVector Opposite=GetMesh()->GetSocketLocation(BarrageNextShot%2==0 ? TEXT("Jet_Outlet_R") : TEXT("Jet_Outlet_L"));
    const float HalfHeight=GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
    const FVector UprightOrigin=Port+(Port-Opposite).GetSafeNormal2D()*HalfHeight*.25f+FVector::UpVector*HalfHeight*.62f;
    FVector Aim;
    const float RangeCm = BarrageProjectile->GetGroundStrikeFlight().HorizontalRange;
    if (!GetBarrageAimPoint(Aim)) Aim=SimulationInput.bHasAim ? SimulationInput.Aim : UprightOrigin+GetActorForwardVector()*RangeCm;
    const FVector Pivot=GetActorLocation();
    const float TurretYaw=UpperAimState.bInitialized ? UpperAimState.WorldYaw : GetActorRotation().Yaw;
    const FQuat Facing=BarrageFacing((Aim-Pivot).GetSafeNormal(UE_SMALL_NUMBER,GetActorForwardVector()),TurretYaw);
    const FQuat FromUpright=Facing*FRotator(0,TurretYaw,0).Quaternion().Inverse();
    const FVector Origin=Pivot+FromUpright.RotateVector(UprightOrigin-Pivot);
    const FVector Axis=(Aim-Origin).GetSafeNormal(UE_SMALL_NUMBER,GetActorForwardVector());
    FRandomStream Random(GetTypeHash(Shot));
    const FVector Ray=Random.VRandCone(Axis,FMath::DegreesToRadians(BarrageHalfAngle));
    // Only the reticle direction guides the shot. Close downward shots converge just below their support.
    const double TravelBudget = double(Request.Motion.Speed) * Request.Motion.MaximumLifetime;
    const double RayLength = FMath::Min(TravelBudget, RangeCm / FMath::Max(Ray.Size2D(), UE_DOUBLE_SMALL_NUMBER));
    double GuideDistance = RayLength;
    double Floor;
    if (Ray.Z < -UE_SMALL_NUMBER && GuLiGroundStrikeSurface::HeightBelow(*GetWorld(), Origin, Floor))
    {
        const double T = (Floor - Origin.Z) / Ray.Z;
        const FVector Probe = Origin + Ray * FMath::Min(T, RayLength);
        if (GuLiGroundStrikeSurface::HeightBelow(*GetWorld(), FVector(Probe.X, Probe.Y, Origin.Z), Floor))
            GuideDistance = FMath::Min(RayLength, (Floor - Origin.Z) / Ray.Z);
    }
    Request.TargetLocation = Origin + Ray * (GuideDistance + 2. * Request.Motion.Speed / 30.);
    Request.SourceTransform=FTransform(Ray.Rotation(),Origin);
    Request.Context.Source=Weapon->SourceHandle;
    Request.Context.RootEventId=BarrageCast;
    Request.Context.ShotId=Shot;
    Request.Context.SkillId=GuLiGroundRogue::BarrageRuleSkill(BarrageCastLevel,bBarrageTrial);
    Request.Context.WeaponBinding.SlotId=TEXT("Barrage");
    Request.Context.EffectConfigId=GuLiGroundRogue::BarrageFieldId;
    Request.Context.Damage=BarrageDamage;
    Request.FrozenField=*Field;
    Request.FrozenField.Damage=BarrageDamage;
    Request.Projectile=BarrageProjectile;
    Request.bUseAuthoredPointTrajectory=true;
    // Short shots keep their shallower departure instead of using a 100 m arc at point blank.
    const float BendScale=FMath::Clamp(float(FVector::Distance(Origin,Request.TargetLocation))/10000.f,.08f,1.f);
    Request.Motion.LateralOffset*=BendScale;
    Request.Motion.VerticalCurve*=BendScale;
    Request.Motion.LongitudinalCurve*=BendScale;
    Request.Motion.MinimumLiftHeight*=BendScale;
    Request.Motion.MaximumLiftHeight*=BendScale;
    if (!GetWorld()->GetSubsystem<UGuLiCombatEffectRuntimeSubsystem>()->LaunchPointProjectile(Request).IsValid())
    {
        StopBarrage();
        UE_LOG(LogTemp,Error,TEXT("GroundNoSurvivors projectile admission failed for %s"),*Shot.ToString());
        return;
    }
    RoguePresentation->RecordShot(Tick, BarrageNextShot % 2 == 0);
    ++BarrageNextShot;
    if (!IsFiringBarrage()) StopBarrage();
}

void AGuLiGroundMechCharacter::StopBarrage()
{
    BarrageNextShot = BarrageCount;
    RoguePresentation->SetCast(0);
}
