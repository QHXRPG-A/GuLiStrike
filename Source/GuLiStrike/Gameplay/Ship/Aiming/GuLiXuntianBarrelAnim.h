#pragma once
#include "Animation/AnimInstance.h"
#include "GuLiXuntianBarrelAnim.generated.h"

UCLASS(Transient)
class UGuLiXuntianBarrelAnim final : public UAnimInstance
{
    GENERATED_BODY()
public:
    FQuat HingeRotation = FQuat::Identity;
protected:
    virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;
    virtual void DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy) override;
};
