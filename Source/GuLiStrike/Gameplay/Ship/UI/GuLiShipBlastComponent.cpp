#include "Gameplay/Ship/UI/GuLiShipBlastComponent.h"
#include "Gameplay/Ship/GuLiStrikeShip.h"
#include "Gameplay/Ship/UI/GuLiShipWorldHUDComponent.h"
#include "Engine/GameInstance.h"
#include "Containers/StaticArray.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "MeshElementCollector.h"
#include "PrimitiveSceneProxy.h"
#include "PrimitiveViewRelevance.h"
#include "SceneManagement.h"
#include "SceneView.h"

namespace
{
	constexpr float BlastLifetime = .48f;
	class FShipBlastProxy final : public FPrimitiveSceneProxy
	{
	public:
		FShipBlastProxy(const UPrimitiveComponent* Component, const TArray<FGuLiShipBlast>& InBlasts)
			: FPrimitiveSceneProxy(Component), Blasts(InBlasts) { bWillEverBeLit = false; }
		virtual SIZE_T GetTypeHash() const override { static uint8 Type; return reinterpret_cast<SIZE_T>(&Type); }
		virtual uint32 GetMemoryFootprint() const override { return sizeof(*this) + Blasts.GetAllocatedSize(); }
		virtual FPrimitiveViewRelevance GetViewRelevance(const FSceneView* View) const override
		{
			FPrimitiveViewRelevance Relevance;
			Relevance.bDrawRelevance = IsShown(View);
			Relevance.bDynamicRelevance = true;
			Relevance.bNormalTranslucency = true;
			return Relevance;
		}
		virtual void GetDynamicMeshElements(const TArray<const FSceneView*>& Views, const FSceneViewFamily& Family,
			uint32 Visibility, FMeshElementCollector& Collector) const override
		{
			static const FLinearColor Hot(FColor(254,228,217)), Fire(FColor(238,157,88));
			static const auto Spokes = [] {
				TStaticArray<FVector2D, 8> Directions;
				for (int32 I=0; I<Directions.Num(); ++I)
					Directions[I]=FVector2D(FMath::Cos(I*UE_DOUBLE_PI/4), FMath::Sin(I*UE_DOUBLE_PI/4));
				return Directions;
			}();
			for (int32 I=0; I<Views.Num(); ++I)
			{
				if (!(Visibility & (1u<<I))) continue;
				const FSceneView& View = *Views[I];
				const double Projection = View.UnscaledViewRect.Height() * View.ViewMatrices.GetProjectionMatrix().M[1][1];
				if (Projection <= 0) continue;
				const FVector Side = View.GetViewRight(), Up = View.GetViewUp();
				auto* Lines = Collector.GetPDI(I);
				for (const auto& Blast : Blasts)
				{
					const float Age = FMath::Clamp(float((Family.Time.GetWorldTimeSeconds()-Blast.StartTime)/BlastLifetime),0.f,1.f);
					const double Depth = (Blast.Center-View.ViewLocation).Dot(View.GetViewDirection());
					if (Age >= 1 || Depth <= 0) continue;
					const double CmPerPixel = 2 * Depth / Projection;
					// The projected minimum keeps the impact spark readable at altitude.
					const double Radius = FMath::Clamp(Blast.Radius*.45, 8.*CmPerPixel, 34.*CmPerPixel);
					if (!View.ViewFrustum.IntersectSphere(Blast.Center, Radius)) continue;
					const FVector Core = Blast.Center + FVector::UpVector * (Radius * .23);
					const double Reach = Radius * (.34 + .66 * Age);
					for (int32 J=0; J<Spokes.Num(); ++J)
					{
						const FVector Arm = (Side*Spokes[J].X + Up*Spokes[J].Y) * Reach * (J%2 ? .72 : 1.);
						Lines->DrawTranslucentLine(Core+Arm*(.13+.56*Age), Core+Arm,
							Fire.CopyWithNewOpacity(1.f-Age), SDPG_World, 2.f-Age, 0, true);
					}
					const FVector Flare = Side * Radius * (.23*(1-Age));
					Lines->DrawTranslucentLine(Core-Flare, Core+Flare,Hot.CopyWithNewOpacity(1.f-Age), SDPG_World, 3.f, 0, true);
				}
			}
		}
	private:
		TArray<FGuLiShipBlast> Blasts;
	};
}

UGuLiShipBlastComponent::UGuLiShipBlastComponent()
{
	SetOnlyOwnerSee(true);
	SetCastShadow(false);
	SetGenerateOverlapEvents(false);
	SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SetCanEverAffectNavigation(false);
	bVisibleInRayTracing = bVisibleInReflectionCaptures = false;
	PrimaryComponentTick.bCanEverTick = false;
}

void UGuLiShipBlastComponent::NotifyPilots(UWorld& World, const FVector& Location, float Radius)
{
	const auto* Game = World.GetGameInstance();
	if (!Game || Location.ContainsNaN() || !FMath::IsFinite(Radius)) return;
	for (const ULocalPlayer* Player : Game->GetLocalPlayers())
	{
		const auto* PC = Player ? Player->GetPlayerController(&World) : nullptr;
		const auto* Ship = PC ? Cast<AGuLiStrikeShip>(PC->GetViewTarget()) : nullptr;
		if (Ship && Ship->GetController()==PC && !Ship->IsCarrierDormant())
			if (auto* HUD = Ship->GetShipWorldHUDComponent()) HUD->AddBlast(Location, Radius);
	}
}

void UGuLiShipBlastComponent::AddBlast(const FVector& Location, float Radius)
{
	Blasts.Add({Location, 0., FMath::Max(1.f,Radius)});
	++PendingBlasts;
}

void UGuLiShipBlastComponent::Publish(double Now)
{
	if (Blasts.IsEmpty()) return;
	// Start the visual lifetime at submission so a late frame cannot expire an unseen impact.
	for (int32 I=Blasts.Num()-PendingBlasts; I<Blasts.Num(); ++I) Blasts[I].StartTime = Now;
	const int32 Removed = Blasts.RemoveAllSwap([Now](const auto& B){ return Now-B.StartTime >= BlastLifetime; }, EAllowShrinking::No);
	if (!PendingBlasts && !Removed) return;
	PublishedBlasts += PendingBlasts;
	PendingBlasts = 0;
	UpdateBounds();
	MarkRenderTransformDirty();
	MarkRenderStateDirty();
}

FBoxSphereBounds UGuLiShipBlastComponent::CalcBounds(const FTransform& LocalToWorld) const
{
	FBox Box(ForceInit);
	for (const auto& B : Blasts)
	{
		// Include the screen-space minimum even at the far edge of the ship's combat range.
		const FVector Extent(FMath::Max(12000.f, 1.3f*B.Radius));
		Box += FBox(B.Center-Extent,B.Center+Extent);
	}
	return Box.IsValid ? FBoxSphereBounds(Box) : FBoxSphereBounds(LocalToWorld.GetLocation(),FVector::ZeroVector,0.);
}

FPrimitiveSceneProxy* UGuLiShipBlastComponent::CreateSceneProxy()
{
	return Blasts.IsEmpty() ? nullptr : new FShipBlastProxy(this, Blasts);
}
