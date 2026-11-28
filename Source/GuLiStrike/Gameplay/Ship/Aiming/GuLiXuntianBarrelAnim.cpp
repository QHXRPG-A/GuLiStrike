#include "Gameplay/Ship/Aiming/GuLiXuntianBarrelAnim.h"
#include "Animation/AnimInstanceProxy.h"
#include "Animation/AnimNodeBase.h"
#include "Animation/Skeleton.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"

namespace
{
class FBarrelPose final : public FAnimInstanceProxy
{
public:
    explicit FBarrelPose(UAnimInstance* Instance) : FAnimInstanceProxy(Instance) {}
    void PreUpdate(UAnimInstance* Instance, float Delta) override
    {
        FAnimInstanceProxy::PreUpdate(Instance, Delta);
        Rotation = CastChecked<UGuLiXuntianBarrelAnim>(Instance)->GetHingeRotation();
    }
    bool Evaluate(FPoseContext& Output) override
    {
        Output.ResetToRefPose();
        const FBoneContainer& Bones = Output.Pose.GetBoneContainer();
        static const FName PitchBone(TEXT("BarrelPitch"));
        const auto Index=Bones.GetSkeletonAsset()->GetReferenceSkeleton().FindBoneIndex(PitchBone);
        if (Index==INDEX_NONE) return true;
        const FCompactPoseBoneIndex Bone=Bones.GetCompactPoseIndexFromSkeletonPoseIndex(FSkeletonPoseBoneIndex(Index));
        if (Bone.IsValid())
        {
            FTransform& Hinge = Output.Pose[Bone];
            Hinge.SetRotation((Hinge.GetRotation() * Rotation).GetNormalized());
        }
        return true;
    }
    const FQuat& EvaluatedRotation() const { return Rotation; }
private:
    FQuat Rotation = FQuat::Identity;
};
}

FAnimInstanceProxy* UGuLiXuntianBarrelAnim::CreateAnimInstanceProxy() { return new FBarrelPose(this); }
void UGuLiXuntianBarrelAnim::DestroyAnimInstanceProxy(FAnimInstanceProxy* Proxy) { delete static_cast<FBarrelPose*>(Proxy); }

void UGuLiXuntianBarrelAnim::NativeInitializeAnimation()
{
    Super::NativeInitializeAnimation();
    const auto* Mesh = GetSkelMeshComponent();
    const auto* Asset = Mesh ? Mesh->GetSkeletalMeshAsset() : nullptr;
    bCanSleep = Asset && Asset->GetLODNum() == 1 && !Asset->GetPostProcessAnimBlueprint()
        && Asset->GetMeshClothingAssets().IsEmpty() && !Mesh->IsSimulatingPhysics();
}

void UGuLiXuntianBarrelAnim::SetHingeRotation(const FQuat& Rotation)
{
    if (HingeRotation.Equals(Rotation, 1.e-7)) return;
    HingeRotation = Rotation;
    if (bSleeping)
    {
        bSleeping = false;
        GetSkelMeshComponent()->SetComponentTickEnabled(true);
    }
}

void UGuLiXuntianBarrelAnim::NativePostEvaluateAnimation()
{
    Super::NativePostEvaluateAnimation();
    if (!bCanSleep) return;
    // This graph only rotates one hinge. Sleep after the last requested pose has actually
    // reached the mesh; an older parallel evaluation must never put a newly aimed gun to sleep.
    const FQuat Evaluated = GetProxyOnGameThread<FBarrelPose>().EvaluatedRotation();
    if (bHasEvaluated && LastEvaluated.Equals(Evaluated, 1.e-7) && HingeRotation.Equals(Evaluated, 1.e-7))
    {
        bSleeping = true;
        GetSkelMeshComponent()->SetComponentTickEnabled(false);
    }
    LastEvaluated = Evaluated;
    bHasEvaluated = true;
}

void UGuLiXuntianBarrelAnim::NativeUninitializeAnimation()
{
    if (bSleeping && GetSkelMeshComponent()) GetSkelMeshComponent()->SetComponentTickEnabled(true);
    bSleeping = bHasEvaluated = false;
    Super::NativeUninitializeAnimation();
}
