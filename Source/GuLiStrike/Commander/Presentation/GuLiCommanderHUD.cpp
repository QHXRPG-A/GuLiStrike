// Copyright Epic Games, Inc. All Rights Reserved.

#include "Commander/Presentation/GuLiCommanderHUD.h"

#include "Commander/Framework/GuLiCommanderNetSyncComponent.h"
#include "Commander/Framework/GuLiCommanderPlayerController.h"
#include "Commander/Framework/GuLiCommanderPlayerState.h"
#include "Commander/Network/GuLiSoldierStateReplicator.h"
#include "Commander/Presentation/GuLiCommanderCameraPawn.h"
#include "Commander/Presentation/GuLiCommanderLandscapeQuerySubsystem.h"
#include "Commander/Presentation/GuLiCommanderMiniMapTransform.h"
#include "Commander/Presentation/GuLiCommanderPresentationActor.h"
#include "Commander/UI/GuLiCommanderHealthBarRenderer.h"
#include "Commander/UI/GuLiCommanderHUDWidget.h"
#include "Camera/PlayerCameraManager.h"
#include "Blueprint/UserWidget.h"
#include "UObject/ConstructorHelpers.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/UserInterfaceSettings.h"
#include "Engine/World.h"
#include "EngineUtils.h"

namespace GuLiCommanderHUD
{
	const FName SmallPresetHitBox(TEXT("Commander.SelectionPreset.Small"));
	const FName MediumPresetHitBox(TEXT("Commander.SelectionPreset.Medium"));
	const FName LargePresetHitBox(TEXT("Commander.SelectionPreset.Large"));
	const FName MiniMapHitBox(TEXT("Commander.TacticalMap.Jump"));
	constexpr int32 SelectionCircleSegments = 32;
	constexpr int32 MaxVisibleCohortCards = 10;
	constexpr int32 MiniMapTerrainResolution = 20;

	bool ShouldDrawMoveEndpoint(
		const FGuLiMoveEndpointItem& Endpoint,
		const bool bIsSelected,
		const FGuLiSoldierStateItem* SoldierState)
	{
		return Endpoint.IsValid()
			&& bIsSelected
			&& SoldierState
			&& SoldierState->SoldierId == Endpoint.SoldierId
			&& SoldierState->IsAlive()
			&& SoldierState->ActiveOrderId != 0u
			&& SoldierState->ActiveOrderId == Endpoint.ActiveOrderId;
	}

	bool ClipScreenLineToBounds(
		const FBox2D& Bounds,
		FVector2D& InOutStart,
		FVector2D& InOutEnd)
	{
		if (!Bounds.bIsValid
			|| !FMath::IsFinite(Bounds.Min.X) || !FMath::IsFinite(Bounds.Min.Y)
			|| !FMath::IsFinite(Bounds.Max.X) || !FMath::IsFinite(Bounds.Max.Y)
			|| !FMath::IsFinite(InOutStart.X) || !FMath::IsFinite(InOutStart.Y)
			|| !FMath::IsFinite(InOutEnd.X) || !FMath::IsFinite(InOutEnd.Y)
			|| Bounds.Min.X > Bounds.Max.X || Bounds.Min.Y > Bounds.Max.Y)
		{
			return false;
		}
		const FVector2D OriginalStart = InOutStart;
		const FVector2D Delta = InOutEnd - OriginalStart;
		float EnterTime = 0.0f;
		float ExitTime = 1.0f;
		auto ClipBoundary = [&EnterTime, &ExitTime](const float Direction, const float Distance)
		{
			if (FMath::IsNearlyZero(Direction))
			{
				return Distance >= 0.0f;
			}
			const float Time = Distance / Direction;
			if (Direction < 0.0f)
			{
				if (Time > ExitTime)
				{
					return false;
				}
				EnterTime = FMath::Max(EnterTime, Time);
			}
			else
			{
				if (Time < EnterTime)
				{
					return false;
				}
				ExitTime = FMath::Min(ExitTime, Time);
			}
			return true;
		};

		if (!ClipBoundary(-Delta.X, OriginalStart.X - Bounds.Min.X)
			|| !ClipBoundary(Delta.X, Bounds.Max.X - OriginalStart.X)
			|| !ClipBoundary(-Delta.Y, OriginalStart.Y - Bounds.Min.Y)
			|| !ClipBoundary(Delta.Y, Bounds.Max.Y - OriginalStart.Y))
		{
			return false;
		}
		InOutStart = OriginalStart + Delta * EnterTime;
		InOutEnd = OriginalStart + Delta * ExitTime;
		return true;
	}

	FBox2D GetMiniMapScreenBounds(const UCanvas& Canvas)
	{
		const float MapSize = FMath::Clamp(Canvas.SizeY * 0.28f, 190.0f, 512.0f);
		const FVector2D MapMinimum(Canvas.SizeX - MapSize - 24.0f, 24.0f);
		return FBox2D(MapMinimum, MapMinimum + FVector2D(MapSize, MapSize));
	}

	FBox2D GetWorldBounds(const UWorld* World)
	{
		FBox2D Bounds(ForceInit);
		const UGuLiCommanderLandscapeQuerySubsystem* Query = World
			? World->GetSubsystem<UGuLiCommanderLandscapeQuerySubsystem>()
			: nullptr;
		if (Query && Query->TryGetBounds(Bounds))
		{
			return Bounds;
		}
		// A tiny valid box keeps pure map transforms safe while a streamed landscape is unavailable.
		return FBox2D(FVector2D(-1.0), FVector2D(1.0));
	}

	GuLiCommanderMiniMap::FHeadingUpTransform MakeMiniMapTransform(
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
		case EGuLiTeam::Red:
			return FLinearColor(1.0f, 0.06f, 0.04f, 0.95f);
		case EGuLiTeam::Blue:
			return FLinearColor(0.04f, 0.32f, 1.0f, 0.95f);
		default:
			return FLinearColor(0.55f, 0.55f, 0.55f, 0.8f);
		}
	}

	bool IsAcceptedResult(const EGuLiCommandAckResult Result)
	{
		return Result == EGuLiCommandAckResult::Accepted
			|| Result == EGuLiCommandAckResult::PartiallyAccepted;
	}

	const FGuLiCohortCommandAck* FindCohortAck(
		const FGuLiCommandAck& Ack,
		const FGuLiControlCohortId CohortId)
	{
		return Ack.CohortResults.FindByPredicate(
			[CohortId](const FGuLiCohortCommandAck& CohortAck)
			{
				return CohortAck.CohortId == CohortId;
			});
	}

	struct FCohortCardStatus
	{
		FString Label;
		FLinearColor Color = FLinearColor(0.65f, 0.78f, 0.9f, 0.95f);
	};

	// 旧 HUD 的本地消费入口：按选择版本关联移动回执，再结合当前组摘要展示等待/执行/拒绝。
	FCohortCardStatus BuildCohortCardStatus(
		const FGuLiControlCohortDescriptor& Cohort,
		const FGuLiCommandAck& LastAck,
		const uint32 SelectionRevision)
	{
		if (Cohort.AliveCount == 0u)
		{
			return {TEXT("DOWN"), FLinearColor(0.8f, 0.15f, 0.12f, 0.95f)};
		}

		if (LastAck.CommandKind == EGuLiCommandKind::Move
			&& LastAck.ServerSelectionRevision == SelectionRevision)
		{
			if (const FGuLiCohortCommandAck* CohortAck = FindCohortAck(LastAck, Cohort.CohortId))
			{
				if (!IsAcceptedResult(CohortAck->Result))
				{
					return {TEXT("REJECT"), FLinearColor(1.0f, 0.28f, 0.18f, 0.95f)};
				}
				if (Cohort.ActiveOrderId == 0u && LastAck.BatchOrderId != 0u)
				{
					return {TEXT("WAIT"), FLinearColor(1.0f, 0.76f, 0.18f, 0.95f)};
				}
			}
		}

		if (Cohort.ActiveOrderId != 0u)
		{
			return {
				FString::Printf(TEXT("ORDER %u"), Cohort.ActiveOrderId),
				FLinearColor(0.18f, 0.88f, 1.0f, 0.95f)};
		}
		return {TEXT("READY"), FLinearColor(0.65f, 0.78f, 0.9f, 0.95f)};
	}

	FLinearColor GetSoldierPointColor(const FGuLiSoldierStateItem& Soldier)
	{
		if (!Soldier.IsAlive())
		{
			return FLinearColor(0.38f, 0.06f, 0.04f, 0.8f);
		}
		return GetTeamColor(Soldier.Team);
	}
}

AGuLiCommanderHUD::AGuLiCommanderHUD()
{
	static ConstructorHelpers::FClassFinder<UGuLiCommanderHUDWidget> CommanderHUDWidgetFinder(
		TEXT("/Game/Commander/UI/Widgets/WBP_CommanderHUD"));
	if (CommanderHUDWidgetFinder.Succeeded())
	{
		CommanderHUDWidgetClass = CommanderHUDWidgetFinder.Class;
	}
}

void AGuLiCommanderHUD::BeginPlay()
{
	Super::BeginPlay();
	RefreshCommanderRole();
}

void AGuLiCommanderHUD::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	DestroyRuntimeHUD();
	Super::EndPlay(EndPlayReason);
}

void AGuLiCommanderHUD::RefreshCommanderRole()
{
	const AGuLiCommanderPlayerController* CommanderController = Cast<AGuLiCommanderPlayerController>(PlayerOwner);
	if (CommanderController && CommanderController->IsCommanderViewActive())
	{
		CreateRuntimeHUD();
	}
	else
	{
		// RemoveFromParent 会触发 Widget 清理定时器与委托；离开指挥角色时同步释放血条 Actor。
		DestroyRuntimeHUD();
	}
}

void AGuLiCommanderHUD::CreateRuntimeHUD()
{
	AGuLiCommanderPlayerController* CommanderController = Cast<AGuLiCommanderPlayerController>(PlayerOwner);
	if (!CommanderController || !CommanderController->IsCommanderViewActive())
	{
		return;
	}

	if (!RuntimeHUDWidget)
	{
		if (CommanderHUDWidgetClass)
		{
			RuntimeHUDWidget = CreateWidget<UGuLiCommanderHUDWidget>(
				CommanderController,
				CommanderHUDWidgetClass);
		}
		if (RuntimeHUDWidget)
		{
			RuntimeHUDWidget->InitializeForController(CommanderController);
			RuntimeHUDWidget->AddToPlayerScreen(10);
		}
	}

	if (!HealthBarRenderer)
	{
		if (UWorld* World = GetWorld())
		{
			FActorSpawnParameters SpawnParameters;
			SpawnParameters.Owner = CommanderController;
			SpawnParameters.ObjectFlags |= RF_Transient;
			SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			HealthBarRenderer = World->SpawnActor<AGuLiCommanderHealthBarRenderer>(
				AGuLiCommanderHealthBarRenderer::StaticClass(),
				FTransform::Identity,
				SpawnParameters);
			if (HealthBarRenderer)
			{
				HealthBarRenderer->InitializeForController(CommanderController);
			}
		}
	}
}

void AGuLiCommanderHUD::DestroyRuntimeHUD()
{
	if (RuntimeHUDWidget)
	{
		RuntimeHUDWidget->RemoveFromParent();
		RuntimeHUDWidget = nullptr;
	}
	if (HealthBarRenderer)
	{
		HealthBarRenderer->Destroy();
		HealthBarRenderer = nullptr;
	}
}

void AGuLiCommanderHUD::DrawHUD()
{
	Super::DrawHUD();
	if (!Canvas)
	{
		return;
	}

	const AGuLiCommanderPlayerController* CommanderController = Cast<AGuLiCommanderPlayerController>(PlayerOwner);
	// 绘制回调只负责 Canvas；角色/UI 生命周期不能依赖 showhud 或视口是否渲染。
	if (CommanderController && CommanderController->IsCommanderViewActive())
	{
		if (CommanderController->GetCommanderToolMode() == EGuLiCommanderToolMode::Select)
		{
			if (CommanderController->GetSelectionShape() == EGuLiCommanderSelectionShape::Radius)
			{
				DrawSelectionCircle(*CommanderController);
			}
			else
			{
				DrawSelectionRectangle(*CommanderController);
			}
		}
		DrawActiveCommandLine(*CommanderController);
	}
}

void AGuLiCommanderHUD::NotifyHitBoxClick(const FName BoxName)
{
	Super::NotifyHitBoxClick(BoxName);
}

bool AGuLiCommanderHUD::IsScreenPositionOverCommanderUI(
	const FVector2D& ScreenPosition) const
{
	const AGuLiCommanderPlayerController* CommanderController = Cast<AGuLiCommanderPlayerController>(PlayerOwner);
	if (!CommanderController || !CommanderController->IsCommanderViewActive())
	{
		return false;
	}

	if (RuntimeHUDWidget && RuntimeHUDWidget->HasValidBlockingGeometry())
	{
		return RuntimeHUDWidget->IsScreenPositionBlocked(ScreenPosition);
	}

	int32 ViewportWidth = 0;
	int32 ViewportHeight = 0;
	if (!PlayerOwner)
	{
		return false;
	}
	PlayerOwner->GetViewportSize(ViewportWidth, ViewportHeight);
	if (ViewportWidth <= 0 || ViewportHeight <= 0)
	{
		return false;
	}

	// Conservative warm-up fallback before Slate has produced cached geometry.
	const FVector2D ViewportSize(
		static_cast<float>(ViewportWidth),
		static_cast<float>(ViewportHeight));
	const float DPIScale = FMath::Max(0.01f, GetDefault<UUserInterfaceSettings>()->GetDPIScaleBasedOnSize(
		FIntPoint(ViewportWidth, ViewportHeight)));
	const float DockScale = FMath::Min(DPIScale, static_cast<float>(ViewportSize.X) * (7.0f / 12.0f) / 1120.0f);
	const FVector2D DockSize(1120.0f * DockScale, 260.0f * DockScale);
	const FBox2D TopPanel(
		FVector2D((ViewportSize.X - 420.0f * DPIScale) * 0.5f, 18.0f * DPIScale),
		FVector2D((ViewportSize.X + 420.0f * DPIScale) * 0.5f, 82.0f * DPIScale));
	const FBox2D TacticalMapPanel(
		FVector2D(24.0f * DPIScale, FMath::Max(0.0, ViewportSize.Y - 300.0f * DPIScale)),
		FVector2D(300.0f * DPIScale, FMath::Max(276.0 * DPIScale, ViewportSize.Y - 24.0f * DPIScale)));
	const FBox2D DockPanel(
		FVector2D((ViewportSize.X - DockSize.X) * 0.5f, ViewportSize.Y - 24.0f * DPIScale - DockSize.Y),
		FVector2D((ViewportSize.X + DockSize.X) * 0.5f, ViewportSize.Y - 24.0f * DPIScale));
	return TopPanel.IsInsideOrOn(ScreenPosition)
		|| TacticalMapPanel.IsInsideOrOn(ScreenPosition)
		|| DockPanel.IsInsideOrOn(ScreenPosition);
}

AGuLiSoldierStateReplicator* AGuLiCommanderHUD::FindSoldierStateReplicator() const
{
	if (CachedSoldierStateReplicator.IsValid())
	{
		return CachedSoldierStateReplicator.Get();
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}
	for (TActorIterator<AGuLiSoldierStateReplicator> It(World); It; ++It)
	{
		CachedSoldierStateReplicator = *It;
		return *It;
	}
	return nullptr;
}

AGuLiCommanderPresentationActor* AGuLiCommanderHUD::FindPresentationActor() const
{
	if (CachedPresentationActor.IsValid())
	{
		return CachedPresentationActor.Get();
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}
	for (TActorIterator<AGuLiCommanderPresentationActor> It(World); It; ++It)
	{
		CachedPresentationActor = *It;
		return *It;
	}
	return nullptr;
}

bool AGuLiCommanderHUD::EnsureMiniMapTerrainCache()
{
	UWorld* World = GetWorld();
	const UGuLiCommanderLandscapeQuerySubsystem* LandscapeQuery = World
		? World->GetSubsystem<UGuLiCommanderLandscapeQuerySubsystem>()
		: nullptr;
	const uint32 LandscapeRevision = LandscapeQuery ? LandscapeQuery->GetCacheRevision() : 0u;
	if (bMiniMapTerrainCacheInitialized && MiniMapLandscapeRevision == LandscapeRevision)
	{
		return !MiniMapTerrainHeights.IsEmpty();
	}

	bMiniMapTerrainCacheInitialized = true;
	MiniMapLandscapeRevision = LandscapeRevision;
	MiniMapWorldBounds = FBox2D(ForceInit);
	MiniMapTerrainHeights.Reset();
	MiniMapTerrainValidity.Reset();
	if (!World)
	{
		MiniMapWorldBounds = GuLiCommanderHUD::GetWorldBounds(nullptr);
		bMiniMapTerrainCacheInitialized = false;
		return false;
	}

	if (!LandscapeQuery || !LandscapeQuery->TryGetBounds(MiniMapWorldBounds))
	{
		MiniMapWorldBounds = GuLiCommanderHUD::GetWorldBounds(World);
		return false;
	}

	constexpr int32 Resolution = GuLiCommanderHUD::MiniMapTerrainResolution;
	const int32 SampleCount = Resolution * Resolution;
	MiniMapTerrainHeights.Init(0.0f, SampleCount);
	MiniMapTerrainValidity.Init(0u, SampleCount);
	MiniMapTerrainMinimumHeight = TNumericLimits<float>::Max();
	MiniMapTerrainMaximumHeight = TNumericLimits<float>::Lowest();
	const FVector2D WorldSize = MiniMapWorldBounds.GetSize();
	int32 ValidSampleCount = 0;

	for (int32 Row = 0; Row < Resolution; ++Row)
	{
		for (int32 Column = 0; Column < Resolution; ++Column)
		{
			const float NormalizedX = (static_cast<float>(Column) + 0.5f)
				/ static_cast<float>(Resolution);
			const float NormalizedY = (static_cast<float>(Row) + 0.5f)
				/ static_cast<float>(Resolution);
			const FVector2D SampleXY(
				MiniMapWorldBounds.Min.X + WorldSize.X * NormalizedX,
				MiniMapWorldBounds.Min.Y + WorldSize.Y * NormalizedY);
			const int32 SampleIndex = Row * Resolution + Column;

			float Height = 0.0f;
			if (LandscapeQuery->TryGetLandscapeHeight(SampleXY, Height))
			{
				MiniMapTerrainHeights[SampleIndex] = Height;
				MiniMapTerrainValidity[SampleIndex] = 1u;
				MiniMapTerrainMinimumHeight = FMath::Min(
					MiniMapTerrainMinimumHeight,
					Height);
				MiniMapTerrainMaximumHeight = FMath::Max(
					MiniMapTerrainMaximumHeight,
					Height);
				++ValidSampleCount;
			}
		}
	}

	if (ValidSampleCount == 0)
	{
		MiniMapTerrainHeights.Reset();
		MiniMapTerrainValidity.Reset();
		MiniMapTerrainMinimumHeight = 0.0f;
		MiniMapTerrainMaximumHeight = 1.0f;
		bMiniMapTerrainCacheInitialized = false;
		return false;
	}
	return true;
}

bool AGuLiCommanderHUD::TryMiniMapScreenToWorld(
	const FVector2D& ScreenPosition,
	FVector& OutWorldLocation)
{
	if (!Canvas)
	{
		return false;
	}
	EnsureMiniMapTerrainCache();
	const FBox2D ScreenBounds = GuLiCommanderHUD::GetMiniMapScreenBounds(*Canvas);
	if (!ScreenBounds.IsInsideOrOn(ScreenPosition))
	{
		return false;
	}
	const FBox2D WorldBounds = MiniMapWorldBounds.bIsValid
		? MiniMapWorldBounds
		: GuLiCommanderHUD::GetWorldBounds(GetWorld());
	const GuLiCommanderMiniMap::FHeadingUpTransform Transform = GuLiCommanderHUD::MakeMiniMapTransform(
			WorldBounds,
			ScreenBounds,
			GetMiniMapCameraYawDegrees());
	FVector2D WorldPosition;
	if (!Transform.TryScreenToWorld(ScreenPosition, WorldPosition))
	{
		return false;
	}

	OutWorldLocation = FVector(WorldPosition.X, WorldPosition.Y, 0.0f);
	return !OutWorldLocation.ContainsNaN();
}

bool AGuLiCommanderHUD::TrySampleMiniMapTerrainHeight(
	const FVector2D& WorldPosition,
	float& OutHeight) const
{
	constexpr int32 Resolution = GuLiCommanderHUD::MiniMapTerrainResolution;
	if (!MiniMapWorldBounds.bIsValid
		|| !MiniMapWorldBounds.IsInsideOrOn(WorldPosition)
		|| MiniMapTerrainHeights.Num() != Resolution * Resolution
		|| MiniMapTerrainValidity.Num() != Resolution * Resolution)
	{
		return false;
	}

	const FVector2D WorldSize = MiniMapWorldBounds.GetSize();
	if (WorldSize.X <= UE_DOUBLE_SMALL_NUMBER || WorldSize.Y <= UE_DOUBLE_SMALL_NUMBER)
	{
		return false;
	}

	const double NormalizedX = (WorldPosition.X - MiniMapWorldBounds.Min.X) / WorldSize.X;
	const double NormalizedY = (WorldPosition.Y - MiniMapWorldBounds.Min.Y) / WorldSize.Y;
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
			if (Weight <= UE_DOUBLE_SMALL_NUMBER
				|| MiniMapTerrainValidity[SampleIndex] == 0u)
			{
				continue;
			}

			WeightedHeight += static_cast<double>(MiniMapTerrainHeights[SampleIndex]) * Weight;
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

float AGuLiCommanderHUD::GetMiniMapCameraYawDegrees() const
{
	if (PlayerOwner && PlayerOwner->PlayerCameraManager)
	{
		const float CameraYaw = PlayerOwner->PlayerCameraManager->GetCameraRotation().Yaw;
		if (FMath::IsFinite(CameraYaw))
		{
			return FRotator::NormalizeAxis(CameraYaw);
		}
	}

	if (PlayerOwner)
	{
		if (const AGuLiCommanderCameraPawn* CameraPawn = PlayerOwner->GetPawn<AGuLiCommanderCameraPawn>())
		{
			const float CameraYaw = CameraPawn->GetActorRotation().Yaw;
			return FMath::IsFinite(CameraYaw)
				? FRotator::NormalizeAxis(CameraYaw)
				: 0.0f;
		}
	}
	return 0.0f;
}

void AGuLiCommanderHUD::DrawMiniMapTerrain(
	const float MapX,
	const float MapY,
	const float MapSize,
	const float CameraYawDegrees)
{
	DrawRect(FLinearColor(0.015f, 0.025f, 0.035f, 0.94f), MapX, MapY, MapSize, MapSize);
	if (!EnsureMiniMapTerrainCache() || MiniMapTerrainHeights.IsEmpty())
	{
		DrawRect(FLinearColor(0.045f, 0.09f, 0.065f, 0.84f), MapX + 2.0f, MapY + 2.0f, MapSize - 4.0f, MapSize - 4.0f);
		return;
	}

	constexpr int32 Resolution = GuLiCommanderHUD::MiniMapTerrainResolution;
	const float CellSize = MapSize / static_cast<float>(Resolution);
	const float HeightRange = FMath::Max(
		MiniMapTerrainMaximumHeight - MiniMapTerrainMinimumHeight,
		1.0f);
	const FLinearColor LowColor(0.025f, 0.075f, 0.055f, 0.96f);
	const FLinearColor MidColor(0.11f, 0.17f, 0.075f, 0.96f);
	const FLinearColor HighColor(0.28f, 0.24f, 0.12f, 0.96f);
	const FBox2D ScreenBounds(
		FVector2D(MapX, MapY),
		FVector2D(MapX + MapSize, MapY + MapSize));
	const GuLiCommanderMiniMap::FHeadingUpTransform Transform = GuLiCommanderHUD::MakeMiniMapTransform(
			MiniMapWorldBounds,
			ScreenBounds,
			CameraYawDegrees);

	for (int32 Row = 0; Row < Resolution; ++Row)
	{
		for (int32 Column = 0; Column < Resolution; ++Column)
		{
			const FVector2D CellCenter(
				MapX + (static_cast<float>(Column) + 0.5f) * CellSize,
				MapY + (static_cast<float>(Row) + 0.5f) * CellSize);
			FVector2D SampleWorldPosition;
			float SampleHeight = 0.0f;
			if (!Transform.TryScreenToWorld(CellCenter, SampleWorldPosition)
				|| !TrySampleMiniMapTerrainHeight(SampleWorldPosition, SampleHeight))
			{
				continue;
			}
			const float HeightAlpha = FMath::Clamp(
				(SampleHeight - MiniMapTerrainMinimumHeight) / HeightRange,
				0.0f,
				1.0f);
			const FLinearColor TerrainColor = HeightAlpha < 0.55f
				? FMath::Lerp(LowColor, MidColor, HeightAlpha / 0.55f)
				: FMath::Lerp(MidColor, HighColor, (HeightAlpha - 0.55f) / 0.45f);
			FVector2D CellMinimum(
				MapX + static_cast<float>(Column) * CellSize,
				MapY + static_cast<float>(Row) * CellSize);
			FVector2D CellDrawSize(CellSize + 0.5f, CellSize + 0.5f);
			if (GuLiCommanderMiniMap::ClipRectToScreenBounds(
				ScreenBounds,
				CellMinimum,
				CellDrawSize))
			{
				DrawRect(
					TerrainColor,
					CellMinimum.X,
					CellMinimum.Y,
					CellDrawSize.X,
					CellDrawSize.Y);
			}
		}
	}
}

void AGuLiCommanderHUD::DrawMiniMapCameraFrame(
	const float MapX,
	const float MapY,
	const float MapSize,
	const float CameraYawDegrees)
{
	if (!PlayerOwner)
	{
		return;
	}
	const AGuLiCommanderCameraPawn* CameraPawn = PlayerOwner->GetPawn<AGuLiCommanderCameraPawn>();
	if (!CameraPawn)
	{
		return;
	}

	int32 ViewportWidth = 0;
	int32 ViewportHeight = 0;
	PlayerOwner->GetViewportSize(ViewportWidth, ViewportHeight);
	if (ViewportWidth <= 0 || ViewportHeight <= 0)
	{
		return;
	}
	const FBox2D WorldBounds = MiniMapWorldBounds.bIsValid
		? MiniMapWorldBounds
		: GuLiCommanderHUD::GetWorldBounds(GetWorld());
	const FBox2D ScreenBounds(
		FVector2D(MapX, MapY),
		FVector2D(MapX + MapSize, MapY + MapSize));
	const GuLiCommanderMiniMap::FHeadingUpTransform Transform = GuLiCommanderHUD::MakeMiniMapTransform(
			WorldBounds,
			ScreenBounds,
			CameraYawDegrees);
	const FVector2D ViewportCorners[4] = {
		FVector2D(0.0f, 0.0f),
		FVector2D(static_cast<float>(ViewportWidth), 0.0f),
		FVector2D(static_cast<float>(ViewportWidth), static_cast<float>(ViewportHeight)),
		FVector2D(0.0f, static_cast<float>(ViewportHeight))
	};
	FVector2D MiniMapCorners[4];
	const float FocusPlaneZ = CameraPawn->GetActorLocation().Z;
	for (int32 CornerIndex = 0; CornerIndex < UE_ARRAY_COUNT(ViewportCorners); ++CornerIndex)
	{
		FVector RayOrigin;
		FVector RayDirection;
		if (!PlayerOwner->DeprojectScreenPositionToWorld(
			ViewportCorners[CornerIndex].X,
			ViewportCorners[CornerIndex].Y,
			RayOrigin,
			RayDirection)
			|| RayDirection.Z >= -UE_SMALL_NUMBER)
		{
			return;
		}
		const double Distance = (static_cast<double>(FocusPlaneZ) - RayOrigin.Z)
			/ static_cast<double>(RayDirection.Z);
		if (Distance <= 0.0 || !FMath::IsFinite(Distance))
		{
			return;
		}
		const FVector WorldCorner = RayOrigin + RayDirection * Distance;
		MiniMapCorners[CornerIndex] = Transform.WorldToScreenUnchecked(
			FVector2D(WorldCorner.X, WorldCorner.Y));
		if (MiniMapCorners[CornerIndex].ContainsNaN())
		{
			return;
		}
	}

	const FLinearColor FrameColor(1.0f, 0.88f, 0.24f, 0.94f);
	for (int32 CornerIndex = 0; CornerIndex < UE_ARRAY_COUNT(MiniMapCorners); ++CornerIndex)
	{
		FVector2D Start = MiniMapCorners[CornerIndex];
		FVector2D End = MiniMapCorners[(CornerIndex + 1) % UE_ARRAY_COUNT(MiniMapCorners)];
		if (GuLiCommanderMiniMap::ClipLineToScreenBounds(ScreenBounds, Start, End))
		{
			DrawLine(Start.X, Start.Y, End.X, End.Y, FrameColor, 1.8f);
		}
	}

	const FVector CameraLocation = CameraPawn->GetActorLocation();
	FVector2D CameraPoint;
	if (Transform.TryWorldToScreen(
		FVector2D(CameraLocation.X, CameraLocation.Y),
		CameraPoint))
	{
		const float MinimumX = FMath::Max(MapX, static_cast<float>(CameraPoint.X - 2.5));
		const float MinimumY = FMath::Max(MapY, static_cast<float>(CameraPoint.Y - 2.5));
		const float MaximumX = FMath::Min(MapX + MapSize, static_cast<float>(CameraPoint.X + 2.5));
		const float MaximumY = FMath::Min(MapY + MapSize, static_cast<float>(CameraPoint.Y + 2.5));
		if (MaximumX > MinimumX && MaximumY > MinimumY)
		{
			DrawRect(
				FrameColor,
				MinimumX,
				MinimumY,
				MaximumX - MinimumX,
				MaximumY - MinimumY);
		}
	}
}

void AGuLiCommanderHUD::GatherSelectedSoldierValues(
	const FGuLiCommanderSelectionState& Selection,
	TSet<uint32>& OutSelectedSoldierValues) const
{
	OutSelectedSoldierValues.Reset();
	int32 SelectedMemberCount = 0;
	for (const FGuLiControlCohortDescriptor& Cohort : Selection.Cohorts)
	{
		SelectedMemberCount += Cohort.MemberIds.Num();
	}
	OutSelectedSoldierValues.Reserve(SelectedMemberCount);

	for (const FGuLiControlCohortDescriptor& Cohort : Selection.Cohorts)
	{
		for (const FGuLiSoldierId SoldierId : Cohort.MemberIds)
		{
			if (SoldierId.IsValid())
			{
				OutSelectedSoldierValues.Add(SoldierId.Value);
			}
		}
	}
}

void AGuLiCommanderHUD::DrawCohortCards(
	const FGuLiCommanderSelectionState& Selection,
	const FGuLiCommandAck& LastAck,
	const EGuLiTeam LocalTeam)
{
	constexpr float Gap = 6.0f;
	constexpr float CardHeight = 78.0f;
	UFont* Font = GEngine ? GEngine->GetSmallFont() : nullptr;
	const int32 VisibleCount = FMath::Min(
		Selection.Cohorts.Num(),
		GuLiCommanderHUD::MaxVisibleCohortCards);

	if (VisibleCount == 0)
	{
		constexpr float EmptyWidth = 164.0f;
		constexpr float EmptyHeight = 46.0f;
		const float EmptyX = (Canvas->SizeX - EmptyWidth) * 0.5f;
		const float EmptyY = Canvas->SizeY - EmptyHeight - 14.0f;
		DrawRect(FLinearColor(0.025f, 0.03f, 0.045f, 0.76f), EmptyX, EmptyY, EmptyWidth, EmptyHeight);
		DrawRect(GuLiCommanderHUD::GetTeamColor(LocalTeam), EmptyX, EmptyY, EmptyWidth, 3.0f);
		DrawText(
			TEXT("NO COHORTS"),
			FLinearColor(0.52f, 0.62f, 0.68f, 0.9f),
			EmptyX + 25.0f,
			EmptyY + 15.0f,
			Font,
			0.78f,
			false);
		return;
	}

	const float AvailableWidth = FMath::Max(320.0f, Canvas->SizeX - 40.0f);
	const float CardWidth = FMath::Min(
		128.0f,
		(AvailableWidth - Gap * static_cast<float>(VisibleCount - 1))
			/ static_cast<float>(VisibleCount));
	const float StripWidth = CardWidth * static_cast<float>(VisibleCount)
		+ Gap * static_cast<float>(VisibleCount - 1);
	const float StartX = (Canvas->SizeX - StripWidth) * 0.5f;
	const float StartY = Canvas->SizeY - CardHeight - 14.0f;
	const FLinearColor TeamColor = GuLiCommanderHUD::GetTeamColor(LocalTeam);
	const float PrimaryTextScale = CardWidth < 80.0f ? 0.62f : 0.78f;
	const float SecondaryTextScale = CardWidth < 80.0f ? 0.58f : 0.72f;

	for (int32 Index = 0; Index < VisibleCount; ++Index)
	{
		const FGuLiControlCohortDescriptor& Cohort = Selection.Cohorts[Index];
		const float X = StartX + static_cast<float>(Index) * (CardWidth + Gap);
		const bool bUnderstrength = Cohort.MemberIds.Num()
			< static_cast<int32>(GULI_CONTROL_COHORT_TARGET_SIZE);
		const FString AliveLabel = bUnderstrength
			? FString::Printf(TEXT("%u / 25  UNDER"), static_cast<uint32>(Cohort.AliveCount))
			: FString::Printf(TEXT("%u / 25"), static_cast<uint32>(Cohort.AliveCount));
		const GuLiCommanderHUD::FCohortCardStatus Status = GuLiCommanderHUD::BuildCohortCardStatus(
				Cohort,
				LastAck,
				Selection.SelectionRevision);

		DrawRect(FLinearColor(0.08f, 0.07f, 0.025f, 0.88f), X, StartY, CardWidth, CardHeight);
		DrawRect(TeamColor, X, StartY, CardWidth, 4.0f);
		DrawRect(
			FLinearColor(1.0f, 0.82f, 0.04f, 0.95f),
			X,
			StartY + CardHeight - 3.0f,
			CardWidth,
			3.0f);

		DrawText(
			FString::Printf(TEXT("COHORT %u"), Cohort.CohortId.Value),
			FLinearColor::White,
			X + 7.0f,
			StartY + 11.0f,
			Font,
			PrimaryTextScale,
			false);
		DrawText(
			AliveLabel,
			Cohort.AliveCount > 0u
				? FLinearColor(0.72f, 1.0f, 0.72f)
				: FLinearColor(0.8f, 0.15f, 0.12f),
			X + 7.0f,
			StartY + 34.0f,
			Font,
			SecondaryTextScale,
			false);
		DrawText(
			Status.Label,
			Status.Color,
			X + 7.0f,
			StartY + 54.0f,
			Font,
			SecondaryTextScale,
			false);
	}

	if (Selection.Cohorts.Num() > VisibleCount)
	{
		DrawText(
			FString::Printf(TEXT("+%d COHORTS"), Selection.Cohorts.Num() - VisibleCount),
			FLinearColor(0.7f, 0.8f, 0.88f, 0.9f),
			StartX + StripWidth - 84.0f,
			StartY - 18.0f,
			Font,
			0.62f,
			false);
	}
}

void AGuLiCommanderHUD::DrawMiniMap(
	const AGuLiSoldierStateReplicator* SoldierStates,
	AGuLiCommanderPresentationActor* PresentationActor,
	const TSet<uint32>& SelectedSoldierValues)
{
	const FBox2D ScreenBounds = GuLiCommanderHUD::GetMiniMapScreenBounds(*Canvas);
	const float MapX = ScreenBounds.Min.X;
	const float MapY = ScreenBounds.Min.Y;
	const float MapSize = ScreenBounds.GetSize().X;
	const float CameraYawDegrees = GetMiniMapCameraYawDegrees();
	DrawMiniMapTerrain(MapX, MapY, MapSize, CameraYawDegrees);
	const FBox2D WorldBounds = MiniMapWorldBounds.bIsValid
		? MiniMapWorldBounds
		: GuLiCommanderHUD::GetWorldBounds(GetWorld());
	const GuLiCommanderMiniMap::FHeadingUpTransform Transform = GuLiCommanderHUD::MakeMiniMapTransform(
			WorldBounds,
			ScreenBounds,
			CameraYawDegrees);
	const FLinearColor AxisColor(0.3f, 0.55f, 0.4f, 0.22f);
	const auto DrawClippedWorldLine = [this, &Transform, &ScreenBounds, &AxisColor](
			const FVector2D& WorldStart,
			const FVector2D& WorldEnd)
		{
			FVector2D ScreenStart = Transform.WorldToScreenUnchecked(WorldStart);
			FVector2D ScreenEnd = Transform.WorldToScreenUnchecked(WorldEnd);
			if (GuLiCommanderMiniMap::ClipLineToScreenBounds(
				ScreenBounds,
				ScreenStart,
				ScreenEnd))
			{
				DrawLine(
					ScreenStart.X,
					ScreenStart.Y,
					ScreenEnd.X,
					ScreenEnd.Y,
					AxisColor,
					1.0f);
			}
		};
	if (WorldBounds.Min.Y <= 0.0 && WorldBounds.Max.Y >= 0.0)
	{
		DrawClippedWorldLine(
			FVector2D(WorldBounds.Min.X, 0.0),
			FVector2D(WorldBounds.Max.X, 0.0));
	}
	if (WorldBounds.Min.X <= 0.0 && WorldBounds.Max.X >= 0.0)
	{
		DrawClippedWorldLine(
			FVector2D(0.0, WorldBounds.Min.Y),
			FVector2D(0.0, WorldBounds.Max.Y));
	}

	int32 DrawnSoldierCount = 0;
	if (SoldierStates && PresentationActor)
	{
		for (const FGuLiSoldierStateItem& Soldier : SoldierStates->GetItems())
		{
			if (!Soldier.SoldierId.IsValid())
			{
				continue;
			}

			FTransform PresentedTransform;
			if (!PresentationActor->TryGetPresentedSoldierTransform(
				Soldier.SoldierId,
				PresentedTransform)
				|| PresentedTransform.ContainsNaN())
			{
				continue;
			}

			const FVector Location = PresentedTransform.GetLocation();
			FVector2D MapPoint;
			if (!Transform.TryWorldToScreen(
				FVector2D(Location.X, Location.Y),
				MapPoint))
			{
				continue;
			}
			const bool bSelected = Soldier.IsAlive()
				&& SelectedSoldierValues.Contains(Soldier.SoldierId.Value);
			const float PointSize = !Soldier.IsAlive() ? 2.0f : (bSelected ? 4.5f : 2.5f);
			const FLinearColor PointColor = bSelected
				? FLinearColor(1.0f, 0.82f, 0.04f, 1.0f)
				: GuLiCommanderHUD::GetSoldierPointColor(Soldier);
			const float HalfPointSize = PointSize * 0.5f;
			const float MinimumX = FMath::Max(
				MapX,
				static_cast<float>(MapPoint.X) - HalfPointSize);
			const float MinimumY = FMath::Max(
				MapY,
				static_cast<float>(MapPoint.Y) - HalfPointSize);
			const float MaximumX = FMath::Min(
				MapX + MapSize,
				static_cast<float>(MapPoint.X) + HalfPointSize);
			const float MaximumY = FMath::Min(
				MapY + MapSize,
				static_cast<float>(MapPoint.Y) + HalfPointSize);
			if (MaximumX > MinimumX && MaximumY > MinimumY)
			{
				DrawRect(
					PointColor,
					MinimumX,
					MinimumY,
					MaximumX - MinimumX,
					MaximumY - MinimumY);
				++DrawnSoldierCount;
			}
		}
	}
	DrawMiniMapCameraFrame(MapX, MapY, MapSize, CameraYawDegrees);

	const FLinearColor BorderColor(0.15f, 0.7f, 0.75f, 0.72f);
	DrawRect(BorderColor, MapX, MapY, MapSize, 2.0f);
	DrawRect(BorderColor, MapX, MapY + MapSize - 2.0f, MapSize, 2.0f);
	DrawRect(BorderColor, MapX, MapY, 2.0f, MapSize);
	DrawRect(BorderColor, MapX + MapSize - 2.0f, MapY, 2.0f, MapSize);

	DrawText(
		FString::Printf(TEXT("TACTICAL MAP  %d"), DrawnSoldierCount),
		FLinearColor(0.6f, 0.9f, 0.95f),
		MapX + 8.0f,
		MapY + 6.0f,
		GEngine ? GEngine->GetSmallFont() : nullptr,
		0.72f,
		false);
	DrawText(
		TEXT("CLICK TO FOCUS"),
		FLinearColor(0.72f, 0.84f, 0.64f, 0.82f),
		MapX + 8.0f,
		MapY + MapSize - 20.0f,
		GEngine ? GEngine->GetSmallFont() : nullptr,
		0.58f,
		false);
	AddHitBox(
		ScreenBounds.Min,
		ScreenBounds.GetSize(),
		GuLiCommanderHUD::MiniMapHitBox,
		true,
		20);
}

void AGuLiCommanderHUD::DrawSelectionPresetButtons(
	const EGuLiSelectionRadiusPreset ActivePreset)
{
	constexpr float ButtonWidth = 48.0f;
	constexpr float ButtonHeight = 38.0f;
	constexpr float Gap = 7.0f;
	const float StartX = 24.0f;
	const float StartY = Canvas->SizeY - 140.0f;
	const EGuLiSelectionRadiusPreset Presets[3] = {
		EGuLiSelectionRadiusPreset::Small,
		EGuLiSelectionRadiusPreset::Medium,
		EGuLiSelectionRadiusPreset::Large
	};
	const FName HitBoxNames[3] = {
		GuLiCommanderHUD::SmallPresetHitBox,
		GuLiCommanderHUD::MediumPresetHitBox,
		GuLiCommanderHUD::LargePresetHitBox
	};

	for (int32 Index = 0; Index < 3; ++Index)
	{
		const float X = StartX + Index * (ButtonWidth + Gap);
		const bool bActive = ActivePreset == Presets[Index];
		DrawRect(
			bActive ? FLinearColor(0.08f, 0.78f, 0.88f, 0.62f) : FLinearColor(0.04f, 0.08f, 0.11f, 0.58f),
			X,
			StartY,
			ButtonWidth,
			ButtonHeight);
		DrawText(
			FString::FromInt(Index + 1),
			bActive ? FLinearColor::White : FLinearColor(0.65f, 0.78f, 0.82f),
			X + 18.0f,
			StartY + 8.0f,
			GEngine ? GEngine->GetSmallFont() : nullptr,
			0.9f,
			false);
		AddHitBox(FVector2D(X, StartY), FVector2D(ButtonWidth, ButtonHeight), HitBoxNames[Index], true, 10);
	}
}

void AGuLiCommanderHUD::DrawSelectionCircle(
	const AGuLiCommanderPlayerController& Controller)
{
	float CursorX = 0.0f;
	float CursorY = 0.0f;
	if (!Controller.GetMousePosition(CursorX, CursorY)
		|| IsScreenPositionOverCommanderUI(FVector2D(CursorX, CursorY)))
	{
		return;
	}
	FVector GroundLocation;
	if (!Controller.GetCursorGroundLocation(GroundLocation))
	{
		return;
	}

	const float Radius = GuLiCommanderProtocol::GetSelectionRadiusCentimeters(
		Controller.GetSelectionRadiusPreset());
	FVector2D FirstScreenPoint = FVector2D::ZeroVector;
	FVector2D PreviousScreenPoint = FVector2D::ZeroVector;
	bool bHasFirstPoint = false;
	bool bHasPreviousPoint = false;
	for (int32 Segment = 0; Segment < GuLiCommanderHUD::SelectionCircleSegments; ++Segment)
	{
		const float Angle = 2.0f * PI
			* static_cast<float>(Segment)
			/ static_cast<float>(GuLiCommanderHUD::SelectionCircleSegments);
		const FVector WorldPoint = GroundLocation
			+ FVector(FMath::Cos(Angle) * Radius, FMath::Sin(Angle) * Radius, 80.0f);
		FVector2D ScreenPoint;
		const bool bProjected = Controller.ProjectWorldLocationToScreen(WorldPoint, ScreenPoint, false);
		if (bProjected && bHasPreviousPoint)
		{
			DrawLine(
				PreviousScreenPoint.X,
				PreviousScreenPoint.Y,
				ScreenPoint.X,
				ScreenPoint.Y,
				FLinearColor(0.12f, 0.9f, 1.0f, 0.76f),
				2.0f);
		}
		if (bProjected && !bHasFirstPoint)
		{
			FirstScreenPoint = ScreenPoint;
			bHasFirstPoint = true;
		}
		PreviousScreenPoint = ScreenPoint;
		bHasPreviousPoint = bProjected;
	}

	if (bHasFirstPoint && bHasPreviousPoint)
	{
		DrawLine(
			PreviousScreenPoint.X,
			PreviousScreenPoint.Y,
			FirstScreenPoint.X,
			FirstScreenPoint.Y,
			FLinearColor(0.12f, 0.9f, 1.0f, 0.76f),
			2.0f);
	}
	DrawText(FString::Printf(TEXT("范围 %dm  [+] 调整  [7] 框选"), FMath::RoundToInt(Radius / 100.0f)),
		FLinearColor(0.65f, 0.94f, 1.0f, 0.9f),
		FMath::Clamp(CursorX + 20.0f, 8.0f, FMath::Max(8.0f, Canvas->ClipX - 240.0f)),
		FMath::Clamp(CursorY + 24.0f, 8.0f, FMath::Max(8.0f, Canvas->ClipY - 28.0f)),
		GEngine ? GEngine->GetSmallFont() : nullptr);
}

void AGuLiCommanderHUD::DrawSelectionRectangle(const AGuLiCommanderPlayerController& Controller)
{
	FVector2D Start;
	FVector2D End;
	if (!Controller.GetSelectionDragRectangle(Start, End))
	{
		return;
	}
	const FVector2D Minimum(FMath::Min(Start.X, End.X), FMath::Min(Start.Y, End.Y));
	const FVector2D Maximum(FMath::Max(Start.X, End.X), FMath::Max(Start.Y, End.Y));
	const FLinearColor Outline(0.094f, 0.843f, 1.0f, 0.95f);
	DrawRect(FLinearColor(0.05f, 0.75f, 1.0f, 0.075f), Minimum.X, Minimum.Y,
		Maximum.X - Minimum.X, Maximum.Y - Minimum.Y);
	DrawLine(Minimum.X, Minimum.Y, Maximum.X, Minimum.Y, Outline, 1.5f);
	DrawLine(Maximum.X, Minimum.Y, Maximum.X, Maximum.Y, Outline, 1.5f);
	DrawLine(Maximum.X, Maximum.Y, Minimum.X, Maximum.Y, Outline, 1.5f);
	DrawLine(Minimum.X, Maximum.Y, Minimum.X, Minimum.Y, Outline, 1.5f);
}

void AGuLiCommanderHUD::DrawActiveCommandLine(
	const AGuLiCommanderPlayerController& Controller)
{
	// Authoritative accepted routes are one static 1px line per still-selected moving Soldier.
	// The endpoints already carry the real commit location and the actual freely assigned slot.
	if (const UGuLiCommanderNetSyncComponent* NetSync = Controller.GetCommanderNetSyncComponent();
		NetSync && NetSync->IsSoldierStreamReady())
	{
		const AGuLiSoldierStateReplicator* SoldierStates = FindSoldierStateReplicator();
		TMap<FGuLiSoldierId, const FGuLiSoldierStateItem*> StatesBySoldier;
		if (SoldierStates)
		{
			StatesBySoldier.Reserve(SoldierStates->GetItems().Num());
			for (const FGuLiSoldierStateItem& SoldierState : SoldierStates->GetItems())
			{
				if (SoldierState.SoldierId.IsValid())
				{
					StatesBySoldier.Add(SoldierState.SoldierId, &SoldierState);
				}
			}
		}
		TSet<FGuLiSoldierId> SelectedSoldiers;
		SelectedSoldiers.Reserve(
			NetSync->GetSelectionState().Cohorts.Num()
			* static_cast<int32>(GULI_CONTROL_COHORT_TARGET_SIZE));
		for (const FGuLiControlCohortDescriptor& Cohort : NetSync->GetSelectionState().Cohorts)
		{
			for (const FGuLiSoldierId SoldierId : Cohort.MemberIds)
			{
				SelectedSoldiers.Add(SoldierId);
			}
		}
		const FLinearColor StaticRouteColor(0.08f, 0.94f, 0.20f, 0.92f);
		for (const FGuLiMoveEndpointItem& Endpoint : NetSync->GetMoveEndpoints().Items)
		{
			const FGuLiSoldierStateItem* const* SoldierState = StatesBySoldier.Find(Endpoint.SoldierId);
			if (!GuLiCommanderHUD::ShouldDrawMoveEndpoint(
					Endpoint,
					SelectedSoldiers.Contains(Endpoint.SoldierId),
					SoldierState ? *SoldierState : nullptr))
			{
				continue;
			}
			FVector2D StartScreen;
			FVector2D EndScreen;
			if (!Controller.ProjectWorldLocationToScreen(
					FVector(Endpoint.CommandStart), StartScreen, false)
				|| !Controller.ProjectWorldLocationToScreen(
					FVector(Endpoint.FinalDestination), EndScreen, false))
			{
				continue;
			}
			const FBox2D ScreenBounds(
				FVector2D::ZeroVector,
				FVector2D(Canvas->ClipX, Canvas->ClipY));
			if (!GuLiCommanderHUD::ClipScreenLineToBounds(
					ScreenBounds, StartScreen, EndScreen)
				|| FVector2D::Distance(StartScreen, EndScreen) < 2.0f)
			{
				continue;
			}
			DrawLine(
				StartScreen.X,
				StartScreen.Y,
				EndScreen.X,
				EndScreen.Y,
				StaticRouteColor,
				1.0f);
		}
	}

	FVector Start;
	FVector End;
	float Alpha = 0.0f;
	EGuLiCommandLineState State = EGuLiCommandLineState::None;
	if (!Controller.GetActiveCommandLine(Start, End, Alpha, State))
	{
		return;
	}
	if (State == EGuLiCommandLineState::Accepted)
	{
		return;
	}

	FVector2D StartScreen;
	FVector2D EndScreen;
	if (!Controller.ProjectWorldLocationToScreen(Start, StartScreen, false)
		|| !Controller.ProjectWorldLocationToScreen(End, EndScreen, false))
	{
		return;
	}

	const float Time = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;
	FLinearColor LineColor;
	FLinearColor FlowColor;
	FString StateLabel;
	float FlowSpeed = 0.9f;
	switch (State)
	{
	case EGuLiCommandLineState::Pending:
		LineColor = FLinearColor(1.0f, 0.62f, 0.08f, 0.78f * Alpha);
		FlowColor = FLinearColor(1.0f, 0.94f, 0.35f, Alpha);
		StateLabel = TEXT("PENDING");
		FlowSpeed = 1.15f;
		break;
	case EGuLiCommandLineState::Rejected:
		LineColor = FLinearColor(1.0f, 0.08f, 0.035f, 0.86f * Alpha);
		FlowColor = FLinearColor(1.0f, 0.5f, 0.18f, Alpha);
		StateLabel = TEXT("REJECTED");
		FlowSpeed = -0.72f;
		break;
	default:
		return;
	}

	const FVector2D LineDelta = EndScreen - StartScreen;
	const float LineLength = LineDelta.Size();
	if (LineLength <= UE_SMALL_NUMBER)
	{
		return;
	}
	const FVector2D Direction = LineDelta / LineLength;
	const FVector2D Perpendicular(-Direction.Y, Direction.X);
	constexpr int32 DashCount = 24;
	for (int32 DashIndex = 0; DashIndex < DashCount; DashIndex += 2)
	{
		const FVector2D DashStart = FMath::Lerp(
			StartScreen,
			EndScreen,
			static_cast<float>(DashIndex) / static_cast<float>(DashCount));
		const FVector2D DashEnd = FMath::Lerp(
			StartScreen,
			EndScreen,
			static_cast<float>(DashIndex + 1) / static_cast<float>(DashCount));
		DrawLine(DashStart.X, DashStart.Y, DashEnd.X, DashEnd.Y, LineColor, 3.0f);
	}

	const int32 FlowMarkerCount = FMath::Clamp(FMath::RoundToInt(LineLength / 90.0f), 6, 18);
	for (int32 MarkerIndex = 0; MarkerIndex < FlowMarkerCount; ++MarkerIndex)
	{
		const float Along = FMath::Frac(
			Time * FlowSpeed + static_cast<float>(MarkerIndex) / static_cast<float>(FlowMarkerCount));
		const FVector2D Point = FMath::Lerp(StartScreen, EndScreen, Along);
		const float Pulse = 0.62f + 0.38f * FMath::Sin(
			(Along + Time * 1.6f) * 2.0f * PI);
		const FVector2D MarkerDirection = FlowSpeed >= 0.0f ? Direction : -Direction;
		const FVector2D Tail = Point - MarkerDirection * 13.0f;
		const FLinearColor MarkerColor(
			FlowColor.R,
			FlowColor.G,
			FlowColor.B,
			FlowColor.A * Pulse);
		DrawLine(Tail.X, Tail.Y, Point.X, Point.Y, MarkerColor, 3.0f);
		DrawLine(
			Point.X,
			Point.Y,
			Point.X - MarkerDirection.X * 6.0f + Perpendicular.X * 4.0f,
			Point.Y - MarkerDirection.Y * 6.0f + Perpendicular.Y * 4.0f,
			MarkerColor,
			2.0f);
		DrawLine(
			Point.X,
			Point.Y,
			Point.X - MarkerDirection.X * 6.0f - Perpendicular.X * 4.0f,
			Point.Y - MarkerDirection.Y * 6.0f - Perpendicular.Y * 4.0f,
			MarkerColor,
			2.0f);
	}

	const float TargetPulse = 9.0f + 3.0f * (0.5f + 0.5f * FMath::Sin(Time * 6.0f));
	FVector2D PreviousCirclePoint = EndScreen + FVector2D(TargetPulse, 0.0f);
	constexpr int32 TargetCircleSegments = 16;
	for (int32 Segment = 1; Segment <= TargetCircleSegments; ++Segment)
	{
		const float Angle = 2.0f * PI * static_cast<float>(Segment)
			/ static_cast<float>(TargetCircleSegments);
		const FVector2D CirclePoint = EndScreen + FVector2D(
			FMath::Cos(Angle) * TargetPulse,
			FMath::Sin(Angle) * TargetPulse);
		DrawLine(
			PreviousCirclePoint.X,
			PreviousCirclePoint.Y,
			CirclePoint.X,
			CirclePoint.Y,
			FlowColor,
			2.0f);
		PreviousCirclePoint = CirclePoint;
	}
	if (State == EGuLiCommandLineState::Rejected)
	{
		DrawLine(
			EndScreen.X - 7.0f,
			EndScreen.Y - 7.0f,
			EndScreen.X + 7.0f,
			EndScreen.Y + 7.0f,
			FlowColor,
			3.0f);
		DrawLine(
			EndScreen.X - 7.0f,
			EndScreen.Y + 7.0f,
			EndScreen.X + 7.0f,
			EndScreen.Y - 7.0f,
			FlowColor,
			3.0f);
	}
	DrawText(
		StateLabel,
		FlowColor,
		EndScreen.X + 14.0f,
		EndScreen.Y - 18.0f,
		GEngine ? GEngine->GetSmallFont() : nullptr,
		0.72f,
		false);
}
