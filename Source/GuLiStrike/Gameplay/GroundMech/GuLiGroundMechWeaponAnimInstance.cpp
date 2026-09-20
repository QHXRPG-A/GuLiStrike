#include "Gameplay/GroundMech/GuLiGroundMechWeaponAnimInstance.h"
#include "Animation/AnimInstanceProxy.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Curves/CurveFloat.h"

namespace
{
	struct FMechWeaponAnimProxy final : FAnimInstanceProxy
	{
		using FAnimInstanceProxy::FAnimInstanceProxy;
		FName Bone;
		float Offset = 0;
		virtual void PreUpdate(UAnimInstance* Instance, float DeltaSeconds) override
		{
			FAnimInstanceProxy::PreUpdate(Instance, DeltaSeconds);
			const auto& Anim = *CastChecked<UGuLiGroundMechWeaponAnimInstance>(Instance);
			Bone = Anim.RecoilBone; Offset = Anim.RecoilOffset;
		}
		virtual bool Evaluate(FPoseContext& Output) override
		{
			Output.ResetToRefPose();
			const FBoneContainer& Bones = Output.Pose.GetBoneContainer();
			const int32 Index = Bones.GetReferenceSkeleton().FindBoneIndex(Bone);
			if (Index != INDEX_NONE)
			{
				const FCompactPoseBoneIndex Compact = Bones.MakeCompactPoseIndex(FMeshPoseBoneIndex(Index));
				if (Compact != INDEX_NONE) Output.Pose[Compact].AddToTranslation(FVector(0, 0, Offset));
			}
			return true;
		}
	};
}

void UGuLiGroundMechWeaponAnimInstance::Configure(UCurveFloat* Curve, FName Bone, float TargetZ, float Duration)
{
	RecoilCurve = Curve; RecoilBone = Bone; RecoilTargetZ = TargetZ; RecoilDuration = Duration;
	if (const auto* Component = GetSkelMeshComponent()) if (const auto* Mesh = Component->GetSkeletalMeshAsset())
	{
		const int32 Index = Mesh->GetRefSkeleton().FindBoneIndex(Bone);
		if (Index != INDEX_NONE) RestZ = Mesh->GetRefSkeleton().GetRefBonePose()[Index].GetTranslation().Z;
	}
}

void UGuLiGroundMechWeaponAnimInstance::TriggerRecoil(float AgeSeconds)
{
	StartAlpha = RecoilAlpha;
	Age = FMath::Max(0.f, AgeSeconds);
}

void UGuLiGroundMechWeaponAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);
	Age += FMath::Max(0.f, DeltaSeconds);
	if (!RecoilCurve || Age >= RecoilDuration) { RecoilAlpha = RecoilOffset = 0; return; }
	const float Value = FMath::Clamp(RecoilCurve->GetFloatValue(Age), 0.f, 1.f);
	RecoilAlpha = Age < .025f ? FMath::Lerp(StartAlpha, 1.f, Value) : Value;
	RecoilOffset = FMath::Min(0.f, RecoilTargetZ - RestZ) * RecoilAlpha;
}

FAnimInstanceProxy* UGuLiGroundMechWeaponAnimInstance::CreateAnimInstanceProxy() { return new FMechWeaponAnimProxy(this); }
void UGuLiGroundMechWeaponAnimInstance::DestroyAnimInstanceProxy(FAnimInstanceProxy* Proxy) { delete Proxy; }
