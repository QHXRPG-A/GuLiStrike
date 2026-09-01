// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Styling/SlateBrush.h"
#include "GuLiCommanderCursorWidget.generated.h"

class UTexture2D;

/** Native viewport software cursor. Slate owns its position; no follower tick. */
UCLASS()
class GULISTRIKE_API UGuLiCommanderCursorWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UGuLiCommanderCursorWidget(const FObjectInitializer& ObjectInitializer);

	/** Position of the arrow tip in the source PNG, normalized to [0,1]. */
	void SetHotSpotFromNormalized(FVector2D InHotSpot);

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
		int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

private:
	UPROPERTY(EditDefaultsOnly, Category = "Commander|Cursor")
	TSoftObjectPtr<UTexture2D> CursorTexture;

	UPROPERTY(Transient)
	FSlateBrush CursorBrush;

	FVector2D HotSpotNormalized = FVector2D(0.064, 0.012);
};
