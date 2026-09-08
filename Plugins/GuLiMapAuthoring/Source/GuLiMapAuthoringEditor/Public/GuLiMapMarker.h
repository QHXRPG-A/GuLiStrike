#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/PrimitiveComponent.h"
#include "GuLiMapTypes.h"
#include "GuLiMapMarker.generated.h"

UCLASS()
class GULIMAPAUTHORINGEDITOR_API UGuLiMapVisualizationComponent : public UPrimitiveComponent
{
    GENERATED_BODY()
public:
    UGuLiMapVisualizationComponent();
    FPrimitiveSceneProxy* CreateSceneProxy() override;
    FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;
};

UCLASS(NotBlueprintable, HideCategories=(Collision,Physics,Rendering,Replication,Input,Actor,LOD,HLOD,Navigation,DataLayers))
class GULIMAPAUTHORINGEDITOR_API AGuLiMapMarker : public AActor
{
    GENERATED_BODY()
public:
    AGuLiMapMarker();
    UPROPERTY(EditAnywhere, Category="Map Authoring", meta=(ShowOnlyInnerProperties)) FGuLiMapMarkerRecord Record;
    UPROPERTY(VisibleAnywhere, Category="Map Authoring") TObjectPtr<UGuLiMapVisualizationComponent> Visualization;
    virtual bool IsEditorOnly() const override { return true; }
    virtual bool IsEditorOnlyLoadedInPIE() const override { return false; }
    virtual bool CanChangeIsSpatiallyLoadedFlag() const override { return false; }
    void PostActorCreated() override;
    void PostDuplicate(EDuplicateMode::Type DuplicateMode) override;
    void PostEditImport() override;
    void PostEditChangeProperty(FPropertyChangedEvent& Event) override;
    void PostEditMove(bool bFinished) override;
    void PostEditUndo() override;
    void RegenerateIdentity();
    void EnsureRegionIdentities();
    void RefreshVisuals();
    UFUNCTION(CallInEditor, Category="Map Authoring") void SynchronizeType();
    UFUNCTION(CallInEditor, Category="Map Authoring") void UpgradeAndResetConflicts();
};
