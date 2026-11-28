#pragma once

#include "CoreMinimal.h"
#include "Battle/Contracts/GuLiWingmanProtocolTypes.h"
#include "Battle/Network/GuLiBattleTypes.h"
#include "Components/LineBatchComponent.h"
#include "Tasks/Task.h"
#include "GuLiXuntianSceneGuide.generated.h"

class UGuLiXuntianGunnery;
class UMaterialInterface;
class UProceduralMeshComponent;
class UStaticMesh;
class UStaticMeshComponent;

enum class EGuLiXuntianAirBadge : uint8 { Air, Belly, Closed };

struct FGuLiXuntianAirMarker
{
    FVector Center, Extent;
    EGuLiXuntianAirBadge Badge;
};

/** Owner-only, depth-tested coverage ink and markers in the pilot's scene. */
UCLASS(Transient, MinimalAPI)
class UGuLiXuntianSceneGuide final : public ULineBatchComponent
{
    GENERATED_BODY()
    friend class UGuLiHybridAuthoringLibrary;
public:
    UGuLiXuntianSceneGuide();
    void UpdateGuide(const UGuLiXuntianGunnery& Aim);
    virtual FPrimitiveSceneProxy* CreateSceneProxy() override;
    virtual void GetUsedMaterials(TArray<UMaterialInterface*>& OutMaterials, bool bGetDebugMaterials = false) const override;
protected:
    virtual void SendRenderDynamicData_Concurrent() override;
    virtual void OnRegister() override;
    virtual void OnUnregister() override;
private:
    void RefreshAirContacts(const UGuLiXuntianGunnery& Aim, EGuLiTeam Team, double Now);
    void UpdateAirMarkersBeforeRender(UWorld* World);
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
    struct FAirContact
    {
        TWeakObjectPtr<const UStaticMeshComponent> Body;
        uint64 Revision = 0;
        FVector Sample = FVector::ZeroVector, From = FVector::ZeroVector;
        double SampleTime = -1, BlendSeconds = 0;
        double Radius = 0, PendingSince = -1;
        EGuLiXuntianAirBadge Badge = EGuLiXuntianAirBadge::Closed, Pending = EGuLiXuntianAirBadge::Closed;
        bool bBelow = false;

        FVector InterpolatedCenter(double Now) const;
    };
    TMap<FGuLiTargetHandle,FAirContact> AirContacts;
    TArray<FGuLiXuntianAirMarker> AirMarkers;
    uint64 AirContactsRevision = MAX_uint64;
    EGuLiTeam AirViewerTeam = EGuLiTeam::Unassigned;
    double MarkerHullHeight = 0, MarkerReach = 0;
    FVector LevelMarkOrigin = FVector::ZeroVector;
    bool bSurveyLinesDirty = false, bHadGroundHint = false;
    int32 AirGunCount = 0;
    bool bAirUpdatePending = false, bCheckAirCoverage = false;
    UPROPERTY() TObjectPtr<UMaterialInterface> GroundInk;
    UPROPERTY() TObjectPtr<UMaterialInterface> SkyInk;
    UPROPERTY() TObjectPtr<UMaterialInterface> MarkerInk;
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
