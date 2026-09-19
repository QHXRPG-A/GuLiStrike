#pragma once
#include "Animation/AnimInstance.h"
#include "GuLiGroundMechAnimInstance.generated.h"

class UBlendSpace;

/** Asset-driven locomotion. No dependency on the battle controller or a particular mech Pawn. */
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
protected:
	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;
	virtual void DestroyAnimInstanceProxy(FAnimInstanceProxy* Proxy) override;
private:
	float PreviousYaw = 0.f;
};
