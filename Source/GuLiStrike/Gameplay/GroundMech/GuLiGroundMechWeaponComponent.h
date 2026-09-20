#pragma once

#include "Components/ActorComponent.h"
#include "Gameplay/Data/Generated/GuLiStrikeMechTableRows.h"
#include "Battle/Combat/GuLiCombatDamageLedger.h"
#include "GuLiGroundMechWeaponComponent.generated.h"

class UNiagaraSystem;
class UNiagaraComponent;
class USkeletalMeshComponent;
class UGuLiGroundMechWeaponAnimInstance;

namespace GuLiMechFire
{
	GULISTRIKE_API double RescaleCooldown(double Now, double NextShot, float OldRate, float NewRate);
}

UCLASS(ClassGroup=(GuLiStrike), meta=(BlueprintSpawnableComponent))
class GULISTRIKE_API UGuLiGroundMechWeaponComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UGuLiGroundMechWeaponComponent();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
	virtual void TickComponent(float DeltaSeconds, ELevelTick Type, FActorComponentTickFunction* Tick) override;
	UFUNCTION(BlueprintCallable, Category="Mech|Weapon") void SetFireHeld(bool bHeld);
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Mech|Weapon") bool ApplyUpgradeById(const FString& Id);
	UFUNCTION(BlueprintPure, Category="Mech|Weapon") float GetFireRate() const { return ActiveUpgrade.FireRate; }
	UFUNCTION(BlueprintPure, Category="Mech|Weapon") float GetShotDamage() const { return ActiveUpgrade.Damage; }
	UFUNCTION(BlueprintPure, Category="Mech|Weapon") bool IsWeaponReady() const { return bConfigured; }
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Mech|Weapon") bool bWeaponEnabled = false;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Mech|Weapon") TObjectPtr<UDataTable> UpgradeTable;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Mech|Weapon") TObjectPtr<UDataTable> SkillTable;
	UPROPERTY(ReplicatedUsing=OnRep_Upgrade, BlueprintReadOnly, Category="Mech|Weapon") FString CurrentUpgradeId = TEXT("1.1");
	UPROPERTY(Replicated, BlueprintReadOnly, Category="Mech|Weapon") int32 ShotsFired = 0;
	UPROPERTY(BlueprintReadOnly, Category="Mech|Weapon") int32 CosmeticShots = 0;
	UPROPERTY(Replicated) FGuLiTargetHandle SourceHandle;
	UPROPERTY(Replicated) FVector_NetQuantize AimPoint = FVector::ZeroVector;
	UPROPERTY(BlueprintReadOnly, Category="Mech|Weapon") bool bFireHeld = false;
protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(EEndPlayReason::Type Reason) override;
private:
#if WITH_DEV_AUTOMATION_TESTS
	friend struct FGuLiMechFireTestAccess;
#endif
	UFUNCTION(Server, Reliable) void ServerSetFireHeld(bool bHeld, FVector_NetQuantize Point);
	UFUNCTION(Server, Unreliable) void ServerUpdateAim(FVector_NetQuantize Point);
	UFUNCTION(NetMulticast, Reliable) void MulticastShot(FGuid ShotId, float ServerTime);
	UFUNCTION() void OnRep_Upgrade();
	bool LoadConfiguration(const FString& Id);
	bool CanControl(bool bLocal) const;
	bool AcceptAim(const FVector& Point);
	void RegisterSource();
	void AlignGun();
	void TryFire();
	USkeletalMeshComponent* Gun() const;
	UPROPERTY() FGuLiStrikeMechUpgradesRow ActiveUpgrade;
	UPROPERTY() FGuLiStrikeMechSkillsRow ActiveSkill;
	UPROPERTY() TObjectPtr<UNiagaraSystem> BulletSystem;
	UPROPERTY() TObjectPtr<UNiagaraSystem> MuzzleSystem;
	UPROPERTY() TArray<TObjectPtr<UNiagaraComponent>> MuzzleEffects;
	TWeakObjectPtr<UGuLiGroundMechWeaponAnimInstance> ConfiguredAnim;
	bool bConfigured = false;
	bool bHasAim = false;
	double NextShotTime = 0;
	double NextAimSendTime = 0;
	uint32 SourceEpoch = 0;
	TSet<FGuid> SeenShots;
};
