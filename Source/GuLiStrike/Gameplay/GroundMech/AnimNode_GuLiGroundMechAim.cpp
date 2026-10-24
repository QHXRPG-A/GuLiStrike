#include "Gameplay/GroundMech/AnimNode_GuLiGroundMechAim.h"
#include "Math/RotationMatrix.h"

FAnimNode_GuLiGroundMechAim::FAnimNode_GuLiGroundMechAim()
{
	RightArm.BoneName=TEXT("GunArm_R"); LeftArm.BoneName=TEXT("GunArm_L");
	RightGun.BoneName=TEXT("Gun_R"); LeftGun.BoneName=TEXT("Gun_L");
}
void FAnimNode_GuLiGroundMechAim::InitializeBoneReferences(const FBoneContainer& Bones)
{
	RightArm.Initialize(Bones); LeftArm.Initialize(Bones); RightGun.Initialize(Bones); LeftGun.Initialize(Bones);
}
bool FAnimNode_GuLiGroundMechAim::IsValidToEvaluate(const USkeleton*,const FBoneContainer& Bones)
{
	return RightArm.IsValidToEvaluate(Bones) && LeftArm.IsValidToEvaluate(Bones)
		&& RightGun.IsValidToEvaluate(Bones) && LeftGun.IsValidToEvaluate(Bones);
}
void FAnimNode_GuLiGroundMechAim::EvaluateSkeletalControl_AnyThread(FComponentSpacePoseContext& Output,TArray<FBoneTransform>& Out)
{
	const FBoneContainer& Bones=Output.Pose.GetPose().GetBoneContainer();
	auto Solve=[&](const FBoneReference& ArmRef,const FBoneReference& GunRef,const FTransform& Socket,float Recoil)
	{
		const auto ArmIndex=ArmRef.GetCompactPoseIndex(Bones), GunIndex=GunRef.GetCompactPoseIndex(Bones);
		FTransform Arm=Output.Pose.GetComponentSpaceTransform(ArmIndex);
		FTransform Gun=Output.Pose.GetComponentSpaceTransform(GunIndex);
		const FVector Bore=(Socket*Gun).GetUnitAxis(EAxis::X);
		Gun.AddToTranslation(-Bore*FMath::Clamp(Recoil,0.f,1.f)*RecoilDistance);
		const FTransform GunInArm=Gun.GetRelativeTransform(Arm);
		if (bAimValid && !AimPointComponent.ContainsNaN()
			&& (AimPointComponent-Arm.GetLocation()).SizeSquared()>UE_SMALL_NUMBER)
		{
			// Solve the two aim motors before constructing a rotation. An unrestricted
			// look-at followed by yaw correction retains target-dependent roll at the
			// stop, making the arm/barrel keep chasing an unreachable mouse position.
			const FTransform Muzzle=Socket*Gun;
			const FVector Offset=Muzzle.GetLocation()-Arm.GetLocation();
			const FVector Target=AimPointComponent-Arm.GetLocation();
			const FVector Forward=Muzzle.GetUnitAxis(EAxis::X);
			const FQuat AimFrame=FRotationMatrix::MakeFromXZ(Forward,FVector::UpVector).ToQuat();
			const FQuat ArmInAim=AimFrame.Inverse()*Arm.GetRotation();
			const double Along=FVector::DotProduct(Offset,Forward);
			const double Discriminant=Along*Along+Target.SizeSquared()-Offset.SizeSquared();
			const double Distance=Discriminant>=0 ? -Along+FMath::Sqrt(Discriminant) : -1.;
			const FVector Ray=AimFrame.UnrotateVector(Distance>=0 ? Offset+Distance*Forward : Forward);
			// At zero aim yaw/pitch the frame is X=component +Y, Y=-X, Z=+Z.
			// Its lateral ray offset stays invariant under pitch; solve yaw first.
			const double Radius=Target.Size2D();
			const double Lateral=-Ray.Y;
			const double YawOffset=FMath::Asin(FMath::Clamp(Lateral/FMath::Max(Radius,double(UE_SMALL_NUMBER)),-1.,1.));
			float Yaw=FMath::RadiansToDegrees(FMath::Atan2(-Target.X,Target.Y)+YawOffset);
			Yaw=FRotator::NormalizeAxis(Yaw);
			const double Reach=FMath::Sqrt(FMath::Max(0.,Radius*Radius-Lateral*Lateral));
			const float Pitch=FMath::Clamp(float(FMath::RadiansToDegrees(FMath::Atan2(Target.Z,Reach)-FMath::Atan2(Ray.Z,Ray.X))),
				-GunPitchDownLimit,GunPitchUpLimit);
			// Intersect the bore and shoulder limits. The authored barrel cant means
			// these are different cones; both must stop before applying the pose.
			const FVector ArmAtZeroYaw=(FRotator(Pitch,90.f,0.f).Quaternion()*ArmInAim).GetAxisX();
			const float ArmYawOffset=FMath::RadiansToDegrees(FMath::Atan2(-ArmAtZeroYaw.X,ArmAtZeroYaw.Y));
			const float MinimumYaw=FMath::Max(-GunYawLimit,-GunYawLimit-ArmYawOffset);
			const float MaximumYaw=FMath::Min(GunYawLimit,GunYawLimit-ArmYawOffset);
			Yaw=MinimumYaw<=MaximumYaw ? FMath::Clamp(Yaw,MinimumYaw,MaximumYaw) : FMath::Clamp(Yaw,-GunYawLimit,GunYawLimit);
			Arm.SetRotation((FRotator(Pitch,90.f+Yaw,0.f).Quaternion()*ArmInAim).GetNormalized());
			Gun=GunInArm*Arm;
		}
		// GunArm X is the upper arm's forward axis on this rig. The barrels are
		// canted outwards by ~23.7 degrees, so limiting the bore alone still lets
		// the arm swing past the configured shoulder limit. Apply the arm limit
		// last, including animation-only poses, and carry the whole gun with it.
		// This yaw-only correction preserves pitch, barrel spin and recoil.
		const FVector ArmForward=Arm.GetUnitAxis(EAxis::X);
		if (GunYawLimit>0.f && ArmForward.SizeSquared2D()>UE_SMALL_NUMBER)
		{
			const float ArmYaw=FMath::RadiansToDegrees(FMath::Atan2(-ArmForward.X,ArmForward.Y));
			const float LimitedArmYaw=FMath::Clamp(ArmYaw,-GunYawLimit,GunYawLimit);
			const FQuat Correction(FVector::UpVector,FMath::DegreesToRadians(LimitedArmYaw-ArmYaw));
			Arm.SetRotation((Correction*Arm.GetRotation()).GetNormalized());
			Gun=GunInArm*Arm;
		}
		Out.Emplace(ArmIndex,Arm); Out.Emplace(GunIndex,Gun);
	};
	Solve(RightArm,RightGun,RightMuzzleLocal,RightRecoil);
	Solve(LeftArm,LeftGun,LeftMuzzleLocal,LeftRecoil);
	Out.Sort(FCompareBoneTransformIndex());
}
