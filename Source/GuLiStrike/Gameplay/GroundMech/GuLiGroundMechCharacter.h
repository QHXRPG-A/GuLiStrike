#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "AbilitySystemInterface.h"
#include "Gameplay/GroundMech/Data/GuLiGroundMechConfiguration.h"
#include "Gameplay/GroundMech/Abilities/Blink/GuLiGroundMechBlinkTypes.h"
#include "Gameplay/GroundMech/Movement/GuLiGroundMechTurnTypes.h"
#include "Gameplay/GroundMech/Weapons/GuLiGroundMechWeaponAimTypes.h"
#include "Gameplay/GroundMech/Network/GuLiHybridGroundInputTypes.h"
#include "GuLiGroundMechCharacter.generated.h"

class UCameraComponent;
class USpringArmComponent;
class UStaticMeshComponent;
class UMeshComponent;
class UInputAction;
class UInputMappingContext;
class UEnhancedInputLocalPlayerSubsystem;
class UGuLiGroundMechWeaponComponent;
class UGuLiCombatHealthComponent;
class UAbilitySystemComponent;
class UGuLiGroundMechAttributeSet;
class UGuLiGroundMechRocketComponent;
class UGuLiGroundMechBlinkComponent;
class UGuLiGroundMechCameraComponent;
struct FInputActionValue;
class FGuLiHybridGroundAdapter;
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
	USkeletalMeshComponent* GetMachinegun() const { return bIntegratedBodyMesh ? GetMesh() : Machinegun.Get(); }
	UMeshComponent* GetJetAttachmentMesh() const;
	bool UsesIntegratedBodyMesh() const { return bIntegratedBodyMesh; }
	FRotator GetIntegratedAimRotation() const;
	FVector GetIntegratedAimPoint() const;
	bool IsShoulderLookActive() const;
	void SetShoulderAimHeld(bool bHeld);
	bool IsShoulderAimHeld() const { return bShoulderAimHeld; }
	const FGuLiGroundMechConfiguration& GetMobilityConfiguration() const { return MobilityConfiguration; }
	const FGuLiGroundMechTurnState& GetTurnState() const { return TurnState; }
	const FGuLiGroundMechWeaponAimState& GetWeaponAimState() const { return WeaponAimState; }
	const FGuLiGroundMechBlinkState& GetBlinkState() const;
    UFUNCTION(BlueprintPure,Category="Mech|Blink") uint8 GetBlinkPhase() const;
	float GetBlinkPoseAlpha() const;
	UFUNCTION(BlueprintCallable,Category="Mech|Blink") bool RequestBlink();
    UFUNCTION(BlueprintPure,Category="Mech|Blink") bool CanActivateBlink() const;
    UFUNCTION(BlueprintPure,Category="Mech|Blink") float GetBlinkCooldownRemaining() const;
    UFUNCTION(BlueprintPure,Category="Mech|Blink") float GetBlinkCooldownDuration() const;
    UPROPERTY(BlueprintAssignable,Category="Mech|Blink") FGuLiBlinkStateChanged OnBlinkStateChanged;
	bool HasMovementIntent() const { return !SimulationInput.Axis.IsNearlyZero(); }
    UGuLiGroundMechBlinkComponent* GetBlinkComponent() const { return Blink; }
    UGuLiGroundMechCameraComponent* GetCameraController() const { return MechCameraController; }
    USkeletalMeshComponent* GetSeparateWeaponMesh() const { return Machinegun; }
    const FGuLiHybridGroundInput& GetHybridSimulationInput() const { return SimulationInput; }
    void ResetFacingForBlink(float Yaw);
	UGuLiGroundMechWeaponComponent* GetWeapon() const { return Weapon; }
    UFUNCTION(BlueprintPure,Category="Mech|Health") UGuLiCombatHealthComponent* GetCombatHealthComponent() const { return CombatHealth; }
    uint32 GetHybridRocketRestoreCount() const { return HybridRocketRestoreCount; }
    void SetCarrierDormant(bool Dormant);
    UFUNCTION(BlueprintPure,Category="Mech|Control") bool IsCarrierDormant() const { return bCarrierDormant; }
	UStaticMeshComponent* GetArmor() const { return Armor; }
	float GetWalkSpeed() const { return MobilityConfiguration.bReady ? MobilityConfiguration.Mobility.WalkSpeed : WalkSpeed; }
	/** Shared local cursor target for the upper body and weapon, including empty sky. */
	UFUNCTION(BlueprintPure, Category="Mech|Aim") bool GetCursorAimPoint(FVector& OutPoint) const;
	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override { return AbilitySystem; }
	UFUNCTION(BlueprintPure, Category="Mech|Rocket Jump") UGuLiGroundMechRocketComponent* GetRocketJump() const { return RocketJump; }
	UFUNCTION(BlueprintCallable, Category="Mech|Rocket Jump") void SetRocketJumpInput(bool bHeld);
	/** Semantic world input; usable by input bindings and logic-only acceptance without a viewport. */
	UFUNCTION(BlueprintCallable,Category="Mech|Input") bool SubmitGameplayInput(FVector2D Axis,FVector AimPoint,
		bool bSprint,bool bFire,bool bRocket,bool bHasAim=true,bool bFaceAim=true);

protected:
	/** Full-body art uses its own skeleton sockets; movement, combat and ownership stay unchanged. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Mech|Assembly") bool bIntegratedBodyMesh = false;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Mech|Weapon") TObjectPtr<UGuLiGroundMechWeaponComponent> Weapon;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Mech|Health") TObjectPtr<UGuLiCombatHealthComponent> CombatHealth;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Mech|Abilities") TObjectPtr<UAbilitySystemComponent> AbilitySystem;
	UPROPERTY() TObjectPtr<UGuLiGroundMechAttributeSet> FuelAttributes;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Mech|Rocket Jump") TObjectPtr<UGuLiGroundMechRocketComponent> RocketJump;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Mech|Blink") TObjectPtr<UGuLiGroundMechBlinkComponent> Blink;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Mech|Camera") TObjectPtr<UGuLiGroundMechCameraComponent> MechCameraController;
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
	UPROPERTY(EditDefaultsOnly, Category="Mech|Input") TObjectPtr<UInputAction> LookAction;
	UPROPERTY(EditDefaultsOnly, Category="Mech|Camera") float InitialCameraDistance = 1084.4236f;
	UPROPERTY(EditDefaultsOnly, Category="Mech|Camera") float LookSensitivity = .15f;
	UPROPERTY(EditDefaultsOnly, Category="Mech|Movement", meta=(Units="cm/s"))
	float WalkSpeed = 720.f;
	UPROPERTY(EditDefaultsOnly, Category="Mech|Camera", meta=(Units="cm"))
	float StandingHeight = 748.3796f * (300.f / 460.f);

private:
	UPROPERTY() FGuLiGroundMechConfiguration MobilityConfiguration;
	UPROPERTY() FGuLiGroundMechTurnState TurnState;
	UPROPERTY() FGuLiGroundMechWeaponAimState WeaponAimState;
	void AdvanceFacing(float DeltaSeconds);
	void AdvanceWeaponAim(float DeltaSeconds);
	bool bShoulderAimHeld=false;
	uint32 FacingDisplacementRevision=0;
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
	void Look(const FInputActionValue& Value);
	void Sprint(const FInputActionValue& Value);
	void StopSprint(const FInputActionValue& Value);
	void Zoom(const FInputActionValue& Value);
	void StartRocketJump(const FInputActionValue& Value);
	void StopRocketJump(const FInputActionValue& Value);
	void RemoveInputContext();

	UPROPERTY()
	uint16 AimYaw = 0;
	float DisplayAimYaw = 0.f;
	FVector2D CameraMoveAxis = FVector2D::ZeroVector;
	bool bSprintHeld = false;
	TWeakObjectPtr<UEnhancedInputLocalPlayerSubsystem> InputSubsystem;
	FGuLiHybridGroundInput HybridInput,LastHybridInput,SimulationInput;
	uint64 LastHybridInputTick=0;
	bool bHybridInputSent=false;
	bool bSemanticInput=false;
    uint32 HybridRocketRestoreCount=0;
};
