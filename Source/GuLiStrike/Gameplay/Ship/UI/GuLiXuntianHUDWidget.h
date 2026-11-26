#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Battle/Combat/GuLiCombatDamageLedger.h"
#include "GuLiXuntianHUDWidget.generated.h"

class UFont;
class UTexture2D;

UCLASS()
class GULISTRIKE_API UGuLiXuntianHUDWidget final : public UUserWidget
{
    GENERATED_BODY()
public:
    explicit UGuLiXuntianHUDWidget(const FObjectInitializer& Initializer);
protected:
    virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& Culling,
        FSlateWindowElementList& Draw, int32 Layer, const FWidgetStyle& Style, bool bEnabled) const override;
private:
    UPROPERTY() TObjectPtr<UTexture2D> Instruments;
    UPROPERTY() TObjectPtr<UFont> PanelFont;
    UPROPERTY() TObjectPtr<UFont> NameFont;
    UPROPERTY() FSlateBrush InstrumentBrush;
    mutable FGuLiTargetHandle LastLabelTarget;
};
