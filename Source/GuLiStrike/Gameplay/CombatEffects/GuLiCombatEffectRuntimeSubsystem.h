#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Gameplay/CombatEffects/GuLiCombatEffectDefinition.h"
#include "GuLiCombatEffectRuntimeSubsystem.generated.h"

USTRUCT()
struct FGuLiRuntimeCombatEffect
{
	GENERATED_BODY()
	UPROPERTY() FGuLiCombatEffectState State;
	UPROPERTY() FGuLiCombatEffectContext Context;
	UPROPERTY() TObjectPtr<UGuLiProjectileEffectDefinition> Projectile;
	UPROPERTY() TObjectPtr<UGuLiSpellFieldDefinition> Field;
	FGuid SourceLease;
	EGuLiSpellFieldTiming Timing = EGuLiSpellFieldTiming::Instant;
	float Interval = 1.0f;
	float Duration = 0.0f;
	float FadeSeconds = 0.0f;
	float FrozenRadius = 0.0f;
	float FrozenDelay = 0.0f;
	float StopDistance = 0.0f;
	float OrbitRadius = 0.0f;
	float CooldownSeconds = 0.0f;
	float GunSpeed = 0.0f;
	float GunLifetime = 0.0f;
	float GunSweepRadius = 0.0f;
	float GunRange = 0.0f;
	int32 VariantCount = 0;
	int32 DeliveredPulses = 0;
	float LastCorrection = 0.0f;
};

struct FGuLiWingmanBurstCooldown
{
	FGuLiTargetHandle Carrier;
	float OrbitRadius = 0.0f;
	float DurationSeconds = 0.0f;
	double OrbitEntryTime = -1.0;
};

DECLARE_MULTICAST_DELEGATE_TwoParams(FGuLiCombatEffectStateEvent, const FGuLiCombatEffectState&, bool /* reliable */);
DECLARE_MULTICAST_DELEGATE_OneParam(FGuLiCombatShotBatchEvent, const TArray<FGuLiCombatShotCue>&);
DECLARE_MULTICAST_DELEGATE_OneParam(FGuLiCombatEffectEpochEvent, uint32);

/** Server-only, 30 Hz, non-Mass skill runtime. Clients cannot create authoritative effects. */
UCLASS()
class GULISTRIKE_API UGuLiCombatEffectRuntimeSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()
public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual bool IsTickable() const override;
	virtual TStatId GetStatId() const override;

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Combat Effects")
	FGuid LaunchProjectile(UGuLiProjectileEffectDefinition* Definition, const FGuLiCombatEffectContext& Context, const FTransform& LaunchTransform);
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Combat Effects")
	FGuid CreateSpellField(UGuLiSpellFieldDefinition* Definition, const FGuLiCombatEffectContext& Context, FVector Location);
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Combat Effects")
	bool CancelEffect(FGuid EffectId);
	UFUNCTION(BlueprintPure, Category="Combat Effects")
	bool QueryEffect(FGuid EffectId, FGuLiCombatEffectState& OutState) const;
	UFUNCTION(BlueprintPure, Category="Combat Effects")
	int32 GetActiveEffectCount() const;
	UFUNCTION(BlueprintPure, Category="Combat Effects")
	FGuLiCombatEffectCounters GetCounters() const { return Counters; }

	using FAttackExecutor = TFunction<void(const FGuLiCombatAttackRequest&, TArray<FGuLiDamageRequest>&, TArray<FGuLiCombatShotCue>&)>;
	bool RegisterAttackExecutor(FName Id, FAttackExecutor Executor);
	/** Dispatch launches first, then commit collected hitscan damage: same-step mutual kills remain possible. */
	void ExecuteAttackBatch(TConstArrayView<FGuLiCombatAttackRequest> Requests);
	/** Validated wingman adapter. Definitions and damage are resolved by the Ship skill, not Commander tables. */
	bool ExecuteWingmanAttack(const FGuLiCombatAttackRequest& Request);
	/** Starts one server-owned machine-gun segment. Logical shots are settled at the runtime's fixed 30 Hz tick. */
	FGuid StartWingmanGunBurst(const FGuLiCombatAttackRequest& Request, float ShotIntervalSeconds,
		float DurationSeconds, float StopDistanceCentimeters, float OrbitRadiusCentimeters, float CooldownSeconds);
	int32 CancelWingmanGunBurst(const FGuLiWingmanHandle& Emitter, bool bClearCooldown = false);
	FGuid LaunchPointProjectile(const FGuLiCombatAttackRequest& Request);
	void BuildActiveSnapshot(TArray<FGuLiCombatEffectState>& OutStates) const;
	uint32 GetEffectEpoch() const { return Epoch; }

	FGuLiCombatEffectStateEvent OnState;
	FGuLiCombatShotBatchEvent OnShots;
	FGuLiCombatEffectEpochEvent OnEpoch;

private:
	friend struct FGuLiCombatEffectRuntimeTestAccess;
	friend struct FGuLiWingmanAttackCombatTestAccess;
	bool SynchronizeEpoch();
	bool PrepareContext(const FGuLiCombatEffectContext& Input, FGuLiCombatEffectContext& Output);
	bool ResolveFieldConfig(UGuLiSpellFieldDefinition* Definition, const FGuLiCombatEffectContext& Input,
		FGuLiSpellFieldConfig& OutConfig, FGuLiCombatEffectContext& OutContext);
	FGuid CreateFieldInternal(UGuLiSpellFieldDefinition* Definition, const FGuLiCombatEffectContext& Context, FVector Location,
		const FGuid& Lease, const FGuLiRuntimeCombatEffect* Frozen = nullptr, const FGuLiSpellFieldConfig* Config = nullptr);
	void StepProjectile(const FGuid& Id, float DeltaTime, float Now);
	void StepField(const FGuid& Id, float Now);
	void StepSustainedHitscan(const FGuid& Id, float Now);
	void HandlePooledState(const FGuLiCombatEffectState& State, bool bReliable);
	void StepWingmanBurstCooldowns(float Now);
	void BeginWingmanBurstCooldown(const FGuLiRuntimeCombatEffect& Instance);
	void Finish(const FGuid& Id, EGuLiCombatEffectEndReason Reason, float Now);
	void Publish(const FGuid& Id, bool bReliable);
	void BuildSpatialIndex();
	void QuerySphere(FVector Center, float Radius, TArray<FGuLiCombatTargetSnapshot>& OutTargets);
	UGuLiCombatEffectCatalog* GetCatalog();
	void ExecuteDirect(const FGuLiCombatAttackRequest& Request, TArray<FGuLiDamageRequest>& OutDamage, TArray<FGuLiCombatShotCue>& OutCues);
	void ExecuteProjectile(const FGuLiCombatAttackRequest& Request, TArray<FGuLiDamageRequest>& OutDamage, TArray<FGuLiCombatShotCue>& OutCues);
	static FGuLiDamageRequest MakeDamage(const FGuLiCombatEffectContext& Context, FGuid DamageId, FGuLiTargetHandle Target, FVector Location);

	UPROPERTY(Transient) TObjectPtr<UGuLiDamageLedgerSubsystem> Ledger;
	UPROPERTY(Transient) TObjectPtr<class UGuLiProjectilePoolSubsystem> ProjectilePool;
	UPROPERTY(Transient) TObjectPtr<class UGuLiCommanderDataSubsystem> CommanderData;
	UPROPERTY(Transient) TObjectPtr<class UGuLiSpellFieldDataSubsystem> FieldData;
	UPROPERTY(Transient) TObjectPtr<UGuLiCombatEffectCatalog> Catalog;
	UPROPERTY(Transient) TMap<FGuid, FGuLiRuntimeCombatEffect> Effects;
	TMap<FName, FAttackExecutor> Executors;
	TArray<FGuLiCombatTargetSnapshot> TargetSnapshots;
	TMap<FIntPoint, TArray<int32>> SpatialGrid;
	TMap<FGuLiWingmanHandle, TMap<FName, FGuLiWingmanBurstCooldown>> WingmanBurstCooldowns;
	TArray<FGuid> StepIds;
	FGuLiCombatEffectCounters Counters;
	uint32 Epoch = 0;
	double Accumulator = 0.0;
	float MaximumTargetRadius = 0.0f;
	bool bIndexReady = false;
	bool bInsideStep = false;
	bool bCatalogWarning = false;
	TSet<FName> WarnedMounts;
	TSet<FName> WarnedFieldConfigs;
};
