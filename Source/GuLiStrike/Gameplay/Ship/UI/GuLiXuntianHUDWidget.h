#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Battle/Combat/GuLiCombatDamageLedger.h"
#include "UI/GuLiSegmentedHealthBar.h"
#include "GuLiXuntianHUDWidget.generated.h"

class UFont;
class UTexture2D;

#if WITH_EDITOR
struct FGuLiXuntianContactPaint
{
    uint32 SoldierId;
    float Health, ShieldFraction;
    bool bHasShield, bVitalChange;
    FVector2D Origin;
    FVector2D Size;
    double Depth, HorizontalCm;
};

struct FGuLiXuntianHUDPaint
{
    uint64 Frame = 0;
    int32 WarningStrokes = 0;
    bool bGround = false;
    FVector2D DesignSize = FVector2D::ZeroVector, HullBarOrigin = FVector2D::ZeroVector;
    TArray<FGuLiXuntianContactPaint> Soldiers;
};
#endif

UCLASS()
class GULISTRIKE_API UGuLiXuntianHUDWidget final : public UUserWidget
{
    GENERATED_BODY()
public:
    explicit UGuLiXuntianHUDWidget(const FObjectInitializer& Initializer);
#if WITH_EDITOR
    const FGuLiXuntianHUDPaint& RequestPaintInspection() { bInspectNextPaint=true; return LastInspectedPaint; }
#endif
protected:
    virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& Culling,
        FSlateWindowElementList& Draw, int32 Layer, const FWidgetStyle& Style, bool bEnabled) const override;
private:
    UPROPERTY() TObjectPtr<UTexture2D> Instruments;
    UPROPERTY() TObjectPtr<UFont> PanelFont;
    UPROPERTY() TObjectPtr<UFont> NameFont;
    UPROPERTY() FSlateBrush InstrumentBrush;
    mutable FGuLiTargetHandle LastLabelTarget;
    mutable GuLiHealthGrid::FMeshBatch VitalsMesh;
#if WITH_EDITOR
    mutable bool bInspectNextPaint = false;
    mutable FGuLiXuntianHUDPaint LastInspectedPaint;
#endif
};
