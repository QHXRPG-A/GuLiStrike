#include "Gameplay/Ship/GuLiShipTargetingRangeComponent.h"

#include "PrimitiveSceneProxy.h"
#include "SceneManagement.h"

namespace
{
	class FGuLiShipTargetingRangeSceneProxy final : public FPrimitiveSceneProxy
	{
	public:
		FGuLiShipTargetingRangeSceneProxy(const UGuLiShipTargetingRangeComponent& Component)
			: FPrimitiveSceneProxy(&Component)
			, Radius(Component.GetTargetingRadius())
			, Color(Component.GetRangeColor())
			, Thickness(FMath::Max(0.0f, Component.GetLineThickness()))
			, Segments(FMath::Clamp(Component.GetSegmentCount(), 16, 256))
		{
		}

		virtual SIZE_T GetTypeHash() const override
		{
			static size_t UniquePointer;
			return reinterpret_cast<SIZE_T>(&UniquePointer);
		}

		virtual void GetDynamicMeshElements(const TArray<const FSceneView*>& Views,
			const FSceneViewFamily&, uint32 VisibilityMap, FMeshElementCollector& Collector) const override
		{
			if (!FMath::IsFinite(Radius) || Radius <= 0.0f) return;
			const FVector Origin = GetLocalToWorld().GetOrigin();
			const FVector Axes[][2] = {
				{FVector::ForwardVector, FVector::RightVector},
				{FVector::ForwardVector, FVector::UpVector},
				{FVector::RightVector, FVector::UpVector}
			};
			for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex)
			{
				if ((VisibilityMap & (1u << ViewIndex)) == 0u) continue;
				FPrimitiveDrawInterface* PDI = Collector.GetPDI(ViewIndex);
				for (const FVector (&AxisPair)[2] : Axes)
				{
					FVector Previous = Origin + AxisPair[0] * Radius;
					for (int32 SegmentIndex = 1; SegmentIndex <= Segments; ++SegmentIndex)
					{
						const double Angle = UE_TWO_PI * static_cast<double>(SegmentIndex) / Segments;
						const FVector Current = Origin + Radius
							* (AxisPair[0] * FMath::Cos(Angle) + AxisPair[1] * FMath::Sin(Angle));
						PDI->DrawLine(Previous, Current, Color, SDPG_World, Thickness, 0.0f, true);
						Previous = Current;
					}
				}
			}
		}

		virtual FPrimitiveViewRelevance GetViewRelevance(const FSceneView* View) const override
		{
			FPrimitiveViewRelevance Result;
			Result.bDrawRelevance = IsShown(View);
			Result.bDynamicRelevance = true;
			Result.bShadowRelevance = false;
			Result.bEditorPrimitiveRelevance = UseEditorCompositing(View);
			return Result;
		}

		virtual uint32 GetMemoryFootprint() const override
		{
			return sizeof(*this) + GetAllocatedSize();
		}

		uint32 GetAllocatedSize() const { return FPrimitiveSceneProxy::GetAllocatedSize(); }

	private:
		const float Radius;
		const FLinearColor Color;
		const float Thickness;
		const int32 Segments;
	};
}

UGuLiShipTargetingRangeComponent::UGuLiShipTargetingRangeComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SetGenerateOverlapEvents(false);
	SetIsReplicatedByDefault(false);
	SetVisibility(false);
	CastShadow = false;
	bUseAsOccluder = false;
}

void UGuLiShipTargetingRangeComponent::SetTargetingRadius(float InRadiusCentimeters)
{
	const float SanitizedRadius = FMath::IsFinite(InRadiusCentimeters)
		? FMath::Max(0.0f, InRadiusCentimeters) : 0.0f;
	if (FMath::IsNearlyEqual(TargetingRadiusCentimeters, SanitizedRadius)) return;
	TargetingRadiusCentimeters = SanitizedRadius;
	UpdateBounds();
	MarkRenderStateDirty();
}

FPrimitiveSceneProxy* UGuLiShipTargetingRangeComponent::CreateSceneProxy()
{
	return TargetingRadiusCentimeters > 0.0f ? new FGuLiShipTargetingRangeSceneProxy(*this) : nullptr;
}

FBoxSphereBounds UGuLiShipTargetingRangeComponent::CalcBounds(const FTransform& LocalToWorld) const
{
	const float Radius = FMath::Max(0.0f, TargetingRadiusCentimeters);
	return FBoxSphereBounds(FVector::ZeroVector, FVector(Radius), Radius).TransformBy(LocalToWorld);
}
