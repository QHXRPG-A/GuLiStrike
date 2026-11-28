#pragma once

#include "CoreMinimal.h"
#include "Battle/Contracts/GuLiWingmanProtocolTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "Gameplay/CombatEffects/GuLiHealthBarEvent.h"
#include "GuLiUnitFeedbackSubsystem.generated.h"

class UMaterialInterface;
class AGuLiUnitWreck;
class UMeshComponent;
class UNiagaraComponent;
class UNiagaraSystem;
class UGuLiSpellFieldDefinition;
struct FStreamableHandle;
struct FGuLiUnitVitalHistory;

/** Local presentation defaults. The config references are also covered by an explicit cook directory. */
UCLASS(Config=Game, DefaultConfig)
class GULISTRIKE_API UGuLiUnitFeedbackSettings : public UObject
{
	GENERATED_BODY()
public:
	UPROPERTY(Config, EditAnywhere, Category="Unit Feedback") int32 HitVfxId = 0;
	UPROPERTY(Config, EditAnywhere, Category="Unit Feedback") int32 InstancedHitVfxId = 0;
	UPROPERTY(Config, EditAnywhere, Category="Unit Feedback|Wreck") int32 WreckVfxId = 0;
	UPROPERTY(Config, EditAnywhere, Category="Unit Feedback|Wreck", meta=(ClampMin="1")) int32 MaximumConcurrentWrecks = 64;
	UPROPERTY(Config, EditAnywhere, Category="Unit Feedback|Wreck", meta=(ClampMin="0.1", Units="s")) float GroundWreckLifetime = 5.0f;
	UPROPERTY(Config, EditAnywhere, Category="Unit Feedback|Wreck", meta=(ClampMin="1", Units="s")) float FallingWreckMaximumLifetime = 30.0f;
	UPROPERTY(Config, EditAnywhere, Category="Unit Feedback") TArray<int32> ExplosionVfxIds;
	/** Aerial one-shots, authored for component-transform scaling (including their shockwaves). */
	UPROPERTY(Config, EditAnywhere, Category="Unit Feedback") TArray<int32> WingmanExplosionVfxIds;
	/** None uses the component transform; a named float receives the complete size once. */
	UPROPERTY(Config, EditAnywhere, Category="Unit Feedback") FName GroundExplosionScaleParameter = TEXT("User.Scale");
	UPROPERTY(Config, EditAnywhere, Category="Unit Feedback") FName WingmanExplosionScaleParameter;
	UPROPERTY(Config, EditAnywhere, Category="Unit Feedback", meta=(ClampMin="1")) int32 MaximumConcurrentExplosions = 64;
	/** Non-commander fallback only; commander feedback is hidden only in overview. */
	UPROPERTY(Config, EditAnywhere, Category="Unit Feedback", meta=(ClampMin="0")) float CullDistance = 36000.0f;
	UPROPERTY(Config, EditAnywhere, Category="Unit Feedback", meta=(ClampMin="0", Units="s")) float HealthBarHoldSeconds = 3.0f;
	UPROPERTY(Config, EditAnywhere, Category="Unit Feedback", meta=(ClampMin="0.01", Units="s")) float HealthBarFadeSeconds = 0.5f;
	float HealthBarOpacity(double Now, double LastChange) const;
};

USTRUCT()
struct FGuLiActiveHitOverlay
{
	GENERATED_BODY()
	UPROPERTY() TWeakObjectPtr<UMeshComponent> Mesh;
	UPROPERTY() TObjectPtr<UMaterialInterface> PreviousMaterial;
	UPROPERTY() TObjectPtr<UMaterialInterface> AppliedMaterial;
	float ExpireTime = 0;
};

USTRUCT()
struct FGuLiUnitExplosion
{
	GENERATED_BODY()
	UPROPERTY() TObjectPtr<UNiagaraComponent> Component;
	float Deadline = 0;
	bool bFinished = false;
};

/** One client-local owner of active hit overlays and pooled destruction effects; never exists on a dedicated server. */
UCLASS()
class GULISTRIKE_API UGuLiUnitFeedbackSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()
public:
	static constexpr float HitDuration = 0.5f;
	// Reserved only while the transient hit overlay is applied; unit base materials do not use this channel.
	static constexpr int32 HitTimeCustomDataIndex = 7;
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual bool IsTickable() const override;
	virtual TStatId GetStatId() const override;

	void RecordActorVitalChange(AActor* Actor, const FGuLiUnitVitalHistory& Vitals);
	void RemoveActorHealthBar(AActor* Actor);
	UFUNCTION(BlueprintCallable, Category="Unit Feedback") void FlashActor(AActor* Actor);
	UFUNCTION(BlueprintCallable, Category="Unit Feedback") void ClearActorFlash(AActor* Actor);
	UFUNCTION(BlueprintCallable, Category="Unit Feedback") void PlayDestruction(FVector Location, float UnitSize = 0.0f);
	/** Ground Mass deaths use one third of the model-relative explosion size. */
	void PlayGroundMassDestruction(FVector Location, float UnitSize);
	UFUNCTION(BlueprintPure, Category="Unit Feedback") float CalculateDestructionScale(float UnitSize) const;
	UFUNCTION(BlueprintPure, Category="Unit Feedback") float GetReferenceUnitSize() const { return ReferenceUnitSize; }
	static bool GetActorVisualBounds(const AActor* Actor, FBox& OutWorldBounds, float& OutSize, double* OutHorizontalWidth = nullptr);
	const TMap<TWeakObjectPtr<AActor>, FGuLiHealthBarEvent>& GetActorHealthBars() const { return ActorHealthBars; }
	UFUNCTION(BlueprintPure, Category="Unit Feedback") int32 GetActiveExplosionCount() const { return ActiveExplosions.Num(); }
	UFUNCTION(BlueprintPure, Category="Unit Feedback") int32 GetLoadedExplosionCount() const { return LoadedExplosions.Num(); }
	UFUNCTION(BlueprintPure, Category="Unit Feedback") int32 GetPendingExplosionCompileCount() const { return PreparingExplosionSystems.Num(); }
	UFUNCTION(BlueprintPure, Category="Unit Feedback") int32 GetExplosionCompileRequestCount() const { return ExplosionCompileRequests; }
	UMaterialInterface* GetInstancedHitMaterial() const { return InstancedHitMaterial; }
	UMaterialInterface* GetWreckMaterial() const { return WreckMaterial; }
	UFUNCTION(BlueprintCallable, Category="Unit Feedback") AGuLiUnitWreck* SpawnActorWreck(AActor* Source, FVector InitialVelocity, bool bFalling);
	bool IsWithinCullDistance(const FVector& Location) const;
	void PlayWingmanDestruction(const FGuLiWingmanHandle& Wingman, const FVector& Location);

private:
	UFUNCTION() void HandleExplosionFinished(UNiagaraComponent* Component);
	enum class EExplosionPreloadStage : uint8 { Stopped, Definitions, Systems, Finished };
	void StartExplosionPreload();
	void RequestExplosionSystems();
	void FinishExplosionPreload();
	void PrepareExplosionCompilation(UNiagaraSystem* System);
	void QueueDestruction(const FVector& Location, float UnitSize, bool bWingman, float ScaleMultiplier = 1.0f);
	void SpawnExplosion(const FVector& Location, float UnitSize, bool bWingman, float ScaleMultiplier);
	AGuLiUnitWreck* AllocateWreck(const FVector& Location);
	float ReferenceUnitSize = 0.0f;
	TMap<TWeakObjectPtr<AActor>, FGuLiHealthBarEvent> ActorHealthBars;
	uint64 NextBarGeneration = 1;
	float NextHealthBarCleanupTime = 0.0f;
	struct FPendingExplosion { FVector Location; float UnitSize; float ExpireTime; bool bWingman; float ScaleMultiplier; };
	TArray<FPendingExplosion> PendingExplosions;
	TArray<TWeakObjectPtr<UNiagaraSystem>> PreparingExplosionSystems;
	int32 ExplosionCompileRequests=0;
	EExplosionPreloadStage ExplosionPreloadStage = EExplosionPreloadStage::Stopped;
	TSharedPtr<FStreamableHandle> ExplosionDefinitionsLoad;
	TSharedPtr<FStreamableHandle> ExplosionSystemsLoad;
	TArray<int32> StartupExplosionVfxIds;
	TArray<int32> StartupWarningVfxIds;
	TArray<FSoftObjectPath> StartupAdditionalSystemPaths;
	UPROPERTY(Transient) TArray<TObjectPtr<UObject>> LoadedProjectileAssets;
	UPROPERTY(Transient) TArray<TObjectPtr<UGuLiSpellFieldDefinition>> LoadedExplosionFields;
	FRandomStream CosmeticRandom;
	TSet<FGuLiWingmanHandle> DestroyedWingmen;
	TArray<FGuLiWingmanHandle> DestroyedWingmanOrder;
	UPROPERTY(Transient) TObjectPtr<UMaterialInterface> HitMaterial;
	UPROPERTY(Transient) TObjectPtr<UMaterialInterface> InstancedHitMaterial;
	UPROPERTY(Transient) TObjectPtr<UMaterialInterface> WreckMaterial;
	UPROPERTY(Transient) TArray<TWeakObjectPtr<AGuLiUnitWreck>> ActiveWrecks;
	UPROPERTY(Transient) TArray<int32> LoadedExplosions;
	UPROPERTY(Transient) TArray<int32> LoadedWingmanExplosions;
	UPROPERTY(Transient) TArray<FGuLiActiveHitOverlay> ActiveHits;
	UPROPERTY(Transient) TArray<FGuLiUnitExplosion> ActiveExplosions;
};
