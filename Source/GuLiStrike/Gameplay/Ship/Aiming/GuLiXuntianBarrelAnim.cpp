#include "Gameplay/Ship/Aiming/GuLiXuntianBarrelAnim.h"
#include "Animation/AnimInstanceProxy.h"
#include "Animation/AnimNodeBase.h"
#include "Animation/Skeleton.h"

namespace
{
class FBarrelPose final : public FAnimInstanceProxy
{
public:
    explicit FBarrelPose(UAnimInstance* Instance) : FAnimInstanceProxy(Instance) {}
    void PreUpdate(UAnimInstance* Instance, float Delta) override
    {
        FAnimInstanceProxy::PreUpdate(Instance, Delta);
        Rotation = CastChecked<UGuLiXuntianBarrelAnim>(Instance)->HingeRotation;
    }
    bool Evaluate(FPoseContext& Output) override
    {
        Output.ResetToRefPose();
        const FBoneContainer& Bones = Output.Pose.GetBoneContainer();
        const auto Index=Bones.GetSkeletonAsset()->GetReferenceSkeleton().FindBoneIndex(TEXT("BarrelPitch"));
        if (Index==INDEX_NONE) return true;
        const FCompactPoseBoneIndex Bone=Bones.GetCompactPoseIndexFromSkeletonPoseIndex(FSkeletonPoseBoneIndex(Index));
        if (Bone.IsValid())
        {
            FTransform& Hinge = Output.Pose[Bone];
            Hinge.SetRotation((Hinge.GetRotation() * Rotation).GetNormalized());
        }
        return true;
    }
private:
    FQuat Rotation = FQuat::Identity;
};
}

FAnimInstanceProxy* UGuLiXuntianBarrelAnim::CreateAnimInstanceProxy() { return new FBarrelPose(this); }
void UGuLiXuntianBarrelAnim::DestroyAnimInstanceProxy(FAnimInstanceProxy* Proxy) { delete static_cast<FBarrelPose*>(Proxy); }
