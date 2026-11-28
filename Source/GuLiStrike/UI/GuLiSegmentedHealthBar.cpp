#include "UI/GuLiSegmentedHealthBar.h"
#include "Framework/Application/SlateApplication.h"
#include "Materials/MaterialInterface.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "UObject/ConstructorHelpers.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SLeafWidget.h"

namespace GuLiHealthGrid
{
	int32 CellCount(float Maximum)
	{
		return Maximum > 0 && FMath::IsFinite(Maximum)
			? FMath::CeilToInt(FMath::Min(double(Maximum) / PointsPerCell, double(MAX_int32))) : 0;
	}

	FMarkerSize FMarkerSize::ForView(bool bHasShield, bool bOverview, double IconPixels, double DPI)
	{
		if (!bOverview) return {ReferenceSize(bHasShield), 1.};
		return {FVector2D(FMath::Max(IconPixels, 22. * DPI) + 2. * DPI, (bHasShield ? 8. : 5.) * DPI), DPI};
	}

	FCanvas::FCanvas(const FGeometry& Geometry, FVector2D InScale, FBox2D InClip)
		: Transform(Geometry.GetAccumulatedRenderTransform()), Scale(InScale), Clip(InClip)
	{
		const float X = TransformVector(Transform, FVector2f(Scale.X, 0)).Size();
		const float Y = TransformVector(Transform, FVector2f(0, Scale.Y)).Size();
		ScreenPixel = FVector2D(1. / FMath::Max(X, UE_SMALL_NUMBER), 1. / FMath::Max(Y, UE_SMALL_NUMBER));
	}

	void FMeshBatch::Reset()
	{
		for (auto& Mesh : Meshes) { Mesh.Vertices.Reset(); Mesh.Indices.Reset(); }
		LastMesh = 0;
	}

	void FMeshBatch::Add(const FCanvas& Canvas, FVector2D Origin, FVector2D Size, FBarValue Health, FBarValue Shield,
		float Opacity, double Pixel, float ShieldOpacityScale)
	{
		if (Size.X <= 0 || Size.Y <= 0 || Pixel <= 0 || Opacity <= 0 || !CellCount(Health.Maximum)) return;
		FVector2D Min = Origin - Canvas.ScreenPixel * .5, Max = Origin + Size + Canvas.ScreenPixel * .5;
		if (Canvas.Clip.bIsValid)
		{
			Min = Min.ComponentMax(Canvas.Clip.Min); Max = Max.ComponentMin(Canvas.Clip.Max);
			if (Min.X >= Max.X || Min.Y >= Max.Y) return;
		}
		if (Meshes.IsValidIndex(LastMesh) && Meshes[LastMesh].Vertices.Num() + 4 > MAX_uint16) ++LastMesh;
		if (!Meshes.IsValidIndex(LastMesh)) Meshes.AddDefaulted();
		auto& Mesh = Meshes[LastMesh];
		const int32 Start = Mesh.Vertices.Num();
		auto Fraction = [](FBarValue Value)
		{
			return Value.Maximum > 0.f && FMath::IsFinite(Value.Current)
				? float(FMath::Clamp(double(Value.Current) / Value.Maximum, 0., 1.)) : 0.f;
		};
		const FVector2f Fractions(Fraction(Health), Fraction(Shield));
		const FVector2f CellSpans(Health.Maximum / PointsPerCell, CellCount(Shield.Maximum) ? Shield.Maximum / PointsPerCell : 0.f);
		const FColor Tint = FLinearColor(ShieldOpacityScale, 1.f, 1.f, Opacity).ToFColorSRGB();
		// Slate UV3 carries this pair of 16-bit values. Fixed point preserves fractional layout sizes at different DPI.
		const FIntPoint PackedSize(FMath::RoundToInt(FMath::Clamp(16. * Size.X / Pixel, 1., double(MAX_uint16))),
			FMath::RoundToInt(FMath::Clamp(16. * Size.Y / Pixel, 1., double(MAX_uint16))));
		for (const FVector2D P : {Min, FVector2D(Max.X, Min.Y), Max, FVector2D(Min.X, Max.Y)})
		{
			const FVector2D UV = (P - Origin) / Size;
			auto& V = Mesh.Vertices.Add_GetRef(FSlateVertex::Make<ESlateVertexRounding::Disabled>(Canvas.Transform,
				FVector2f(P * Canvas.Scale), FVector4f(UV.X, UV.Y, Fractions.X, Fractions.Y), CellSpans, Tint));
			V.PixelSize[0] = uint16(PackedSize.X); V.PixelSize[1] = uint16(PackedSize.Y);
		}
		for (int32 I : {0, 1, 2, 0, 2, 3}) Mesh.Indices.Add(SlateIndex(Start + I));
	}

	void FMeshBatch::Submit(FSlateWindowElementList& Elements, int32 Layer) const
	{
		if (Meshes.IsEmpty() || Meshes[0].Indices.IsEmpty()) return;
		const auto Handle = FSlateApplication::Get().GetRenderer()->GetResourceHandle(UGuLiSegmentedHealthBar::GetBarBrush());
		for (int32 I = 0; I <= LastMesh; ++I)
			FSlateDrawElement::MakeCustomVerts(Elements, Layer, Handle, Meshes[I].Vertices, Meshes[I].Indices, nullptr, 0, 0);
	}
}

namespace
{
	class SGuLiHealthBar final : public SLeafWidget
	{
	public:
		SLATE_BEGIN_ARGS(SGuLiHealthBar) {} SLATE_END_ARGS()
		void Construct(const FArguments&, UGuLiSegmentedHealthBar* InOwner) { Owner = InOwner; }
		virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(46, 7); }
		virtual int32 OnPaint(const FPaintArgs&, const FGeometry& Geo, const FSlateRect&, FSlateWindowElementList& Out,
			int32 Layer, const FWidgetStyle& Style, bool) const override
		{
			if (const auto* Bar = Owner.Get())
			{
				const float Max = Bar->GetMaximumHealth();
				Mesh.Reset();
				Mesh.Add({Geo, FVector2D(1.)}, FVector2D::ZeroVector, Geo.GetLocalSize(), {Bar->GetPercent() * Max, Max},
					Bar->GetShieldValue(), Style.GetColorAndOpacityTint().A, 1.);
				Mesh.Submit(Out, Layer);
			}
			return Layer;
		}
	private:
		TWeakObjectPtr<UGuLiSegmentedHealthBar> Owner;
		mutable GuLiHealthGrid::FMeshBatch Mesh;
	};
}

UGuLiSegmentedHealthBar::UGuLiSegmentedHealthBar(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
	if (!IsRunningDedicatedServer())
	{
		static ConstructorHelpers::FObjectFinder<UMaterialInterface> Material(TEXT("/Game/GuLiStrike/UI/SceneOverlay/M_SegmentedVitals.M_SegmentedVitals"));
		BarBrush.SetResourceObject(Material.Object);
	}
}

const FSlateBrush& UGuLiSegmentedHealthBar::GetBarBrush() { return GetDefault<UGuLiSegmentedHealthBar>()->BarBrush; }

TSharedRef<SWidget> UGuLiSegmentedHealthBar::RebuildWidget() { return SNew(SGuLiHealthBar, this); }

bool UGuLiSegmentedHealthBar::SetHealth(float Current, float Maximum, float CurrentShield, float MaximumShield)
{
	const float SafeMax = Maximum > 0 && FMath::IsFinite(Maximum) ? Maximum : 0.f;
	const float Fraction = SafeMax > 0 && FMath::IsFinite(Current) ? FMath::Clamp(Current / SafeMax, 0.f, 1.f) : 0.f;
	const float SafeShieldMax = FMath::IsFinite(MaximumShield) ? FMath::Max(0.f, MaximumShield) : 0.f;
	const float SafeShield = FMath::IsFinite(CurrentShield) ? FMath::Clamp(CurrentShield, 0.f, SafeShieldMax) : 0.f;
	if (MaximumHealth == SafeMax && GetPercent() == Fraction && Shield.Current == SafeShield && Shield.Maximum == SafeShieldMax) return false;
	MaximumHealth = SafeMax;
	Shield = {SafeShield, SafeShieldMax};
	SetPercent(Fraction);
	if (auto Widget = GetCachedWidget()) Widget->Invalidate(EInvalidateWidgetReason::Paint);
	return true;
}
