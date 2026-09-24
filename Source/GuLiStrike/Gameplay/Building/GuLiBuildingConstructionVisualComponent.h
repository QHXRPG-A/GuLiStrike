#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Gameplay/Building/GuLiConstructionShape.h"
#include "GuLiBuildingConstructionVisualComponent.generated.h"

class UGuLiBuildingLifecycleComponent;
class UMeshComponent;
class USceneComponent;
class UMaterialInstanceDynamic;
class UNiagaraComponent;

UCLASS(Config=Game, DefaultConfig)
class GULISTRIKE_API UGuLiBuildingConstructionSettings : public UObject
{
	GENERATED_BODY()
public:
	UPROPERTY(Config, EditAnywhere, Category="Construction") bool bEnabled = true;
	UPROPERTY(Config, EditAnywhere, Category="Construction") TSoftObjectPtr<UGuLiConstructionShapeCatalog> ShapeCatalog;
	UPROPERTY(Config, EditAnywhere, Category="Construction") int32 HologramVfxId = 0;
	UPROPERTY(Config, EditAnywhere, Category="Construction") int32 FinishGlowVfxId = 0;
	UPROPERTY(Config, EditAnywhere, Category="Construction") int32 GridVfxId = 0;
	UPROPERTY(Config, EditAnywhere, Category="Construction") float FinishSeconds = 0.65f;
	UPROPERTY(Config, EditAnywhere, Category="Construction|VFX") int32 CompleteVfxId = 0;
	UPROPERTY(Config, EditAnywhere, Category="Construction|VFX") int32 TopLoopVfxId = 0;
	UPROPERTY(Config, EditAnywhere, Category="Construction|VFX") int32 LaserVfxId = 0;
	UPROPERTY(Config, EditAnywhere, Category="Construction|VFX") float ContourSampleSpacing = 60.0f;
};

/** Reconstructable local presentation; never moves collision or advances construction work. */
UCLASS(ClassGroup=(GuLiStrike), meta=(BlueprintSpawnableComponent))
class GULISTRIKE_API UGuLiBuildingConstructionVisualComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UGuLiBuildingConstructionVisualComponent();
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* TickFunction) override;
	/** Call after the owner's mesh or presentation child is initialized/replaced. */
	UFUNCTION(BlueprintCallable, Category="Building|Construction") void RefreshVisualSources();
	UFUNCTION(BlueprintPure, Category="Building|Construction") TArray<FVector> GetConstructionContour(int32 Contour = 0) const;
	UFUNCTION(BlueprintPure, Category="Building|Construction") bool SampleConstructionContour(int32 Contour, float Distance, FVector& OutPoint) const;
	UFUNCTION(BlueprintPure, Category="Building|Construction") bool FindNearestConstructionSpan(FVector WorldLocation, FGuLiConstructionSpan& OutSpan) const;
	UFUNCTION(BlueprintPure, Category="Building|Construction") float GetConstructionHeight() const;
	uint32 GetShapeRevision() const { return ShapeRevision; }
	UFUNCTION(BlueprintPure, Category="Building") float GetDisplayedProgress() const { return DisplayedProgress; }
	UFUNCTION(BlueprintPure, Category="Building") bool IsConstructionVisible() const { return bConstructing; }
	/** World-space closed roof polygon; final edge joins last point to first. */
	UFUNCTION(BlueprintPure, Category="Building") TArray<FVector> GetRoofContour() const;
	int32 FindNearestRoofEdge(const FVector& WorldLocation) const;
	bool SampleRoofEdge(int32 Edge, float Fraction, FVector& OutPoint) const;
private:
	struct FOriginalVisibility
	{
		TWeakObjectPtr<USceneComponent> Component;
		bool bVisible = true;
	};
	void RefreshState();
	bool BeginConstruction();
	void FinishConstruction();
	void RestoreOriginals();
	void ClearPresentation();
	void ClearMeshes();
	void ApplyProgress();
	void UpdateTopEffect();
	void StopTopEffect();
	void PlayCompletionEffect();
	void ResolveShape();
	void ConfigureEffect(UNiagaraComponent& Effect, bool bCompletion);
	bool IsContourPointVisible(const FVector& From, const FVector& Point) const;
	UPROPERTY(Transient) TObjectPtr<UGuLiBuildingLifecycleComponent> Lifecycle;
	UPROPERTY(Transient) TObjectPtr<USceneComponent> SolidRoot;
	UPROPERTY(Transient) TObjectPtr<USceneComponent> GhostRoot;
	UPROPERTY(Transient) TArray<TObjectPtr<UMeshComponent>> Solids;
	UPROPERTY(Transient) TArray<TObjectPtr<UMeshComponent>> Ghosts;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> HologramMaterial;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> FinishMaterial;
	UPROPERTY(Transient) TObjectPtr<UNiagaraComponent> TopEffect;
	UPROPERTY(Transient) TObjectPtr<UNiagaraComponent> CompletionEffect;
	UPROPERTY(Transient) TObjectPtr<UGuLiConstructionShape> Shape;
	TArray<TArray<FVector>> WorldContours;
	TArray<float> ContourLengths;
	TArray<FVector> EffectPoints, EffectTangents;
	TArray<float> EffectLengths;
	FBox EffectBounds = FBox(ForceInit);
	float ModelTop = 0;
	float EffectColumnHeight = 360;
	uint32 ShapeRevision = 0;
	FTimerHandle SourceRetryTimer;
	bool bShapeWarningLogged = false;
	TArray<FOriginalVisibility> OriginalVisibility;
	FDelegateHandle StateChangedHandle;
	float RiseHeight = 1.0f;
	float DisplayedProgress = 0.0f;
	float TargetProgress = 0.0f;
	float InterpolationStart = 0.0f;
	float InterpolationElapsed = 0.0f;
	float FinishElapsed = 0.0f;
	bool bConstructing = false;
	bool bFinishing = false;
	bool bAssetsFailed = false;
	uint32 ObservedInstance = 0;
	bool bObservedUnderConstruction = false;
	bool bCompletionPlayed = false;
};
