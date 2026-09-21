#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "AbilitySystemInterface.h"
#include "GuLiGroundMechCharacter.generated.h"

class UCameraComponent;
class USpringArmComponent;
class UStaticMeshComponent;
class UInputAction;
class UInputMappingContext;
class UEnhancedInputLocalPlayerSubsystem;
class UGuLiGroundMechWeaponComponent;
class UAbilitySystemComponent;
class UGuLiGroundMechAttributeSet;
class UGuLiGroundMechRocketComponent;
struct FInputActionValue;

/** Ground player locomotion and aim. Assembly and animation assets belong to its Blueprint. */
UCLASS()
class GULISTRIKE_API AGuLiGroundMechCharacter : public ACharacter, public IAbilitySystemInterface
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
	UStaticMeshComponent* GetArmor() const { return Armor; }
	float GetWalkSpeed() const { return WalkSpeed; }
	/** Shared local cursor target for the upper body and weapon, including empty sky. */
	UFUNCTION(BlueprintPure, Category="Mech|Aim") bool GetCursorAimPoint(FVector& OutPoint) const;
	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override { return AbilitySystem; }
	UFUNCTION(BlueprintPure, Category="Mech|Rocket Jump") UGuLiGroundMechRocketComponent* GetRocketJump() const { return RocketJump; }
	UFUNCTION(BlueprintCallable, Category="Mech|Rocket Jump") void SetRocketJumpInput(bool bHeld);

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Mech|Weapon") TObjectPtr<UGuLiGroundMechWeaponComponent> Weapon;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Mech|Abilities") TObjectPtr<UAbilitySystemComponent> AbilitySystem;
	UPROPERTY() TObjectPtr<UGuLiGroundMechAttributeSet> FuelAttributes;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Mech|Rocket Jump") TObjectPtr<UGuLiGroundMechRocketComponent> RocketJump;
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
	UPROPERTY(EditDefaultsOnly, Category="Mech|Input") TObjectPtr<UInputAction> RocketJumpAction;
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
	void StartRocketJump(const FInputActionValue& Value);
	void StopRocketJump(const FInputActionValue& Value);
	void UpdateAim();
	void RemoveInputContext();

	UPROPERTY(Replicated)
	uint16 AimYaw = 0;
	float DisplayAimYaw = 0.f;
	float DesiredCameraDistance = 2993.5184f;
	bool bSprintHeld = false;
	TWeakObjectPtr<UEnhancedInputLocalPlayerSubsystem> InputSubsystem;
};
