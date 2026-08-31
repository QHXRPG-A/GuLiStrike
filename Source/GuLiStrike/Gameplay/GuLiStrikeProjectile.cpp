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
	if (!HasAuthority())
	{
		// Super 会注册组件 Tick；在注册及蓝图 BeginPlay 完成后再次关闭，保证只有服务器积分。
		ProjectileMovement->Deactivate();
		ProjectileMovement->SetComponentTickEnabled(false);
	}
}

void AGuLiStrikeProjectile::NotifyHit(class UPrimitiveComponent* MyComp, AActor* Other, class UPrimitiveComponent* OtherComp, bool bSelfMoved, FVector HitLocation, FVector HitNormal, FVector NormalImpulse, const FHitResult& Hit)
{
	if (!HasAuthority()) { return; }
	// 放在 Super 前守门，避免客户端 ReceiveHit 蓝图也触发真实效果。
	Super::NotifyHit(MyComp, Other, OtherComp, bSelfMoved, HitLocation, HitNormal, NormalImpulse, Hit);

	// 沿用旧行为：仅旧 NPC 收到 ProjectileImpact；本轮没有新增 Mass/飞船伤害或护盾。
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
