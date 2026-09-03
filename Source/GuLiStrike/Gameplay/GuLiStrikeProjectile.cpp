// Copyright Epic Games, Inc. All Rights Reserved.


#include "GuLiStrikeProjectile.h"
#include "Components/SphereComponent.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "GameFramework/Pawn.h"
#include "Components/StaticMeshComponent.h"
#include "GuLiStrikeNPC.h"

AGuLiStrikeProjectile::AGuLiStrikeProjectile()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostPhysics;
	bReplicates = true;
	SetReplicateMovement(true);
	// 当前公里级战场和最远 1.6km 飞船相机超出默认相关距离；弹丸仍按距离相关，不永久常显。
	SetNetCullDistanceSquared(FMath::Square(1000000.0f));

	// this actor will be destroyed automatically once InitialLifeSpan expires
	InitialLifeSpan = 2.0f;

	// create the collision sphere and set it as the root component
	RootComponent = CollisionSphere = CreateDefaultSubobject<USphereComponent>(TEXT("Collision Sphere"));

	CollisionSphere->SetSphereRadius(35.0f);
	CollisionSphere->SetNotifyRigidBodyCollision(true);
	CollisionSphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	CollisionSphere->SetCollisionObjectType(ECC_WorldDynamic);
	CollisionSphere->SetCollisionResponseToAllChannels(ECR_Block);

	// create the mesh
	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(RootComponent);

	Mesh->SetCollisionProfileName(FName("NoCollision"));

	// create the projectile movement comp. No need to attach it because it's not a scene component
	ProjectileMovement = CreateDefaultSubobject<UProjectileMovementComponent>(TEXT("Projectile Movement"));

	ProjectileMovement->InitialSpeed = 2000.0f;
	ProjectileMovement->MaxSpeed = 15000.0f;
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
		// 旧蓝图若保存过复制开关，也以本弹丸的服务器网络合同为准。
		SetReplicates(true);
		SetReplicateMovement(true);
		CollisionSphere->IgnoreActorWhenMoving(GetOwner(), true);
		CollisionSphere->IgnoreActorWhenMoving(GetInstigator(), true);
	}
	else
	{
		// 客户端只显示服务器复制的轨迹/销毁；不独立碰撞、反弹、结算或寿命销毁。
		CollisionSphere->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		ProjectileMovement->PrimaryComponentTick.bStartWithTickEnabled = false;
		ProjectileMovement->SetComponentTickEnabled(false);
		SetLifeSpan(0.0f);
	}
	Super::BeginPlay();
	if (HasAuthority())
	{
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
		Destroy();
	}
}

void AGuLiStrikeProjectile::NotifyHit(class UPrimitiveComponent* MyComp, AActor* Other, class UPrimitiveComponent* OtherComp, bool bSelfMoved, FVector HitLocation, FVector HitNormal, FVector NormalImpulse, const FHitResult& Hit)
{
	if (!HasAuthority()) { return; }
	// 放在 Super 前守门，避免客户端 ReceiveHit 蓝图也触发真实效果。
	Super::NotifyHit(MyComp, Other, OtherComp, bSelfMoved, HitLocation, HitNormal, NormalImpulse, Hit);

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
		Destroy();
	}
}

void AGuLiStrikeProjectile::OnProjectileStop(const FHitResult& ImpactResult)
{
	// 服务器结束弹丸寿命，Actor 销毁随后复制到相关客户端。
	if (HasAuthority()) { Destroy(); }
}
