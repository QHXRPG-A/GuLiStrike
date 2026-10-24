#pragma once
#include "BoneControllers/AnimNode_SkeletalControlBase.h"
#include "AnimNode_GuLiGroundMechAim.generated.h"

/** Solve both barrels from this frame's evaluated pose, including their socket offsets and recoil. */
USTRUCT(BlueprintInternalUseOnly)
struct GULISTRIKE_API FAnimNode_GuLiGroundMechAim : public FAnimNode_SkeletalControlBase
{
	GENERATED_BODY()
	FAnimNode_GuLiGroundMechAim();
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aim", meta=(PinShownByDefault)) FVector AimPointComponent = FVector::ZeroVector;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aim", meta=(PinShownByDefault)) bool bAimValid = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aim", meta=(PinShownByDefault)) FTransform RightMuzzleLocal;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aim", meta=(PinShownByDefault)) FTransform LeftMuzzleLocal;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Recoil", meta=(PinShownByDefault)) float RightRecoil = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Recoil", meta=(PinShownByDefault)) float LeftRecoil = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Recoil", meta=(PinShownByDefault)) float RecoilDistance = 5.f;
	/** One-sided horizontal arm limit from Mobility; 75 degrees gives a 150-degree total sweep. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aim", meta=(PinShownByDefault)) float GunYawLimit=75.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aim", meta=(PinShownByDefault)) float GunPitchUpLimit=80.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aim", meta=(PinShownByDefault)) float GunPitchDownLimit=65.f;
	virtual void EvaluateSkeletalControl_AnyThread(FComponentSpacePoseContext& Output,TArray<FBoneTransform>& OutBoneTransforms) override;
	virtual bool IsValidToEvaluate(const USkeleton* Skeleton,const FBoneContainer& RequiredBones) override;
private:
	virtual void InitializeBoneReferences(const FBoneContainer& RequiredBones) override;
	UPROPERTY() FBoneReference RightArm;
	UPROPERTY() FBoneReference LeftArm;
	UPROPERTY() FBoneReference RightGun;
	UPROPERTY() FBoneReference LeftGun;
};
