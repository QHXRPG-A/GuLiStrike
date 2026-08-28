// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Commander/Network/GuLiCommanderTypes.h"
#include "GameFramework/PlayerController.h"
#include "GuLiCommanderPlayerController.generated.h"

class UGuLiCommanderNetSyncComponent;
class AGuLiCommanderCameraPawn;

/** Client-only feedback state for the currently displayed commander move line. */
enum class EGuLiCommandLineState : uint8
{
	None,
	Pending,
	Accepted,
	Rejected
};

/** Owner of commander input and the only actor that exposes commander RPCs. */
UCLASS()
class AGuLiCommanderPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	AGuLiCommanderPlayerController();

	virtual void BeginPlay() override;
	virtual void SetupInputComponent() override;
	virtual void PlayerTick(float DeltaTime) override;

	UFUNCTION(BlueprintPure, Category = "Commander")
	bool CanIssueCommanderOrders() const;

	UFUNCTION(BlueprintPure, Category = "Commander|Network")
	UGuLiCommanderNetSyncComponent* GetCommanderNetSyncComponent() const { return NetSyncComponent; }

	void SetSelectionRadiusPreset(EGuLiSelectionRadiusPreset NewPreset);
	EGuLiSelectionRadiusPreset GetSelectionRadiusPreset() const { return SelectionRadiusPreset; }
	bool GetCursorGroundLocation(FVector& OutLocation) const;
	bool GetActiveCommandLine(
		FVector& OutStart,
		FVector& OutEnd,
		float& OutAlpha,
		EGuLiCommandLineState& OutState) const;

private:
	void SelectAtCursor();
	void MoveAtCursor();
	void ClearSelection();
	void SelectSmallRadius();
	void SelectMediumRadius();
	void SelectLargeRadius();
	void ZoomCameraIn();
	void ZoomCameraOut();
	bool TraceGroundUnderCursor(FVector& OutLocation) const;
	FVector FindConfirmedSelectionCenter() const;
	uint32 AllocateSelectionRequestId();
	uint32 AllocateMoveCommandId();
	void UpdateBootstrapRetry();
	void UpdateCameraInput(float DeltaTime);
	void UpdateAcceptedCommandVisual();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Commander|Network", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UGuLiCommanderNetSyncComponent> NetSyncComponent;

	EGuLiSelectionRadiusPreset SelectionRadiusPreset = EGuLiSelectionRadiusPreset::Small;
	FVector CachedCursorGroundLocation = FVector::ZeroVector;
	bool bHasCursorGroundLocation = false;
	uint32 NextSelectionRequestId = 1u;
	uint32 NextMoveCommandId = 1u;
	uint32 NextBootstrapRequestId = 1u;
	uint32 PendingMoveCommandId = 0u;
	uint32 LastVisualizedMoveCommandId = 0u;
	FVector PendingMoveTarget = FVector::ZeroVector;
	FVector CommandLineStart = FVector::ZeroVector;
	FVector CommandLineEnd = FVector::ZeroVector;
	double CommandLineExpireTime = 0.0;
	float CommandLineFadeDurationSeconds = 0.0f;
	EGuLiCommandLineState CommandLineState = EGuLiCommandLineState::None;
	double NextBootstrapRetryTime = 0.0;
};
