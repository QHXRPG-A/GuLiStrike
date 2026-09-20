#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "GuLiGroundMechCharacter.generated.h"

class UCameraComponent;
class USpringArmComponent;
class UStaticMeshComponent;
class UInputAction;
class UInputMappingContext;
class UEnhancedInputLocalPlayerSubsystem;
class UGuLiGroundMechWeaponComponent;
struct FInputActionValue;

/** Ground player locomotion and aim. Assembly and animation assets belong to its Blueprint. */
UCLASS()
class GULISTRIKE_API AGuLiGroundMechCharacter : public ACharacter
{
	GENERATED_BODY()
public:
	AGuLiGroundMechCharacter(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
	virtual void Tick(float DeltaSeconds) override;
	virtual void PawnClientRestart() override;
	virtual void NotifyControllerChanged() override;
	virtual void UnPossessed() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	USkeletalMeshComponent* GetMachinegun() const { return Machinegun; }
	UGuLiGroundMechWeaponComponent* GetWeapon() const { return Weapon; }

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Mech|Weapon") TObjectPtr<UGuLiGroundMechWeaponComponent> Weapon;
	virtual void BeginPlay() override;
	virtual void EndPlay(EEndPlayReason::Type Reason) override;
	virtual void SetupPlayerInputComponent(UInputComponent* Input) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Mech|Assembly")
	TObjectPtr<USceneComponent> UpperBodyPivot;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Mech|Assembly")
	TObjectPtr<UStaticMeshComponent> Armor;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Mech|Assembly")
	TObjectPtr<UStaticMeshComponent> Shoulder;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Mech|Assembly")
	TObjectPtr<USkeletalMeshComponent> Machinegun;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Mech|Camera")
	TObjectPtr<USpringArmComponent> CameraBoom;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Mech|Camera")
	TObjectPtr<UCameraComponent> Camera;
	UPROPERTY(EditDefaultsOnly, Category="Mech|Input")
	TObjectPtr<UInputMappingContext> MappingContext;
	UPROPERTY(EditDefaultsOnly, Category="Mech|Input")
	TObjectPtr<UInputAction> MoveAction;
	UPROPERTY(EditDefaultsOnly, Category="Mech|Input")
	TObjectPtr<UInputAction> SprintAction;
	UPROPERTY(EditDefaultsOnly, Category="Mech|Input")
	TObjectPtr<UInputAction> ZoomAction;
	UPROPERTY(EditDefaultsOnly, Category="Mech|Movement", meta=(Units="cm/s"))
	float WalkSpeed = 720.f;
	UPROPERTY(EditDefaultsOnly, Category="Mech|Camera", meta=(Units="cm"))
	float StandingHeight = 748.3796f;

private:
	bool CanUseControls() const;
	void Move(const FInputActionValue& Value);
	void Sprint(const FInputActionValue& Value);
	void StopSprint(const FInputActionValue& Value);
	void Zoom(const FInputActionValue& Value);
	void UpdateAim();
	void RemoveInputContext();

	UPROPERTY(Replicated)
	uint16 AimYaw = 0;
	float DisplayAimYaw = 0.f;
	float DesiredCameraDistance = 2993.5184f;
	bool bSprintHeld = false;
	TWeakObjectPtr<UEnhancedInputLocalPlayerSubsystem> InputSubsystem;
};
