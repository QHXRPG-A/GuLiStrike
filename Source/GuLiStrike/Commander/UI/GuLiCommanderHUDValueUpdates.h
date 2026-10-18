#pragma once
#include "Components/TextBlock.h"
#include "Components/Button.h"
#include "Components/Image.h"
#include "Components/ProgressBar.h"
#include "Components/ScaleBox.h"
#include "Components/InvalidationBox.h"
#include "Widgets/SWidget.h"
#include "Widgets/SInvalidationPanel.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "Gameplay/Performance/GuLiPerformanceSubsystem.h"

inline void GuLiHUDRecordWrite(UWidget* Widget)
{
    // A semantic write must invalidate the owning local render cache as well as
    // the leaf widget. UE 5.7's UInvalidationBox::InvalidateCache is a no-op.
    // Repeated writes only dirty this root; Slate rebuilds it once on the next paint.
    for (UWidget* Parent=Widget?Widget->GetParent():nullptr;Parent;Parent=Parent->GetParent())
        if (auto* Box=Cast<UInvalidationBox>(Parent))
        {
            if (const auto Slate=Box->GetCachedWidget())
                StaticCastSharedPtr<SInvalidationPanel>(Slate)->InvalidateRootLayout(Slate.Get());
            break;
        }
    auto* W=Widget?Widget->GetWorld():nullptr;
    if (auto* Perf=W?W->GetSubsystem<UGuLiPerformanceSubsystem>():nullptr;Perf&&Perf->IsCapturing()) Perf->Record(TEXT("CommanderHUDControlWrites"),1);
}

/** FText construction alone must not invalidate otherwise unchanged Slate layout. */
inline void GuLiHUDSetText(UTextBlock* Widget,const FText& Value)
{if (Widget && !Widget->GetText().EqualTo(Value)) {Widget->SetText(Value);GuLiHUDRecordWrite(Widget);}}
inline void GuLiHUDSetEnabled(UWidget* Widget,bool Value)
{if (Widget && Widget->GetIsEnabled()!=Value) {Widget->SetIsEnabled(Value);GuLiHUDRecordWrite(Widget);}}
inline void GuLiHUDSetVisibility(UWidget* Widget,ESlateVisibility Value)
{if (Widget && Widget->GetVisibility()!=Value) {Widget->SetVisibility(Value);GuLiHUDRecordWrite(Widget);}}
inline void GuLiHUDSetOpacity(UWidget* Widget,float Value)
{if (Widget && Widget->GetRenderOpacity()!=Value) {Widget->SetRenderOpacity(Value);GuLiHUDRecordWrite(Widget);}}
inline void GuLiHUDSetBackground(UButton* Widget,FLinearColor Value)
{if (Widget && Widget->GetBackgroundColor()!=Value) {Widget->SetBackgroundColor(Value);GuLiHUDRecordWrite(Widget);}}
inline void GuLiHUDSetColor(UTextBlock* Widget,FLinearColor Value)
{if (Widget && Widget->GetColorAndOpacity()!=FSlateColor(Value)) {Widget->SetColorAndOpacity(Value);GuLiHUDRecordWrite(Widget);}}
inline void GuLiHUDSetPercent(UProgressBar* Widget,float Value)
{if (Widget && Widget->GetPercent()!=Value) {Widget->SetPercent(Value);GuLiHUDRecordWrite(Widget);}}
inline void GuLiHUDSetFillColor(UProgressBar* Widget,FLinearColor Value)
{if (Widget && Widget->GetFillColorAndOpacity()!=Value) {Widget->SetFillColorAndOpacity(Value);GuLiHUDRecordWrite(Widget);}}
inline void GuLiHUDSetTexture(UImage* Widget,UTexture2D* Value,bool MatchSize)
{
    if (!Widget || Widget->GetBrush().GetResourceObject()==Value) return;
    const FVector2D PreviousSize=Widget->GetBrush().ImageSize;
    Widget->SetBrushFromTexture(Value,MatchSize);
    // A cached SScaleBox also owns a normalized desired size. Image layout
    // invalidation alone leaves its old content scale after a texture swap.
    if (MatchSize && PreviousSize!=FVector2D(Widget->GetBrush().ImageSize))
        if (const auto* ScaleBox=Cast<UScaleBox>(Widget->GetParent()))
            if (const auto Slate=ScaleBox->GetCachedWidget()) Slate->Invalidate(EInvalidateWidgetReason::Prepass);
    GuLiHUDRecordWrite(Widget);
}
