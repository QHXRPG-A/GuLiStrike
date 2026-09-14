#pragma once

#include "CoreMinimal.h"
#include "Components/PrimitiveComponent.h"
#include "GameFramework/Actor.h"
#include "GuLiMapTypes.h"
#include "GuLiMapDensityMap.generated.h"

UCLASS()
class GULIMAPAUTHORINGEDITOR_API UGuLiMapDensityVisualizationComponent : public UPrimitiveComponent
{
    GENERATED_BODY()
public:
    UGuLiMapDensityVisualizationComponent();
    FPrimitiveSceneProxy* CreateSceneProxy() override;
    FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;
    double SampleSurfaceHeight(FIntPoint Corner, double CellSizeCm) const;
    void InvalidateSurfaceCache();
private:
    mutable TMap<FIntPoint, double> SurfaceHeightCache;
};

UCLASS(NotBlueprintable, HideCategories=(Collision,Physics,Rendering,Replication,Input,Actor,LOD,HLOD,Navigation,DataLayers,Transform))
class GULIMAPAUTHORINGEDITOR_API AGuLiMapDensityMap : public AActor
{
    GENERATED_BODY()
public:
    AGuLiMapDensityMap();
    UPROPERTY(EditAnywhere, Category="Map Authoring", meta=(ShowOnlyInnerProperties)) FGuLiMapDensityMapRecord Record;
    UPROPERTY(VisibleAnywhere, Category="Map Authoring") TObjectPtr<UGuLiMapDensityVisualizationComponent> Visualization;

    virtual bool IsEditorOnly() const override { return true; }
    virtual bool IsEditorOnlyLoadedInPIE() const override { return false; }
    virtual bool CanChangeIsSpatiallyLoadedFlag() const override { return false; }
    void PostActorCreated() override;
    void PostDuplicate(EDuplicateMode::Type DuplicateMode) override;
    void PostEditImport() override;
    void PreEditChange(FProperty* PropertyAboutToChange) override;
    void PostEditChangeProperty(FPropertyChangedEvent& Event) override;
    void PostEditMove(bool bFinished) override;
    void PostEditUndo() override;
    void PostLoad() override;
    void RefreshVisuals(bool bInvalidateSurface = false);
    bool IsLayerVisible(FName LayerKey) const;
    void SetLayerVisible(FName LayerKey, bool bVisible);
    void ShowAllLayers();
    void RegenerateIdentity();
private:
    TSet<FName> HiddenLayers;
    double CellSizeBeforeEdit = 2500.0;
    bool bDensityWasEmptyBeforeEdit = true;
    void EnforceEditorInvariants();
};
