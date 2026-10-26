#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "AbilitySystemInterface.h"
#include "Gameplay/GroundMech/GuLiGroundMechConfiguration.h"
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
struct FInputActionValue;
class FGuLiHybridGroundAdapter;
enum class EGuLiGroundMoveSpace : uint8 { World, Body };
enum class EGuLiGroundWeaponAimMode : uint8 { Target, Forward };
struct FGuLiHybridGroundInput
{
    FVector2D Axis=FVector2D::ZeroVector;
    FVector Aim=FVector::ZeroVector;
    bool bSprint=false,bFire=false,bRocket=false,bHasAim=false,bFaceAim=false;
    // Forward movement can face the camera without entering right-button shoulder aim.
    bool bFaceCamera=false;
    uint16 CameraYaw=0;
    EGuLiGroundMoveSpace MoveSpace=EGuLiGroundMoveSpace::World;
    EGuLiGroundWeaponAimMode WeaponAimMode=EGuLiGroundWeaponAimMode::Target;
    uint32 BlinkRequest=0;
    // Captured with BlinkRequest; independent of the continuously updated W-facing yaw.
    uint16 BlinkYaw=0;
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
	const FGuLiGroundMechBlinkState& GetBlinkState() const { return BlinkState; }
	float GetBlinkPoseAlpha() const;
	UFUNCTION(BlueprintCallable,Category="Mech|Blink") bool RequestBlink();
	bool HasMovementIntent() const { return !SimulationInput.Axis.IsNearlyZero(); }
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
	UPROPERTY() FGuLiGroundMechBlinkState BlinkState;
	void AdvanceBlink(float DeltaSeconds);
	void PresentBlink();
	int32 PresentedBlinkSerial=0;
	int32 PresentedBlinkAfterimages=0;
	FVector PresentedBlinkEnd=FVector::ZeroVector;
	void UpdateMechCamera(float DeltaSeconds);
	void AddBlinkCameraDisplacement(const FVector& Delta, bool bFirst);
	void ResetBlinkCamera();
	// Local presentation only. SocketOffset keeps the collision sweep rooted at the pawn.
	FVector BlinkCameraOffset=FVector::ZeroVector, BlinkCameraVelocity=FVector::ZeroVector;
	float BlinkCameraHoldRemaining=0.f, BlinkCameraSettleSeconds=.45f;
	uint32 BlinkCameraDisplacementRevision=0;
	uint32 BlinkSimulationDisplacementRevision=0;
	void AdvanceFacing(float DeltaSeconds);
	void AdvanceWeaponAim(float DeltaSeconds);
	bool bShoulderAimHeld=false;
	float CameraModeAlpha=0.f;
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
	float DesiredCameraDistance = 1084.4236f;
	FVector2D CameraMoveAxis = FVector2D::ZeroVector;
	bool bSprintHeld = false;
	TWeakObjectPtr<UEnhancedInputLocalPlayerSubsystem> InputSubsystem;
	FGuLiHybridGroundInput HybridInput,LastHybridInput,SimulationInput;
	uint64 LastHybridInputTick=0;
	bool bHybridInputSent=false;
	bool bSemanticInput=false;
    uint32 HybridRocketRestoreCount=0;
};
