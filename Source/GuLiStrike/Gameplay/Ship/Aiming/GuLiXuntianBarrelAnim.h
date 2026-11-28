#pragma once
#include "Animation/AnimInstance.h"
#include "GuLiXuntianBarrelAnim.generated.h"

UCLASS(Transient)
class UGuLiXuntianBarrelAnim final : public UAnimInstance
{
    GENERATED_BODY()
public:
    void SetHingeRotation(const FQuat& Rotation);
    const FQuat& GetHingeRotation() const { return HingeRotation; }
protected:
    virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;
    virtual void DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy) override;
    virtual void NativeInitializeAnimation() override;
    virtual void NativePostEvaluateAnimation() override;
    virtual void NativeUninitializeAnimation() override;
private:
    FQuat HingeRotation = FQuat::Identity, LastEvaluated = FQuat::Identity;
    bool bCanSleep = false, bHasEvaluated = false, bSleeping = false;
};
