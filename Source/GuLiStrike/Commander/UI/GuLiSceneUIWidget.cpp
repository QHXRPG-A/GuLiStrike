#include "Commander/UI/GuLiSceneUIWidget.h"
#include "Commander/UI/GuLiSceneUITypes.h"
#include "Commander/UI/GuLiCommanderHealthBarRenderer.h"
#include "Commander/Framework/GuLiCommanderPlayerController.h"
#include "Commander/Framework/GuLiCommanderNetSyncComponent.h"
#include "Commander/Presentation/GuLiCommanderPresentationActor.h"
#include "Commander/Presentation/GuLiCommanderRouteLineComponent.h"
#include "Commander/Presentation/GuLiCommanderCameraPawn.h"
#include "Commander/Presentation/GuLiCommanderOverviewSubsystem.h"
#include "Battle/Framework/GuLiBattlePlayerState.h"
#include "Gameplay/Presentation/GuLiLocalTeamColors.h"
#include "Gameplay/Units/GuLiExternalUnitControlComponent.h"
#include "Gameplay/Units/GuLiEngineeringTravelComponent.h"
#include "Gameplay/Stronghold/GuLiOutpostPresentationComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/Pawn.h"
#include "Gameplay/Building/GuLiBuildingPlacementComponent.h"
#include "Gameplay/Building/GuLiBuildingPlacementPreview.h"
#include "Gameplay/Ship/UI/GuLiShipWorldHUDComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/MeshComponent.h"
#include "Engine/SceneCapture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/Texture2D.h"
#include "Engine/LocalPlayer.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "SceneView.h"
#include "Styling/CoreStyle.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SLeafWidget.h"
#include "SlateMaterialBrush.h"
#include "GuLiStrike.h"

struct FGuLiSceneUIWidgetData
{
	enum class EShape : uint8 { Line, Rect, Disc, Image, Text, WorldLine };
	struct FShape
	{
		EShape Kind = EShape::Line;
		FVector2D A = FVector2D::ZeroVector, B = FVector2D::ZeroVector;
		FVector WorldA = FVector::ZeroVector, WorldB = FVector::ZeroVector;
		FLinearColor Color = FLinearColor::White;
		float Width = 1;
		TWeakObjectPtr<UTexture2D> Texture;
		FString Text;
	};
	struct FBatch { TArray<FSlateVertex> Vertices; TArray<SlateIndex> Indices; };
	TArray<FShape> HUDShapes;
	uint64 HUDFrame = MAX_uint64;
	TArray<FGuLiSceneUIRing> Rings;
	TArray<FGuLiSceneUIHealthBar> Bars;
	TArray<FGuLiSceneUILine> Routes;
	TArray<FGuLiSceneUIWorldWidget> WorldWidgets;
	TArray<TSharedPtr<FBatch>> Batches;
	TSharedPtr<FSlateMaterialBrush> OutlineBrush;
	TSharedPtr<FSlateMaterialBrush> PlacementBrush;
	TMap<TWeakObjectPtr<UTexture>, TSharedPtr<FSlateMaterialBrush>> SurfaceBrushes;
};

class SGuLiSceneUI final : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SGuLiSceneUI) {} SLATE_ARGUMENT(TWeakObjectPtr<UGuLiSceneUIWidget>, Owner) SLATE_END_ARGS()
	void Construct(const FArguments& Args) { Owner = Args._Owner; }
	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D::ZeroVector; }
	virtual int32 OnPaint(const FPaintArgs&, const FGeometry& Geometry, const FSlateRect&,
		FSlateWindowElementList& Elements, int32 Layer, const FWidgetStyle&, bool) const override
	{
		if (auto* Widget = Owner.Get()) Widget->PaintSceneUI(Geometry, Elements, Layer);
		return Layer + 2;
	}
private:
	TWeakObjectPtr<UGuLiSceneUIWidget> Owner;
};

namespace
{
FLinearColor Opaque(FLinearColor Color) { Color.A = 1; return Color; }

bool ClipLine(const FMatrix& Matrix, const FVector& Start, const FVector& End, FVector4& P, FVector4& Q)
{
	P=Matrix.TransformFVector4(FVector4(Start,1)); Q=Matrix.TransformFVector4(FVector4(End,1));
	for (int32 Plane=0; Plane<6; ++Plane)
	{
		auto Distance=[Plane](const FVector4& V) { switch (Plane) { case 0: return V.W+V.X; case 1: return V.W-V.X;
			case 2: return V.W+V.Y; case 3: return V.W-V.Y; case 4: return V.W-V.Z; default: return V.Z; } };
		const double D=Distance(P), E=Distance(Q);
		if (D<0 && E<0) return false;
		if ((D<0)!=(E<0)) { const auto Cut=FMath::Lerp(P,Q,D/(D-E)); if (D<0) P=Cut; else Q=Cut; }
	}
	return P.W>.00001 && Q.W>.00001;
}

/** Project once per view; clip in homogeneous space before division, including the near plane. */
struct FViewBatch
{
	const FGeometry& Geometry;
	FSlateWindowElementList& Elements;
	FGuLiSceneUIWidgetData& Data;
	FSceneViewProjectionData Projection;
	FMatrix Matrix;
	FConvexVolume Frustum;
	FVector2D Scale, Inset;
	FIntRect Rect;
	int32 BatchIndex = 0, Layer;
	FSlateResourceHandle White;

	FViewBatch(const FGeometry& InGeometry, FSlateWindowElementList& InElements,
		FGuLiSceneUIWidgetData& InData, const FSceneViewProjectionData& InProjection, int32 InLayer)
		: Geometry(InGeometry), Elements(InElements), Data(InData), Projection(InProjection), Layer(InLayer)
	{
		Matrix = Projection.ComputeViewProjectionMatrix();
		GetViewFrustumBounds(Frustum, Matrix, true);
		Rect = Projection.GetConstrainedViewRect();
		const auto ViewRect = Projection.GetViewRect();
		Scale = Geometry.GetLocalSize() / FVector2D(FMath::Max(1, ViewRect.Width()), FMath::Max(1, ViewRect.Height()));
		Inset = FVector2D(Rect.Min - ViewRect.Min);
		White = FSlateApplication::Get().GetRenderer()->GetResourceHandle(*FCoreStyle::Get().GetBrush("WhiteBrush"));
		for (auto& Batch : Data.Batches) { Batch->Vertices.Reset(); Batch->Indices.Reset(); }
	}

	FVector2D ToPixels(const FVector4& P) const
	{ return Inset + FVector2D((P.X / P.W * .5 + .5) * Rect.Width(), (.5 - P.Y / P.W * .5) * Rect.Height()); }
	bool Project(const FVector& World, FVector2D& Pixels) const
	{
		const FVector4 P = Matrix.TransformFVector4(FVector4(World, 1));
		if (P.W <= .00001 || P.Z > P.W || P.Z < 0) return false;
		Pixels = ToPixels(P);
		return !Pixels.ContainsNaN();
	}
	void Triangle(const FVector2D& A, const FVector2D& B, const FVector2D& C, const FLinearColor& Color)
	{
		if (Color.A <= 0) return;
		if (BatchIndex >= Data.Batches.Num()) Data.Batches.Add(MakeShared<FGuLiSceneUIWidgetData::FBatch>());
		if (Data.Batches[BatchIndex]->Vertices.Num() + 3 > 60000)
		{
			++BatchIndex;
			if (BatchIndex >= Data.Batches.Num()) Data.Batches.Add(MakeShared<FGuLiSceneUIWidgetData::FBatch>());
		}
		auto& Batch = *Data.Batches[BatchIndex];
		const int32 First = Batch.Vertices.Num();
		const FColor Tint = Opaque(Color).ToFColorSRGB();
		for (const auto& P : {A, B, C})
			Batch.Vertices.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(
				Geometry.GetAccumulatedRenderTransform(), FVector2f(P * Scale), FVector2f::ZeroVector, Tint));
		Batch.Indices.Add(First); Batch.Indices.Add(First + 1); Batch.Indices.Add(First + 2);
	}
	void WorldTriangle(const FVector& A, const FVector& B, const FVector& C, const FLinearColor& Color)
	{
		TArray<FVector4, TInlineAllocator<16>> Points, Output;
		for (const auto& P : {A, B, C}) Points.Add(Matrix.TransformFVector4(FVector4(P, 1)));
		for (int32 Plane = 0; Plane < 6 && !Points.IsEmpty(); ++Plane)
		{
			auto Distance = [Plane](const FVector4& P)
			{
				switch (Plane) { case 0: return P.W + P.X; case 1: return P.W - P.X;
				case 2: return P.W + P.Y; case 3: return P.W - P.Y; case 4: return P.W - P.Z; default: return P.Z; }
			};
			Output.Reset();
			FVector4 Previous = Points.Last(); double Prev = Distance(Previous);
			for (const auto& Current : Points)
			{
				const double Next = Distance(Current);
				if ((Prev >= 0) != (Next >= 0)) Output.Add(FMath::Lerp(Previous, Current, Prev / (Prev - Next)));
				if (Next >= 0) Output.Add(Current);
				Previous = Current; Prev = Next;
			}
			Points = Output;
		}
		for (int32 I = 1; I + 1 < Points.Num(); ++I)
			if (Points[0].W > .00001 && Points[I].W > .00001 && Points[I + 1].W > .00001)
				Triangle(ToPixels(Points[0]), ToPixels(Points[I]), ToPixels(Points[I + 1]), Color);
	}
	void Line(FVector2D A, FVector2D B, float Width, const FLinearColor& Color)
	{
		if (Color.A <= 0) return;
		const FVector2D D = (B - A).GetSafeNormal(), N(-D.Y * Width * .5, D.X * Width * .5);
		Triangle(A - N, A + N, B + N, Color); Triangle(A - N, B + N, B - N, Color);
	}
	void WorldLine(const FVector& A, const FVector& B, float Width, const FLinearColor& Color)
	{
		FVector4 P,Q;
		if (ClipLine(Matrix,A,B,P,Q)) Line(ToPixels(P),ToPixels(Q),Width,Color);
	}
	void Box(FVector2D Position, FVector2D Size, FLinearColor Color)
	{
		Triangle(Position, Position + FVector2D(Size.X, 0), Position + Size, Color);
		Triangle(Position, Position + Size, Position + FVector2D(0, Size.Y), Color);
	}
	void Ring(const FGuLiSceneUIRing& R, const FLinearColor& Color)
	{
		if (Color.A <= 0 || R.OuterRadiusCm <= 0 || !Frustum.IntersectSphere(R.Center, R.OuterRadiusCm + 50)) return;
		static const TArray<FVector> Directions = []
		{
			TArray<FVector> Result;
			for (int32 I=0; I<GuLiSceneUI::RingSegments; ++I)
			{ const double A = I * 2.0 * PI / GuLiSceneUI::RingSegments; Result.Add(FVector(FMath::Cos(A), FMath::Sin(A), 0)); }
			return Result;
		}();
		const float Inner = FMath::Max(0.f, R.OuterRadiusCm - GuLiSceneUI::RingWidthCm);
		for (int32 I=0; I<Directions.Num(); ++I)
		{
			const FVector A = Directions[I], B = Directions[(I + 1) % Directions.Num()];
			WorldTriangle(R.Center+A*Inner, R.Center+A*R.OuterRadiusCm, R.Center+B*R.OuterRadiusCm, Color);
			WorldTriangle(R.Center+A*Inner, R.Center+B*R.OuterRadiusCm, R.Center+B*Inner, Color);
		}
		if (R.bSelected) for (int32 I : {0, 16, 32, 48})
		{
			const FVector D = Directions[I], N(-D.Y*10, D.X*10, 0);
			const FVector A = R.Center + D*(R.OuterRadiusCm+10), B = A+D*40;
			WorldTriangle(A-N, A+N, B+N, Color); WorldTriangle(A-N, B+N, B-N, Color);
		}
	}
	void Submit()
	{
		for (const auto& Batch : Data.Batches) if (!Batch->Vertices.IsEmpty())
			FSlateDrawElement::MakeCustomVerts(Elements, Layer, White, Batch->Vertices, Batch->Indices, nullptr, 0, 0);
	}
	void WidgetSurface(const FGuLiSceneUIWorldWidget& W, const FSlateResourceHandle& Resource)
	{
		struct FClipVertex { FVector4 P; FVector2D UV; };
		TArray<FClipVertex, TInlineAllocator<16>> Points, Output;
		for (const FVector2D UV : {FVector2D(0,0), FVector2D(0,1), FVector2D(1,1), FVector2D(1,0)})
		{
			const FVector Local(0, (W.Pivot.X-UV.X)*W.Size.X, (W.Pivot.Y-UV.Y)*W.Size.Y);
			Points.Add({Matrix.TransformFVector4(FVector4(W.Transform.TransformPosition(Local),1)), UV});
		}
		for (int32 Plane=0; Plane<6 && !Points.IsEmpty(); ++Plane)
		{
			auto Distance = [Plane](const FVector4& P) { switch (Plane) { case 0: return P.W+P.X; case 1: return P.W-P.X;
				case 2: return P.W+P.Y; case 3: return P.W-P.Y; case 4: return P.W-P.Z; default: return P.Z; } };
			Output.Reset(); auto Previous=Points.Last(); double Prev=Distance(Previous.P);
			for (const auto& Current : Points)
			{
				const double Next=Distance(Current.P);
				if ((Prev>=0)!=(Next>=0)) { const double T=Prev/(Prev-Next);
					Output.Add({FMath::Lerp(Previous.P,Current.P,T),FMath::Lerp(Previous.UV,Current.UV,T)}); }
				if (Next>=0) Output.Add(Current);
				Previous=Current; Prev=Next;
			}
			Points=Output;
		}
		TArray<FSlateVertex> Vertices;
		TArray<SlateIndex> Indices;
		for (const auto& P : Points)
		{
			if (P.P.W<=.00001) return;
			Vertices.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(Geometry.GetAccumulatedRenderTransform(),
				FVector2f(ToPixels(P.P)*Scale),FVector2f(P.UV),FColor::White));
		}
		for (int32 I=1; I+1<Vertices.Num(); ++I) { Indices.Add(0); Indices.Add(I); Indices.Add(I+1); }
		if (!Indices.IsEmpty())
			FSlateDrawElement::MakeCustomVerts(Elements,Layer+1,Resource,Vertices,Indices,nullptr,0,0);
	}
};

void ConfigureMaskCapture(USceneCaptureComponent2D& Capture, UTextureRenderTarget2D* Target)
{
	Capture.TextureTarget=Target; Capture.CaptureSource=SCS_SceneColorHDR;
	Capture.PrimitiveRenderMode=ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
	Capture.bCaptureEveryFrame=false; Capture.bCaptureOnMovement=false;
	Capture.bAlwaysPersistRenderingState=true;
	Capture.bConsiderUnrenderedOpaquePixelAsFullyTranslucent=true;
	Capture.PostProcessBlendWeight=0;
	Capture.ShowFlags.SetLighting(false); Capture.ShowFlags.SetFog(false);
	Capture.ShowFlags.SetAtmosphere(false); Capture.ShowFlags.SetVolumetricFog(false);
	Capture.ShowFlags.SetDynamicShadows(false); Capture.ShowFlags.SetPostProcessing(false);
	Capture.ShowFlags.SetBloom(false); Capture.ShowFlags.SetEyeAdaptation(false);
	Capture.ShowFlags.SetMotionBlur(false); Capture.ShowFlags.SetSkyLighting(false);
}
}

UGuLiSceneUIWidget::UGuLiSceneUIWidget(const FObjectInitializer& Initializer) : Super(Initializer)
{
	SetIsFocusable(false);
	SetVisibility(ESlateVisibility::HitTestInvisible);
	Data = MakeShared<FGuLiSceneUIWidgetData>();
}

void UGuLiSceneUIWidget::InitializeForController(APlayerController* Controller)
{
	LocalController = Controller;
	RefreshSources();
}

TSharedRef<SWidget> UGuLiSceneUIWidget::RebuildWidget()
{
	SceneSlate = SNew(SGuLiSceneUI).Owner(this);
	return SceneSlate.ToSharedRef();
}

void UGuLiSceneUIWidget::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);
	SceneSlate.Reset();
}

void UGuLiSceneUIWidget::SetModalHidden(bool bHidden)
{
	bModalHidden = bHidden;
	SetVisibility(bHidden ? ESlateVisibility::Hidden : ESlateVisibility::HitTestInvisible);
}

void UGuLiSceneUIWidget::HandleIdentityChanged()
{
	OutlineListRefreshRemaining = 0;
	Data->HUDShapes.Reset();
}

void UGuLiSceneUIWidget::RefreshSources()
{
	if (!LocalController.IsValid() || !GetWorld()) return;
	if (!Presentation.IsValid()) for (TActorIterator<AGuLiCommanderPresentationActor> It(GetWorld()); It; ++It) { Presentation = *It; break; }
	if (Presentation.IsValid() && !RouteData && Cast<AGuLiCommanderPlayerController>(LocalController.Get()))
	{
		RouteData=NewObject<UGuLiCommanderRouteLineComponent>(LocalController.Get(),NAME_None,RF_Transient);
		RouteData->InitializeForController(Cast<AGuLiCommanderPlayerController>(LocalController.Get()),Presentation.Get());
		LocalController->AddInstanceComponent(RouteData);
		RouteData->RegisterComponent();
	}
	HealthBars = AGuLiCommanderHealthBarRenderer::FindOrSpawn(GetWorld(), LocalController.Get());
	ActorRingSources.Reset();
	OutpostHaloSources.Reset();
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
		if (auto* Halo=It->FindComponentByClass<UGuLiOutpostPresentationComponent>()) OutpostHaloSources.Add(Halo);
	for (TActorIterator<APawn> It(GetWorld()); It; ++It)
		if (It->FindComponentByClass<UGuLiEngineeringTravelComponent>() || It->GetPlayerState<AGuLiBattlePlayerState>()) ActorRingSources.Add(*It);
	auto* Identity = LocalController->GetPlayerState<AGuLiBattlePlayerState>();
	if (BoundIdentity.Get() != Identity)
	{
		if (BoundIdentity.IsValid()) BoundIdentity->OnCommanderPlayerStateChanged.RemoveDynamic(this, &ThisClass::HandleIdentityChanged);
		BoundIdentity = Identity;
		if (Identity) Identity->OnCommanderPlayerStateChanged.AddUniqueDynamic(this, &ThisClass::HandleIdentityChanged);
		HandleIdentityChanged();
	}
}

void UGuLiSceneUIWidget::NativeTick(const FGeometry& Geometry, float DeltaSeconds)
{
	Super::NativeTick(Geometry, DeltaSeconds);
	SourceRefreshRemaining -= DeltaSeconds;
	if (SourceRefreshRemaining <= 0) { RefreshSources(); SourceRefreshRemaining = .1f; }
	RefreshOutline(DeltaSeconds);
	RefreshPlacement();
	RefreshWorldWidgets();
	if (SceneSlate) SceneSlate->Invalidate(EInvalidateWidgetReason::Paint);
}

void UGuLiSceneUIWidget::DestroyOutlineCapture()
{
	if (IsValid(OutlineCapture)) OutlineCapture->Destroy();
	OutlineCapture = nullptr;
	Data->OutlineBrush.Reset();
	OutlineMaterialInstance = nullptr;
	OutlineTarget = nullptr;
}

void UGuLiSceneUIWidget::NativeDestruct()
{
	if (BoundIdentity.IsValid()) BoundIdentity->OnCommanderPlayerStateChanged.RemoveDynamic(this, &ThisClass::HandleIdentityChanged);
	BoundIdentity.Reset();
	DestroyOutlineCapture();
	DestroyPlacementCapture();
	if (RouteData) RouteData->DestroyComponent();
	RouteData=nullptr;
	LocalController.Reset(); Presentation.Reset(); HealthBars.Reset();
	Data->HUDShapes.Reset(); Data->Batches.Reset();
	ActorRingSources.Reset();
	OutpostHaloSources.Reset();
	Data->SurfaceBrushes.Reset(); Data->WorldWidgets.Reset(); SurfaceInstances.Reset();
	Super::NativeDestruct();
}

void UGuLiSceneUIWidget::RefreshOutline(float DeltaSeconds)
{
	auto* PC = LocalController.Get();
	if (!PC || !PC->PlayerCameraManager || bModalHidden) return;
	const EGuLiTeam Team = GuLiLocalTeamColors::GetViewTeam(PC);
	if (!GuLiLocalTeamColors::IsAssigned(Team)) { DestroyOutlineCapture(); return; }
	const auto* Camera = Cast<AGuLiCommanderCameraPawn>(PC->GetViewTarget());
	if (Camera && Camera->IsOverviewPresentation()) { DestroyOutlineCapture(); return; }
	FSceneViewProjectionData Projection;
	const auto* Player = PC->GetLocalPlayer();
	if (!Player || !Player->ViewportClient || !Player->ViewportClient->Viewport
		|| !Player->GetProjectionData(Player->ViewportClient->Viewport, Projection)) return;
	const auto Rect = Projection.GetConstrainedViewRect();
	if (Rect.Width() <= 0 || Rect.Height() <= 0) return;
	if (!OutlineCapture)
	{
		auto* Material = EnemyOutlineMaterial.LoadSynchronous();
		if (!Material) return;
		OutlineTarget = NewObject<UTextureRenderTarget2D>(this);
		OutlineTarget->ClearColor = FLinearColor(0, 0, 0, 1); // SceneColor HDR stores inverse opacity in alpha.
		OutlineTarget->InitCustomFormat(Rect.Width(), Rect.Height(), PF_FloatRGBA, true);
		OutlineMaterialInstance = UMaterialInstanceDynamic::Create(Material, this);
		OutlineMaterialInstance->SetTextureParameterValue(TEXT("Silhouette"), OutlineTarget);
		Data->OutlineBrush = MakeShared<FSlateMaterialBrush>(*OutlineMaterialInstance, FVector2D(Rect.Width(), Rect.Height()));
		FActorSpawnParameters Params; Params.Owner = PC; Params.ObjectFlags |= RF_Transient;
		OutlineCapture = GetWorld()->SpawnActor<ASceneCapture2D>(ASceneCapture2D::StaticClass(), FTransform::Identity, Params);
		if (!OutlineCapture) { DestroyOutlineCapture(); return; }
		ConfigureMaskCapture(*OutlineCapture->GetCaptureComponent2D(),OutlineTarget);
		OutlineListRefreshRemaining = 0;
	}
	if (OutlineTarget->SizeX != Rect.Width() || OutlineTarget->SizeY != Rect.Height())
		OutlineTarget->ResizeTarget(Rect.Width(), Rect.Height());
	auto* Capture = OutlineCapture->GetCaptureComponent2D();
	OutlineListRefreshRemaining -= DeltaSeconds;
	if (OutlineListRefreshRemaining <= 0)
	{
		Capture->ClearShowOnlyComponents();
		if (Presentation.IsValid())
		{
			TArray<UInstancedStaticMeshComponent*> Batches; Presentation->GetUnitInstanceComponents(Batches);
			for (auto* Batch : Batches)
				if (GuLiLocalTeamColors::IsEnemy(EGuLiTeam(Batch->CustomDepthStencilValue), Team)) Capture->ShowOnlyComponent(Batch);
		}
		if (const auto* Registry = GetWorld()->GetSubsystem<UGuLiCommanderOverviewSubsystem>())
			for (const auto& Weak : Registry->GetActors())
			{
				auto* Actor = Weak.Get();
				if (!Actor || Actor->IsHidden() || UGuLiExternalUnitControlComponent::IsActorPhased(Actor)
					|| !GuLiLocalTeamColors::IsEnemy(GuLiLocalTeamColors::GetActorTeam(Actor), Team)) continue;
				TInlineComponentArray<UMeshComponent*> Meshes(Actor);
				for (auto* Mesh : Meshes) if (Mesh->IsVisible()) Capture->ShowOnlyComponent(Mesh);
			}
		OutlineListRefreshRemaining = .1f;
	}
	const auto& POV = PC->PlayerCameraManager->GetCameraCacheView();
	OutlineCapture->SetActorLocationAndRotation(POV.Location, POV.Rotation);
	Capture->FOVAngle = POV.FOV; Capture->ProjectionType = POV.ProjectionMode; Capture->OrthoWidth = POV.OrthoWidth;
	Capture->bUseCustomProjectionMatrix=true; Capture->CustomProjectionMatrix=Projection.ProjectionMatrix;
	Capture->CaptureScene();
}

void UGuLiSceneUIWidget::DestroyPlacementCapture()
{
	if (IsValid(PlacementCapture)) PlacementCapture->Destroy();
	PlacementCapture=nullptr; PlacementTarget=nullptr; PlacementMaterialInstance=nullptr;
	Data->PlacementBrush.Reset();
}

void UGuLiSceneUIWidget::RefreshPlacement()
{
	auto* PC=LocalController.Get();
	const auto* Placement=PC ? PC->FindComponentByClass<UGuLiBuildingPlacementComponent>() : nullptr;
	const auto* Preview=Placement && Placement->IsBuildModeActive() ? Placement->GetSceneUIPreview() : nullptr;
	if (!Preview || Preview->IsHidden() || bModalHidden || !PC->PlayerCameraManager)
	{ DestroyPlacementCapture(); return; }
	const auto* Player=PC->GetLocalPlayer(); FSceneViewProjectionData Projection;
	if (!Player || !Player->ViewportClient || !Player->ViewportClient->Viewport
		|| !Player->GetProjectionData(Player->ViewportClient->Viewport,Projection)) return;
	const auto Rect=Projection.GetConstrainedViewRect();
	if (Rect.Width()<=0 || Rect.Height()<=0) return;
	if (!PlacementCapture)
	{
		auto* Material=EnemyOutlineMaterial.LoadSynchronous(); if (!Material) return;
		PlacementTarget=NewObject<UTextureRenderTarget2D>(this);
		PlacementTarget->ClearColor=FLinearColor(0,0,0,1);
		PlacementTarget->InitCustomFormat(Rect.Width(),Rect.Height(),PF_FloatRGBA,true);
		PlacementMaterialInstance=UMaterialInstanceDynamic::Create(Material,this);
		PlacementMaterialInstance->SetTextureParameterValue(TEXT("Silhouette"),PlacementTarget);
		PlacementMaterialInstance->SetScalarParameterValue(TEXT("SolidFill"),1);
		Data->PlacementBrush=MakeShared<FSlateMaterialBrush>(*PlacementMaterialInstance,FVector2D(Rect.Width(),Rect.Height()));
		FActorSpawnParameters Params; Params.Owner=PC; Params.ObjectFlags|=RF_Transient;
		PlacementCapture=GetWorld()->SpawnActor<ASceneCapture2D>(ASceneCapture2D::StaticClass(),FTransform::Identity,Params);
		if (!PlacementCapture) { DestroyPlacementCapture(); return; }
		ConfigureMaskCapture(*PlacementCapture->GetCaptureComponent2D(),PlacementTarget);
	}
	if (PlacementTarget->SizeX!=Rect.Width() || PlacementTarget->SizeY!=Rect.Height())
		PlacementTarget->ResizeTarget(Rect.Width(),Rect.Height());
	PlacementMaterialInstance->SetVectorParameterValue(TEXT("TintColor"),Preview->IsSceneUIPlacementValid()
		? FLinearColor(.04f,1,.12f) : FLinearColor(1,.03f,.02f));
	auto* Capture=PlacementCapture->GetCaptureComponent2D(); Capture->ClearShowOnlyComponents();
	for (UMeshComponent* Mesh : Preview->GetSceneUIMeshes()) if (Mesh) Capture->ShowOnlyComponent(Mesh);
	const auto& POV=PC->PlayerCameraManager->GetCameraCacheView();
	PlacementCapture->SetActorLocationAndRotation(POV.Location,POV.Rotation);
	Capture->FOVAngle=POV.FOV; Capture->ProjectionType=POV.ProjectionMode; Capture->OrthoWidth=POV.OrthoWidth;
	Capture->bUseCustomProjectionMatrix=true; Capture->CustomProjectionMatrix=Projection.ProjectionMatrix;
	Capture->CaptureScene();
}

UMaterialInstanceDynamic* UGuLiSceneUIWidget::GetSurfaceInstance(UTexture* Texture)
{
	if (!Texture) return nullptr;
	if (const auto* Existing=SurfaceInstances.Find(Texture)) return Existing->Get();
	auto* Material=WidgetSurfaceMaterial.LoadSynchronous(); if (!Material) return nullptr;
	auto* Instance=UMaterialInstanceDynamic::Create(Material,this);
	Instance->SetTextureParameterValue(TEXT("WidgetTexture"),Texture);
	SurfaceInstances.Add(Texture,Instance);
	Data->SurfaceBrushes.Add(Texture,MakeShared<FSlateMaterialBrush>(*Instance,FVector2D(1)));
	return Instance;
}

void UGuLiSceneUIWidget::RefreshWorldWidgets()
{
	Data->WorldWidgets.Reset();
	auto* PC=LocalController.Get();
	const APawn* Pawn=PC ? PC->GetPawn() : nullptr;
	const auto* Source=Pawn && !bModalHidden ? Pawn->FindComponentByClass<UGuLiShipWorldHUDComponent>() : nullptr;
	TSet<UTexture*> ActiveTextures;
	if (Source) Source->GatherSceneUIWidgets(Data->WorldWidgets);
	for (const auto& W : Data->WorldWidgets) if (auto* Texture=W.Texture.Get())
	{ ActiveTextures.Add(Texture); GetSurfaceInstance(Texture); }
	if (!bModalHidden && Data->HUDFrame!=MAX_uint64 && GFrameCounter-Data->HUDFrame<=1)
		for (const auto& S : Data->HUDShapes) if (auto* Texture=S.Texture.Get()) ActiveTextures.Add(Texture);
	for (auto It=SurfaceInstances.CreateIterator(); It; ++It) if (!ActiveTextures.Contains(It.Key().Get()))
	{ Data->SurfaceBrushes.Remove(It.Key().Get()); It.RemoveCurrent(); }
}

void UGuLiSceneUIWidget::BeginHUDFrame() { Data->HUDShapes.Reset(); Data->HUDFrame = GFrameCounter; }
void UGuLiSceneUIWidget::AddScreenLine(float X1, float Y1, float X2, float Y2, FLinearColor Color, float Width)
{
	if (Color.A <= 0) return;
	auto& S = Data->HUDShapes.AddDefaulted_GetRef(); S.A={X1,Y1}; S.B={X2,Y2}; S.Color=Opaque(Color); S.Width=Width;
}
void UGuLiSceneUIWidget::AddScreenRect(FLinearColor Color, float X, float Y, float W, float H)
{
	if (Color.A <= 0 || W<=0 || H<=0) return;
	auto& S=Data->HUDShapes.AddDefaulted_GetRef(); S.Kind=FGuLiSceneUIWidgetData::EShape::Rect; S.A={X,Y}; S.B={W,H}; S.Color=Opaque(Color);
}
void UGuLiSceneUIWidget::AddScreenDisc(FVector2D Center, float Radius, FLinearColor Color)
{
	if (Color.A<=0) return;
	auto& S=Data->HUDShapes.AddDefaulted_GetRef(); S.Kind=FGuLiSceneUIWidgetData::EShape::Disc; S.A=Center; S.Width=Radius; S.Color=Opaque(Color);
}
void UGuLiSceneUIWidget::AddScreenImage(UTexture2D* Texture, FVector2D Position, FVector2D Size)
{
	if (!Texture || !GetSurfaceInstance(Texture)) return;
	auto& S=Data->HUDShapes.AddDefaulted_GetRef(); S.Kind=FGuLiSceneUIWidgetData::EShape::Image; S.Texture=Texture; S.A=Position; S.B=Size;
}
void UGuLiSceneUIWidget::AddScreenText(const FString& Text, FLinearColor Color, float X, float Y)
{
	if (Color.A<=0) return;
	auto& S=Data->HUDShapes.AddDefaulted_GetRef(); S.Kind=FGuLiSceneUIWidgetData::EShape::Text; S.Text=Text; S.Color=Opaque(Color); S.A={X,Y};
}

void UGuLiSceneUIWidget::AddWorldLine(const FVector& Start, const FVector& End, FLinearColor Color, float Width)
{
	if (Color.A<=0) return;
	auto& S=Data->HUDShapes.AddDefaulted_GetRef(); S.Kind=FGuLiSceneUIWidgetData::EShape::WorldLine;
	S.WorldA=Start; S.WorldB=End; S.Color=Opaque(Color); S.Width=Width;
}

bool UGuLiSceneUIWidget::ProjectWorldLineToScreen(const FVector& Start, const FVector& End, FVector2D& A, FVector2D& B) const
{
	const auto* PC=LocalController.Get(); const auto* Player=PC ? PC->GetLocalPlayer() : nullptr;
	FSceneViewProjectionData Projection;
	if (!Player || !Player->ViewportClient || !Player->ViewportClient->Viewport
		|| !Player->GetProjectionData(Player->ViewportClient->Viewport,Projection)) return false;
	FVector4 P,Q;
	if (!ClipLine(Projection.ComputeViewProjectionMatrix(),Start,End,P,Q)) return false;
	const auto Rect=Projection.GetConstrainedViewRect();
	auto Pixels=[&Rect](const FVector4& V) { return FVector2D((V.X/V.W*.5+.5)*Rect.Width(),(.5-V.Y/V.W*.5)*Rect.Height()); };
	A=Pixels(P); B=Pixels(Q); return !A.ContainsNaN() && !B.ContainsNaN();
}

FVector2D UGuLiSceneUIWidget::ToPlayerScreen(const FVector2D& ViewportPosition) const
{
	const auto* PC=LocalController.Get(); const auto* Player=PC ? PC->GetLocalPlayer() : nullptr;
	FSceneViewProjectionData Projection;
	return Player && Player->ViewportClient && Player->ViewportClient->Viewport
		&& Player->GetProjectionData(Player->ViewportClient->Viewport,Projection)
		? ViewportPosition-FVector2D(Projection.GetConstrainedViewRect().Min) : ViewportPosition;
}

void UGuLiSceneUIWidget::PaintSceneUI(const FGeometry& Geometry, FSlateWindowElementList& Elements, int32 Layer) const
{
	auto* PC=LocalController.Get();
	if (!PC || !PC->PlayerCameraManager || bModalHidden || Geometry.GetLocalSize().X<=0 || Geometry.GetLocalSize().Y<=0) return;
	const auto* Player=PC->GetLocalPlayer(); FSceneViewProjectionData Projection;
	if (!Player || !Player->ViewportClient || !Player->ViewportClient->Viewport
		|| !Player->GetProjectionData(Player->ViewportClient->Viewport, Projection)) return;
	FViewBatch View(Geometry, Elements, *Data, Projection, Layer+1);
	const auto* Camera=Cast<AGuLiCommanderCameraPawn>(PC->GetViewTarget());
	const bool bOverview=Camera && Camera->IsOverviewPresentation();
	Elements.PushClip(FSlateClippingZone(Geometry));
	if (Data->OutlineBrush && !bOverview && GuLiLocalTeamColors::IsAssigned(GuLiLocalTeamColors::GetViewTeam(PC)))
	{
		const FVector2D Size(View.Rect.Width(), View.Rect.Height());
		FSlateDrawElement::MakeBox(Elements, Layer, Geometry.ToPaintGeometry(FVector2f(Size*View.Scale),
			FSlateLayoutTransform(FVector2f(View.Inset*View.Scale))), Data->OutlineBrush.Get());
	}
	if (Data->PlacementBrush)
	{
		const FVector2D Size(View.Rect.Width(), View.Rect.Height());
		FSlateDrawElement::MakeBox(Elements,Layer,Geometry.ToPaintGeometry(FVector2f(Size*View.Scale),
			FSlateLayoutTransform(FVector2f(View.Inset*View.Scale))),Data->PlacementBrush.Get());
	}
	if (!bOverview && Presentation.IsValid())
	{
		Presentation->GatherSceneUIRings(Data->Rings,PC);
		const EGuLiTeam Team=GuLiLocalTeamColors::GetViewTeam(PC);
		for (bool SelectedPass : {false, true}) for (const auto& Ring : Data->Rings)
			if (Ring.bSelected==SelectedPass) View.Ring(Ring, GuLiLocalTeamColors::GetUI(Ring.Team, Team));
		if (const auto* Commander=Cast<AGuLiCommanderPlayerController>(PC); Commander && Commander->IsCommanderViewActive()) if (const auto* Routes=RouteData.Get())
		{
			Routes->GatherSceneUILines(Data->Routes);
			for (const auto& Line : Data->Routes) View.WorldLine(Line.Start, Line.End, 1.5f, FLinearColor(.08f,.94f,.20f,1));
		}
	}
	if (!bOverview)
	{
		const auto ViewTeam=GuLiLocalTeamColors::GetViewTeam(PC);
		const FRotationMatrix CameraAxes(PC->PlayerCameraManager->GetCameraRotation());
		for (const auto& Source : OutpostHaloSources) if (const auto* Halo=Source.Get())
		{
			FVector Center; EGuLiTeam Team; FVector2D Radii;
			if (!Halo->GetSceneUIHalo(Center,Team,Radii)) continue;
			const FLinearColor Color=GuLiLocalTeamColors::GetUI(Team,ViewTeam);
			if (Color.A<=0) continue;
			const FVector Right=CameraAxes.GetScaledAxis(EAxis::Y)*Radii.X, Up=CameraAxes.GetScaledAxis(EAxis::Z)*Radii.Y;
			for (int32 I=0; I<48; ++I)
			{
				const double A=I*2.0*PI/48, B=(I+1)*2.0*PI/48;
				View.WorldLine(Center+Right*FMath::Cos(A)+Up*FMath::Sin(A),Center+Right*FMath::Cos(B)+Up*FMath::Sin(B),2.f,Color);
			}
		}
		for (const auto& Source : ActorRingSources) if (const auto* Actor=Source.Get())
		{
			FGuLiSceneUIRing Ring; Ring.Team=GuLiLocalTeamColors::GetActorTeam(Actor);
			if (const auto* Vehicle=Cast<IGuLiEngineeringVehicle>(Actor))
				if (const auto* Sync=PC->FindComponentByClass<UGuLiCommanderNetSyncComponent>()) Ring.bSelected=Sync->GetSelectionState().ActorIds.Contains(Vehicle->GetStableActorId());
			if (const auto* Capsule=Actor->FindComponentByClass<UCapsuleComponent>())
			{ Ring.Center=Capsule->GetComponentLocation()-FVector(0,0,Capsule->GetScaledCapsuleHalfHeight()-5); Ring.OuterRadiusCm=Capsule->GetScaledCapsuleRadius(); }
			else { FVector Center,Extent; Actor->GetActorBounds(false,Center,Extent); Ring.Center=FVector(Center.X,Center.Y,Center.Z-Extent.Z+5); Ring.OuterRadiusCm=FMath::Max(Extent.X,Extent.Y); }
			if (!Actor->IsHidden() && Ring.OuterRadiusCm>0) View.Ring(Ring,GuLiLocalTeamColors::GetUI(Ring.Team,ViewTeam));
		}
	}
	if (!bOverview && HealthBars.IsValid())
	{
		HealthBars->GatherSceneUIBars(Data->Bars);
		for (const auto& Bar : Data->Bars)
		{
			FVector2D P; if (!View.Project(Bar.Center, P)) continue;
			P-=FVector2D(42,6);
			View.Box(P-FVector2D(1), FVector2D(86,14), Bar.bSelected ? FLinearColor::White : FLinearColor::Black);
			View.Box(P,FVector2D(84,12),FLinearColor(.025f,.03f,.035f));
			View.Box(P,FVector2D(84*Bar.Fraction,12),FLinearColor(.15f,.9f,.35f));
		}
	}
	if (const auto* Placement=PC->FindComponentByClass<UGuLiBuildingPlacementComponent>(); Placement && Placement->IsBuildModeActive())
		if (const auto* Preview=Placement->GetSceneUIPreview(); Preview && !Preview->IsHidden())
		{
			const FVector E=Preview->GetSceneUIFootprintExtent(); const FTransform T=Preview->GetActorTransform();
			const FLinearColor Color=Preview->IsSceneUIPlacementValid() ? FLinearColor(.04f,1,.12f) : FLinearColor(1,.03f,.02f);
			for (int32 Axis=0; Axis<2; ++Axis)
			{
				const double Extent=Axis==0 ? E.X : E.Y, Other=Axis==0 ? E.Y : E.X;
				const int32 Count=FMath::Min(64,FMath::CeilToInt(Extent/100.0));
				for (int32 I=-Count; I<=Count; ++I)
				{
					const double Along=FMath::Clamp(I*100.0,-Extent,Extent);
					const FVector A=Axis==0 ? FVector(Along,-Other,7) : FVector(-Other,Along,7);
					const FVector B=Axis==0 ? FVector(Along,Other,7) : FVector(Other,Along,7);
					View.WorldLine(T.TransformPosition(A),T.TransformPosition(B),1,Color);
				}
			}
		}
	if (Data->HUDFrame!=MAX_uint64 && GFrameCounter-Data->HUDFrame<=1) for (const auto& S : Data->HUDShapes)
	{
		switch (S.Kind)
		{
		case FGuLiSceneUIWidgetData::EShape::Line: View.Line(S.A+View.Inset,S.B+View.Inset,S.Width,S.Color); break;
		case FGuLiSceneUIWidgetData::EShape::WorldLine: View.WorldLine(S.WorldA,S.WorldB,S.Width,S.Color); break;
		case FGuLiSceneUIWidgetData::EShape::Rect: View.Box(S.A+View.Inset,S.B,S.Color); break;
		case FGuLiSceneUIWidgetData::EShape::Disc:
			for (int32 I=0; I<32; ++I) { const double A=I*PI/16, B=(I+1)*PI/16;
				View.Triangle(S.A+View.Inset,S.A+View.Inset+FVector2D(FMath::Cos(A),FMath::Sin(A))*S.Width,S.A+View.Inset+FVector2D(FMath::Cos(B),FMath::Sin(B))*S.Width,S.Color); } break;
		case FGuLiSceneUIWidgetData::EShape::Image:
			if (const auto* Brush=Data->SurfaceBrushes.Find(S.Texture.Get()); Brush && Brush->IsValid()) {
				FSlateDrawElement::MakeBox(Elements,Layer+2,Geometry.ToPaintGeometry(FVector2f(S.B*View.Scale),
					FSlateLayoutTransform(FVector2f((S.A+View.Inset)*View.Scale))), Brush->Get()); } break;
		case FGuLiSceneUIWidgetData::EShape::Text:
			FSlateDrawElement::MakeText(Elements,Layer+2,Geometry.ToPaintGeometry(FSlateLayoutTransform(FVector2f((S.A+View.Inset)*View.Scale))),
				S.Text,FCoreStyle::GetDefaultFontStyle("Regular",10),ESlateDrawEffect::None,S.Color); break;
		}
	}
	View.Submit();
	if (const auto* Commander=Cast<AGuLiCommanderPlayerController>(PC); Commander && Commander->IsCommanderViewActive())
		if (!Data->Routes.IsEmpty() && RouteData && !bOverview) RouteData->RecordSceneUISubmission();
	for (const auto& W : Data->WorldWidgets)
		if (const auto* Brush=Data->SurfaceBrushes.Find(W.Texture.Get()); Brush && Brush->IsValid())
			View.WidgetSurface(W,FSlateApplication::Get().GetRenderer()->GetResourceHandle(**Brush));
	Elements.PopClip();
}
