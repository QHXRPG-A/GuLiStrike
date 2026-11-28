// Copyright Epic Games, Inc. All Rights Reserved.

#include "Gameplay/Ship/UI/GuLiShipWorldWidgets.h"

#include "Components/ProgressBar.h"
#include "UI/GuLiSegmentedHealthBar.h"
#include "Components/TextBlock.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Blueprint/WidgetTree.h"

namespace GuLiShipWorldWidgets
{
	void ConfigureDisplayOnlyWidget(UUserWidget& Widget)
	{
		Widget.SetIsFocusable(false);
		Widget.SetVisibility(ESlateVisibility::HitTestInvisible);
	}

	FText RoundedNumber(const float Value)
	{
		FNumberFormattingOptions Formatting;
		Formatting.SetMaximumFractionalDigits(0);
		Formatting.SetMinimumFractionalDigits(0);
		return FText::AsNumber(FMath::Max(0.0f, Value), &Formatting);
	}
}

void UGuLiShipWorldStatusWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	GuLiShipWorldWidgets::ConfigureDisplayOnlyWidget(*this);
	// Existing cooked status panels supply their authored canvas placement through PB_Hull.
	if (PB_Hull && !PB_Hull->IsA<UGuLiSegmentedHealthBar>())
	{
		auto* Canvas = Cast<UCanvasPanel>(PB_Hull->GetParent());
		auto* SlotData = Cast<UCanvasPanelSlot>(PB_Hull->Slot);
		if (Canvas && SlotData)
		{
			const auto Layout = SlotData->GetLayout();
			const int32 Order = SlotData->GetZOrder();
			const bool AutoSize = SlotData->GetAutoSize();
			PB_Hull->RemoveFromParent();
			PB_Hull = WidgetTree->ConstructWidget<UGuLiSegmentedHealthBar>();
			auto* Replacement = Canvas->AddChildToCanvas(PB_Hull);
			Replacement->SetLayout(Layout);
			Replacement->SetAutoSize(AutoSize);
			Replacement->SetZOrder(Order);
		}
	}
}

void UGuLiShipWorldStatusWidget::SetStatus(
	const bool bReady,
	const bool bAlive,
	const float Health,
	const float MaxHealth,
	const float Shield,
	const float MaxShield)
{
	if (TXT_Readiness)
	{
		TXT_Readiness->SetText(bAlive
			? (bReady ? NSLOCTEXT("GuLiShipWorldHUD", "Ready", "SHIP / READY")
				: NSLOCTEXT("GuLiShipWorldHUD", "Synchronizing", "SHIP / SYNC"))
			: NSLOCTEXT("GuLiShipWorldHUD", "Offline", "SHIP / OFFLINE"));
	}
	if (auto* Hull = Cast<UGuLiSegmentedHealthBar>(PB_Hull))
		Hull->SetHealth(Health, MaxHealth, Shield, MaxShield);
	if (TXT_Hull)
	{
		TXT_Hull->SetText(FText::Format(
			NSLOCTEXT("GuLiShipWorldHUD", "HullFormat", "HULL {0} / {1}"),
			GuLiShipWorldWidgets::RoundedNumber(Health),
			GuLiShipWorldWidgets::RoundedNumber(MaxHealth)));
	}
}

void UGuLiShipWorldFlightWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	GuLiShipWorldWidgets::ConfigureDisplayOnlyWidget(*this);
}

void UGuLiShipWorldFlightWidget::SetFlight(
	const float CurrentSpeedMetersPerSecond,
	const float MaximumSpeedMetersPerSecond,
	const bool bBoostActive)
{
	if (TXT_CurrentSpeed)
	{
		TXT_CurrentSpeed->SetText(FText::Format(
			NSLOCTEXT("GuLiShipWorldHUD", "SpeedFormat", "SPEED {0} m/s"),
			GuLiShipWorldWidgets::RoundedNumber(CurrentSpeedMetersPerSecond)));
	}
	if (TXT_MaxSpeed)
	{
		TXT_MaxSpeed->SetText(FText::Format(
			NSLOCTEXT("GuLiShipWorldHUD", "MaximumSpeedFormat", "MAX {0} m/s"),
			GuLiShipWorldWidgets::RoundedNumber(MaximumSpeedMetersPerSecond)));
	}
	if (TXT_BoostState)
	{
		TXT_BoostState->SetText(bBoostActive
			? NSLOCTEXT("GuLiShipWorldHUD", "BoostActive", "BOOST / ACTIVE")
			: NSLOCTEXT("GuLiShipWorldHUD", "BoostStandby", "BOOST / STANDBY"));
	}
}

void UGuLiShipWorldCombatWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	GuLiShipWorldWidgets::ConfigureDisplayOnlyWidget(*this);
}

void UGuLiShipWorldCombatWidget::SetCombat(
	const bool bBasicWeaponAvailable,
	const bool bMissileAvailable,
	const bool bMissileCoolingDown)
{
	if (TXT_BasicWeaponState)
	{
		TXT_BasicWeaponState->SetText(bBasicWeaponAvailable
			? NSLOCTEXT("GuLiShipWorldHUD", "BasicReady", "BASIC / READY")
			: NSLOCTEXT("GuLiShipWorldHUD", "BasicUnavailable", "BASIC / UNAVAILABLE"));
	}
	if (TXT_MissileState)
	{
		TXT_MissileState->SetText(!bMissileAvailable
			? NSLOCTEXT("GuLiShipWorldHUD", "MissileUnavailable", "MISSILE / UNAVAILABLE")
			: bMissileCoolingDown
				? NSLOCTEXT("GuLiShipWorldHUD", "MissileCooling", "MISSILE / COOLDOWN")
				: NSLOCTEXT("GuLiShipWorldHUD", "MissileReady", "MISSILE / READY"));
	}
}

void UGuLiShipWorldReticleWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	GuLiShipWorldWidgets::ConfigureDisplayOnlyWidget(*this);
}

void UGuLiShipWorldAimBoundsWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	GuLiShipWorldWidgets::ConfigureDisplayOnlyWidget(*this);
}
