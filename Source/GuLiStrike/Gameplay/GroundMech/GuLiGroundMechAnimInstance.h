#pragma once
#include "Animation/AnimInstance.h"
#include "Gameplay/GroundMech/AnimNode_GuLiGroundMechGrounding.h"
#include "GuLiGroundMechAnimInstance.generated.h"

class UBlendSpace;
class UCurveFloat;
class UAnimSequence;

/** Read-only movement snapshot. The editable Animation Blueprint owns all pose evaluation. */
UCLASS(Transient, Blueprintable)
class GULISTRIKE_API UGuLiGroundMechAnimInstance : public UAnimInstance
{
	GENERATED_BODY()
public:
	UPROPERTY(BlueprintReadOnly,Category="Ground Tuning") FGuLiMechGroundingSnapshot Grounding;
	/** X=pose alpha, Y=rear arm angle, Z=upper-body lean. Evaluated after aim/IK. */
	UPROPERTY(BlueprintReadOnly,Category="Ground Tuning") FVector BlinkPose=FVector::ZeroVector;
	UPROPERTY(BlueprintReadOnly,Category="Ground Tuning") uint8 BlinkPhase=0;
	UPROPERTY(BlueprintReadOnly,Category="Ground Tuning") float BlinkPhaseProgress=0.f;
	UPROPERTY(BlueprintReadOnly,Category="Ground Tuning") float WalkPlayRate=1.f;
	UPROPERTY(BlueprintReadOnly,Category="Ground Tuning") float RunPlayRate=1.f;
	UPROPERTY(BlueprintReadOnly,Category="Ground Tuning") float JumpLoopPlayRate=1.f;
	UPROPERTY(BlueprintReadOnly,Category="Ground Tuning") float GaitTransitionSeconds=.18f;
	UPROPERTY(BlueprintReadOnly,Category="Ground Tuning") float GunYawLimit=0.f;
	UPROPERTY(BlueprintReadOnly,Category="Ground Tuning") float GunPitchUpLimit=0.f;
	UPROPERTY(BlueprintReadOnly,Category="Ground Tuning") float GunPitchDownLimit=0.f;
	UPROPERTY(BlueprintReadOnly,Category="Ground Tuning") TObjectPtr<UBlendSpace> WalkBlendSpace;
	UPROPERTY(BlueprintReadOnly,Category="Ground Tuning") TObjectPtr<UBlendSpace> RunBlendSpace;
	UPROPERTY(BlueprintReadOnly,Category="Ground Tuning") TObjectPtr<UAnimSequence> JumpStartSequence;
	UPROPERTY(BlueprintReadOnly,Category="Ground Tuning") TObjectPtr<UAnimSequence> JumpLoopSequence;
	UPROPERTY(BlueprintReadOnly,Category="Ground Tuning") TObjectPtr<UAnimSequence> ShootSequence;
	UPROPERTY(BlueprintReadOnly,Category="Ground Tuning") TObjectPtr<UAnimSequence> MovingTurnLeft;
	UPROPERTY(BlueprintReadOnly,Category="Ground Tuning") TObjectPtr<UAnimSequence> MovingTurnRight;
	UPROPERTY(BlueprintReadOnly,Category="Ground Tuning") float MovingTurnLeftWeight=0.f;
	UPROPERTY(BlueprintReadOnly,Category="Ground Tuning") float MovingTurnRightWeight=0.f;
	UPROPERTY(BlueprintReadOnly,Category="Ground Tuning") float MovingTurnPlayRate=1.f;
	virtual void NativeInitializeAnimation() override;
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Locomotion")
	TObjectPtr<UBlendSpace> LocomotionBlendSpace;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Locomotion")
	float RunSpeed = 1440.f;
	UPROPERTY(BlueprintReadOnly, Category="Locomotion")
	float Speed = 0.f;
	UPROPERTY(BlueprintReadOnly, Category="Locomotion")
	float TurnRate = 0.f;
	UPROPERTY(BlueprintReadOnly, Category="Locomotion") FVector HorizontalVelocity = FVector::ZeroVector;
	UPROPERTY(BlueprintReadOnly, Category="Locomotion") FVector LocalHorizontalVelocity = FVector::ZeroVector;
	UPROPERTY(BlueprintReadOnly, Category="Locomotion") bool bIsGrounded = true;
	UPROPERTY(BlueprintReadOnly, Category="Locomotion") bool bIsAirborne = false;
	UPROPERTY(BlueprintReadOnly, Category="Locomotion") bool bIsThrusting = false;
	/** Changes on initialization, possession changes and external displacement. */
	UPROPERTY(BlueprintReadOnly, Category="Locomotion") int32 AnimationStateRevision = 0;
	UPROPERTY(BlueprintReadOnly, Category="Integrated Mech") float IntegratedForwardBlend = 0.f;
	UPROPERTY(BlueprintReadOnly, Category="Integrated Mech") float IntegratedStrafeBlend = 0.f;
	UPROPERTY(BlueprintReadOnly, Category="Integrated Mech") bool bIntegratedRunning = false;
	UPROPERTY(BlueprintReadOnly, Category="Integrated Mech") FRotator IntegratedControlRotation = FRotator::ZeroRotator;
	UPROPERTY(BlueprintReadOnly, Category="Integrated Mech") float IntegratedFireBlend = 0.f;
	UPROPERTY(BlueprintReadOnly, Category="Integrated Mech") float IntegratedFireBlendLeft = 0.f;
	UPROPERTY(BlueprintReadOnly, Category="Integrated Mech") FVector IntegratedAimPointComponent = FVector::ZeroVector;
	UPROPERTY(BlueprintReadOnly, Category="Integrated Mech") bool bIntegratedAimValid = false;
	UPROPERTY(BlueprintReadOnly, Category="Integrated Mech") bool bForwardAim=false;
	UPROPERTY(BlueprintReadOnly, Category="Integrated Mech") float ReturnAlpha=1.f;
	UPROPERTY(BlueprintReadOnly, Category="Integrated Mech") FQuat RightReturnStart=FQuat::Identity;
	UPROPERTY(BlueprintReadOnly, Category="Integrated Mech") FQuat LeftReturnStart=FQuat::Identity;
	UPROPERTY(BlueprintReadOnly, Category="Integrated Mech") FTransform RightMuzzleLocal;
	UPROPERTY(BlueprintReadOnly, Category="Integrated Mech") FTransform LeftMuzzleLocal;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Integrated Mech") float RecoilDistance = 5.f;
	UPROPERTY(BlueprintReadOnly, Category="Integrated Mech") float RightArmRecoil = 0.f;
	UPROPERTY(BlueprintReadOnly, Category="Integrated Mech") float LeftArmRecoil = 0.f;
	UPROPERTY(BlueprintReadOnly, Category="Integrated Mech") float ArmRecoilDegrees = 0.f;
	UPROPERTY(BlueprintReadOnly, Category="Integrated Mech") float RightShootTime = 0.f;
	UPROPERTY(BlueprintReadOnly, Category="Integrated Mech") float LeftShootTime = 0.f;
	UPROPERTY(BlueprintReadOnly, Category="Integrated Mech") float RightShootWeight = 0.f;
	UPROPERTY(BlueprintReadOnly, Category="Integrated Mech") float LeftShootWeight = 0.f;
	UPROPERTY(EditDefaultsOnly, Category="Integrated Mech") float ShootDuration = .9666667f;
	UPROPERTY(EditDefaultsOnly, Category="Integrated Mech") float JumpStartDuration = .3333333f;
	UPROPERTY(BlueprintReadOnly, Category="Integrated Mech") bool bJumpStarting = false;
	UPROPERTY(BlueprintReadOnly, Category="Integrated Mech") float JumpStartTime = 0.f;
	void ConfigureIntegratedRecoil(UCurveFloat* Curve, float Duration);
	void TriggerIntegratedRecoil(float AgeSeconds, uint8 MuzzleIndex);
private:
	void UpdateGrounding(class AGuLiGroundMechCharacter& Pawn,float DeltaSeconds,bool bReset);
	float SmoothedSpeed=0.f;
	float RunBlend=0.f;
	int32 TurnSerial=INDEX_NONE;
	UPROPERTY() TObjectPtr<UAnimMontage> ActiveTurnMontage;
	UPROPERTY() TObjectPtr<UCurveFloat> IntegratedRecoilCurve;
	float IntegratedRecoilDuration = .15f;
	float IntegratedRecoilAge[2] = {BIG_NUMBER,BIG_NUMBER};
	float ArmRecoilStart[2] = {0.f,0.f};
	float PreviousYaw = 0.f;
	uint32 PreviousDisplacementRevision = 0;
	int32 PreviousBlinkSerial=0;
	uint8 PreviousBlinkPhase=0;
	FVector BlinkEntryDirection=FVector::ZeroVector;
	TWeakObjectPtr<APawn> PreviousPawn;
	TWeakObjectPtr<AController> PreviousController;
};
