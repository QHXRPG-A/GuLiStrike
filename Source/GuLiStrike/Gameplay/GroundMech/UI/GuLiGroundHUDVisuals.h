#pragma once

#include "Components/Border.h"
#include "Components/Widget.h"
#include "Styling/SlateBrush.h"
#include "Fonts/SlateFontInfo.h"
#include "GuLiGroundHUDVisuals.generated.h"

class UDataTable;
class UTexture2D;
class SGuLiGroundCockpit;
class USizeBox;
enum class EGuLiGroundInstrumentShape:uint8 { Standard,Health,Jet,LocalMap,Rogue };

namespace GuLiGroundUI
{
    FLinearColor Color(const UDataTable* Table,FName Name,FLinearColor Fallback=FLinearColor::White);
    float Number(const UDataTable* Table,FName Name,float Fallback);
    FSlateBrush Brush(const UDataTable* Table,FName Name);
    /** Project-owned Chinese weights, shared by all Ground screen and card titles. */
    FSlateFontInfo Font(const UDataTable* Table,float Size,bool Bold=false);
    /** Bright edge and broad, translucent glow use the same palette base. */
    FLinearColor Light(FLinearColor Base,float Gain=3.f);
    /** Resolution-independent reference symbols. The returned surface caches its paint. */
    TSharedRef<SWidget> Glyph(FName Name,FLinearColor Line,FLinearColor Accent,FLinearColor Text);
    /** Preserve the 1080p instrument scale while allowing edge anchors to fill any aspect. */
    void FitDesignToViewport(USizeBox* Design,FVector2D ViewportSize);
}

/** Cut-corner instrument enclosure. Standard UMG content/input stays in the border slot. */
UCLASS()
class GULISTRIKE_API UGuLiGroundHUDPanel final : public UBorder
{
    GENERATED_BODY()
public:
    void SetTheme(UDataTable* Table,float InHeaderHeight=0,bool InCompact=false);
    void SetHighlighted(bool Highlighted);
    bool IsHighlighted() const {return bHighlighted;}
    FLinearColor EdgeColor,AccentColor,DetailColor;
    FSlateBrush GridBrush;
    float HeaderHeight=0;
    bool bCompact=false;
    bool bKeycap=false;
    EGuLiGroundInstrumentShape Shape=EGuLiGroundInstrumentShape::Standard;
    /** Normalized transparent artwork aperture, used only by the world-space Ground card frame. */
    bool bArtworkFrame=false;
    FVector2D ArtworkMin=FVector2D(.105,.145),ArtworkMax=FVector2D(.895,.875);
protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
private:
    bool bHighlighted=false;
    UPROPERTY(Transient) TObjectPtr<UObject> GridResource;
};

/** Shared by combat, map and Ground card overlay. No media player, capture or gameplay polling. */
UCLASS()
class GULISTRIKE_API UGuLiGroundHUDChrome final : public UWidget
{
    GENERATED_BODY()
public:
    void SetTheme(UDataTable* Table);
    void SetMode(uint8 InMode);
    void SetHeading(float Degrees);
    virtual void ReleaseSlateResources(bool ReleaseChildren) override;
    FLinearColor PanelColor,LineColor,SecondaryColor,AccentColor,TextColor;
    FSlateBrush CockpitBrush,GridBrush;
    uint8 Mode=0;
    float Heading=0;
protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
private:
    TSharedPtr<SGuLiGroundCockpit> Frame,Compass;
    UPROPERTY(Transient) TArray<TObjectPtr<UObject>> Resources;
};
