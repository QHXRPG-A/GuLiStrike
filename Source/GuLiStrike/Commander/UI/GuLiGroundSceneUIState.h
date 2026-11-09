#pragma once

#include "CoreMinimal.h"
#include "Network/GuLiHybridWorldSubsystem.h"
#include "Commander/UI/GuLiSceneUITypes.h"
#include "GuLiGroundSceneUIState.generated.h"

class APlayerController;
class AGuLiCommanderPresentationActor;
class UGuLiGroundOutlinePool;
class UMaterialInterface;
class UMaterial;
class USceneCaptureComponent2D;
struct FSceneViewProjectionData;

USTRUCT()
struct FGuLiGroundMaskMaterial
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere,Category="Ground World UI") TSoftObjectPtr<UMaterialInterface> Source;
    UPROPERTY(EditAnywhere,Category="Ground World UI") TSoftObjectPtr<UMaterialInterface> Mask;
};

/** Ground scene UI has its own distance policy; mesh/effect LOD settings stay independent. */
UCLASS(Config=Game,DefaultConfig)
class GULISTRIKE_API UGuLiGroundSceneUISettings final : public UObject
{
    GENERATED_BODY()
public:
    UPROPERTY(Config,EditAnywhere,Category="Ground World UI") float FadeStartCm=35000;
    UPROPERTY(Config,EditAnywhere,Category="Ground World UI") float FadeEndCm=50000;
    UPROPERTY(Config,EditAnywhere,Category="Ground World UI") TSoftObjectPtr<UMaterialInterface> OutlineMaterial;
    UPROPERTY(Config,EditAnywhere,Category="Ground World UI") TArray<FGuLiGroundMaskMaterial> MaskMaterials;
};

namespace GuLiGroundSceneUI
{
    GULISTRIKE_API bool IsView(const APlayerController* Controller);
    GULISTRIKE_API bool IsDisplayKind(EGuLiHybridObjectKind Kind);
    GULISTRIKE_API float DistanceOpacity(double Distance,float Start,float End);
    GULISTRIKE_API float EventOpacity(double Now,double LastChange,float Hold,float Fade);

    /** Previous values are display-only. A life/team/visibility transition seeds a new baseline. */
    struct GULISTRIKE_API FVitalHistory
    {
        FGuLiCombatHealthState Health;
        EGuLiTeam Team=EGuLiTeam::Unassigned;
        double LastChange=-1;
        bool bInitialized=false;
        bool Observe(const FGuLiCombatHealthState& Next,EGuLiTeam NextTeam,bool bEligible,double Now);
    };
}

/** One local view's read model, never a gameplay writer or a second damage detector. */
UCLASS()
class GULISTRIKE_API UGuLiGroundSceneUIState final : public UObject
{
    GENERATED_BODY()
public:
    virtual UWorld* GetWorld() const override;
    void Bind(APlayerController* Controller);
    void Unbind();
    void Update(const FSceneViewProjectionData& Projection,AGuLiCommanderPresentationActor* Presentation,
        USceneCaptureComponent2D& Capture,TArray<FGuLiSceneUIHealthBar>& Bars);
    FString GetStatsJson() const;
    static float DistanceOpacity(double Distance);

private:
    struct FEntry
    {
        GuLiGroundSceneUI::FVitalHistory Vitals;
        uint32 Ordinal=0;
        uint64 Element=0;
        FVector Location=FVector::ZeroVector;
    };
    void ObjectChanged(const FGuLiHybridObjectKey& Key);
    void Read(const FGuLiHybridObjectState& State,bool bSeed);
    void Seed();
    TWeakObjectPtr<APlayerController> Controller;
    TWeakObjectPtr<UGuLiHybridWorldSubsystem> Hybrid;
    FDelegateHandle ObjectHandle;
    TMap<FGuLiHybridObjectKey,FEntry> Entries;
    TMap<FGuLiHybridObjectKey,FGuLiHybridObjectKey> LatestLives;
    EGuLiTeam ViewTeam=EGuLiTeam::Unassigned;
    uint64 NextElement=1,Notifications=0,Changes=0;
    int32 ActiveEvents=0,VisibleBars=0,FarEvents=0;
    UPROPERTY(Transient) TObjectPtr<UGuLiGroundOutlinePool> OutlinePool;
};
