// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "GuLiCommanderCameraPawn.generated.h"

class UCameraComponent;
class USceneComponent;
class USpringArmComponent;

/** Lightweight perspective RTS camera used by the commander prototype. */
UCLASS()
class GULISTRIKE_API AGuLiCommanderCameraPawn : public APawn
{
	GENERATED_BODY()

public:
	AGuLiCommanderCameraPawn();

	virtual void Tick(float DeltaSeconds) override;

	/** X moves along camera-planar forward and Y along camera-planar right. */
	UFUNCTION(BlueprintCallable, Category = "Commander|Camera")
	void AddPlanarMovement(FVector2D Movement);

	UFUNCTION(BlueprintCallable, Category = "Commander|Camera")
	void AddYawInput(float YawInput);

	/** Negative zooms in and positive zooms out. */
	UFUNCTION(BlueprintCallable, Category = "Commander|Camera")
	void AddZoomInput(float ZoomInput);

	/** Local presentation-only camera focus jump, used by the tactical map. */
	UFUNCTION(BlueprintCallable, Category = "Commander|Camera")
	void JumpToWorldLocation(FVector WorldLocation);

	UFUNCTION(BlueprintPure, Category = "Commander|Camera")
	USpringArmComponent* GetCommanderSpringArm() const { return SpringArm; }

	UFUNCTION(BlueprintPure, Category = "Commander|Camera")
	UCameraComponent* GetCommanderCamera() const { return PerspectiveCamera; }

private:
	bool FindLandscapeHeight(const FVector& AtLocation, float& OutGroundZ) const;

	UPROPERTY(VisibleAnywhere, Category = "Commander|Camera")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, Category = "Commander|Camera")
	TObjectPtr<USpringArmComponent> SpringArm;

	UPROPERTY(VisibleAnywhere, Category = "Commander|Camera")
	TObjectPtr<UCameraComponent> PerspectiveCamera;

	FVector2D PendingPlanarMovement = FVector2D::ZeroVector;
	float PendingYawInput = 0.0f;
	float PendingZoomInput = 0.0f;
	float DesiredArmLength = 80000.0f;
};
