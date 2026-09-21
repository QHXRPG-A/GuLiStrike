#pragma once
#include "Animation/AnimInstance.h"
#include "GuLiGroundMechAnimInstance.generated.h"

class UBlendSpace;

/** Read-only movement snapshot. The editable Animation Blueprint owns all pose evaluation. */
UCLASS(Transient, Blueprintable)
class GULISTRIKE_API UGuLiGroundMechAnimInstance : public UAnimInstance
{
	GENERATED_BODY()
public:
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
private:
	float PreviousYaw = 0.f;
	uint32 PreviousDisplacementRevision = 0;
	TWeakObjectPtr<APawn> PreviousPawn;
	TWeakObjectPtr<AController> PreviousController;
};
