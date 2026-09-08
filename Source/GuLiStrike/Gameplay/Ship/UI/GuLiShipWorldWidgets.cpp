// Copyright Epic Games, Inc. All Rights Reserved.

#include "Gameplay/Ship/UI/GuLiShipWorldWidgets.h"

#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"

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
}

void UGuLiShipWorldStatusWidget::SetStatus(
	const bool bReady,
	const bool bAlive,
	const float Health,
	const float MaxHealth)
{
	if (TXT_Readiness)
	{
		TXT_Readiness->SetText(bAlive
			? (bReady ? NSLOCTEXT("GuLiShipWorldHUD", "Ready", "SHIP / READY")
				: NSLOCTEXT("GuLiShipWorldHUD", "Synchronizing", "SHIP / SYNC"))
			: NSLOCTEXT("GuLiShipWorldHUD", "Offline", "SHIP / OFFLINE"));
	}
	const float HealthFraction = FMath::IsFinite(Health) && FMath::IsFinite(MaxHealth)
		&& MaxHealth > UE_SMALL_NUMBER
		? FMath::Clamp(Health / MaxHealth, 0.0f, 1.0f)
		: 0.0f;
	if (PB_Hull)
	{
		PB_Hull->SetPercent(HealthFraction);
	}
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
