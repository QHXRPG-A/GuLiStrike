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
class UGuLiCombatHealthComponent;
class UAbilitySystemComponent;
class UGuLiGroundMechAttributeSet;
class UGuLiGroundMechRocketComponent;
struct FInputActionValue;
class FGuLiHybridGroundAdapter;
struct FGuLiHybridGroundInput
{
    FVector2D Axis=FVector2D::ZeroVector;
    FVector Aim=FVector::ZeroVector;
    bool bSprint=false,bFire=false,bRocket=false,bHasAim=false;
    bool operator==(const FGuLiHybridGroundInput&) const = default;
};

/** Ground player locomotion and aim. Assembly and animation assets belong to its Blueprint. */
UCLASS()
class GULISTRIKE_API AGuLiGroundMechCharacter : public ACharacter, public IAbilitySystemInterface
{
	GENERATED_BODY()
public:
	AGuLiGroundMechCharacter(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
	virtual void Tick(float DeltaSeconds) override;
	virtual void PostInitializeComponents() override;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Models") int32 ModelId = 4001;
	virtual void PawnClientRestart() override;
	virtual void NotifyControllerChanged() override;
	virtual void UnPossessed() override;
	USkeletalMeshComponent* GetMachinegun() const { return Machinegun; }
	UGuLiGroundMechWeaponComponent* GetWeapon() const { return Weapon; }
    UFUNCTION(BlueprintPure,Category="Mech|Health") UGuLiCombatHealthComponent* GetCombatHealthComponent() const { return CombatHealth; }
    uint32 GetHybridRocketRestoreCount() const { return HybridRocketRestoreCount; }
    void SetCarrierDormant(bool Dormant);
    UFUNCTION(BlueprintPure,Category="Mech|Control") bool IsCarrierDormant() const { return bCarrierDormant; }
	UStaticMeshComponent* GetArmor() const { return Armor; }
	float GetWalkSpeed() const { return WalkSpeed; }
	/** Shared local cursor target for the upper body and weapon, including empty sky. */
	UFUNCTION(BlueprintPure, Category="Mech|Aim") bool GetCursorAimPoint(FVector& OutPoint) const;
	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override { return AbilitySystem; }
	UFUNCTION(BlueprintPure, Category="Mech|Rocket Jump") UGuLiGroundMechRocketComponent* GetRocketJump() const { return RocketJump; }
	UFUNCTION(BlueprintCallable, Category="Mech|Rocket Jump") void SetRocketJumpInput(bool bHeld);
	/** Semantic world input; usable by input bindings and logic-only acceptance without a viewport. */
	UFUNCTION(BlueprintCallable,Category="Mech|Input") bool SubmitGameplayInput(FVector2D Axis,FVector AimPoint,
		bool bSprint,bool bFire,bool bRocket,bool bHasAim=true);

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Mech|Weapon") TObjectPtr<UGuLiGroundMechWeaponComponent> Weapon;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Mech|Health") TObjectPtr<UGuLiCombatHealthComponent> CombatHealth;
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
    UPROPERTY() bool bCarrierDormant=false;
    void ApplyCarrierDormancy();
    UFUNCTION() void HandleCommittedDeath();
	friend class FGuLiHybridGroundAdapter;
    friend class FGuLiHybridGroundControls;
	friend class UGuLiGroundMechWeaponComponent;
	void StopMove(const FInputActionValue& Value);
	void PumpHybridInput();
	void AdvanceHybrid(float DeltaSeconds);
	bool CanUseControls() const;
	void Move(const FInputActionValue& Value);
	void Sprint(const FInputActionValue& Value);
	void StopSprint(const FInputActionValue& Value);
	void Zoom(const FInputActionValue& Value);
	void StartRocketJump(const FInputActionValue& Value);
	void StopRocketJump(const FInputActionValue& Value);
	void RemoveInputContext();

	UPROPERTY()
	uint16 AimYaw = 0;
	float DisplayAimYaw = 0.f;
	float DesiredCameraDistance = 2993.5184f;
	bool bSprintHeld = false;
	TWeakObjectPtr<UEnhancedInputLocalPlayerSubsystem> InputSubsystem;
	FGuLiHybridGroundInput HybridInput,LastHybridInput,SimulationInput;
	uint64 LastHybridInputTick=0;
	bool bHybridInputSent=false;
	bool bSemanticInput=false;
    uint32 HybridRocketRestoreCount=0;
};
