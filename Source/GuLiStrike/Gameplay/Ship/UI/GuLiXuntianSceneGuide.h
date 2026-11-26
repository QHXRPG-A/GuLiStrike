#pragma once

#include "CoreMinimal.h"
#include "Components/LineBatchComponent.h"
#include "Tasks/Task.h"
#include "GuLiXuntianSceneGuide.generated.h"

class UGuLiXuntianGunnery;
class UMaterialInterface;
class UProceduralMeshComponent;
class UStaticMesh;
class UStaticMeshComponent;

/** Owner-only, depth-tested coverage ink and markers in the pilot's scene. */
UCLASS(Transient)
class UGuLiXuntianSceneGuide final : public ULineBatchComponent
{
    GENERATED_BODY()
public:
    UGuLiXuntianSceneGuide();
    void UpdateGuide(const UGuLiXuntianGunnery& Aim);
    virtual FPrimitiveSceneProxy* CreateSceneProxy() override;
protected:
    virtual void OnRegister() override;
    virtual void OnUnregister() override;
private:
    void SurveyGround(const UGuLiXuntianGunnery& Aim, const FVector& Origin, int32 BellyCount);
    void RebuildGroundPatch(const UGuLiXuntianGunnery& Aim, const FVector& Origin, int32& TraceBudget);
    void PublishGroundCoverage(const UGuLiXuntianGunnery& Aim);
    struct FSample
    {
        FVector Point = FVector::ZeroVector;
        bool bValid = false;
    };
    struct FGroundPatch
    {
        TArray<FVector> Vertices;
        TArray<int32> Indices;
    };
    UPROPERTY() TObjectPtr<UMaterialInterface> GroundInk;
    UPROPERTY() TObjectPtr<UMaterialInterface> SkyInk;
    UPROPERTY() TObjectPtr<UStaticMesh> DomeMesh;
    UPROPERTY(Transient) TObjectPtr<UProceduralMeshComponent> Surface;
    UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> Sky;
    TMap<FIntPoint,FSample> Terrain;
    TArray<FIntPoint> MissingSurfaceKeys;
    TSharedPtr<const FGroundPatch,ESPMode::ThreadSafe> GroundPatch, DisplayedPatch;
    UE::Tasks::TTask<TArray<FVector2D>> CoverageTask;
    FIntPoint FineCenter = FIntPoint::ZeroValue, CoarseCenter = FIntPoint::ZeroValue;
    double CoverageStarted = 0;
    double NextSurvey = 0, NextMissingSurfaceRetry = 0;
    int32 LastBellyCount = -1;
    FVector LastPosition = FVector(UE_BIG_NUMBER);
    FQuat LastOrientation = FQuat::Identity;
    float LastMin = -1, LastMax = -1;
    uint32 LastLoadout = MAX_uint32;
    bool bSurveyPending = true;
};
