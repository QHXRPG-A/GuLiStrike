#pragma once
#include "Animation/AnimNotifies/AnimNotify.h"
#include "AnimNotify_GuLiGroundFire.generated.h"

/** An authored shot in the alternating fire loop. Gameplay consumes it after pose evaluation. */
UCLASS()
class GULISTRIKE_API UAnimNotify_GuLiGroundFire : public UAnimNotify
{
    GENERATED_BODY()
public:
    UAnimNotify_GuLiGroundFire();
    UPROPERTY(EditAnywhere,Category="Fire") uint8 MuzzleIndex=0;
    virtual FString GetNotifyName_Implementation() const override { return TEXT("Fire"); }
    virtual void BranchingPointNotify(FBranchingPointNotifyPayload& Payload) override;
};
