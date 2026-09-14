// Copyright Epic Games, Inc. All Rights Reserved.

#include "Gameplay/Ship/UI/GuLiShipWorldHUDComponent.h"

#include "Battle/Combat/GuLiCombatDamageLedger.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/StaticMeshComponent.h"
#include "Components/WidgetComponent.h"
#include "Containers/StaticArray.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Gameplay/Ship/Abilities/GuLiShipAbilitySystemComponent.h"
#include "Gameplay/Ship/Abilities/GuLiShipAbilityTags.h"
#include "Gameplay/Ship/Aiming/GuLiShipAimComponent.h"
#include "Gameplay/Ship/GuLiShipMovementComponent.h"
#include "Gameplay/Ship/GuLiStrikeShip.h"
#include "Gameplay/Ship/UI/GuLiShipWorldWidgets.h"
#include "GuLiStrike.h"
#include "Materials/MaterialInterface.h"

namespace GuLiShipWorldHUD
{
	const FVector2D StatusDrawSize(420.0, 72.0);
	const FVector2D FlightDrawSize(280.0, 144.0);
	const FVector2D CombatDrawSize(300.0, 164.0);
	const FVector2D ReticleDrawSize(96.0, 96.0);
	const FVector2D InitialAimBoundsDrawSize(768.0, 324.0);

	FVector2D ClampNodeCenter(
		const FVector2D& Desired,
		const FVector2D& DrawSize,
		const FVector2D& ViewportSize,
		const float SafeMargin)
	{
		const FVector2D HalfSize = DrawSize * 0.5;
		const FVector2D Minimum = HalfSize + FVector2D(SafeMargin, SafeMargin);
		const FVector2D Maximum = ViewportSize - Minimum;
		return FVector2D(
			Minimum.X <= Maximum.X ? FMath::Clamp(Desired.X, Minimum.X, Maximum.X) : ViewportSize.X * 0.5,
			Minimum.Y <= Maximum.Y ? FMath::Clamp(Desired.Y, Minimum.Y, Maximum.Y) : ViewportSize.Y * 0.5);
	}

	int32 RoundedNonNegative(const float Value)
	{
		return FMath::IsFinite(Value) ? FMath::Max(0, FMath::RoundToInt(Value)) : 0;
	}
}

UGuLiShipWorldHUDComponent::UGuLiShipWorldHUDComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
	SetIsReplicatedByDefault(false);

	StatusWidgetClass = TSoftClassPtr<UGuLiShipWorldStatusWidget>(FSoftObjectPath(
		TEXT("/Game/Ship/UI/Widgets/World/WBP_ShipWorldStatus.WBP_ShipWorldStatus_C")));
	FlightWidgetClass = TSoftClassPtr<UGuLiShipWorldFlightWidget>(FSoftObjectPath(
		TEXT("/Game/Ship/UI/Widgets/World/WBP_ShipWorldFlight.WBP_ShipWorldFlight_C")));
	CombatWidgetClass = TSoftClassPtr<UGuLiShipWorldCombatWidget>(FSoftObjectPath(
		TEXT("/Game/Ship/UI/Widgets/World/WBP_ShipWorldCombat.WBP_ShipWorldCombat_C")));
	ReticleWidgetClass = TSoftClassPtr<UGuLiShipWorldReticleWidget>(FSoftObjectPath(
		TEXT("/Game/Ship/UI/Widgets/World/WBP_ShipWorldReticle.WBP_ShipWorldReticle_C")));
	AimBoundsWidgetClass = TSoftClassPtr<UGuLiShipWorldAimBoundsWidget>(FSoftObjectPath(
		TEXT("/Game/Ship/UI/Widgets/World/WBP_ShipWorldAimBounds.WBP_ShipWorldAimBounds_C")));
	NoDepthWidgetMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(
		TEXT("/Game/Ship/UI/Materials/M_UI_ShipWorld_NoDepth.M_UI_ShipWorld_NoDepth")));
}

void UGuLiShipWorldHUDComponent::BeginPlay()
{
	Super::BeginPlay();
	OwnerShip = Cast<AGuLiStrikeShip>(GetOwner());
	HandleOwnerControllerChanged();
}

void UGuLiShipWorldHUDComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	DestroyRuntimeWidgets();
	OwnerShip.Reset();
	Super::EndPlay(EndPlayReason);
}

void UGuLiShipWorldHUDComponent::TickComponent(
	const float DeltaTime,
	const ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	(void)DeltaTime;

	if (!ShouldPresent())
	{
		HideAllNodes();
		return;
	}

	SetStatusVisible(UpdateStatusViewportLayout());
	if (!UpdateNodeTransforms())
	{
		HideWorldNodes();
	}
	RefreshDataIfDue(false);
}

void UGuLiShipWorldHUDComponent::HandleOwnerControllerChanged()
{
	AGuLiStrikeShip* Ship = OwnerShip.Get();
	if (!Ship)
	{
		Ship = Cast<AGuLiStrikeShip>(GetOwner());
		OwnerShip = Ship;
	}

	APlayerController* Controller = Ship ? Cast<APlayerController>(Ship->GetController()) : nullptr;
	if (!Ship || GetNetMode() == NM_DedicatedServer || !Ship->IsLocallyControlled()
		|| !Controller || !Controller->IsLocalController())
	{
		DestroyRuntimeWidgets();
		return;
	}
	if (LocalController.IsValid() && LocalController.Get() != Controller)
	{
		DestroyRuntimeWidgets();
	}

	LocalController = Controller;
	HullMesh = Ship->GetHullMeshComponent();
	BindAimComponent(Ship->GetShipAimComponent());
	CreateRuntimeWidgets();
	SetComponentTickEnabled(
		StatusWidget || FlightNode || CombatNode || ReticleNode || AimBoundsNode);
	RefreshDataIfDue(true);
}

UWidgetComponent* UGuLiShipWorldHUDComponent::CreateWidgetNode(
	const FName ComponentName,
	const TSubclassOf<UUserWidget> WidgetClass,
	const FVector2D& DrawSize,
	const int32 TranslucencySortPriority)
{
	AGuLiStrikeShip* Ship = OwnerShip.Get();
	APlayerController* Controller = LocalController.Get();
	if (!Ship || !Controller || !WidgetClass)
	{
		return nullptr;
	}

	UWidgetComponent* Node = NewObject<UWidgetComponent>(Ship, ComponentName, RF_Transient);
	if (!Node)
	{
		return nullptr;
	}
	Ship->AddInstanceComponent(Node);
	Node->SetupAttachment(Ship->GetRootComponent());
	Node->SetAbsolute(true, true, true);
	Node->SetMobility(EComponentMobility::Movable);
	Node->SetIsReplicated(false);
	Node->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Node->SetGenerateOverlapEvents(false);
	Node->SetCanEverAffectNavigation(false);
	Node->SetCastShadow(false);
	Node->SetAffectDistanceFieldLighting(false);
	Node->SetAffectDynamicIndirectLighting(false);
	Node->SetVisibleInRayTracing(false);
	Node->SetReceivesDecals(false);
	Node->SetWidgetSpace(EWidgetSpace::World);
	Node->SetGeometryMode(EWidgetGeometryMode::Plane);
	Node->SetBlendMode(EWidgetBlendMode::Transparent);
	// The node is explicitly rotated so its front face follows the camera every
	// frame.  Keeping it one-sided avoids relying on a second material shader
	// permutation during the first PIE run after generated assets are loaded.
	Node->SetTwoSided(false);
	Node->SetPivot(FVector2D(0.5, 0.5));
	Node->SetDrawAtDesiredSize(false);
	Node->SetDrawSize(DrawSize);
	Node->SetManuallyRedraw(true);
	Node->SetTickWhenOffscreen(false);
	Node->SetTickMode(ETickMode::Disabled);
	Node->SetWindowFocusable(false);
	Node->SetOwnerPlayer(Controller->GetLocalPlayer());
	Node->SetWidgetClass(WidgetClass);
	Node->SetTranslucentSortPriority(TranslucencySortPriority);
	Node->SetHiddenInGame(true);
	Node->SetVisibility(false, true);
	Node->RegisterComponent();
	Node->InitWidget();
	if (UMaterialInterface* NoDepthMaterial = NoDepthWidgetMaterial.LoadSynchronous())
	{
		Node->SetMaterial(0, NoDepthMaterial);
	}
	return Node;
}

void UGuLiShipWorldHUDComponent::CreateRuntimeWidgets()
{
	if (StatusWidget || FlightNode || CombatNode || ReticleNode || AimBoundsNode)
	{
		return;
	}
	APlayerController* Controller = LocalController.Get();
	if (!Controller)
	{
		return;
	}

	UClass* StatusClass = StatusWidgetClass.LoadSynchronous();
	UClass* FlightClass = FlightWidgetClass.LoadSynchronous();
	UClass* CombatClass = CombatWidgetClass.LoadSynchronous();
	UClass* ReticleClass = ReticleWidgetClass.LoadSynchronous();
	UClass* AimBoundsClass = AimBoundsWidgetClass.LoadSynchronous();
	if ((!StatusClass || !FlightClass || !CombatClass || !ReticleClass || !AimBoundsClass)
		&& !bLoggedMissingAssets)
	{
		UE_LOG(LogGuLiStrike, Warning,
			TEXT("Ship world HUD widget assets are incomplete; only available nodes will be created."));
		bLoggedMissingAssets = true;
	}

	if (StatusClass)
	{
		StatusWidget = CreateWidget<UGuLiShipWorldStatusWidget>(Controller, StatusClass);
		if (StatusWidget)
		{
			StatusWidget->SetIsFocusable(false);
			StatusWidget->SetVisibility(ESlateVisibility::HitTestInvisible);
			StatusWidget->SetDesiredSizeInViewport(GuLiShipWorldHUD::StatusDrawSize);
			StatusWidget->SetAlignmentInViewport(FVector2D(0.5, 0.0));
			if (!StatusWidget->AddToPlayerScreen(20))
			{
				StatusWidget = nullptr;
			}
		}
	}
	FlightNode = CreateWidgetNode(
		TEXT("ShipWorldHUD_Flight"), FlightClass, GuLiShipWorldHUD::FlightDrawSize, 1);
	CombatNode = CreateWidgetNode(
		TEXT("ShipWorldHUD_Combat"), CombatClass, GuLiShipWorldHUD::CombatDrawSize, 1);
	AimBoundsNode = CreateWidgetNode(
		TEXT("ShipWorldHUD_AimBounds"), AimBoundsClass, GuLiShipWorldHUD::InitialAimBoundsDrawSize, 0);
	ReticleNode = CreateWidgetNode(
		TEXT("ShipWorldHUD_Reticle"), ReticleClass, GuLiShipWorldHUD::ReticleDrawSize, 2);

	bHasStatusSnapshot = false;
	bHasFlightSnapshot = false;
	bHasCombatSnapshot = false;
	HandleAimModeChanged(BoundAimComponent.IsValid()
		? BoundAimComponent->GetReticleMode()
		: EGuLiShipReticleMode::None);
}

void UGuLiShipWorldHUDComponent::DestroyRuntimeWidgets()
{
	BindAimComponent(nullptr);
	SetComponentTickEnabled(false);

	auto DestroyNode = [](TObjectPtr<UWidgetComponent>& Node)
	{
		if (Node)
		{
			Node->SetVisibility(false, true);
			Node->SetWidget(nullptr);
			Node->DestroyComponent();
			Node = nullptr;
		}
	};
	if (StatusWidget)
	{
		StatusWidget->SetVisibility(ESlateVisibility::Collapsed);
		StatusWidget->RemoveFromParent();
		StatusWidget = nullptr;
	}
	DestroyNode(FlightNode);
	DestroyNode(CombatNode);
	DestroyNode(ReticleNode);
	DestroyNode(AimBoundsNode);
	LocalController.Reset();
	HullMesh.Reset();
	bHasStatusSnapshot = false;
	bHasFlightSnapshot = false;
	bHasCombatSnapshot = false;
}

void UGuLiShipWorldHUDComponent::BindAimComponent(UGuLiShipAimComponent* AimComponent)
{
	if (BoundAimComponent.Get() == AimComponent)
	{
		return;
	}
	if (UGuLiShipAimComponent* Previous = BoundAimComponent.Get())
	{
		Previous->OnAimModeChanged().Remove(AimModeChangedHandle);
	}
	AimModeChangedHandle.Reset();
	BoundAimComponent = AimComponent;
	if (AimComponent)
	{
		AimModeChangedHandle = AimComponent->OnAimModeChanged().AddUObject(
			this, &UGuLiShipWorldHUDComponent::HandleAimModeChanged);
	}
}

void UGuLiShipWorldHUDComponent::HandleAimModeChanged(const EGuLiShipReticleMode NewMode)
{
	SetNodeVisible(ReticleNode, NewMode != EGuLiShipReticleMode::None && ShouldPresent());
	SetNodeVisible(AimBoundsNode, NewMode == EGuLiShipReticleMode::Bounded && ShouldPresent());
}

void UGuLiShipWorldHUDComponent::RefreshDataIfDue(const bool bForce)
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	const double Now = static_cast<double>(World->GetTimeSeconds());
	if (!bForce && Now < NextDataRefreshTimeSeconds)
	{
		return;
	}
	NextDataRefreshTimeSeconds = Now + FMath::Max(0.02f, DataRefreshIntervalSeconds);
	RefreshData();
}

void UGuLiShipWorldHUDComponent::RefreshData()
{
	AGuLiStrikeShip* Ship = OwnerShip.Get();
	if (!Ship)
	{
		return;
	}

	UGuLiCombatHealthComponent* HealthComponent = Ship->GetCombatHealthComponent();
	const FGuLiCombatHealthState HealthState = HealthComponent
		? HealthComponent->GetHealthState()
		: FGuLiCombatHealthState();
	const bool bAlive = HealthComponent && HealthComponent->IsAlive();
	const bool bReady = Ship->IsShipReady();
	const int32 Health = GuLiShipWorldHUD::RoundedNonNegative(HealthState.Health);
	const int32 MaximumHealth = GuLiShipWorldHUD::RoundedNonNegative(HealthState.MaxHealth);
	if (!bHasStatusSnapshot || bCachedReady != bReady || bCachedAlive != bAlive
		|| CachedHealth != Health || CachedMaximumHealth != MaximumHealth)
	{
		if (StatusWidget)
		{
			StatusWidget->SetStatus(bReady, bAlive, HealthState.Health, HealthState.MaxHealth);
		}
		bCachedReady = bReady;
		bCachedAlive = bAlive;
		CachedHealth = Health;
		CachedMaximumHealth = MaximumHealth;
		bHasStatusSnapshot = true;
	}

	const UGuLiShipMovementComponent* Movement = Ship->GetShipMovement();
	const int32 CurrentSpeed = GuLiShipWorldHUD::RoundedNonNegative(Ship->GetVelocity().Size() / 100.0f);
	const int32 MaximumSpeed = GuLiShipWorldHUD::RoundedNonNegative(Ship->GetCurrentMaxSpeed() / 100.0f);
	const bool bBoost = Movement && Movement->IsBoostActive();
	if (!bHasFlightSnapshot || CachedCurrentSpeed != CurrentSpeed
		|| CachedMaximumSpeed != MaximumSpeed || bCachedBoost != bBoost)
	{
		if (FlightNode)
		{
			if (UGuLiShipWorldFlightWidget* Widget =
				Cast<UGuLiShipWorldFlightWidget>(FlightNode->GetUserWidgetObject()))
			{
				Widget->SetFlight(CurrentSpeed, MaximumSpeed, bBoost);
				FlightNode->RequestRenderUpdate();
			}
		}
		CachedCurrentSpeed = CurrentSpeed;
		CachedMaximumSpeed = MaximumSpeed;
		bCachedBoost = bBoost;
		bHasFlightSnapshot = true;
	}

	const UGuLiShipAbilitySystemComponent* AbilitySystem = Ship->GetShipAbilitySystemComponent();
	const bool bBasicAvailable = AbilitySystem
		&& (AbilitySystem->IsAbilityGranted(TAG_GuLi_ShipAbility_Weapon_Basic_Auto)
			|| AbilitySystem->IsAbilityGranted(TAG_GuLi_ShipAbility_Weapon_Wingman_MachineGun));
	const bool bMissileAvailable = AbilitySystem
		&& (AbilitySystem->IsAbilityGranted(TAG_GuLi_ShipAbility_Weapon_Missile_Salvo)
			|| AbilitySystem->IsAbilityGranted(TAG_GuLi_ShipAbility_Weapon_Wingman_GroundMissile));
	const bool bMissileCoolingDown = AbilitySystem && AbilitySystem->IsMissileCooldownActive();
	if (!bHasCombatSnapshot || bCachedBasicAvailable != bBasicAvailable
		|| bCachedMissileAvailable != bMissileAvailable
		|| bCachedMissileCoolingDown != bMissileCoolingDown)
	{
		if (CombatNode)
		{
			if (UGuLiShipWorldCombatWidget* Widget =
				Cast<UGuLiShipWorldCombatWidget>(CombatNode->GetUserWidgetObject()))
			{
				Widget->SetCombat(bBasicAvailable, bMissileAvailable, bMissileCoolingDown);
				CombatNode->RequestRenderUpdate();
			}
		}
		bCachedBasicAvailable = bBasicAvailable;
		bCachedMissileAvailable = bMissileAvailable;
		bCachedMissileCoolingDown = bMissileCoolingDown;
		bHasCombatSnapshot = true;
	}
}

bool UGuLiShipWorldHUDComponent::UpdateStatusViewportLayout()
{
	UGuLiShipWorldStatusWidget* Widget = StatusWidget;
	APlayerController* Controller = LocalController.Get();
	ULocalPlayer* Player = Controller ? Controller->GetLocalPlayer() : nullptr;
	if (!Widget || !Controller || !Player)
	{
		return false;
	}

	const FVector2D VisibleViewportPixels =
		GuLiShipReticle::ResolveVisibleViewportSize(*Controller);
	const FVector2D PlayerViewportPixels(
		VisibleViewportPixels.X * FMath::Clamp(Player->Size.X, 0.0, 1.0),
		VisibleViewportPixels.Y * FMath::Clamp(Player->Size.Y, 0.0, 1.0));
	const float ViewportScale = UWidgetLayoutLibrary::GetViewportScale(Controller);
	if (PlayerViewportPixels.X <= 0.0 || PlayerViewportPixels.Y <= 0.0
		|| !FMath::IsFinite(ViewportScale) || ViewportScale <= UE_SMALL_NUMBER)
	{
		return false;
	}

	// AddToPlayerScreen constrains this canvas to the owning local player's split-screen layer.
	// SetPositionInViewport receives logical Slate units here, so DPI removal must stay disabled.
	const FVector2D LogicalViewportSize = PlayerViewportPixels / ViewportScale;
	Widget->SetDesiredSizeInViewport(GuLiShipWorldHUD::StatusDrawSize);
	Widget->SetAlignmentInViewport(FVector2D(0.5, 0.0));
	Widget->SetPositionInViewport(
		FVector2D(LogicalViewportSize.X * 0.5, ViewportSafeMarginPixels), false);
	return true;
}

bool UGuLiShipWorldHUDComponent::UpdateNodeTransforms()
{
	APlayerController* Controller = LocalController.Get();
	if (!Controller || !Controller->PlayerCameraManager)
	{
		return false;
	}

	int32 ViewportWidth = 0;
	int32 ViewportHeight = 0;
	Controller->GetViewportSize(ViewportWidth, ViewportHeight);
	if (ViewportWidth <= 0 || ViewportHeight <= 0)
	{
		return false;
	}
	const FVector2D VisibleViewportSize =
		GuLiShipReticle::ResolveVisibleViewportSize(*Controller);
	if (VisibleViewportSize.X <= 0.0 || VisibleViewportSize.Y <= 0.0)
	{
		return false;
	}
	const FVector CameraLocation = Controller->PlayerCameraManager->GetCameraLocation();
	const FRotator CameraRotation = Controller->PlayerCameraManager->GetCameraRotation();
	const FVector CameraForward = CameraRotation.Vector();
	FVector2D HullMinimum;
	FVector2D HullMaximum;
	FVector PlaneOrigin;
	if (!ProjectHullBounds(
		*Controller, CameraLocation, CameraForward, HullMinimum, HullMaximum, PlaneOrigin))
	{
		return false;
	}

	const float PlaneDistance = FVector::DotProduct(PlaneOrigin - CameraLocation, CameraForward);
	const float CentimetersPerPixel = GuLiShipReticle::CalculateCentimetersPerPixel(
		PlaneDistance,
		Controller->PlayerCameraManager->GetFOVAngle(),
		ViewportWidth,
		ViewportHeight);
	if (CentimetersPerPixel <= UE_SMALL_NUMBER)
	{
		return false;
	}

	const FVector2D HullCenter = (HullMinimum + HullMaximum) * 0.5;
	const FVector2D FlightPosition = GuLiShipWorldHUD::ClampNodeCenter(
		FVector2D(HullMinimum.X - HullPanelGapPixels - GuLiShipWorldHUD::FlightDrawSize.X * 0.5, HullCenter.Y),
		GuLiShipWorldHUD::FlightDrawSize, VisibleViewportSize, ViewportSafeMarginPixels);
	const FVector2D CombatPosition = GuLiShipWorldHUD::ClampNodeCenter(
		FVector2D(HullMaximum.X + HullPanelGapPixels + GuLiShipWorldHUD::CombatDrawSize.X * 0.5, HullCenter.Y),
		GuLiShipWorldHUD::CombatDrawSize, VisibleViewportSize, ViewportSafeMarginPixels);

	bool bValid = true;
	if (FlightNode)
	{
		bValid &= SetNodeTransform(*FlightNode, *Controller, FlightPosition, PlaneOrigin,
			CameraForward, CameraRotation, CentimetersPerPixel);
	}
	if (CombatNode)
	{
		bValid &= SetNodeTransform(*CombatNode, *Controller, CombatPosition, PlaneOrigin,
			CameraForward, CameraRotation, CentimetersPerPixel);
	}
	if (!bValid)
	{
		return false;
	}

	SetNodeVisible(FlightNode, FlightNode != nullptr);
	SetNodeVisible(CombatNode, CombatNode != nullptr);

	const UGuLiShipAimComponent* AimComponent = BoundAimComponent.Get();
	const EGuLiShipReticleMode AimMode = AimComponent
		? AimComponent->GetReticleMode()
		: EGuLiShipReticleMode::None;
	if (AimMode != EGuLiShipReticleMode::None && ReticleNode)
	{
		const FVector2D ReticleScreenPosition =
			AimComponent->GetReticleScreenPositionNormalized() * VisibleViewportSize;
		if (!SetNodeTransform(*ReticleNode, *Controller, ReticleScreenPosition, PlaneOrigin,
			CameraForward, CameraRotation, CentimetersPerPixel))
		{
			return false;
		}
		SetNodeVisible(ReticleNode, true);
	}
	else
	{
		SetNodeVisible(ReticleNode, false);
	}

	if (AimMode == EGuLiShipReticleMode::Bounded && AimBoundsNode)
	{
		const FGuLiShipReticleConfig Config = AimComponent->GetActiveReticleConfig();
		if (!Config.IsValid())
		{
			SetNodeVisible(AimBoundsNode, false);
		}
		else
		{
			const FVector2D AimBoundsDrawSize(
				FMath::Max(1.0, FMath::RoundToDouble(Config.BoundsSizeNormalized.X * VisibleViewportSize.X)),
				FMath::Max(1.0, FMath::RoundToDouble(Config.BoundsSizeNormalized.Y * VisibleViewportSize.Y)));
			if (!AimBoundsNode->GetDrawSize().Equals(AimBoundsDrawSize, 0.5))
			{
				AimBoundsNode->SetDrawSize(AimBoundsDrawSize);
				AimBoundsNode->RequestRenderUpdate();
			}
			const FVector2D AimBoundsScreenPosition = Config.BoundsCenterNormalized * VisibleViewportSize;
			if (!SetNodeTransform(*AimBoundsNode, *Controller, AimBoundsScreenPosition, PlaneOrigin,
				CameraForward, CameraRotation, CentimetersPerPixel))
			{
				return false;
			}
			SetNodeVisible(AimBoundsNode, true);
		}
	}
	else
	{
		SetNodeVisible(AimBoundsNode, false);
	}
	return true;
}

bool UGuLiShipWorldHUDComponent::ProjectHullBounds(
	APlayerController& Controller,
	const FVector& CameraLocation,
	const FVector& CameraForward,
	FVector2D& OutMinimum,
	FVector2D& OutMaximum,
	FVector& OutPlaneOrigin) const
{
	const UStaticMeshComponent* Mesh = HullMesh.Get();
	if (!Mesh)
	{
		return false;
	}
	const FBox WorldBounds = Mesh->Bounds.GetBox();
	OutPlaneOrigin = WorldBounds.GetCenter();
	const double CenterDepth = FVector::DotProduct(
		OutPlaneOrigin - CameraLocation, CameraForward);
	if (!FMath::IsFinite(CenterDepth) || CenterDepth <= UE_SMALL_NUMBER)
	{
		return false;
	}

	OutMinimum = FVector2D(DBL_MAX, DBL_MAX);
	OutMaximum = FVector2D(-DBL_MAX, -DBL_MAX);
	TStaticArray<FVector, 8> Corners;
	TStaticArray<double, 8> CornerDepths;
	int32 CornerIndex = 0;
	for (int32 X = 0; X < 2; ++X)
	{
		for (int32 Y = 0; Y < 2; ++Y)
		{
			for (int32 Z = 0; Z < 2; ++Z)
			{
				Corners[CornerIndex] = FVector(
					X == 0 ? WorldBounds.Min.X : WorldBounds.Max.X,
					Y == 0 ? WorldBounds.Min.Y : WorldBounds.Max.Y,
					Z == 0 ? WorldBounds.Min.Z : WorldBounds.Max.Z);
				CornerDepths[CornerIndex] = FVector::DotProduct(
					Corners[CornerIndex] - CameraLocation, CameraForward);
				++CornerIndex;
			}
		}
	}

	int32 ProjectedPointCount = 0;
	auto AccumulateProjectedPoint =
		[&Controller, &OutMinimum, &OutMaximum, &ProjectedPointCount](const FVector& WorldPoint)
		{
			FVector2D ScreenPosition;
			if (!Controller.ProjectWorldLocationToScreen(WorldPoint, ScreenPosition, false)
				|| !FMath::IsFinite(ScreenPosition.X) || !FMath::IsFinite(ScreenPosition.Y))
			{
				return;
			}
			OutMinimum.X = FMath::Min(OutMinimum.X, ScreenPosition.X);
			OutMinimum.Y = FMath::Min(OutMinimum.Y, ScreenPosition.Y);
			OutMaximum.X = FMath::Max(OutMaximum.X, ScreenPosition.X);
			OutMaximum.Y = FMath::Max(OutMaximum.Y, ScreenPosition.Y);
			++ProjectedPointCount;
		};

	// A close orbit can put some axis-aligned hull corners behind the camera even
	// while the visible hull centre is still in front.  Treating one failed corner
	// as a failed hull hid every HUD node.  Project all visible corners and clip
	// crossing box edges against a small camera-facing depth instead.
	constexpr double MinimumProjectionDepth = 10.0;
	for (int32 Index = 0; Index < Corners.Num(); ++Index)
	{
		if (CornerDepths[Index] >= MinimumProjectionDepth)
		{
			AccumulateProjectedPoint(Corners[Index]);
		}
	}

	static constexpr int32 BoxEdges[12][2] =
	{
		{0, 1}, {0, 2}, {0, 4}, {1, 3}, {1, 5}, {2, 3},
		{2, 6}, {3, 7}, {4, 5}, {4, 6}, {5, 7}, {6, 7}
	};
	for (const int32 (&Edge)[2] : BoxEdges)
	{
		const int32 A = Edge[0];
		const int32 B = Edge[1];
		const bool bAInFront = CornerDepths[A] >= MinimumProjectionDepth;
		const bool bBInFront = CornerDepths[B] >= MinimumProjectionDepth;
		if (bAInFront == bBInFront)
		{
			continue;
		}
		const double DepthRange = CornerDepths[B] - CornerDepths[A];
		if (FMath::IsNearlyZero(DepthRange))
		{
			continue;
		}
		const double Alpha = FMath::Clamp(
			(MinimumProjectionDepth - CornerDepths[A]) / DepthRange, 0.0, 1.0);
		AccumulateProjectedPoint(FMath::Lerp(Corners[A], Corners[B], Alpha));
	}

	if (ProjectedPointCount == 0)
	{
		AccumulateProjectedPoint(OutPlaneOrigin);
	}
	return ProjectedPointCount > 0
		&& OutMinimum.X <= OutMaximum.X && OutMinimum.Y <= OutMaximum.Y;
}

bool UGuLiShipWorldHUDComponent::SetNodeTransform(
	UWidgetComponent& Node,
	APlayerController& Controller,
	const FVector2D& ScreenPosition,
	const FVector& PlaneOrigin,
	const FVector& CameraForward,
	const FRotator& CameraRotation,
	const float CentimetersPerPixel) const
{
	FVector RayOrigin;
	FVector RayDirection;
	if (!Controller.DeprojectScreenPositionToWorld(
		ScreenPosition.X, ScreenPosition.Y, RayOrigin, RayDirection))
	{
		return false;
	}
	const float Denominator = FVector::DotProduct(RayDirection, CameraForward);
	if (Denominator <= UE_SMALL_NUMBER)
	{
		return false;
	}
	const float RayDistance = FVector::DotProduct(PlaneOrigin - RayOrigin, CameraForward) / Denominator;
	if (!FMath::IsFinite(RayDistance) || RayDistance <= UE_SMALL_NUMBER)
	{
		return false;
	}

	Node.SetWorldLocation(RayOrigin + RayDirection * RayDistance);
	Node.SetWorldRotation(GuLiShipReticle::CalculateScreenFacingWidgetRotation(CameraRotation));
	Node.SetWorldScale3D(FVector(CentimetersPerPixel));
	return true;
}

void UGuLiShipWorldHUDComponent::SetNodeVisible(UWidgetComponent* Node, const bool bVisible) const
{
	if (!Node)
	{
		return;
	}
	const bool bWasVisible = Node->IsVisible() && !Node->bHiddenInGame;
	Node->SetHiddenInGame(!bVisible);
	Node->SetVisibility(bVisible, true);
	if (bVisible && !bWasVisible)
	{
		Node->RequestRenderUpdate();
	}
}

void UGuLiShipWorldHUDComponent::SetStatusVisible(const bool bVisible) const
{
	if (StatusWidget)
	{
		StatusWidget->SetVisibility(
			bVisible ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
}

void UGuLiShipWorldHUDComponent::HideWorldNodes() const
{
	SetNodeVisible(FlightNode, false);
	SetNodeVisible(CombatNode, false);
	SetNodeVisible(ReticleNode, false);
	SetNodeVisible(AimBoundsNode, false);
}

void UGuLiShipWorldHUDComponent::HideAllNodes() const
{
	SetStatusVisible(false);
	HideWorldNodes();
}

bool UGuLiShipWorldHUDComponent::ShouldPresent() const
{
	const AGuLiStrikeShip* Ship = OwnerShip.Get();
	const APlayerController* Controller = LocalController.Get();
	const UGuLiCombatHealthComponent* Health = Ship ? Ship->GetCombatHealthComponent() : nullptr;
	const bool bExplicitlyDead = Health && Health->GetHealthState().IsWellFormed()
		&& !Health->IsAlive();
	return Ship && Controller && Controller->IsLocalController() && Ship->IsLocallyControlled()
		&& GetNetMode() != NM_DedicatedServer && !bExplicitlyDead;
}
