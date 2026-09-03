// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Battle/Combat/GuLiShipProjectileLedgerBridge.h"
#include "GameFramework/Actor.h"
#include "GuLiStrikeProjectile.generated.h"

class USphereComponent;
class UStaticMeshComponent;
class UProjectileMovementComponent;

/**
 *  服务器模拟与命中结算的弹丸；客户端消费移动复制与服务器销毁。
 *  Ship 武器命中统一 TargetHandle/Ledger，未注册的旧 NPC 仍沿用既有命中行为。
 */
UCLASS(abstract)
class AGuLiStrikeProjectile : public AActor
{
	GENERATED_BODY()
	
	/** Projectile collision sphere */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	USphereComponent* CollisionSphere;

	/** Mesh that provides the visual representation for this projectile */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	UStaticMeshComponent* Mesh;

	/** Handles movement behaviors for this projectile */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	UProjectileMovementComponent* ProjectileMovement;

public:	

	/** Constructor */
	AGuLiStrikeProjectile();
	virtual void Tick(float DeltaSeconds) override;

	/**
	 * Authority-only one-shot configuration used by the retained Ship weapon
	 * component. Legacy character projectiles remain valid when this is absent.
	 */
	bool ConfigureServerDamageLedger(const AActor& SourceActor, float Damage);

	/** Handles collisions */
	virtual void NotifyHit(class UPrimitiveComponent* MyComp, AActor* Other, class UPrimitiveComponent* OtherComp, bool bSelfMoved, FVector HitLocation, FVector HitNormal, FVector NormalImpulse, const FHitResult& Hit) override;

protected:
	virtual void BeginPlay() override;
	
	/** Handles collisions that stop this projectile from moving */
	UFUNCTION()
	void OnProjectileStop(const FHitResult& ImpactResult);

private:
	/** Server-only immutable launch identity; never replicated to visual clients. */
	FGuLiShipProjectileLedgerContext DamageLedgerContext;

	/** Previous authoritative transform sample for Actor-less Mass target sweeps. */
	FVector PreviousServerSweepLocation = FVector::ZeroVector;
	bool bHasPreviousServerSweepLocation = false;

};
