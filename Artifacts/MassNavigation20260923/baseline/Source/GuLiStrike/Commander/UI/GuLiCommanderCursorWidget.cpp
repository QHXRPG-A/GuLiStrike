// Copyright Epic Games, Inc. All Rights Reserved.

#include "Commander/UI/GuLiCommanderCursorWidget.h"

#include "Engine/Texture2D.h"
#include "Rendering/DrawElements.h"
#include "Widgets/Layout/SBox.h"

UGuLiCommanderCursorWidget::UGuLiCommanderCursorWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, CursorTexture(FSoftObjectPath(TEXT("/Game/Commander/UI/Textures/Icons/T_UI_Cmd_Cursor.T_UI_Cmd_Cursor")))
{
	SetVisibility(ESlateVisibility::HitTestInvisible);
}

TSharedRef<SWidget> UGuLiCommanderCursorWidget::RebuildWidget()
{
	return SNew(SBox).WidthOverride(32.0f).HeightOverride(32.0f);
}

void UGuLiCommanderCursorWidget::NativeConstruct()
{
	Super::NativeConstruct();
	CursorBrush.SetResourceObject(CursorTexture.LoadSynchronous());
	CursorBrush.DrawAs = ESlateBrushDrawType::Image;
	CursorBrush.ImageSize = FVector2D(32.0, 32.0);
}

void UGuLiCommanderCursorWidget::SetHotSpotFromNormalized(FVector2D InHotSpot)
{
	HotSpotNormalized.X = FMath::Clamp(InHotSpot.X, 0.0, 1.0);
	HotSpotNormalized.Y = FMath::Clamp(InHotSpot.Y, 0.0, 1.0);
	InvalidateLayoutAndVolatility();
}

int32 UGuLiCommanderCursorWidget::NativePaint(
	const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId,
	const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	LayerId = Super::NativePaint(Args, AllottedGeometry, MyCullingRect,
		OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
	if (bTeleportMode)
	{
		TArray<FVector2D> Ring;
		for (int32 I = 0; I <= 32; ++I) { const float A=I*UE_TWO_PI/32; Ring.Add(FVector2D(16+FMath::Cos(A)*11,16+FMath::Sin(A)*11)); }
		FSlateDrawElement::MakeLines(OutDrawElements,++LayerId,AllottedGeometry.ToPaintGeometry(),Ring,ESlateDrawEffect::None,FLinearColor(0,.9f,1),true,2);
		for (const auto& Points : { TArray<FVector2D>{{16,0},{16,9}},TArray<FVector2D>{{16,23},{16,32}},TArray<FVector2D>{{0,16},{9,16}},TArray<FVector2D>{{23,16},{32,16}} })
		{ FSlateDrawElement::MakeLines(OutDrawElements,LayerId,AllottedGeometry.ToPaintGeometry(),Points,ESlateDrawEffect::None,FLinearColor::White,true,2); }
		return LayerId;
	}
	if (CursorBrush.GetResourceObject())
	{
		// UE5.7 FSlateUser::DrawCursor aligns the widget CENTER with the cursor.
		// Offset the image inside that geometry so the PNG tip is the exact hotspot.
		const FVector2D Size = AllottedGeometry.GetLocalSize();
		const FVector2D Origin = Size * (FVector2D(0.5, 0.5) - HotSpotNormalized);
		FSlateDrawElement::MakeBox(OutDrawElements, ++LayerId,
			AllottedGeometry.ToPaintGeometry(Size, FSlateLayoutTransform(Origin)),
			&CursorBrush, ESlateDrawEffect::None, FLinearColor::White);
	}
	return LayerId;
}
