#pragma once

#include "Components/ProgressBar.h"
#include "Rendering/RenderingCommon.h"
#include "GuLiSegmentedHealthBar.generated.h"

namespace GuLiHealthGrid
{
	constexpr float PointsPerCell = 100.f;
	struct FBarValue
	{
		float Current = 0;
		float Maximum = 0;
	};
	int32 CellCount(float Maximum);
	struct FCanvas
	{
		FCanvas(const FGeometry& Geometry, FVector2D Scale, FBox2D Clip = FBox2D(ForceInit));
		FSlateRenderTransform Transform;
		FVector2D Scale, ScreenPixel;
		FBox2D Clip;
	};

	/** Each material quad draws a whole bar, including both rows and the common outline. */
	struct FMeshBatch
	{
		struct FMesh { TArray<FSlateVertex> Vertices; TArray<SlateIndex> Indices; };
		TArray<FMesh> Meshes;
		void Reset();
		void Add(const FCanvas& Canvas, FVector2D Origin, FVector2D Size, FBarValue Health, FBarValue Shield,
			float Opacity, double Pixel, float ShieldOpacityScale = 1.f);
		void Submit(FSlateWindowElementList& Elements, int32 Layer) const;
	private:
		int32 LastMesh = 0;
	};

	/** Full footprint of the marker in the current view. */
	struct FMarkerSize
	{
		static constexpr double ReferenceWidth = 86.;
		static double StylePixel(double Width) { return Width / ReferenceWidth; }
		static FVector2D ReferenceSize(bool bHasShield) { return FVector2D(ReferenceWidth, bHasShield ? 18. : 14.); }
		FVector2D Size;
		double Pixel;
		static FMarkerSize ForView(bool bHasShield, bool bOverview = false, double IconPixels = 0., double DPI = 1.);
	};
}

/** The same health geometry is used in world markers, pilot instruments and UMG panels. */
UCLASS()
class GULISTRIKE_API UGuLiSegmentedHealthBar : public UProgressBar
{
	GENERATED_BODY()
public:
	UGuLiSegmentedHealthBar(const FObjectInitializer& ObjectInitializer);
	static const FSlateBrush& GetBarBrush();
	bool SetHealth(float Current, float Maximum, float CurrentShield = 0.f, float MaximumShield = 0.f);
	float GetMaximumHealth() const { return MaximumHealth; }
	GuLiHealthGrid::FBarValue GetShieldValue() const { return Shield; }
protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
private:
	UPROPERTY()
	FSlateBrush BarBrush;
	float MaximumHealth = 0;
	GuLiHealthGrid::FBarValue Shield;
};
