#pragma once

#include "Components/ActorComponent.h"
#include "Gameplay/Data/Generated/GuLiStrikeMechTableRows.h"
#include "Battle/Combat/GuLiCombatDamageLedger.h"
#include "GuLiGroundAttackSnapshot.h"
#include "Gameplay/GroundMech/Network/GuLiHybridGroundInputTypes.h"
#include "GuLiGroundMechWeaponComponent.generated.h"

class UNiagaraSystem;
class UNiagaraComponent;
class USkeletalMeshComponent;
class UGuLiGroundMechWeaponAnimInstance;
class AGuLiSoldierStateReplicator;
class AGuLiCommanderPresentationActor;
class AGuLiWingmanPresentationActor;
struct FGuLiHybridObjectState;
struct FGuLiGroundAttackSnapshot;
struct FSceneViewProjectionData;

/** One side of the point-up target marker, in 1080p design pixels. */
struct FGuLiAILockHexSide
{
	FGuLiAILockHexSide(int32 Index, double Progress, double LockedRadius, double Release = 0.);
	bool TouchesAperture(const FVector2D& ApertureFromTarget, double ApertureRadius) const;
	double Angle, Closure, Radius, Outset;
};

/** Player-viewport pixels from the same projection used to admit a target. */
struct FGuLiAILockSight
{
    float DesignRadius=0, HexRadius=0;
    double Started=0;
    FVector WorldPoint=FVector::ZeroVector;
    FVector2D Center=FVector2D::ZeroVector, TargetPoint=FVector2D::ZeroVector;
    FGuLiTargetHandle Target;
    float Radius=0, Progress=0, DistanceMeters=0;
    bool bProjected=false;
    bool IsLocked() const { return Target.IsValid() && Progress>=1.f; }
	bool IncludesTarget(const FGuLiTargetHandle& Candidate, const FVector2D& PlayerPixel) const;
    void UpdateAperture(const FSceneViewProjectionData& Projection);
};

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
	UFUNCTION(BlueprintPure, Category="Mech|Weapon") float GetFireRate() const { return EffectiveFireRate; }
	UFUNCTION(BlueprintPure, Category="Mech|Weapon") float GetShotDamage() const { return CachedAttack.Damage; }
    int32 GetWeaponLevel() const { return ActiveUpgrade.Level; }
    int32 GetSkillIconId() const { return ActiveSkill.IconId; }
    bool CanUseLocalWeapon() const { return CanControl(true); }
	UFUNCTION(BlueprintPure, Category="Mech|Weapon") bool IsWeaponReady() const { return bConfigured; }
	bool GetActualShotRay(FVector& Origin,FVector& Direction) const;
    bool GetActualSightPoint(FVector& Point,FVector& Direction) const;
    const FGuLiAILockSight& GetAILockSight() const { return AILockSight; }
    const TArray<FGuLiAILockSight>& GetLockTargets() const { return LocalLocks; }
    bool HasAIEngine() const { return AILockRank.Seconds>0; }
    /** Build the per-shot payload; admission independently freezes its authorized numeric capability. */
    FGuLiGroundAttackSnapshot BuildAttackSnapshot() const;
	/** Local presentation only; called from the unified accepted launch event. */
	void PlayFlightShot(FGuid ShotId, float ShotTime, uint8 MuzzleIndex = 0,int32 FrozenMuzzleVfxId=INDEX_NONE,float FrozenFireRate=0);
	/** Called only by the real animation notify; never submits gameplay during pose evaluation. */
	void QueueFireAnimationNotify(uint8 Side);
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
	friend class UGuLiGroundRogueDebugLibrary;
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
    bool UsesTargetLocks() const { return HasAIEngine() || MaxLockTargets>1; }
    bool InManualSplitSight(const FGuLiGroundLockInput& View,const FVector& Point) const;
    void ClearAILock();
	void RefreshAimAssistSources(uint32 MatchEpoch);
	void QueueAimAssistActor(AActor* Actor);
	void CacheAimAssistActor(AActor* Actor);
	void ResetAimAssistSources();
	void RegisterSource();
	void AlignGun();
	void TryFire();
	void FireFromAnimation(uint8 Side,uint64 Serial,double FireTime);
    UFUNCTION() void OnRogueBuildChanged(FGuid Player,int64 Revision);
    void RefreshRogueBuild(FGuid Player=FGuid(),int64 Revision=0);
    FGuLiGroundAttackSnapshot CachedAttack;
    GuLiNet::Effects::AILockRank AILockRank;
    FGuLiAILockSight AILockSight;
    TArray<FGuLiAILockSight> LocalLocks;
    FGuLiGroundLockInput LocalLockInput;
    TMap<FGuLiHybridObjectKey,double> AuthorityLockStarts;
    int32 MaxLockTargets=1;
    bool CheckAuthorityTarget(const FGuLiHybridObjectKey& Key,uint8 Side,double Progress,bool Retain,FVector& Point) const;
    void UpdateAuthorityLocks(bool bAim);
    float EffectiveFireRate=0;
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
	uint64 FireSerial=0;
	uint8 NextFireSide=0;
	float SavedFirePhase=0;
	bool bAcceptingFireNotifies=false,bRestoreFirePhase=false;
	struct FFireNotification {uint8 Side;uint64 Serial;double Time;};
	TArray<FFireNotification,TInlineAllocator<2>> PendingFireNotifications;
	void PresentAnimationFire(FGuid Shot,float Time,uint8 Side,int32 MuzzleVfx);
	uint32 SourceEpoch = 0;
	TSet<FGuid> SeenShots;
};
