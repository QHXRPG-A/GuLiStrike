// Copyright Epic Games, Inc. All Rights Reserved.

#include "Commander/UI/GuLiCommanderMiniMapWidget.h"
#include "InputCoreTypes.h"

#include "Blueprint/SlateBlueprintLibrary.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Camera/PlayerCameraManager.h"
#include "Commander/Framework/GuLiCommanderNetSyncComponent.h"
#include "Commander/Framework/GuLiCommanderPlayerController.h"
#include "Commander/Network/GuLiSoldierStateReplicator.h"
#include "Commander/Presentation/GuLiCommanderCameraPawn.h"
#include "Commander/Presentation/GuLiCommanderLandscapeQuerySubsystem.h"
#include "Commander/Presentation/GuLiCommanderMiniMapTransform.h"
#include "Commander/Presentation/GuLiCommanderPresentationActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Gameplay/Resources/GuLiResourceWorldSubsystem.h"
#include "Slate/SlateBrushAsset.h"
#include "TimerManager.h"
#include "Widgets/SLeafWidget.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SInvalidationPanel.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "ProfilingDebugging/CsvProfiler.h"

CSV_DEFINE_CATEGORY(GuLiCommanderMiniMap, true);

class SGuLiCommanderMiniMapLayer : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SGuLiCommanderMiniMapLayer) {} SLATE_END_ARGS()
	void Construct(const FArguments&, UGuLiCommanderMiniMapWidget* InOwner, bool bInTerrain)
	{ Owner = InOwner; bTerrain = bInTerrain; SetVisibility(EVisibility::HitTestInvisible); }
	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D::ZeroVector; }
	virtual int32 OnPaint(const FPaintArgs&, const FGeometry& Geometry, const FSlateRect& Culling,
		FSlateWindowElementList& Elements, int32 Layer, const FWidgetStyle& Style, bool bEnabled) const override
	{
		return Owner.IsValid() ? Owner->PaintMapLayer(bTerrain, Geometry, Culling, Elements, Layer, Style, bEnabled) : Layer;
	}
private:
	TWeakObjectPtr<UGuLiCommanderMiniMapWidget> Owner;
	bool bTerrain = false;
};

TSharedRef<SWidget> UGuLiCommanderMiniMapWidget::RebuildWidget()
{
	const auto Content = Super::RebuildWidget();
	TerrainLayer = SNew(SGuLiCommanderMiniMapLayer, this, true);
	DynamicLayer = SNew(SGuLiCommanderMiniMapLayer, this, false);
	return SNew(SOverlay)
		+ SOverlay::Slot()[Content]
		+ SOverlay::Slot()[SNew(SInvalidationPanel).Visibility(EVisibility::HitTestInvisible)[TerrainLayer.ToSharedRef()]]
		+ SOverlay::Slot()[SNew(SInvalidationPanel).Visibility(EVisibility::HitTestInvisible)[DynamicLayer.ToSharedRef()]];
}

void UGuLiCommanderMiniMapWidget::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);
	TerrainLayer.Reset(); DynamicLayer.Reset();
}

void UGuLiCommanderMiniMapWidget::InvalidateMapLayer(bool bTerrain)
{
	if (bTerrain) { ++TerrainInvalidations; if (TerrainLayer) TerrainLayer->Invalidate(EInvalidateWidgetReason::Paint); }
	else { ++DynamicInvalidations; if (DynamicLayer) DynamicLayer->Invalidate(EInvalidateWidgetReason::Paint); }
}

bool UGuLiCommanderMiniMapWidget::IsHierarchyVisible() const
{
	if (!IsVisible()) return false;
	for (auto Widget = GetCachedWidget(); Widget; Widget = Widget->GetParentWidget())
		if (!Widget->GetVisibility().IsVisible()) return false;
	return true;
}

void UGuLiCommanderMiniMapWidget::SetVisibility(ESlateVisibility InVisibility)
{
	Super::SetVisibility(InVisibility);
	if (!bNativeConstructed) return;
	if (IsVisible()) { RequestImmediateRefresh(); StartRefreshTimer(); }
	else { bWasHierarchyVisible = false; StopRefreshTimer(); }
}

namespace GuLiCommanderNativeMiniMap
{
	constexpr float RefreshIntervalSeconds = 0.1f;
	constexpr float ContentPadding = 8.0f;
	constexpr int32 TerrainResolution = 20;

	const FLinearColor BackgroundColor(0.005f, 0.018f, 0.03f, 0.98f);
	const FLinearColor FallbackTerrainColor(0.018f, 0.075f, 0.085f, 0.94f);
	const FLinearColor TerrainLowColor(0.012f, 0.07f, 0.075f, 0.96f);
	const FLinearColor TerrainMidColor(0.035f, 0.16f, 0.17f, 0.96f);
	const FLinearColor TerrainHighColor(0.14f, 0.22f, 0.20f, 0.96f);
	const FLinearColor GridColor(0.10f, 0.58f, 0.66f, 0.18f);
	const FLinearColor BorderColor(0.09f, 0.78f, 0.9f, 0.76f);
	const FLinearColor BlueTeamColor(0.08f, 0.82f, 1.0f, 0.98f);
	const FLinearColor RedTeamColor(1.0f, 0.20f, 0.26f, 0.98f);
	const FLinearColor NeutralTeamColor(0.68f, 0.72f, 0.74f, 0.9f);
	const FLinearColor SelectionColor(0.20f, 1.0f, 0.92f, 1.0f);
	const FLinearColor CameraFrameColor(0.82f, 0.69f, 0.52f, 0.95f);

	FBox2D GetWorldBounds(const UWorld* World)
	{
		const UGuLiResourceWorldSubsystem* Resources = World
			? World->GetSubsystem<UGuLiResourceWorldSubsystem>() : nullptr;
		if (Resources && Resources->IsResourceWorldActive() && Resources->GetMapDefinition())
		{
			return Resources->GetPlayableBounds();
		}
		FBox2D Bounds(ForceInit);
		const UGuLiCommanderLandscapeQuerySubsystem* Query = World
			? World->GetSubsystem<UGuLiCommanderLandscapeQuerySubsystem>()
			: nullptr;
		if (Query && Query->TryGetBounds(Bounds))
		{
			return Bounds;
		}
		return FBox2D(FVector2D(-1.0), FVector2D(1.0));
	}

	GuLiCommanderMiniMap::FHeadingUpTransform MakeHeadingTransform(
		const FBox2D& WorldBounds,
		const FBox2D& ScreenBounds,
		const float CameraYawDegrees)
	{
		GuLiCommanderMiniMap::FHeadingUpTransform Transform;
		Transform.WorldBounds = WorldBounds;
		Transform.ScreenBounds = ScreenBounds;
		Transform.CameraYawDegrees = CameraYawDegrees;
		return Transform;
	}

	FLinearColor GetTeamColor(const EGuLiTeam Team)
	{
		switch (Team)
		{
		case EGuLiTeam::Blue:
			return BlueTeamColor;
		case EGuLiTeam::Red:
			return RedTeamColor;
		default:
			return NeutralTeamColor;
		}
	}

	FLinearColor ApplyWidgetTint(const FLinearColor& Color, const FWidgetStyle& WidgetStyle)
	{
		return Color * WidgetStyle.GetColorAndOpacityTint();
	}

	void DrawSolidBox(
		FPaintContext& PaintContext,
		USlateBrushAsset* SolidBrush,
		const int32 LayerId,
		const FVector2D& Position,
		const FVector2D& Size,
		const FLinearColor& Color,
		const FWidgetStyle& WidgetStyle)
	{
		if (Size.X <= UE_DOUBLE_SMALL_NUMBER || Size.Y <= UE_DOUBLE_SMALL_NUMBER)
		{
			return;
		}

		PaintContext.MaxLayer = LayerId - 1;
		UWidgetBlueprintLibrary::DrawBox(
			PaintContext,
			Position,
			Size,
			SolidBrush,
			ApplyWidgetTint(Color, WidgetStyle));
	}

	void DrawClippedSolidBox(
		FPaintContext& PaintContext,
		USlateBrushAsset* SolidBrush,
		const int32 LayerId,
		const FBox2D& ClipBounds,
		FVector2D Position,
		FVector2D Size,
		const FLinearColor& Color,
		const FWidgetStyle& WidgetStyle)
	{
		if (GuLiCommanderMiniMap::ClipRectToScreenBounds(ClipBounds, Position, Size))
		{
			DrawSolidBox(
				PaintContext,
				SolidBrush,
				LayerId,
				Position,
				Size,
				Color,
				WidgetStyle);
		}
	}

	void DrawLine(
		FPaintContext& PaintContext,
		const int32 LayerId,
		const FVector2D& Start,
		const FVector2D& End,
		const FLinearColor& Color,
		const FWidgetStyle& WidgetStyle,
		const float Thickness)
	{
		PaintContext.MaxLayer = LayerId - 1;
		UWidgetBlueprintLibrary::DrawLine(
			PaintContext,
			Start,
			End,
			ApplyWidgetTint(Color, WidgetStyle),
			true,
			Thickness);
	}
}

void UGuLiCommanderMiniMapWidget::InitializeForController(
	AGuLiCommanderPlayerController* InController)
{
	CommanderController = InController;
	SoldierStateReplicator.Reset();
	PresentationActor.Reset();
	RequestImmediateRefresh();
	StartRefreshTimer();
}

void UGuLiCommanderMiniMapWidget::RequestImmediateRefresh()
{
	if (IsDesignTime())
	{
		return;
	}

	RefreshSnapshot();
	InvalidateMapLayer(true);
	InvalidateMapLayer(false);
}

void UGuLiCommanderMiniMapWidget::NativeConstruct()
{
	Super::NativeConstruct();
	bNativeConstructed = true;
	SetIsFocusable(false);
	if (!SolidBrushAsset)
	{
		SolidBrushAsset = NewObject<USlateBrushAsset>(this, TEXT("MiniMapSolidBrush"));
		SolidBrushAsset->Brush.DrawAs = ESlateBrushDrawType::Box;
		SolidBrushAsset->Brush.TintColor = FSlateColor(FLinearColor::White);
		SolidBrushAsset->Brush.SetImageSize(FVector2D(1.0, 1.0));
	}

	if (!CommanderController.IsValid())
	{
		CommanderController = Cast<AGuLiCommanderPlayerController>(GetOwningPlayer());
	}
	RequestImmediateRefresh();
	StartRefreshTimer();
}

void UGuLiCommanderMiniMapWidget::NativeDestruct()
{
	StopRefreshTimer();
	UnbindRuntimeEvents();
	bNativeConstructed = false;
	SoldierPoints.Reset();
	CameraFootprintWorld.Reset();
	Super::NativeDestruct();
}

void UGuLiCommanderMiniMapWidget::StartRefreshTimer()
{
	if (!bNativeConstructed || IsDesignTime())
	{
		return;
	}

	if (UWorld* World = GetWorld(); World
		&& !World->GetTimerManager().IsTimerActive(RefreshTimerHandle))
	{
		FTimerManagerTimerParameters Parameters;
		Parameters.bLoop = true; Parameters.bMaxOncePerFrame = true;
		World->GetTimerManager().SetTimer(RefreshTimerHandle, this, &ThisClass::RefreshSnapshot,
			GuLiCommanderNativeMiniMap::RefreshIntervalSeconds, Parameters);
	}
}

void UGuLiCommanderMiniMapWidget::StopRefreshTimer()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(RefreshTimerHandle);
	}
}

void UGuLiCommanderMiniMapWidget::ResolveRuntimeSources()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		SoldierStateReplicator.Reset();
		PresentationActor.Reset();
		return;
	}

	if (!SoldierStateReplicator.IsValid())
	{
		for (TActorIterator<AGuLiSoldierStateReplicator> It(World); It; ++It)
		{
			SoldierStateReplicator = *It;
			break;
		}
	}
	if (!PresentationActor.IsValid())
	{
		for (TActorIterator<AGuLiCommanderPresentationActor> It(World); It; ++It)
		{
			PresentationActor = *It;
			break;
		}
	}
}

void UGuLiCommanderMiniMapWidget::UnbindRuntimeEvents()
{
	if (auto* Source = BoundPresentationEvents.Get()) Source->OnVisualStatesChanged.Remove(VisualStateHandle);
	BoundPresentationEvents.Reset(); VisualStateHandle.Reset();
	if (auto* Source = BoundRosterEvents.Get()) Source->OnRosterDelta.Remove(RosterDeltaHandle);
	if (auto* Source = BoundSelectionEvents.Get()) Source->OnSelectionChanged.Remove(SelectionChangedHandle);
	BoundRosterEvents.Reset(); BoundSelectionEvents.Reset();
	RosterDeltaHandle.Reset(); SelectionChangedHandle.Reset();
}

void UGuLiCommanderMiniMapWidget::BindRuntimeEvents()
{
	auto* Presentation = PresentationActor.Get();
	if (BoundPresentationEvents.Get() != Presentation)
	{
		if (auto* Old = BoundPresentationEvents.Get()) Old->OnVisualStatesChanged.Remove(VisualStateHandle);
		BoundPresentationEvents = Presentation;
		if (Presentation) VisualStateHandle = Presentation->OnVisualStatesChanged.AddUObject(this, &ThisClass::HandleVisualStatesChanged);
	}
	auto* Replicator = SoldierStateReplicator.Get();
	if (BoundRosterEvents.Get() != Replicator)
	{
		if (auto* Old = BoundRosterEvents.Get()) Old->OnRosterDelta.Remove(RosterDeltaHandle);
		BoundRosterEvents = Replicator; SoldierPoints.Reset();
		if (Replicator) RosterDeltaHandle = Replicator->OnRosterDelta.AddUObject(this, &ThisClass::HandleRosterDelta);
		InvalidateMapLayer(false);
	}
	const auto* Controller = CommanderController.Get();
	auto* Selection = Controller ? Controller->GetCommanderNetSyncComponent() : nullptr;
	if (BoundSelectionEvents.Get() != Selection)
	{
		if (auto* Old = BoundSelectionEvents.Get()) Old->OnSelectionChanged.Remove(SelectionChangedHandle);
		BoundSelectionEvents = Selection;
		if (Selection)
		{
			SelectionChangedHandle = Selection->OnSelectionChanged.AddUObject(this, &ThisClass::HandleSelectionChanged);
			HandleSelectionChanged(Selection->GetSelectionState());
		}
		else HandleSelectionChanged(FGuLiCommanderSelectionState());
	}
}

bool UGuLiCommanderMiniMapWidget::RefreshPoint(FGuLiSoldierId Id, bool bRefreshPosition)
{
	const auto* State = SoldierStateReplicator.IsValid() ? SoldierStateReplicator->FindSoldierState(Id) : nullptr;
	if (!State || !State->IsAlive() || State->bPhased) return SoldierPoints.Remove(Id) != 0;
	const auto* Previous = SoldierPoints.Find(Id);
	FGuLiCommanderMiniMapSoldierPoint Point;
	if (Previous) Point = *Previous;
	if (bRefreshPosition || !Previous)
	{
		FTransform Pose;
		if (!PresentationActor.IsValid() || !PresentationActor->TryGetPresentedSoldierTransform(Id, Pose) || Pose.ContainsNaN())
			return SoldierPoints.Remove(Id) != 0;
		Point.WorldPosition = FVector2D(Pose.GetLocation());
	}
	Point.Team = State->Team; Point.bSelected = SelectedSoldiers.Contains(Id);
	if (Previous && Previous->WorldPosition.Equals(Point.WorldPosition, 0.01) && Previous->Team == Point.Team && Previous->bSelected == Point.bSelected) return false;
	SoldierPoints.Add(Id, Point);
	return true;
}

void UGuLiCommanderMiniMapWidget::HandleVisualStatesChanged(const TArray<FGuLiSoldierId>& Ids)
{
	bool bChanged = false;
	for (auto Id : Ids) bChanged |= RefreshPoint(Id, true);
	if (bChanged) InvalidateMapLayer(false);
}

void UGuLiCommanderMiniMapWidget::HandleRosterDelta(const FGuLiSoldierRosterDelta& Delta)
{
	bool bChanged = false;
	if (Delta.bReset) { SoldierPoints.Reset(); InvalidateMapLayer(false); return; }
	for (auto Id : Delta.Removed) bChanged |= SoldierPoints.Remove(Id) != 0;
	for (auto Id : Delta.Added) bChanged |= RefreshPoint(Id, true);
	for (const auto& Pair : Delta.Changed)
		if (EnumHasAnyFlags(Pair.Value, EGuLiSoldierStateChange::Team | EGuLiSoldierStateChange::Life | EGuLiSoldierStateChange::Phase))
			bChanged |= RefreshPoint(Pair.Key, false);
	if (bChanged) InvalidateMapLayer(false);
}

void UGuLiCommanderMiniMapWidget::HandleSelectionChanged(const FGuLiCommanderSelectionState& Selection)
{
	TSet<FGuLiSoldierId> Next;
	for (const auto& Cohort : Selection.Cohorts) for (auto Id : Cohort.MemberIds) if (Id.IsValid()) Next.Add(Id);
	TSet<FGuLiSoldierId> Affected;
	for (auto Id : Next) if (!SelectedSoldiers.Contains(Id)) Affected.Add(Id);
	for (auto Id : SelectedSoldiers) if (!Next.Contains(Id)) Affected.Add(Id);
	SelectedSoldiers = MoveTemp(Next);
	bool bChanged = false;
	for (auto Id : Affected) bChanged |= RefreshPoint(Id, false);
	if (bChanged) InvalidateMapLayer(false);
}

void UGuLiCommanderMiniMapWidget::RefreshSnapshot()
{
	if (IsDesignTime() || !GetWorld()) return;
	if (!IsHierarchyVisible()) { bWasHierarchyVisible = false; return; }
	TRACE_CPUPROFILER_EVENT_SCOPE(GuLiCommanderMiniMap_Snapshot);
	const bool bShown = !bWasHierarchyVisible;
	bWasHierarchyVisible = true;
	ResolveRuntimeSources(); BindRuntimeEvents();
	const uint32 PreviousTerrainRevision = TerrainLandscapeRevision;
	const bool bPreviousTerrainReady = bTerrainCacheInitialized;
	const FBox2D PreviousBounds = TerrainWorldBounds;
	EnsureTerrainCache();
	bool bDynamicChanged = bShown;
	if (const auto* Replicator = SoldierStateReplicator.Get())
		for (const auto& State : Replicator->GetItems()) bDynamicChanged |= RefreshPoint(State.SoldierId, true);
	else if (!SoldierPoints.IsEmpty()) { SoldierPoints.Reset(); bDynamicChanged = true; }
	const float PreviousYaw = CameraYawDegrees;
	const auto PreviousFootprint = CameraFootprintWorld;
	const auto PreviousPosition = CameraWorldPosition;
	const bool bPreviousCamera = bHasCameraWorldPosition;
	CameraYawDegrees = ResolveCameraYawDegrees(); RefreshCameraFootprint();
	const bool bHeadingChanged = !FMath::IsNearlyEqual(PreviousYaw, CameraYawDegrees);
	if (bShown || bHeadingChanged || PreviousTerrainRevision != TerrainLandscapeRevision || !bPreviousTerrainReady || PreviousBounds != TerrainWorldBounds) InvalidateMapLayer(true);
	bDynamicChanged |= bHeadingChanged || PreviousFootprint != CameraFootprintWorld || PreviousPosition != CameraWorldPosition || bPreviousCamera != bHasCameraWorldPosition;
	if (bDynamicChanged) InvalidateMapLayer(false);
	CSV_CUSTOM_STAT(GuLiCommanderMiniMap, DataCacheBytes, float(SoldierPoints.GetAllocatedSize() + TerrainHeights.GetAllocatedSize() + TerrainValidity.GetAllocatedSize() + CameraFootprintWorld.GetAllocatedSize()), ECsvCustomStatOp::Set);
}

void UGuLiCommanderMiniMapWidget::EnsureTerrainCache()
{
	UWorld* World = GetWorld();
	const UGuLiCommanderLandscapeQuerySubsystem* LandscapeQuery = World
		? World->GetSubsystem<UGuLiCommanderLandscapeQuerySubsystem>()
		: nullptr;
	const uint32 LandscapeRevision = LandscapeQuery ? LandscapeQuery->GetCacheRevision() : 0u;
	const FBox2D CurrentBounds = GuLiCommanderNativeMiniMap::GetWorldBounds(World);
	if (bTerrainCacheInitialized && TerrainLandscapeRevision == LandscapeRevision && TerrainWorldBounds == CurrentBounds)
	{
		return;
	}
	bTerrainCacheInitialized = true;
	TerrainLandscapeRevision = LandscapeRevision;
	TerrainWorldBounds = FBox2D(ForceInit);
	TerrainHeights.Reset();
	TerrainValidity.Reset();
	TerrainMinimumHeight = 0.0f;
	TerrainMaximumHeight = 1.0f;

	if (!World)
	{
		TerrainWorldBounds = GuLiCommanderNativeMiniMap::GetWorldBounds(nullptr);
		bTerrainCacheInitialized = false;
		return;
	}

	const UGuLiResourceWorldSubsystem* Resources =
		World->GetSubsystem<UGuLiResourceWorldSubsystem>();
	if (Resources && Resources->IsResourceWorldActive() && Resources->GetMapDefinition())
	{
		TerrainWorldBounds = Resources->GetPlayableBounds();
	}
	else if (!LandscapeQuery || !LandscapeQuery->TryGetBounds(TerrainWorldBounds))
	{
		TerrainWorldBounds = GuLiCommanderNativeMiniMap::GetWorldBounds(World);
		return;
	}

	constexpr int32 Resolution = GuLiCommanderNativeMiniMap::TerrainResolution;
	const int32 SampleCount = Resolution * Resolution;
	TerrainHeights.Init(0.0f, SampleCount);
	TerrainValidity.Init(0u, SampleCount);
	TerrainMinimumHeight = TNumericLimits<float>::Max();
	TerrainMaximumHeight = TNumericLimits<float>::Lowest();
	const FVector2D WorldSize = TerrainWorldBounds.GetSize();
	int32 ValidSampleCount = 0;

	for (int32 Row = 0; Row < Resolution; ++Row)
	{
		for (int32 Column = 0; Column < Resolution; ++Column)
		{
			const float NormalizedX = (static_cast<float>(Column) + 0.5f) / static_cast<float>(Resolution);
			const float NormalizedY = (static_cast<float>(Row) + 0.5f) / static_cast<float>(Resolution);
			const FVector2D SampleXY(
				TerrainWorldBounds.Min.X + WorldSize.X * NormalizedX,
				TerrainWorldBounds.Min.Y + WorldSize.Y * NormalizedY);
			const int32 SampleIndex = Row * Resolution + Column;

			float Height = 0.0f;
			if (LandscapeQuery && LandscapeQuery->TryGetLandscapeHeight(SampleXY, Height))
			{
				TerrainHeights[SampleIndex] = Height;
				TerrainValidity[SampleIndex] = 1u;
				TerrainMinimumHeight = FMath::Min(TerrainMinimumHeight, Height);
				TerrainMaximumHeight = FMath::Max(TerrainMaximumHeight, Height);
				++ValidSampleCount;
			}
		}
	}

	if (ValidSampleCount == 0)
	{
		TerrainHeights.Reset();
		TerrainValidity.Reset();
		TerrainMinimumHeight = 0.0f;
		TerrainMaximumHeight = 1.0f;
		bTerrainCacheInitialized = false;
	}
}

bool UGuLiCommanderMiniMapWidget::TrySampleTerrainHeight(
	const FVector2D& WorldPosition,
	float& OutHeight) const
{
	constexpr int32 Resolution = GuLiCommanderNativeMiniMap::TerrainResolution;
	if (!TerrainWorldBounds.bIsValid
		|| !TerrainWorldBounds.IsInsideOrOn(WorldPosition)
		|| TerrainHeights.Num() != Resolution * Resolution
		|| TerrainValidity.Num() != Resolution * Resolution)
	{
		return false;
	}

	const FVector2D WorldSize = TerrainWorldBounds.GetSize();
	if (WorldSize.X <= UE_DOUBLE_SMALL_NUMBER || WorldSize.Y <= UE_DOUBLE_SMALL_NUMBER)
	{
		return false;
	}

	const double NormalizedX = (WorldPosition.X - TerrainWorldBounds.Min.X) / WorldSize.X;
	const double NormalizedY = (WorldPosition.Y - TerrainWorldBounds.Min.Y) / WorldSize.Y;
	const double SampleX = FMath::Clamp(
		NormalizedX * static_cast<double>(Resolution) - 0.5,
		0.0,
		static_cast<double>(Resolution - 1));
	const double SampleY = FMath::Clamp(
		NormalizedY * static_cast<double>(Resolution) - 0.5,
		0.0,
		static_cast<double>(Resolution - 1));
	const int32 MinimumColumn = FMath::FloorToInt(SampleX);
	const int32 MinimumRow = FMath::FloorToInt(SampleY);
	const int32 MaximumColumn = FMath::Min(MinimumColumn + 1, Resolution - 1);
	const int32 MaximumRow = FMath::Min(MinimumRow + 1, Resolution - 1);
	const double FractionX = SampleX - static_cast<double>(MinimumColumn);
	const double FractionY = SampleY - static_cast<double>(MinimumRow);

	double WeightedHeight = 0.0;
	double TotalWeight = 0.0;
	for (int32 RowIndex = 0; RowIndex < 2; ++RowIndex)
	{
		const int32 Row = RowIndex == 0 ? MinimumRow : MaximumRow;
		const double RowWeight = RowIndex == 0 ? 1.0 - FractionY : FractionY;
		for (int32 ColumnIndex = 0; ColumnIndex < 2; ++ColumnIndex)
		{
			const int32 Column = ColumnIndex == 0 ? MinimumColumn : MaximumColumn;
			const double ColumnWeight = ColumnIndex == 0 ? 1.0 - FractionX : FractionX;
			const double Weight = RowWeight * ColumnWeight;
			const int32 SampleIndex = Row * Resolution + Column;
			if (Weight <= UE_DOUBLE_SMALL_NUMBER || TerrainValidity[SampleIndex] == 0u)
			{
				continue;
			}

			WeightedHeight += static_cast<double>(TerrainHeights[SampleIndex]) * Weight;
			TotalWeight += Weight;
		}
	}

	if (TotalWeight <= UE_DOUBLE_SMALL_NUMBER)
	{
		return false;
	}
	OutHeight = static_cast<float>(WeightedHeight / TotalWeight);
	return FMath::IsFinite(OutHeight);
}

float UGuLiCommanderMiniMapWidget::ResolveCameraYawDegrees() const
{
	const AGuLiCommanderPlayerController* Controller = CommanderController.Get();
	if (!Controller)
	{
		return 0.0f;
	}

	if (Controller->PlayerCameraManager)
	{
		const float Yaw = Controller->PlayerCameraManager->GetCameraRotation().Yaw;
		if (FMath::IsFinite(Yaw))
		{
			return FRotator::NormalizeAxis(Yaw);
		}
	}

	if (const AGuLiCommanderCameraPawn* CameraPawn = Controller->GetPawn<AGuLiCommanderCameraPawn>())
	{
		const float Yaw = CameraPawn->GetActorRotation().Yaw;
		return FMath::IsFinite(Yaw) ? FRotator::NormalizeAxis(Yaw) : 0.0f;
	}
	return 0.0f;
}

void UGuLiCommanderMiniMapWidget::RefreshCameraFootprint()
{
	CameraFootprintWorld.Reset();
	bHasCameraWorldPosition = false;
	AGuLiCommanderPlayerController* Controller = CommanderController.Get();
	if (!Controller)
	{
		return;
	}

	const AGuLiCommanderCameraPawn* CameraPawn = Controller->GetPawn<AGuLiCommanderCameraPawn>();
	if (!CameraPawn)
	{
		return;
	}

	const FVector CameraLocation = CameraPawn->GetActorLocation();
	if (!CameraLocation.ContainsNaN())
	{
		CameraWorldPosition = FVector2D(CameraLocation.X, CameraLocation.Y);
		bHasCameraWorldPosition = true;
	}

	int32 ViewportWidth = 0;
	int32 ViewportHeight = 0;
	Controller->GetViewportSize(ViewportWidth, ViewportHeight);
	if (ViewportWidth <= 0 || ViewportHeight <= 0)
	{
		return;
	}

	const FVector2D ViewportCorners[4] = {
		FVector2D(0.0, 0.0),
		FVector2D(static_cast<double>(ViewportWidth), 0.0),
		FVector2D(static_cast<double>(ViewportWidth), static_cast<double>(ViewportHeight)),
		FVector2D(0.0, static_cast<double>(ViewportHeight))
	};
	CameraFootprintWorld.Reserve(UE_ARRAY_COUNT(ViewportCorners));
	const float FocusPlaneZ = CameraLocation.Z;
	for (const FVector2D& Corner : ViewportCorners)
	{
		FVector RayOrigin;
		FVector RayDirection;
		if (!Controller->DeprojectScreenPositionToWorld(
			static_cast<float>(Corner.X),
			static_cast<float>(Corner.Y),
			RayOrigin,
			RayDirection)
			|| RayDirection.Z >= -UE_SMALL_NUMBER)
		{
			CameraFootprintWorld.Reset();
			return;
		}

		const double Distance = (static_cast<double>(FocusPlaneZ) - RayOrigin.Z)
			/ static_cast<double>(RayDirection.Z);
		if (Distance <= 0.0 || !FMath::IsFinite(Distance))
		{
			CameraFootprintWorld.Reset();
			return;
		}

		const FVector WorldCorner = RayOrigin + RayDirection * Distance;
		if (WorldCorner.ContainsNaN())
		{
			CameraFootprintWorld.Reset();
			return;
		}
		CameraFootprintWorld.Add(FVector2D(WorldCorner.X, WorldCorner.Y));
	}
}

FBox2D UGuLiCommanderMiniMapWidget::GetLocalContentBounds(const FGeometry& Geometry) const
{
	const FVector2D LocalSize = Geometry.GetLocalSize();
	const double MaximumX = FMath::Max(
		static_cast<double>(GuLiCommanderNativeMiniMap::ContentPadding),
		LocalSize.X - GuLiCommanderNativeMiniMap::ContentPadding);
	const double MaximumY = FMath::Max(
		static_cast<double>(GuLiCommanderNativeMiniMap::ContentPadding),
		LocalSize.Y - GuLiCommanderNativeMiniMap::ContentPadding);
	return FBox2D(
		FVector2D(
			GuLiCommanderNativeMiniMap::ContentPadding,
			GuLiCommanderNativeMiniMap::ContentPadding),
		FVector2D(MaximumX, MaximumY));
}

int32 UGuLiCommanderMiniMapWidget::NativePaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& Culling,
	FSlateWindowElementList& Elements, int32 Layer, const FWidgetStyle& Style, bool bEnabled) const
{
	return Super::NativePaint(Args, Geometry, Culling, Elements, Layer, Style, bEnabled);
}

int32 UGuLiCommanderMiniMapWidget::PaintMapLayer(bool bTerrain, const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId,
	const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE_CONDITIONAL(GuLiCommanderMiniMap_TerrainPaint, bTerrain);
	TRACE_CPUPROFILER_EVENT_SCOPE_CONDITIONAL(GuLiCommanderMiniMap_DynamicPaint, !bTerrain);
	const int32 PaintLayer = LayerId + 1;
	const FVector2D WidgetSize = AllottedGeometry.GetLocalSize();
	if (WidgetSize.X <= 2.0 || WidgetSize.Y <= 2.0) return PaintLayer;
	using namespace GuLiCommanderNativeMiniMap;
	FPaintContext PaintContext(
		AllottedGeometry,
		MyCullingRect,
		OutDrawElements,
		PaintLayer - 1,
		InWidgetStyle,
		bParentEnabled);
	const FBox2D ContentBounds = GetLocalContentBounds(AllottedGeometry);
	const FVector2D ContentSize = ContentBounds.GetSize();
	const FBox2D WorldBounds = TerrainWorldBounds.bIsValid
		? TerrainWorldBounds
		: GetWorldBounds(GetWorld());
	const GuLiCommanderMiniMap::FHeadingUpTransform Transform = MakeHeadingTransform(
		WorldBounds,
		ContentBounds,
		CameraYawDegrees);

	if (bTerrain)
	{
	DrawSolidBox(
		PaintContext,
		SolidBrushAsset.Get(),
		PaintLayer,
		FVector2D::ZeroVector,
		WidgetSize,
		BackgroundColor,
		InWidgetStyle);
	DrawSolidBox(
		PaintContext,
		SolidBrushAsset.Get(),
		PaintLayer,
		ContentBounds.Min,
		ContentSize,
		FallbackTerrainColor,
		InWidgetStyle);

	if (!TerrainHeights.IsEmpty() && Transform.IsValid())
	{
		constexpr int32 Resolution = TerrainResolution;
		const FVector2D CellSize(
			ContentSize.X / static_cast<double>(Resolution),
			ContentSize.Y / static_cast<double>(Resolution));
		const float HeightRange = FMath::Max(
			TerrainMaximumHeight - TerrainMinimumHeight,
			1.0f);

		for (int32 Row = 0; Row < Resolution; ++Row)
		{
			for (int32 Column = 0; Column < Resolution; ++Column)
			{
				const FVector2D CellCenter(
					ContentBounds.Min.X + (static_cast<double>(Column) + 0.5) * CellSize.X,
					ContentBounds.Min.Y + (static_cast<double>(Row) + 0.5) * CellSize.Y);
				FVector2D SampleWorldPosition;
				float SampleHeight = 0.0f;
				if (!Transform.TryScreenToWorld(CellCenter, SampleWorldPosition)
					|| !TrySampleTerrainHeight(SampleWorldPosition, SampleHeight))
				{
					continue;
				}

				const float HeightAlpha = FMath::Clamp(
					(SampleHeight - TerrainMinimumHeight) / HeightRange,
					0.0f,
					1.0f);
				const FLinearColor TerrainColor = HeightAlpha < 0.55f
					? FMath::Lerp(TerrainLowColor, TerrainMidColor, HeightAlpha / 0.55f)
					: FMath::Lerp(
						TerrainMidColor,
						TerrainHighColor,
						(HeightAlpha - 0.55f) / 0.45f);
				DrawClippedSolidBox(
					PaintContext,
					SolidBrushAsset.Get(),
					PaintLayer,
					ContentBounds,
					FVector2D(
						ContentBounds.Min.X + static_cast<double>(Column) * CellSize.X,
						ContentBounds.Min.Y + static_cast<double>(Row) * CellSize.Y),
					CellSize + FVector2D(0.5, 0.5),
					TerrainColor,
					InWidgetStyle);
			}
		}
	}

	for (int32 GridLine = 1; GridLine < 4; ++GridLine)
	{
		const double Alpha = static_cast<double>(GridLine) / 4.0;
		const double X = FMath::Lerp(ContentBounds.Min.X, ContentBounds.Max.X, Alpha);
		const double Y = FMath::Lerp(ContentBounds.Min.Y, ContentBounds.Max.Y, Alpha);
		DrawLine(
			PaintContext,
			PaintLayer,
			FVector2D(X, ContentBounds.Min.Y),
			FVector2D(X, ContentBounds.Max.Y),
			GridColor,
			InWidgetStyle,
			1.0f);
		DrawLine(
			PaintContext,
			PaintLayer,
			FVector2D(ContentBounds.Min.X, Y),
			FVector2D(ContentBounds.Max.X, Y),
			GridColor,
			InWidgetStyle,
			1.0f);
	}


	DrawSolidBox(
		PaintContext,
		SolidBrushAsset.Get(),
		PaintLayer,
		ContentBounds.Min,
		FVector2D(ContentSize.X, 1.5),
		BorderColor,
		InWidgetStyle);
	DrawSolidBox(
		PaintContext,
		SolidBrushAsset.Get(),
		PaintLayer,
		FVector2D(ContentBounds.Min.X, ContentBounds.Max.Y - 1.5),
		FVector2D(ContentSize.X, 1.5),
		BorderColor,
		InWidgetStyle);
	DrawSolidBox(
		PaintContext,
		SolidBrushAsset.Get(),
		PaintLayer,
		ContentBounds.Min,
		FVector2D(1.5, ContentSize.Y),
		BorderColor,
		InWidgetStyle);
	DrawSolidBox(
		PaintContext,
		SolidBrushAsset.Get(),
		PaintLayer,
		FVector2D(ContentBounds.Max.X - 1.5, ContentBounds.Min.Y),
		FVector2D(1.5, ContentSize.Y),
		BorderColor,
		InWidgetStyle);

	}
	else
	{
	if (Transform.IsValid())
	{
		for (const auto& Pair : SoldierPoints)
		{
			const auto& Point = Pair.Value;
			FVector2D MapPoint;
			if (!Transform.TryWorldToScreen(Point.WorldPosition, MapPoint))
			{
				continue;
			}

			if (Point.bSelected)
			{
				DrawClippedSolidBox(
					PaintContext,
					SolidBrushAsset.Get(),
					PaintLayer,
					ContentBounds,
					MapPoint - FVector2D(4.5, 4.5),
					FVector2D(9.0, 9.0),
					SelectionColor,
					InWidgetStyle);
			}

			const double HalfPointSize = Point.bSelected ? 2.5 : 1.8;
			DrawClippedSolidBox(
				PaintContext,
				SolidBrushAsset.Get(),
				PaintLayer,
				ContentBounds,
				MapPoint - FVector2D(HalfPointSize, HalfPointSize),
				FVector2D(HalfPointSize * 2.0, HalfPointSize * 2.0),
				GetTeamColor(Point.Team),
				InWidgetStyle);
		}

		if (CameraFootprintWorld.Num() == 4)
		{
			for (int32 CornerIndex = 0; CornerIndex < CameraFootprintWorld.Num(); ++CornerIndex)
			{
				FVector2D Start = Transform.WorldToScreenUnchecked(
					CameraFootprintWorld[CornerIndex]);
				FVector2D End = Transform.WorldToScreenUnchecked(
					CameraFootprintWorld[(CornerIndex + 1) % CameraFootprintWorld.Num()]);
				if (GuLiCommanderMiniMap::ClipLineToScreenBounds(ContentBounds, Start, End))
				{
					DrawLine(
						PaintContext,
						PaintLayer,
						Start,
						End,
						CameraFrameColor,
						InWidgetStyle,
						1.6f);
				}
			}
		}

		if (bHasCameraWorldPosition)
		{
			FVector2D CameraPoint;
			if (Transform.TryWorldToScreen(CameraWorldPosition, CameraPoint))
			{
				DrawClippedSolidBox(
					PaintContext,
					SolidBrushAsset.Get(),
					PaintLayer,
					ContentBounds,
					CameraPoint - FVector2D(2.0, 2.0),
					FVector2D(4.0, 4.0),
					CameraFrameColor,
					InWidgetStyle);
			}
		}
	}

	}
	return FMath::Max(PaintLayer, PaintContext.MaxLayer);
}

bool UGuLiCommanderMiniMapWidget::ResolveScreenWorldPoint(
	const FVector2D& ScreenPixelPosition, FVector& Point) const
{
	// 角色复制可以先于旧 HUD 析构到达；迟到点击不得再移动旧指挥相机。
	const AGuLiCommanderPlayerController* InputOwner = CommanderController.Get();
	if (!InputOwner || !InputOwner->IsCommanderViewActive())
	{
		return false;
	}

	const FGeometry& InGeometry = GetCachedGeometry();
	const FVector2D WidgetSize = InGeometry.GetLocalSize();
	FVector2D PixelMinimum;
	FVector2D ViewportMinimum;
	FVector2D PixelMaximum;
	FVector2D ViewportMaximum;
	USlateBlueprintLibrary::LocalToViewport(
		this,
		InGeometry,
		FVector2D::ZeroVector,
		PixelMinimum,
		ViewportMinimum);
	USlateBlueprintLibrary::LocalToViewport(
		this,
		InGeometry,
		WidgetSize,
		PixelMaximum,
		ViewportMaximum);
	const FVector2D PixelSize = PixelMaximum - PixelMinimum;
	if (WidgetSize.X <= 1.0 || WidgetSize.Y <= 1.0
		|| PixelSize.X <= 1.0 || PixelSize.Y <= 1.0)
	{
		return false;
	}
	const FVector2D LocalPosition(
		(ScreenPixelPosition.X - PixelMinimum.X) * WidgetSize.X / PixelSize.X,
		(ScreenPixelPosition.Y - PixelMinimum.Y) * WidgetSize.Y / PixelSize.Y);
	const FBox2D WidgetBounds(FVector2D::ZeroVector, WidgetSize);
	if (!WidgetBounds.IsInsideOrOn(LocalPosition))
	{
		return false;
	}

	const FBox2D ContentBounds = GetLocalContentBounds(InGeometry);
	const FBox2D WorldBounds = TerrainWorldBounds.bIsValid
		? TerrainWorldBounds
		: GuLiCommanderNativeMiniMap::GetWorldBounds(GetWorld());
	const GuLiCommanderMiniMap::FHeadingUpTransform Transform = GuLiCommanderNativeMiniMap::MakeHeadingTransform(
			WorldBounds,
			ContentBounds,
			CameraYawDegrees);
	FVector2D WorldPosition;
	if (!Transform.TryScreenToWorld(LocalPosition, WorldPosition)) return false;
	Point = FVector(WorldPosition.X, WorldPosition.Y, 0); return true;
}

bool UGuLiCommanderMiniMapWidget::HandleMapClickAtScreenPosition(const FVector2D& ScreenPixelPosition)
{
	FVector Point; auto* PC = CommanderController.Get();
	if (!PC || PC->IsCommanderMenuOpen() || !ResolveScreenWorldPoint(ScreenPixelPosition, Point)) return false;
	if (auto* Camera = PC->GetPawn<AGuLiCommanderCameraPawn>()) { Camera->JumpToWorldLocation(Point); return true; }
	return false;
}
bool UGuLiCommanderMiniMapWidget::HandleMapOrderAtScreenPosition(const FVector2D& ScreenPixelPosition, bool bAppend)
{
	FVector Point; auto* PC = CommanderController.Get();
	return PC && ResolveScreenWorldPoint(ScreenPixelPosition, Point) && PC->IssueMapMove(Point, bAppend);
}
FReply UGuLiCommanderMiniMapWidget::NativeOnMouseButtonDown(const FGeometry&, const FPointerEvent& Event)
{
	FVector2D Pixel, Viewport; USlateBlueprintLibrary::AbsoluteToViewport(this, Event.GetScreenSpacePosition(), Pixel, Viewport);
	if (Event.GetEffectingButton() == EKeys::RightMouseButton)
	{ HandleMapOrderAtScreenPosition(Pixel, Event.IsShiftDown()); return FReply::Handled(); }
	if (Event.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		auto* PC = CommanderController.Get();
		if (PC && PC->IsGroundMoveTargetMode()) HandleMapOrderAtScreenPosition(Pixel, Event.IsShiftDown());
		else { bMapDragging = HandleMapClickAtScreenPosition(Pixel); if (bMapDragging) return FReply::Handled().CaptureMouse(TakeWidget()); }
		return FReply::Handled();
	}
	return FReply::Unhandled();
}
FReply UGuLiCommanderMiniMapWidget::NativeOnMouseButtonUp(const FGeometry&, const FPointerEvent& Event)
{
	if (Event.GetEffectingButton() == EKeys::LeftMouseButton) { bMapDragging = false; return FReply::Handled().ReleaseMouseCapture(); }
	return FReply::Handled();
}
FReply UGuLiCommanderMiniMapWidget::NativeOnMouseMove(const FGeometry&, const FPointerEvent& Event)
{
	if (!bMapDragging) return FReply::Unhandled();
	if (!Event.IsMouseButtonDown(EKeys::LeftMouseButton)) { bMapDragging = false; return FReply::Handled().ReleaseMouseCapture(); }
	FVector2D Pixel, Viewport; USlateBlueprintLibrary::AbsoluteToViewport(this, Event.GetScreenSpacePosition(), Pixel, Viewport);
	HandleMapClickAtScreenPosition(Pixel); return FReply::Handled();
}
