#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GuLiBuildingConstructionVisualComponent.generated.h"

class UGuLiBuildingLifecycleComponent;
class UMeshComponent;
class USceneComponent;
class UMaterialInstanceDynamic;

UCLASS(Config=Game, DefaultConfig)
class GULISTRIKE_API UGuLiBuildingConstructionSettings : public UObject
{
	GENERATED_BODY()
public:
	/** Factory is the initial playable candidate; enable other IDs after visual review. */
	UPROPERTY(Config, EditAnywhere, Category="Construction") TArray<int32> EnabledDefinitionIds;
	UPROPERTY(Config, EditAnywhere, Category="Construction") int32 HologramVfxId = 0;
	UPROPERTY(Config, EditAnywhere, Category="Construction") int32 FinishGlowVfxId = 0;
	UPROPERTY(Config, EditAnywhere, Category="Construction") int32 GridVfxId = 0;
	UPROPERTY(Config, EditAnywhere, Category="Construction") float FinishSeconds = 0.65f;
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
	void RefreshVisualSources();
	UFUNCTION(BlueprintPure, Category="Building") float GetDisplayedProgress() const { return DisplayedProgress; }
	UFUNCTION(BlueprintPure, Category="Building") bool IsConstructionVisible() const { return bConstructing; }
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
	void ApplyProgress();
	UPROPERTY(Transient) TObjectPtr<UGuLiBuildingLifecycleComponent> Lifecycle;
	UPROPERTY(Transient) TObjectPtr<USceneComponent> SolidRoot;
	UPROPERTY(Transient) TObjectPtr<USceneComponent> GhostRoot;
	UPROPERTY(Transient) TArray<TObjectPtr<UMeshComponent>> Solids;
	UPROPERTY(Transient) TArray<TObjectPtr<UMeshComponent>> Ghosts;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> HologramMaterial;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> FinishMaterial;
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
};
