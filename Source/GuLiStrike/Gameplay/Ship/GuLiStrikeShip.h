// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "GuLiStrikeShip.generated.h"

class UStaticMeshComponent;
class USpringArmComponent;
class UCameraComponent;
class UInputAction;
class UInputMappingContext;
class UGuLiStrikeShipPartComponent;
class UGuLiStrikeEnginePart;
class UGuLiStrikeWeaponPart;
struct FInputActionValue;

/** A part the ship spawns with, paired with the hull socket it is installed on */
USTRUCT(BlueprintType)
struct FGuLiStrikeShipDefaultPart
{
	GENERATED_BODY()

	/** Hull socket to install this part on */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Ship")
	FName SocketName;

	/** Part component class to install */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Ship")
	TSubclassOf<UGuLiStrikeShipPartComponent> PartClass;
};

/** Internal bookkeeping for one installed part */
struct FGuLiStrikeInstalledPart
{
	FName SocketName;
	TWeakObjectPtr<UGuLiStrikeShipPartComponent> Part;
};

/**
 *  A player-controlled, modular DIY spaceship.
 *  The hull is a single StaticMesh (sockets on the mesh act as hardpoints);
 *  parts are UStaticMeshComponent subclasses attached to those sockets at
 *  runtime and can be hot-swapped in flight. Thrust and mass of all installed
 *  engine parts drive the flight performance.
 */
UCLASS(abstract)
class AGuLiStrikeShip : public ACharacter
{
	GENERATED_BODY()

	/** Static mesh of the ship hull; its sockets are the part hardpoints */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	UStaticMeshComponent* HullMesh;

	/** Camera boom following behind and above the ship */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	USpringArmComponent* SpringArm;

	/** Player camera */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	UCameraComponent* Camera;

protected:

	/** Mapping context added while this ship is possessed */
	UPROPERTY(EditAnywhere, Category="Input")
	UInputMappingContext* ShipMappingContext;

	/** Forward thrust input action (W) */
	UPROPERTY(EditAnywhere, Category="Input")
	UInputAction* ForwardAction;

	/** Backward thrust input action (S) */
	UPROPERTY(EditAnywhere, Category="Input")
	UInputAction* BackwardAction;

	/** Strafe right input action (D) */
	UPROPERTY(EditAnywhere, Category="Input")
	UInputAction* StrafeRightAction;

	/** Strafe left input action (A) */
	UPROPERTY(EditAnywhere, Category="Input")
	UInputAction* StrafeLeftAction;

	/** Ascend input action (Space) */
	UPROPERTY(EditAnywhere, Category="Input")
	UInputAction* AscendAction;

	/** Descend input action (C) */
	UPROPERTY(EditAnywhere, Category="Input")
	UInputAction* DescendAction;

	/** Roll left input action (Q) */
	UPROPERTY(EditAnywhere, Category="Input")
	UInputAction* RollLeftAction;

	/** Roll right input action (E) */
	UPROPERTY(EditAnywhere, Category="Input")
	UInputAction* RollRightAction;

	/** Mouse look input action (2D delta) */
	UPROPERTY(EditAnywhere, Category="Input")
	UInputAction* LookAction;

	/** Boost input action */
	UPROPERTY(EditAnywhere, Category="Input")
	UInputAction* BoostAction;

	/** Fire input action */
	UPROPERTY(EditAnywhere, Category="Input")
	UInputAction* FireAction;

	/** Cycle engine parts input action */
	UPROPERTY(EditAnywhere, Category="Input")
	UInputAction* CycleEnginesAction;

	/** Cycle weapon parts input action */
	UPROPERTY(EditAnywhere, Category="Input")
	UInputAction* CycleWeaponsAction;

	/** Parts installed on spawn */
	UPROPERTY(EditDefaultsOnly, Category="Ship")
	TArray<FGuLiStrikeShipDefaultPart> DefaultParts;

	/** All part classes the hot-swap keys may cycle through */
	UPROPERTY(EditDefaultsOnly, Category="Ship")
	TArray<TSubclassOf<UGuLiStrikeShipPartComponent>> PartCatalogue;

	/** Mass of the bare hull (without any parts) */
	UPROPERTY(EditDefaultsOnly, Category="Ship|Stats", meta=(ClampMin = 1))
	float HullMass = 100.0f;

	/** Max speed at the nominal thrust-to-mass ratio */
	UPROPERTY(EditDefaultsOnly, Category="Ship|Stats", meta=(ClampMin = 0))
	float BaseMaxSpeed = 2000.0f;

	/** Acceleration at the nominal thrust-to-mass ratio */
	UPROPERTY(EditDefaultsOnly, Category="Ship|Stats", meta=(ClampMin = 0))
	float BaseAcceleration = 800.0f;

	/** Thrust-to-mass ratio that maps to the base flight stats */
	UPROPERTY(EditDefaultsOnly, Category="Ship|Stats", meta=(ClampMin = 0.01))
	float NominalThrustRatio = 6.0f;

	/** Lower clamp for the thrust-ratio speed multiplier */
	UPROPERTY(EditDefaultsOnly, Category="Ship|Stats", meta=(ClampMin = 0.1))
	float SpeedMultiplierMin = 0.5f;

	/** Upper clamp for the thrust-ratio speed multiplier */
	UPROPERTY(EditDefaultsOnly, Category="Ship|Stats", meta=(ClampMin = 1))
	float SpeedMultiplierMax = 2.0f;

	/** Extra thrust multiplier while the boost input is held */
	UPROPERTY(EditDefaultsOnly, Category="Ship|Stats", meta=(ClampMin = 1))
	float BoostThrustMultiplier = 2.0f;

	/** Degrees of pitch per unit of mouse Y delta */
	UPROPERTY(EditDefaultsOnly, Category="Ship|Handling")
	float MousePitchScale = 1.0f;

	/** Degrees of yaw per unit of mouse X delta */
	UPROPERTY(EditDefaultsOnly, Category="Ship|Handling")
	float MouseYawScale = 1.0f;

	/** Degrees of roll per second at full roll input */
	UPROPERTY(EditDefaultsOnly, Category="Ship|Handling", meta=(ClampMin = 0))
	float RollRate = 90.0f;

	/** Offset applied to the hull mesh so the ship is centered on the actor */
	UPROPERTY(EditDefaultsOnly, Category="Ship|Components")
	FVector HullMeshOffset = FVector(0.0f, 0.0f, 700.0f);

	/** Parts currently installed (one per socket) */
	TArray<FGuLiStrikeInstalledPart> InstalledParts;

	/** Whether the boost input is currently held */
	bool bBoosting = false;

	/** Aggregated thrust of all installed engine parts */
	float TotalThrust = 0.0f;

	/** Hull mass plus every installed part mass */
	float TotalMass = 0.0f;

	/** Current max speed after the last stats recompute */
	float CurrentMaxSpeed = 0.0f;

public:

	/** Constructor */
	AGuLiStrikeShip();

protected:

	/** Gameplay initialization */
	virtual void BeginPlay() override;

	/** Possessed by controller initialization */
	virtual void NotifyControllerChanged() override;

	/** Adds input bindings */
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;

	/** Handles forward thrust input */
	void ThrustForward(const FInputActionValue& Value);

	/** Handles backward thrust input */
	void ThrustBackward(const FInputActionValue& Value);

	/** Handles strafe right input */
	void ThrustRight(const FInputActionValue& Value);

	/** Handles strafe left input */
	void ThrustLeft(const FInputActionValue& Value);

	/** Handles ascend input */
	void ThrustUp(const FInputActionValue& Value);

	/** Handles descend input */
	void ThrustDown(const FInputActionValue& Value);

	/** Handles roll left input */
	void RollLeft(const FInputActionValue& Value);

	/** Handles roll right input */
	void RollRight(const FInputActionValue& Value);

	/** Handles mouse look inputs (pitch/yaw) */
	void Look(const FInputActionValue& Value);

	/** Handles boost press */
	void BoostStart(const FInputActionValue& Value);

	/** Handles boost release */
	void BoostEnd(const FInputActionValue& Value);

	/** Handles the fire input */
	void Fire(const FInputActionValue& Value);

	/** Handles the engine hot-swap input */
	void CycleEngines(const FInputActionValue& Value);

	/** Handles the weapon hot-swap input */
	void CycleWeapons(const FInputActionValue& Value);

	/** Allows Blueprint code to react to a part installation */
	UFUNCTION(BlueprintImplementableEvent, Category="Ship", meta=(DisplayName = "Part Installed"))
	void BP_OnPartInstalled(UGuLiStrikeShipPartComponent* Part, FName SocketName);

	/** Allows Blueprint code to react to a part removal */
	UFUNCTION(BlueprintImplementableEvent, Category="Ship", meta=(DisplayName = "Part Uninstalled"))
	void BP_OnPartUninstalled(UGuLiStrikeShipPartComponent* Part, FName SocketName);

	/** Allows Blueprint code to react to flight stat changes */
	UFUNCTION(BlueprintImplementableEvent, Category="Ship", meta=(DisplayName = "Stats Changed"))
	void BP_OnStatsChanged();

public:

	/** Installs a part of the given class on a hull socket, replacing any part already there */
	UFUNCTION(BlueprintCallable, Category="Ship")
	bool InstallPart(TSubclassOf<UGuLiStrikeShipPartComponent> PartClass, FName SocketName);

	/** Removes the part installed on the given socket (if any) */
	UFUNCTION(BlueprintCallable, Category="Ship")
	bool UninstallPart(FName SocketName);

	/** Returns the part currently installed on the given socket (or nullptr) */
	UFUNCTION(BlueprintCallable, Category="Ship")
	UGuLiStrikeShipPartComponent* GetPartAt(FName SocketName) const;

	/** Cycles every installed part of the given base class (e.g. engines) to its next catalogue entry */
	UFUNCTION(BlueprintCallable, Category="Ship")
	void CycleParts(TSubclassOf<UGuLiStrikeShipPartComponent> PartClass);

	/** Fires every installed weapon part that is off cooldown */
	UFUNCTION(BlueprintCallable, Category="Ship")
	void FireInstalledWeapons();

	/** Recomputes flight stats from the installed parts and applies them to the movement component */
	UFUNCTION(BlueprintCallable, Category="Ship")
	void RecomputeStats();

	/** Total thrust of all installed engine parts */
	UFUNCTION(BlueprintPure, Category="Ship|Stats")
	float GetTotalThrust() const { return TotalThrust; }

	/** Hull mass plus all installed part masses */
	UFUNCTION(BlueprintPure, Category="Ship|Stats")
	float GetTotalMass() const { return TotalMass; }

	/** Current max speed derived from the thrust-to-mass ratio */
	UFUNCTION(BlueprintPure, Category="Ship|Stats")
	float GetCurrentMaxSpeed() const { return CurrentMaxSpeed; }

private:

	/** Cycles the socket's part within the catalogue entries compatible with the socket */
	bool CyclePartAtSocket(UClass* PartClass, FName SocketName);
};
