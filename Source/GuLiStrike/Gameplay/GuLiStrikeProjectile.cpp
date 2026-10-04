// Copyright Epic Games, Inc. All Rights Reserved.


#include "GuLiStrikeProjectile.h"
#include "Gameplay/CombatEffects/GuLiCombatEffectReplicationComponent.h"
#include "Components/SphereComponent.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "GameFramework/Pawn.h"
#include "Components/StaticMeshComponent.h"
#include "GuLiStrikeNPC.h"

AGuLiStrikeProjectile::AGuLiStrikeProjectile()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostPhysics;
	bReplicates = false;
	SetReplicateMovement(false);
	// Only authority owns this physical object. Clients receive bounded flight events.

	// this actor will be destroyed automatically once InitialLifeSpan expires
	InitialLifeSpan = 2.0f;

	// create the collision sphere and set it as the root component
	RootComponent = CollisionSphere = CreateDefaultSubobject<USphereComponent>(TEXT("Collision Sphere"));

	CollisionSphere->SetSphereRadius(7.0f);
	CollisionSphere->SetNotifyRigidBodyCollision(true);
	CollisionSphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	CollisionSphere->SetCollisionObjectType(ECC_WorldDynamic);
	CollisionSphere->SetCollisionResponseToAllChannels(ECR_Block);

	// create the mesh
	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(RootComponent);
	Mesh->SetRelativeScale3D(FVector(0.2f));

	Mesh->SetCollisionProfileName(FName("NoCollision"));

	// create the projectile movement comp. No need to attach it because it's not a scene component
	ProjectileMovement = CreateDefaultSubobject<UProjectileMovementComponent>(TEXT("Projectile Movement"));

	ProjectileMovement->InitialSpeed = 400.0f;
	ProjectileMovement->MaxSpeed = 3000.0f;
	ProjectileMovement->bRotationFollowsVelocity = true;
	ProjectileMovement->bRotationRemainsVertical = true;
	ProjectileMovement->ProjectileGravityScale = 0.0f;
	ProjectileMovement->bShouldBounce = true;
	ProjectileMovement->bForceSubStepping = true;

	ProjectileMovement->OnProjectileStop.AddDynamic(this, &AGuLiStrikeProjectile::OnProjectileStop);
}

bool AGuLiStrikeProjectile::ConfigureServerDamageLedger(
	const AActor& SourceActor, const float Damage)
{
	if (!HasAuthority() || DamageLedgerContext.IsWellFormed())
	{
		return false;
	}
	return GuLiShipProjectileLedger::BuildServerLaunchContext(
		SourceActor, Damage, DamageLedgerContext);
}

void AGuLiStrikeProjectile::BeginPlay()
{
	if (HasAuthority())
	{
		// Override replication switches saved in derived Blueprints.
		SetReplicates(false);
		SetReplicateMovement(false);
		CollisionSphere->IgnoreActorWhenMoving(GetOwner(), true);
		CollisionSphere->IgnoreActorWhenMoving(GetInstigator(), true);
	}
	else
	{
		// A legacy accidental client spawn has no damage or physical movement.
		CollisionSphere->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		ProjectileMovement->PrimaryComponentTick.bStartWithTickEnabled = false;
		ProjectileMovement->SetComponentTickEnabled(false);
		SetLifeSpan(0.0f);
	}
	Super::BeginPlay();
	if (HasAuthority())
	{
		SetReplicates(false); SetReplicateMovement(false);
		Mesh->SetHiddenInGame(true); Mesh->SetVisibility(false);
		PublishServerLaunch();
		PreviousServerSweepLocation = GetActorLocation();
		bHasPreviousServerSweepLocation = !PreviousServerSweepLocation.ContainsNaN();
	}
	if (!HasAuthority())
	{
		// Super 会注册组件 Tick；在注册及蓝图 BeginPlay 完成后再次关闭，保证只有服务器积分。
		ProjectileMovement->Deactivate();
		ProjectileMovement->SetComponentTickEnabled(false);
	}
}

void AGuLiStrikeProjectile::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!HasAuthority() || !DamageLedgerContext.IsWellFormed() || !GetWorld()
		|| IsActorBeingDestroyed())
	{
		return;
	}

	const FVector CurrentLocation = GetActorLocation();
    if (bFlightPublished && !bFlightEnded)
    {
        Flight.State.Location=CurrentLocation; Flight.State.Velocity=ProjectileMovement->Velocity;
        Flight.State.SampleTime=GetWorld()->GetTimeSeconds(); ++Flight.State.Sequence;
        UGuLiCombatEffectReplicationComponent::UpdateFlight(GetWorld(),Flight.State);
    }
	if (!bHasPreviousServerSweepLocation || CurrentLocation.ContainsNaN())
	{
		PreviousServerSweepLocation = CurrentLocation;
		bHasPreviousServerSweepLocation = !CurrentLocation.ContainsNaN();
		return;
	}

	const float ProjectileRadius = CollisionSphere
		? CollisionSphere->GetScaledSphereRadius() : 0.0f;
	const FGuLiShipProjectileLedgerImpact LedgerImpact =
		GuLiShipProjectileLedger::CommitServerWingmanSweepImpact(
			*GetWorld(),
			DamageLedgerContext,
			PreviousServerSweepLocation,
			CurrentLocation,
			ProjectileRadius);
	PreviousServerSweepLocation = CurrentLocation;
	if (LedgerImpact.HasResolvedTarget())
	{
		SetActorLocation(LedgerImpact.HitLocation, false, nullptr, ETeleportType::TeleportPhysics);
		FinishFlight(EGuLiCombatEffectEndReason::Impact,LedgerImpact.HitLocation);
		Destroy();
	}
}

void AGuLiStrikeProjectile::NotifyHit(class UPrimitiveComponent* MyComp, AActor* Other, class UPrimitiveComponent* OtherComp, bool bSelfMoved, FVector HitLocation, FVector HitNormal, FVector NormalImpulse, const FHitResult& Hit)
{
	if (!HasAuthority()) { return; }
	// Ledger and terminal run before Blueprint ReceiveHit can destroy the actor.

	// Ship 自身的既有 Actor 弹丸只在服务器把命中接到统一 TargetHandle/Ledger。
	// 同一弹丸永远复用 DamageEventId，因此引擎重复命中回调不会重复扣血。
	if (Other && DamageLedgerContext.IsWellFormed() && GetWorld())
	{
		const FGuLiShipProjectileLedgerImpact LedgerImpact =
			GuLiShipProjectileLedger::CommitServerImpact(
				*GetWorld(), DamageLedgerContext, *Other, HitLocation);
		if (LedgerImpact.HasResolvedTarget())
		{
			// 命中已注册战斗目标后，无论伤害被接纳（敌方）还是规则拒绝
			//（友军/死亡/旧 Epoch），这枚物理弹都不能继续反弹命中别处。
			Flight.ImpactNormal=HitNormal;
			FinishFlight(EGuLiCombatEffectEndReason::Impact,HitLocation);
			Super::NotifyHit(MyComp,Other,OtherComp,bSelfMoved,HitLocation,HitNormal,NormalImpulse,Hit);
			Destroy();
			return;
		}
	}

	// 沿用旧行为：尚未接入统一目标目录的旧 NPC 仍收到 ProjectileImpact。
	if (AGuLiStrikeNPC* NPC = Cast<AGuLiStrikeNPC>(Other))
	{
		// tell the NPC it's been hit
		NPC->ProjectileImpact(FVector::ZeroVector);

		// destroy this projectile
		Flight.ImpactNormal=HitNormal;
		FinishFlight(EGuLiCombatEffectEndReason::Impact,HitLocation);
		Destroy();
	}
	Super::NotifyHit(MyComp,Other,OtherComp,bSelfMoved,HitLocation,HitNormal,NormalImpulse,Hit);
}

void AGuLiStrikeProjectile::OnProjectileStop(const FHitResult& ImpactResult)
{
	// A server stop ends the independent local flight at the authoritative point.
	if (HasAuthority()) { FinishFlight(EGuLiCombatEffectEndReason::Blocked,ImpactResult.Location); Destroy(); }
}

void AGuLiStrikeProjectile::PublishServerLaunch()
{
    if (bFlightPublished || !HasAuthority() || !DamageLedgerContext.IsWellFormed() || !GetWorld()) return;
    auto& State=Flight.State;
    State.MatchEpoch=DamageLedgerContext.MatchEpoch; State.EffectId=DamageLedgerContext.ShotId;
    State.Sequence=1; State.Kind=EGuLiCombatEffectKind::LinearProjectile; State.Source=DamageLedgerContext.Source;
    State.Location=State.LaunchLocation=GetActorLocation();
    FVector Velocity=ProjectileMovement->Velocity;
    if (Velocity.IsNearlyZero()) Velocity=GetActorForwardVector()*FMath::Max(1.f,ProjectileMovement->InitialSpeed);
    State.Velocity=Velocity; State.LaunchDirection=Velocity.GetSafeNormal(); State.Motion.Speed=Velocity.Size();
    State.Motion.SweepRadius=CollisionSphere->GetScaledSphereRadius();
    State.StartTime=State.SampleTime=State.ActivationTime=GetWorld()->GetTimeSeconds();
    State.EndTime=State.StartTime+FMath::Clamp(GetLifeSpan()>0 ? GetLifeSpan() : InitialLifeSpan,.01f,120.f);
    FGuLiCombatTargetSnapshot Source;
    if (auto* Ledger=GetWorld()->GetSubsystem<UGuLiDamageLedgerSubsystem>(); Ledger && Ledger->TryGetTargetSnapshot(State.Source,Source)) State.SourceTeam=Source.Team;
    Flight.ShipVisualClass=GetClass(); Flight.VisualScale=GetActorScale3D(); Flight.Gravity=GetWorld()->GetGravityZ()*ProjectileMovement->ProjectileGravityScale;
    Flight.bBounce=ProjectileMovement->bShouldBounce; Flight.Bounciness=ProjectileMovement->Bounciness;
    Flight.Friction=ProjectileMovement->Friction; Flight.StopSpeed=ProjectileMovement->BounceVelocityStopSimulatingThreshold;
    Flight.MaximumSpeed=ProjectileMovement->MaxSpeed;
    bFlightPublished=UGuLiCombatEffectReplicationComponent::PublishFlight(GetWorld(),Flight);
}

void AGuLiStrikeProjectile::FinishFlight(EGuLiCombatEffectEndReason Reason,const FVector& Location)
{
    if (!HasAuthority() || bFlightEnded) return;
    PublishServerLaunch();
    bFlightEnded=true;
    if (!bFlightPublished) return;
    Flight.State.Phase=EGuLiCombatEffectPhase::Finished; Flight.State.EndReason=Reason;
    Flight.State.Location=Location; Flight.State.SampleTime=GetWorld()->GetTimeSeconds(); ++Flight.State.Sequence;
    UGuLiCombatEffectReplicationComponent::PublishFlight(GetWorld(),Flight);
}

void AGuLiStrikeProjectile::LifeSpanExpired()
{
    FinishFlight(EGuLiCombatEffectEndReason::Expired,GetActorLocation());
    Super::LifeSpanExpired();
}

void AGuLiStrikeProjectile::EndPlay(const EEndPlayReason::Type Reason)
{
    FinishFlight(Reason==EEndPlayReason::Destroyed ? EGuLiCombatEffectEndReason::Cancelled : EGuLiCombatEffectEndReason::EpochEnded,GetActorLocation());
    Super::EndPlay(Reason);
}
