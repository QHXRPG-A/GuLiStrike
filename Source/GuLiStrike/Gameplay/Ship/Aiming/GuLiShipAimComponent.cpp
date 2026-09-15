// Copyright Epic Games, Inc. All Rights Reserved.

#include "Gameplay/Ship/Aiming/GuLiShipAimComponent.h"

#include "Gameplay/Ship/GuLiStrikeShip.h"
#include "Gameplay/Ship/Capabilities/GuLiShipHangarCapabilityComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "HAL/IConsoleManager.h"
#include "Engine/World.h"
#include "GuLiStrike.h"

UGuLiShipAimComponent::UGuLiShipAimComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
	SetIsReplicatedByDefault(false);
}

void UGuLiShipAimComponent::BeginPlay()
{
	Super::BeginPlay();
	OwnerShip = Cast<AGuLiStrikeShip>(GetOwner());
	HandleOwnerControllerChanged();
}

void UGuLiShipAimComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ResetAimState();
	UnbindHangar();
	LocalController.Reset();
	ShipSpringArm.Reset();
	OwnerShip.Reset();
	Super::EndPlay(EndPlayReason);
}

void UGuLiShipAimComponent::TickComponent(
	const float DeltaTime,
	const ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	(void)DeltaTime;

	if (!IsAiming())
	{
		SetComponentTickEnabled(false);
		return;
	}

	if (!CurrentClaimOwner.IsValid()
#if !UE_BUILD_SHIPPING
		&& DebugMode == EGuLiShipReticleMode::None
#endif
	)
	{
		ApplyActiveClaim();
		if (!IsAiming())
		{
			return;
		}
	}
	AlignCameraToShip();
}

void UGuLiShipAimComponent::HandleOwnerControllerChanged()
{
	AGuLiStrikeShip* Ship = OwnerShip.Get();
	if (!Ship)
	{
		Ship = Cast<AGuLiStrikeShip>(GetOwner());
		OwnerShip = Ship;
	}

	APlayerController* Controller = Ship ? Cast<APlayerController>(Ship->GetController()) : nullptr;
	if (!Ship || !Ship->IsLocallyControlled() || !Controller || !Controller->IsLocalController())
	{
		ResetAimState();
		UnbindHangar();
		LocalController.Reset();
		ShipSpringArm.Reset();
		return;
	}

	LocalController = Controller;
	ShipSpringArm = Ship->FindComponentByClass<USpringArmComponent>();
	BindHangar(Ship->GetHangarCapability());
}

void UGuLiShipAimComponent::ResetForOwnerUnavailable()
{
	ResetAimState();
}

void UGuLiShipAimComponent::BindHangar(UGuLiShipHangarCapabilityComponent* Hangar)
{
	if (BoundHangar.Get() == Hangar) return;
	UnbindHangar();
	BoundHangar = Hangar;
	if (Hangar)
		PresentationHandle = Hangar->OnAbilityPresentationChanged().AddUObject(this, &ThisClass::HandleAbilityPresentation);
}

void UGuLiShipAimComponent::UnbindHangar()
{
	if (auto* Hangar = BoundHangar.Get()) Hangar->OnAbilityPresentationChanged().Remove(PresentationHandle);
	PresentationHandle.Reset();
	BoundHangar.Reset();
	ActivationStack.Reset(); PresentationOwners.Reset();
	ApplyActiveClaim();
}

void UGuLiShipAimComponent::HandleAbilityPresentation(FGameplayTag AbilityId, bool bActive)
{
	if (!bActive)
	{
		if (auto* Owner = PresentationOwners.Find(AbilityId))
		{
			ActivationStack.End(*Owner); PresentationOwners.Remove(AbilityId); ApplyActiveClaim();
		}
		return;
	}
	const auto& Grant = *BoundHangar->FindConfiguredGrant(AbilityId);
	const auto Mode = GuLiShipReticle::ResolveMode(Grant.PresentationTags);
	if (Mode == EGuLiShipReticleMode::None) return;
	auto& Owner = PresentationOwners.FindOrAdd(AbilityId);
	if (!Owner) Owner = NewObject<UObject>(this);
	ActivationStack.Activate(Owner, Mode, Grant.ReticleConfig);
	ApplyActiveClaim();
}

void UGuLiShipAimComponent::ApplyActiveClaim()
{
	EGuLiShipReticleMode NewMode = EGuLiShipReticleMode::None;
	FGuLiShipReticleConfig NewConfig;
	UObject* NewOwner = nullptr;

#if !UE_BUILD_SHIPPING
	if (DebugMode != EGuLiShipReticleMode::None)
	{
		NewMode = DebugMode;
		NewOwner = this;
	}
	else
#endif
	if (const FGuLiShipReticleActivation* Active = ActivationStack.GetActive())
	{
		NewMode = Active->Mode;
		NewConfig = Active->Config;
		NewOwner = Active->Owner.Get();
	}

	const bool bClaimChanged = CurrentClaimOwner.Get() != NewOwner
		|| ActiveMode != NewMode
		|| !ActiveConfig.BoundsCenterNormalized.Equals(NewConfig.BoundsCenterNormalized)
		|| !ActiveConfig.BoundsSizeNormalized.Equals(NewConfig.BoundsSizeNormalized);
	if (!bClaimChanged)
	{
		return;
	}

	const bool bWasAiming = IsAiming();
	CurrentClaimOwner = NewOwner;
	ActiveMode = NewMode;
	ActiveConfig = NewConfig;
	if (IsAiming())
	{
		ReticlePositionNormalized = ActiveMode == EGuLiShipReticleMode::Bounded
			? ActiveConfig.BoundsCenterNormalized
			: FVector2D(0.5, 0.5);
		if (!bWasAiming)
		{
			EnterAimMode();
		}
		else
		{
			AlignCameraToShip();
		}
	}
	else if (bWasAiming)
	{
		ExitAimMode();
	}
	AimModeChangedDelegate.Broadcast(ActiveMode);
}

void UGuLiShipAimComponent::EnterAimMode()
{
	if (APlayerController* Controller = LocalController.Get())
	{
		bSavedShowMouseCursor = Controller->bShowMouseCursor;
		bHasSavedMouseCursorState = true;
		Controller->SetShowMouseCursor(false);
	}
	if (USpringArmComponent* SpringArm = ShipSpringArm.Get())
	{
		bSavedCameraRotationLagEnabled = SpringArm->bEnableCameraRotationLag;
		bHasSavedCameraRotationLagState = true;
		SpringArm->bEnableCameraRotationLag = false;
	}
	AlignCameraToShip();
	SetComponentTickEnabled(true);
}

void UGuLiShipAimComponent::ExitAimMode()
{
	if (USpringArmComponent* SpringArm = ShipSpringArm.Get();
		SpringArm && bHasSavedCameraRotationLagState)
	{
		SpringArm->bEnableCameraRotationLag = bSavedCameraRotationLagEnabled;
	}
	bHasSavedCameraRotationLagState = false;
	if (APlayerController* Controller = LocalController.Get();
		Controller && bHasSavedMouseCursorState)
	{
		Controller->SetShowMouseCursor(bSavedShowMouseCursor);
	}
	bHasSavedMouseCursorState = false;
	SetComponentTickEnabled(false);
}

void UGuLiShipAimComponent::ResetAimState()
{
	const bool bWasAiming = IsAiming();
	ActivationStack.Reset();
#if !UE_BUILD_SHIPPING
	DebugMode = EGuLiShipReticleMode::None;
#endif
	CurrentClaimOwner.Reset();
	ActiveMode = EGuLiShipReticleMode::None;
	ActiveConfig = FGuLiShipReticleConfig();
	ReticlePositionNormalized = FVector2D(0.5, 0.5);
	if (bWasAiming)
	{
		ExitAimMode();
		AimModeChangedDelegate.Broadcast(ActiveMode);
	}
}

void UGuLiShipAimComponent::AlignCameraToShip() const
{
	const AGuLiStrikeShip* Ship = OwnerShip.Get();
	USpringArmComponent* SpringArm = ShipSpringArm.Get();
	if (!Ship || !SpringArm)
	{
		return;
	}
	if (APlayerController* Controller = LocalController.Get())
	{
		Controller->SetShowMouseCursor(false);
	}
	SpringArm->SetWorldRotation(GuLiShipReticle::CalculateAlignedCameraRotation(
		Ship->GetActorForwardVector(), Ship->GetActorUpVector()));
}

bool UGuLiShipAimComponent::ConsumeLookInput(const FVector2D& InputDelta)
{
	if (!IsAiming())
	{
		return false;
	}

	FVector2D ViewportSize;
	if (!GetViewportSize(ViewportSize))
	{
		return true;
	}

	FVector2D PixelPosition = ReticlePositionNormalized * ViewportSize;
	PixelPosition += FVector2D(InputDelta.X, -InputDelta.Y) * ReticleInputSensitivity;
	const float Padding = ReticleHalfSizePixels
		+ (ActiveMode == EGuLiShipReticleMode::Bounded
			? BoundedFrameInnerPaddingPixels
			: SafeViewportMarginPixels);
	const FBox2D UsableBounds = GuLiShipReticle::BuildUsablePixelBounds(
		ActiveMode, ActiveConfig, ViewportSize, Padding);
	PixelPosition = GuLiShipReticle::ClampPixelPosition(PixelPosition, UsableBounds);
	ReticlePositionNormalized = PixelPosition / ViewportSize;
	return true;
}

bool UGuLiShipAimComponent::GetReticleAimRay(
	FVector& OutOrigin,
	FVector& OutDirection) const
{
	OutOrigin = FVector::ZeroVector;
	OutDirection = FVector::ZeroVector;
	FVector2D ViewportSize;
	APlayerController* Controller = LocalController.Get();
	if (!IsAiming() || !Controller || !GetViewportSize(ViewportSize))
	{
		return false;
	}
	const FVector2D PixelPosition = ReticlePositionNormalized * ViewportSize;
	return Controller->DeprojectScreenPositionToWorld(
		PixelPosition.X, PixelPosition.Y, OutOrigin, OutDirection)
		&& !OutDirection.IsNearlyZero();
}

FBox2D UGuLiShipAimComponent::GetActiveBoundsNormalized() const
{
	return ActiveMode == EGuLiShipReticleMode::Bounded && ActiveConfig.IsValid()
		? ActiveConfig.GetNormalizedBounds()
		: FBox2D(FVector2D::ZeroVector, FVector2D(1.0, 1.0));
}

bool UGuLiShipAimComponent::GetViewportSize(FVector2D& OutViewportSize) const
{
	OutViewportSize = FVector2D::ZeroVector;
	if (const APlayerController* Controller = LocalController.Get())
	{
		OutViewportSize = GuLiShipReticle::ResolveVisibleViewportSize(*Controller);
	}
	return OutViewportSize.X > 0.0 && OutViewportSize.Y > 0.0;
}

#if !UE_BUILD_SHIPPING
void UGuLiShipAimComponent::SetDebugReticleMode(const EGuLiShipReticleMode Mode)
{
	DebugMode = Mode;
	ApplyActiveClaim();
}

namespace
{
	UGuLiShipAimComponent* FindLocalShipAim(UWorld* World)
	{
		APlayerController* Controller = World ? World->GetFirstPlayerController() : nullptr;
		AGuLiStrikeShip* Ship = Controller ? Cast<AGuLiStrikeShip>(Controller->GetPawn()) : nullptr;
		return Ship ? Ship->GetShipAimComponent() : nullptr;
	}

	void SetShipReticleDebugMode(const TArray<FString>& Args, UWorld* World)
	{
		if (!World)
		{
			return;
		}
		int32 RequestedMode = 0;
		if (!Args.IsEmpty())
		{
			RequestedMode = FCString::Atoi(*Args[0]);
		}
		const EGuLiShipReticleMode Mode = RequestedMode == 1
			? EGuLiShipReticleMode::Omni
			: RequestedMode == 2
				? EGuLiShipReticleMode::Bounded
				: EGuLiShipReticleMode::None;
		if (UGuLiShipAimComponent* Aim = FindLocalShipAim(World))
		{
			Aim->SetDebugReticleMode(Mode);
		}
	}

	void MoveShipReticleDebug(const TArray<FString>& Args, UWorld* World)
	{
		float DeltaX = 0.0f;
		float DeltaY = 0.0f;
		if (Args.Num() != 2
			|| !LexTryParseString(DeltaX, *Args[0])
			|| !LexTryParseString(DeltaY, *Args[1]))
		{
			UE_LOG(LogGuLiStrike, Error,
				TEXT("Usage: GuLi.Ship.WorldHUD.DebugReticleMove DeltaX DeltaY"));
			return;
		}
		if (UGuLiShipAimComponent* Aim = FindLocalShipAim(World))
		{
			Aim->ConsumeLookInput(FVector2D(DeltaX, DeltaY));
		}
	}

	FAutoConsoleCommandWithWorldAndArgs DebugShipReticleCommand(
		TEXT("GuLi.Ship.WorldHUD.DebugReticle"),
		TEXT("0=off, 1=omni, 2=bounded. Non-shipping visual QA only."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&SetShipReticleDebugMode));

	FAutoConsoleCommandWithWorldAndArgs DebugShipReticleMoveCommand(
		TEXT("GuLi.Ship.WorldHUD.DebugReticleMove"),
		TEXT("Apply one virtual LookAction delta to the non-shipping debug reticle."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&MoveShipReticleDebug));
}
#endif
