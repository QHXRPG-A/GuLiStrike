// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Battle/Combat/GuLiShipProjectileLedgerBridge.h"
#include "Gameplay/CombatEffects/GuLiFlightEvent.h"
#include "GameFramework/Actor.h"
#include "GuLiStrikeProjectile.generated.h"

class USphereComponent;
class UStaticMeshComponent;
class UProjectileMovementComponent;

/**
 *  服务器模拟与命中结算的弹丸；客户端使用独立池对象消费创建/结束事件。
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
	const UStaticMeshComponent* GetFlightMesh() const { return Mesh; }
	FGuid GetFlightId() const { return DamageLedgerContext.ShotId; }

	/** Handles collisions */
	virtual void NotifyHit(class UPrimitiveComponent* MyComp, AActor* Other, class UPrimitiveComponent* OtherComp, bool bSelfMoved, FVector HitLocation, FVector HitNormal, FVector NormalImpulse, const FHitResult& Hit) override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void LifeSpanExpired() override;
	
	/** Handles collisions that stop this projectile from moving */
	UFUNCTION()
	void OnProjectileStop(const FHitResult& ImpactResult);

private:
	/** Server-only immutable launch identity; never replicated to visual clients. */
	FGuLiShipProjectileLedgerContext DamageLedgerContext;

	/** Previous authoritative transform sample for Actor-less Mass target sweeps. */
	FVector PreviousServerSweepLocation = FVector::ZeroVector;
	TArray<FGuLiCombatTargetSnapshot> WingmanSweepWorkspace;
	bool bHasPreviousServerSweepLocation = false;
	void PublishServerLaunch();
	void FinishFlight(EGuLiCombatEffectEndReason Reason, const FVector& Location);
	FGuLiFlightEvent Flight;
	bool bFlightPublished = false;
	bool bFlightEnded = false;

};
