#pragma once

#include "Components/ActorComponent.h"
#include "Gameplay/Data/Generated/GuLiStrikeMechTableRows.h"
#include "Battle/Combat/GuLiCombatDamageLedger.h"
#include "GuLiGroundMechWeaponComponent.generated.h"

class UNiagaraSystem;
class UNiagaraComponent;
class USkeletalMeshComponent;
class UGuLiGroundMechWeaponAnimInstance;
class AGuLiSoldierStateReplicator;
class AGuLiCommanderPresentationActor;
class AGuLiWingmanPresentationActor;
struct FGuLiHybridObjectState;

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

	virtual void TickComponent(float DeltaSeconds, ELevelTick Type, FActorComponentTickFunction* Tick) override;
	UFUNCTION(BlueprintCallable, Category="Mech|Weapon") void SetFireHeld(bool bHeld);
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Mech|Weapon") bool ApplyUpgradeById(const FString& Id);
    UFUNCTION(BlueprintCallable, Category="Mech|Weapon") FGuid RequestUpgradeById(const FString& Id);
    bool ApplyHybridUpgrade(const FGuLiHybridObjectState& State);
	UFUNCTION(BlueprintPure, Category="Mech|Weapon") float GetFireRate() const { return ActiveUpgrade.FireRate; }
	UFUNCTION(BlueprintPure, Category="Mech|Weapon") float GetShotDamage() const { return ActiveUpgrade.Damage; }
	UFUNCTION(BlueprintPure, Category="Mech|Weapon") bool IsWeaponReady() const { return bConfigured; }
	bool GetActualShotRay(FVector& Origin,FVector& Direction) const;
	/** Local presentation only; called from the unified accepted launch event. */
	void PlayFlightShot(FGuid ShotId, float ShotTime, uint8 MuzzleIndex = 0);
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Mech|Weapon") bool bWeaponEnabled = false;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Mech|Weapon") TObjectPtr<UDataTable> UpgradeTable;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Mech|Weapon") TObjectPtr<UDataTable> SkillTable;
	UPROPERTY(BlueprintReadOnly, Category="Mech|Weapon") FString CurrentUpgradeId = TEXT("1.1");
	UPROPERTY(BlueprintReadOnly, Category="Mech|Weapon") int32 ShotsFired = 0;
	UPROPERTY(BlueprintReadOnly, Category="Mech|Weapon") int32 CosmeticShots = 0;
	UPROPERTY() FGuLiTargetHandle SourceHandle;
	UPROPERTY() FVector_NetQuantize AimPoint = FVector::ZeroVector;
	UPROPERTY(BlueprintReadOnly, Category="Mech|Weapon") bool bFireHeld = false;
protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(EEndPlayReason::Type Reason) override;
private:
	friend class FGuLiHybridGroundAdapter;
	friend class AGuLiGroundMechCharacter;
	void AdvanceHybrid(float DeltaSeconds,bool bFire,const FVector& Aim,bool bHasAim);
#if WITH_DEV_AUTOMATION_TESTS
	friend struct FGuLiMechFireTestAccess;
#endif
	UFUNCTION() void OnRep_Upgrade();
	bool LoadConfiguration(const FString& Id);
	bool CanControl(bool bLocal) const;
	bool AcceptAim(const FVector& Point);
	/** Keeps the raw cursor/upper-body aim independent from the assisted weapon point. */
	bool ResolveLocalWeaponAim(FVector& OutPoint);
	void RefreshAimAssistSources(uint32 MatchEpoch);
	void QueueAimAssistActor(AActor* Actor);
	void CacheAimAssistActor(AActor* Actor);
	void ResetAimAssistSources();
	void RegisterSource();
	void AlignGun();
	void TryFire();
	USkeletalMeshComponent* Gun() const;
	FName MuzzleSocket(uint8 Index) const;
	UPROPERTY() FGuLiStrikeMechUpgradesRow ActiveUpgrade;
	UPROPERTY() FGuLiStrikeMechSkillsRow ActiveSkill;
	UPROPERTY() TObjectPtr<UNiagaraSystem> MuzzleSystem;
	UPROPERTY() TArray<TObjectPtr<UNiagaraComponent>> MuzzleEffects;
	TWeakObjectPtr<UGuLiGroundMechWeaponAnimInstance> ConfiguredAnim;
	TWeakObjectPtr<AGuLiSoldierStateReplicator> AimSoldierStates;
	TWeakObjectPtr<AGuLiCommanderPresentationActor> AimCommanderPresentation;
	TWeakObjectPtr<AGuLiWingmanPresentationActor> AimWingmanPresentation;
	TArray<TWeakObjectPtr<UGuLiCombatHealthComponent>> AimHealthComponents;
	TArray<TWeakObjectPtr<AActor>> PendingAimActors;
	FDelegateHandle AimActorSpawnedHandle;
	uint32 AimMatchEpoch = 0;
	bool bAimSourcesInitialized = false;
	bool bConfigured = false;
	bool bHasAim = false;
	double NextShotTime = 0;
	uint32 SourceEpoch = 0;
	TSet<FGuid> SeenShots;
};
