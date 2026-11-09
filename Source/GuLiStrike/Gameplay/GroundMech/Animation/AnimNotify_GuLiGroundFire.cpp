#include "Gameplay/GroundMech/Animation/AnimNotify_GuLiGroundFire.h"
#include "Gameplay/GroundMech/Animation/GuLiGroundMechAnimInstance.h"
#include "Components/SkeletalMeshComponent.h"

UAnimNotify_GuLiGroundFire::UAnimNotify_GuLiGroundFire()
{
    bIsNativeBranchingPoint=true;
#if WITH_EDITORONLY_DATA
    bShouldFireInEditor=false;
#endif
}

void UAnimNotify_GuLiGroundFire::BranchingPointNotify(FBranchingPointNotifyPayload& Payload)
{
    if (Payload.SkelMeshComponent)
        if (auto* Anim=Cast<UGuLiGroundMechAnimInstance>(Payload.SkelMeshComponent->GetAnimInstance()))
            Anim->OnFireAnimationNotify(Payload.MontageInstanceID,MuzzleIndex);
}
