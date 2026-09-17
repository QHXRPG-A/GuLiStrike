#pragma once

#include "CoreMinimal.h"
#include "Battle/Contracts/GuLiWingmanProtocolTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "GuLiUnitFeedbackSubsystem.generated.h"

class UMaterialInterface;
class AGuLiUnitWreck;
class UMeshComponent;
class UNiagaraComponent;
class UNiagaraSystem;
struct FStreamableHandle;
DECLARE_MULTICAST_DELEGATE_OneParam(FGuLiActorHealthBarChanged, TWeakObjectPtr<AActor>);

/** Local presentation defaults. The config references are also covered by an explicit cook directory. */
UCLASS(Config=Game, DefaultConfig)
class GULISTRIKE_API UGuLiUnitFeedbackSettings : public UObject
{
	GENERATED_BODY()
public:
	UPROPERTY(Config, EditAnywhere, Category="Unit Feedback") TSoftObjectPtr<UMaterialInterface> HitMaterial;
	UPROPERTY(Config, EditAnywhere, Category="Unit Feedback") TSoftObjectPtr<UMaterialInterface> InstancedHitMaterial;
	UPROPERTY(Config, EditAnywhere, Category="Unit Feedback|Wreck") TSoftObjectPtr<UMaterialInterface> WreckMaterial;
	UPROPERTY(Config, EditAnywhere, Category="Unit Feedback|Wreck", meta=(ClampMin="1")) int32 MaximumConcurrentWrecks = 64;
	UPROPERTY(Config, EditAnywhere, Category="Unit Feedback|Wreck", meta=(ClampMin="0.1", Units="s")) float GroundWreckLifetime = 5.0f;
	UPROPERTY(Config, EditAnywhere, Category="Unit Feedback|Wreck", meta=(ClampMin="1", Units="s")) float FallingWreckMaximumLifetime = 30.0f;
	UPROPERTY(Config, EditAnywhere, Category="Unit Feedback") TArray<TSoftObjectPtr<UNiagaraSystem>> Explosions;
	/** Aerial one-shots, authored for component-transform scaling (including their shockwaves). */
	UPROPERTY(Config, EditAnywhere, Category="Unit Feedback") TArray<TSoftObjectPtr<UNiagaraSystem>> WingmanExplosions;
	/** None uses the component transform; a named float receives the complete size once. */
	UPROPERTY(Config, EditAnywhere, Category="Unit Feedback") FName GroundExplosionScaleParameter = TEXT("User.Scale");
	UPROPERTY(Config, EditAnywhere, Category="Unit Feedback") FName WingmanExplosionScaleParameter;
	UPROPERTY(Config, EditAnywhere, Category="Unit Feedback", meta=(ClampMin="1")) int32 MaximumConcurrentExplosions = 64;
	UPROPERTY(Config, EditAnywhere, Category="Unit Feedback", meta=(ClampMin="0")) float CullDistance = 180000.0f;
	/** Art calibration for the smallest model in the soldier catalog; model sizes are never hardcoded. */
	UPROPERTY(Config, EditAnywhere, Category="Unit Feedback", meta=(ClampMin="0.001")) float ReferenceExplosionScale = 1.0f;
	UPROPERTY(Config, EditAnywhere, Category="Unit Feedback", meta=(ClampMin="0", Units="s")) float HealthBarHoldSeconds = 3.0f;
	UPROPERTY(Config, EditAnywhere, Category="Unit Feedback", meta=(ClampMin="0.01", Units="s")) float HealthBarFadeSeconds = 0.5f;
};

USTRUCT()
struct FGuLiActiveHitOverlay
{
	GENERATED_BODY()
	UPROPERTY() TWeakObjectPtr<UMeshComponent> Mesh;
	UPROPERTY() TObjectPtr<UMaterialInterface> PreviousMaterial;
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

/** Client-local transient cue, not a second source of gameplay health. Negative fraction means no health provider. */
struct FGuLiActorHitHealthBar
{
	float StartTime = 0.0f;
	float HealthFraction = -1.0f;
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
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual bool IsTickable() const override;
	virtual TStatId GetStatId() const override;

	UFUNCTION(BlueprintCallable, Category="Unit Feedback") void FlashActor(AActor* Actor, float HealthFraction = -1.0f);
	UFUNCTION(BlueprintCallable, Category="Unit Feedback") void ClearActorFlash(AActor* Actor);
	UFUNCTION(BlueprintCallable, Category="Unit Feedback") void PlayDestruction(FVector Location, float UnitSize = 0.0f);
	UFUNCTION(BlueprintPure, Category="Unit Feedback") float CalculateDestructionScale(float UnitSize) const;
	UFUNCTION(BlueprintPure, Category="Unit Feedback") float GetReferenceUnitSize() const { return ReferenceUnitSize; }
	static bool GetActorVisualBounds(const AActor* Actor, FBox& OutWorldBounds, float& OutSize);
	static float HealthBarOpacity(float Age);
	void EnsureHealthBarRenderers();
	const TMap<TWeakObjectPtr<AActor>, FGuLiActorHitHealthBar>& GetActorHealthBars() const { return ActorHealthBars; }
	FGuLiActorHealthBarChanged OnActorHealthBarChanged;
	UFUNCTION(BlueprintPure, Category="Unit Feedback") int32 GetActiveExplosionCount() const { return ActiveExplosions.Num(); }
	UFUNCTION(BlueprintPure, Category="Unit Feedback") int32 GetLoadedExplosionCount() const { return LoadedExplosions.Num(); }
	UMaterialInterface* GetInstancedHitMaterial() const { return InstancedHitMaterial; }
	UMaterialInterface* GetWreckMaterial() const { return WreckMaterial; }
	UFUNCTION(BlueprintCallable, Category="Unit Feedback") AGuLiUnitWreck* SpawnActorWreck(AActor* Source, FVector InitialVelocity, bool bFalling);
	bool IsWithinCullDistance(const FVector& Location) const;
	void ApplyWingmanFeedback(const FGuLiWingmanHandle& Wingman, const FVector& Location, bool bDestroyed, float HealthFraction = -1.0f);

private:
	UFUNCTION() void HandleExplosionFinished(UNiagaraComponent* Component);
	void FinishLoading();
	void QueueDestruction(const FVector& Location, float UnitSize, bool bWingman);
	void SpawnExplosion(const FVector& Location, float UnitSize, bool bWingman);
	AGuLiUnitWreck* AllocateWreck(const FVector& Location);
	float ReferenceUnitSize = 0.0f;
	TMap<TWeakObjectPtr<AActor>, FGuLiActorHitHealthBar> ActorHealthBars;
	float NextHealthBarCleanupTime = 0.0f;
	struct FPendingExplosion { FVector Location; float UnitSize; float ExpireTime; bool bWingman; };
	TArray<FPendingExplosion> PendingExplosions;
	TSharedPtr<FStreamableHandle> LoadHandle;
	FRandomStream CosmeticRandom;
	TSet<FGuLiWingmanHandle> DestroyedWingmen;
	TArray<FGuLiWingmanHandle> DestroyedWingmanOrder;
	UPROPERTY(Transient) TObjectPtr<UMaterialInterface> HitMaterial;
	UPROPERTY(Transient) TObjectPtr<UMaterialInterface> InstancedHitMaterial;
	UPROPERTY(Transient) TObjectPtr<UMaterialInterface> WreckMaterial;
	UPROPERTY(Transient) TArray<TWeakObjectPtr<AGuLiUnitWreck>> ActiveWrecks;
	UPROPERTY(Transient) TArray<TObjectPtr<UNiagaraSystem>> LoadedExplosions;
	UPROPERTY(Transient) TArray<TObjectPtr<UNiagaraSystem>> LoadedWingmanExplosions;
	UPROPERTY(Transient) TArray<FGuLiActiveHitOverlay> ActiveHits;
	UPROPERTY(Transient) TArray<FGuLiUnitExplosion> ActiveExplosions;
};
