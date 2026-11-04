#include "Gameplay/GroundMech/Animation/AnimNode_GuLiGroundMechAim.h"
#include "Math/RotationMatrix.h"

FAnimNode_GuLiGroundMechAim::FAnimNode_GuLiGroundMechAim()
{
	RightArm.BoneName=TEXT("GunArm_R"); LeftArm.BoneName=TEXT("GunArm_L");
	RightGun.BoneName=TEXT("Gun_R"); LeftGun.BoneName=TEXT("Gun_L");
	Chest.BoneName=TEXT("cockpit");
}
void FAnimNode_GuLiGroundMechAim::InitializeBoneReferences(const FBoneContainer& Bones)
{
	RightArm.Initialize(Bones); LeftArm.Initialize(Bones); RightGun.Initialize(Bones); LeftGun.Initialize(Bones);
	Chest.Initialize(Bones);
	RestChestRotation=FQuat::Identity;
	if (Chest.IsValidToEvaluate(Bones))
	{
		auto Index=Chest.GetCompactPoseIndex(Bones);
		FTransform Reference=Bones.GetRefPoseTransform(Index);
		while ((Index=Bones.GetParentBoneIndex(Index)).IsValid()) Reference=Reference*Bones.GetRefPoseTransform(Index);
		RestChestRotation=Reference.GetRotation();
	}
}
bool FAnimNode_GuLiGroundMechAim::IsValidToEvaluate(const USkeleton*,const FBoneContainer& Bones)
{
	return RightArm.IsValidToEvaluate(Bones) && LeftArm.IsValidToEvaluate(Bones)
		&& RightGun.IsValidToEvaluate(Bones) && LeftGun.IsValidToEvaluate(Bones) && Chest.IsValidToEvaluate(Bones);
}
void FAnimNode_GuLiGroundMechAim::EvaluateSkeletalControl_AnyThread(FComponentSpacePoseContext& Output,TArray<FBoneTransform>& Out)
{
	const FBoneContainer& Bones=Output.Pose.GetPose().GetBoneContainer();
	if (UpperBodyFrame.ContainsNaN() || !UpperBodyFrame.IsNormalized()) return;
	const FTransform TurretFrame(UpperBodyFrame);
	const FVector TurretAimPoint=TurretFrame.InverseTransformPosition(AimPointComponent);
	auto Descends=[&](FCompactPoseBoneIndex Child,FCompactPoseBoneIndex Parent)
	{ for (;Child.IsValid();Child=Bones.GetParentBoneIndex(Child)) if (Child==Parent) return true; return false; };
	const bool bSway=Chest.IsValidToEvaluate(Bones) && !UpperBodyRecoilRotation.ContainsNaN()
		&& UpperBodyRecoilRotation.IsNormalized() && !UpperBodyRecoilRotation.Equals(FQuat::Identity);
	const auto ChestIndex=Chest.GetCompactPoseIndex(Bones);
	const FTransform OldChest=Output.Pose.GetComponentSpaceTransform(ChestIndex);
	const FVector ChestPivot=OldChest.GetLocation();
	// Keep the animated waist connection but replace inherited rotation. The
	// recoil is authored in the neutral mesh frame and follows the turret frame.
	const FQuat Sway=bSway?UpperBodyRecoilRotation:FQuat::Identity;
	const FQuat ChestCorrection=(UpperBodyFrame*Sway*RestChestRotation*OldChest.GetRotation().Inverse()).GetNormalized();
	auto CurrentPose=[&](FCompactPoseBoneIndex Index)
	{
		FTransform Transform=Output.Pose.GetComponentSpaceTransform(Index);
		if (Descends(Index,ChestIndex))
		{
			Transform.SetLocation(ChestPivot+ChestCorrection.RotateVector(Transform.GetLocation()-ChestPivot));
			Transform.SetRotation((ChestCorrection*Transform.GetRotation()).GetNormalized());
		}
		return Transform;
	};
	// MiddleJoint also owns the legs. Rotate only its chest branch before aiming
	// so the shoulders move with the body and both bores are solved from that pose.
	Out.Emplace(ChestIndex,CurrentPose(ChestIndex));
	auto Solve=[&](const FBoneReference& ArmRef,const FBoneReference& GunRef,const FTransform& Socket,float Recoil,float ArmWeight,const FQuat& ReturnStart)
	{
		const auto ArmIndex=ArmRef.GetCompactPoseIndex(Bones), GunIndex=GunRef.GetCompactPoseIndex(Bones);
		// All motor constraints are evaluated relative to the turret, never the
		// stepping chassis. Convert final bones back to mesh component space once.
		FTransform Arm=CurrentPose(ArmIndex).GetRelativeTransform(TurretFrame);
		FTransform Gun=CurrentPose(GunIndex).GetRelativeTransform(TurretFrame);
		const FVector Bore=(Socket*Gun).GetUnitAxis(EAxis::X);
		Gun.AddToTranslation(-Bore*FMath::Clamp(Recoil,0.f,1.f)*RecoilDistance);
		const FTransform GunInArm=Gun.GetRelativeTransform(Arm);
		if (bForwardAim)
		{
			// Parallel, horizontal bores relative to the body; no stale world target
			// or camera ray participates while the player is freely looking around.
			const FQuat AimFrame=FRotationMatrix::MakeFromXZ((Socket*Gun).GetUnitAxis(EAxis::X),FVector::UpVector).ToQuat();
			const FQuat Neutral=(FRotator(0,90,0).Quaternion()*AimFrame.Inverse()*Arm.GetRotation()).GetNormalized();
			Arm.SetRotation(FQuat::Slerp(UpperBodyFrame.Inverse()*ReturnStart,Neutral,FMath::SmoothStep(0.f,1.f,ReturnAlpha)).GetNormalized());
			Gun=GunInArm*Arm;
		}
		else if (bAimValid && !TurretAimPoint.ContainsNaN()
			&& (TurretAimPoint-Arm.GetLocation()).SizeSquared()>UE_SMALL_NUMBER)
		{
			// Solve the two aim motors before constructing a rotation. An unrestricted
			// look-at followed by yaw correction retains target-dependent roll at the
			// stop, making the arm/barrel keep chasing an unreachable mouse position.
			const FTransform Muzzle=Socket*Gun;
			const FVector Offset=Muzzle.GetLocation()-Arm.GetLocation();
			const FVector Target=TurretAimPoint-Arm.GetLocation();
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
		const float KickDegrees=FMath::Clamp(ArmWeight,0.f,1.f)*FMath::Clamp(ArmRecoilDegrees,0.f,30.f);
		if (KickDegrees>UE_SMALL_NUMBER)
		{
			const FVector Forward=(Socket*Gun).GetUnitAxis(EAxis::X);
			const FVector Hinge=FVector::CrossProduct(Forward,FVector::UpVector).GetSafeNormal();
			const FQuat Kick(Hinge,-FMath::DegreesToRadians(KickDegrees));
			// Rotate the rigid upper arm about its shoulder, carrying the forearm
			// back. Counter-rotate the gun at its joint so recoil does not lift the
			// bore off the intended ray or detach the shoulder from the body.
			Gun.SetLocation(Arm.GetLocation()+Kick.RotateVector(Gun.GetLocation()-Arm.GetLocation()));
			Arm.SetRotation((Kick*Arm.GetRotation()).GetNormalized());
			const FVector ArmAxis=Arm.GetUnitAxis(EAxis::X);
			const float ArmYaw=FMath::RadiansToDegrees(FMath::Atan2(-ArmAxis.X,ArmAxis.Y));
			const FQuat Limit(FVector::UpVector,FMath::DegreesToRadians(FMath::Clamp(ArmYaw,-GunYawLimit,GunYawLimit)-ArmYaw));
			Arm.SetRotation((Limit*Arm.GetRotation()).GetNormalized());
			Gun.SetLocation(Arm.GetLocation()+Limit.RotateVector(Gun.GetLocation()-Arm.GetLocation()));
			if (!bForwardAim && bAimValid && !TurretAimPoint.ContainsNaN())
			{
				// Solve again at the final recoiled gun pivot, including the socket
				// offset. Clamp motors before constructing the rotation, as above.
				const FTransform Muzzle=Socket*Gun;
				const FVector Offset=Muzzle.GetLocation()-Gun.GetLocation();
				const FVector Target=TurretAimPoint-Gun.GetLocation();
				const FQuat Frame=FRotationMatrix::MakeFromXZ(Forward,FVector::UpVector).ToQuat();
				const double Along=FVector::DotProduct(Offset,Forward);
				const double Disc=Along*Along+Target.SizeSquared()-Offset.SizeSquared();
				const double Distance=Disc>=0?-Along+FMath::Sqrt(Disc):-1.;
				const FVector Ray=Frame.UnrotateVector(Distance>=0?Offset+Distance*Forward:Forward);
				const double Radius=Target.Size2D(), Lateral=-Ray.Y;
				const float Yaw=FMath::Clamp(FRotator::NormalizeAxis(float(FMath::RadiansToDegrees(
					FMath::Atan2(-Target.X,Target.Y)+FMath::Asin(FMath::Clamp(Lateral/FMath::Max(Radius,double(UE_SMALL_NUMBER)),-1.,1.))))),-GunYawLimit,GunYawLimit);
				const double Reach=FMath::Sqrt(FMath::Max(0.,Radius*Radius-Lateral*Lateral));
				const float Pitch=FMath::Clamp(float(FMath::RadiansToDegrees(FMath::Atan2(Target.Z,Reach)-FMath::Atan2(Ray.Z,Ray.X))),-GunPitchDownLimit,GunPitchUpLimit);
				Gun.SetRotation((FRotator(Pitch,90.f+Yaw,0.f).Quaternion()*Frame.Inverse()*Gun.GetRotation()).GetNormalized());
			}
		}
		if (BlinkPose.X>UE_SMALL_NUMBER)
		{
			// Sweep the rigid upper arm back in its sagittal plane, preserving bone
			// lengths and carrying the complete forearm/barrel assembly with it.
			// The authored angle is measured from forward; Neutral is the hanging
			// arm. Subtract that 90-degree offset instead of lifting it over the shoulder.
			const FQuat Sweep(FVector::ForwardVector,-FMath::DegreesToRadians(FMath::Clamp(float(BlinkPose.Y),90.f,150.f)-90.f));
			const FTransform RelativeGun=Gun.GetRelativeTransform(Arm);
			const FQuat Frame=FRotationMatrix::MakeFromXZ((Socket*Gun).GetUnitAxis(EAxis::X),FVector::UpVector).ToQuat();
			const FQuat Neutral=(FRotator(0,90,0).Quaternion()*Frame.Inverse()*Arm.GetRotation()).GetNormalized();
			Arm.SetRotation(FQuat::Slerp(Arm.GetRotation(),Sweep*Neutral,FMath::Clamp(float(BlinkPose.X),0.f,1.f)).GetNormalized()); Gun=RelativeGun*Arm;
            // The shoulder motor places the elbow behind the body; the forearm
            // motor turns the barrel back independently instead of pointing it
            // at the ground or swinging the elbow above the shoulder.
            const FVector BackBore=FQuat(FVector::ForwardVector,-FMath::DegreesToRadians(float(BlinkPose.Y))).RotateVector(FVector::RightVector);
            const FQuat BackRotation=(FQuat::FindBetweenNormals((Socket*Gun).GetUnitAxis(EAxis::X),BackBore)*Gun.GetRotation()).GetNormalized();
            Gun.SetRotation(FQuat::Slerp(Gun.GetRotation(),BackRotation,FMath::Clamp(float(BlinkPose.X),0.f,1.f)).GetNormalized());
		}
		Out.Emplace(ArmIndex,Arm*TurretFrame); Out.Emplace(GunIndex,Gun*TurretFrame);
	};
	Solve(RightArm,RightGun,RightMuzzleLocal,RightRecoil,RightArmRecoil,RightReturnStart);
	Solve(LeftArm,LeftGun,LeftMuzzleLocal,LeftRecoil,LeftArmRecoil,LeftReturnStart);
	if (BlinkPose.X>UE_SMALL_NUMBER && BlinkPose.Z>0)
	{
		// Find the upper-body branching joint rather than rotating the root/legs.
		const auto R=RightArm.GetCompactPoseIndex(Bones), L=LeftArm.GetCompactPoseIndex(Bones);
		auto Torso=Bones.GetParentBoneIndex(R);
		while (Torso.IsValid() && !Descends(L,Torso)) Torso=Bones.GetParentBoneIndex(Torso);
		const FCompactPoseBoneIndex Hip=Bones.GetCompactPoseIndexFromSkeletonIndex(Bones.GetReferenceSkeleton().FindBoneIndex(TEXT("UpperLeg_R")));
		if (Torso.IsValid() && (!Hip.IsValid() || !Descends(Hip,Torso)))
		{
			TArray<FTransform> Old,New; const int32 Count=Bones.GetCompactPoseNumBones(); Old.SetNum(Count); New.SetNum(Count);
			for (int32 I=0;I<Count;++I) Old[I]=New[I]=Output.Pose.GetComponentSpaceTransform(FCompactPoseBoneIndex(I));
			for (int32 I=0;I<Count;++I)
			{
				const FCompactPoseBoneIndex Index(I); const auto Parent=Bones.GetParentBoneIndex(Index);
				if (const auto* Override=Out.FindByPredicate([&](const FBoneTransform& B){return B.BoneIndex==Index;})) New[I]=Override->Transform;
				else if (Parent.IsValid()) New[I]=Old[I].GetRelativeTransform(Old[Parent.GetInt()])*New[Parent.GetInt()];
			}
			const FQuat Lean(UpperBodyFrame.RotateVector(FVector::ForwardVector),-FMath::DegreesToRadians(float(BlinkPose.X*BlinkPose.Z)));
			const FVector Pivot=Old[Torso.GetInt()].GetLocation();
			Out.Reset();
			for (int32 I=0;I<Count;++I) if (Descends(FCompactPoseBoneIndex(I),Torso))
			{
				New[I].SetLocation(Pivot+Lean.RotateVector(New[I].GetLocation()-Pivot));
				New[I].SetRotation((Lean*New[I].GetRotation()).GetNormalized()); Out.Emplace(FCompactPoseBoneIndex(I),New[I]);
			}
		}
	}
	Out.Sort(FCompareBoneTransformIndex());
}
