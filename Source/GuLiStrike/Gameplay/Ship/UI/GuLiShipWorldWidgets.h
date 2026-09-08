// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "GuLiShipWorldWidgets.generated.h"

class UProgressBar;
class UTextBlock;

/** Display-only native parent for the top Ship status panel. */
UCLASS(Abstract, Blueprintable)
class GULISTRIKE_API UGuLiShipWorldStatusWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetStatus(bool bReady, bool bAlive, float Health, float MaxHealth);

protected:
	virtual void NativeOnInitialized() override;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> TXT_Readiness;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UProgressBar> PB_Hull;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> TXT_Hull;
};

/** Display-only native parent for the left flight panel. */
UCLASS(Abstract, Blueprintable)
class GULISTRIKE_API UGuLiShipWorldFlightWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetFlight(float CurrentSpeedMetersPerSecond, float MaximumSpeedMetersPerSecond, bool bBoostActive);

protected:
	virtual void NativeOnInitialized() override;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> TXT_CurrentSpeed;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> TXT_MaxSpeed;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> TXT_BoostState;
};

/** Display-only native parent for the right combat panel. */
UCLASS(Abstract, Blueprintable)
class GULISTRIKE_API UGuLiShipWorldCombatWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetCombat(bool bBasicWeaponAvailable, bool bMissileAvailable, bool bMissileCoolingDown);

protected:
	virtual void NativeOnInitialized() override;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> TXT_BasicWeaponState;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> TXT_MissileState;
};

/** Static, non-circular virtual reticle. Its authored Blueprint contains visuals only. */
UCLASS(Abstract, Blueprintable)
class GULISTRIKE_API UGuLiShipWorldReticleWidget : public UUserWidget
{
	GENERATED_BODY()

protected:
	virtual void NativeOnInitialized() override;
};

/** Static bounded-aim frame. Its authored Blueprint contains visuals only. */
UCLASS(Abstract, Blueprintable)
class GULISTRIKE_API UGuLiShipWorldAimBoundsWidget : public UUserWidget
{
	GENERATED_BODY()

protected:
	virtual void NativeOnInitialized() override;
};
