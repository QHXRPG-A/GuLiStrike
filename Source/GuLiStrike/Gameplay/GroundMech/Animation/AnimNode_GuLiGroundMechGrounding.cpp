#include "Gameplay/GroundMech/Animation/AnimNode_GuLiGroundMechGrounding.h"

FAnimNode_GuLiGroundMechGrounding::FAnimNode_GuLiGroundMechGrounding()
{
    Pelvis.BoneName=TEXT("MiddleJoint");
    for (const TCHAR* Name:{TEXT("UpperLeg_L"),TEXT("Leg_L"),TEXT("Foot_L"),TEXT("FootEnd_L"),
        TEXT("UpperLeg_R"),TEXT("Leg_R"),TEXT("Foot_R"),TEXT("FootEnd_R")}) LegBones.AddDefaulted_GetRef().BoneName=Name;
}
void FAnimNode_GuLiGroundMechGrounding::InitializeBoneReferences(const FBoneContainer& Bones)
{
    Pelvis.Initialize(Bones); for (auto& B:LegBones) B.Initialize(Bones);
    if (!IsValidToEvaluate(nullptr,Bones)) return;
    auto Reference=[&](int32 Bone)
    {
        auto Index=LegBones[Bone].GetCompactPoseIndex(Bones);
        FTransform Ref=Bones.GetRefPoseTransform(Index);
        while ((Index=Bones.GetParentBoneIndex(Index))!=INDEX_NONE) Ref=Ref*Bones.GetRefPoseTransform(Index);
        return Ref;
    };
    for (int32 Side=0;Side<2;++Side)
    {
        const FTransform Ref=Reference(Side*4+2);
        const FVector Hip=Reference(Side*4).GetLocation(),Knee=Reference(Side*4+1).GetLocation();
        UpperLengths[Side]=FVector::Distance(Hip,Knee); LowerLengths[Side]=FVector::Distance(Knee,Ref.GetLocation());
        RestAxis[Side]=(Ref.GetLocation()-Hip).GetSafeNormal();
        RestPole[Side]=((Knee-Hip)-RestAxis[Side]*FVector::DotProduct(Knee-Hip,RestAxis[Side])).GetSafeNormal();
        RestUpperDirection[Side]=(Knee-Hip).GetSafeNormal(); RestLowerDirection[Side]=(Ref.GetLocation()-Knee).GetSafeNormal();
        RestHingeNormal[Side]=FVector::CrossProduct(RestUpperDirection[Side],RestLowerDirection[Side]).GetSafeNormal();
        RestUpperRotation[Side]=Reference(Side*4).GetRotation(); RestLowerRotation[Side]=Reference(Side*4+1).GetRotation();
        RestAnkle[Side]=Ref.GetLocation(); LocalUp[Side]=Ref.InverseTransformVectorNoScale(FVector::UpVector);
        RestToeLocal[Side]=Bones.GetRefPoseTransform(LegBones[Side*4+3].GetCompactPoseIndex(Bones));
    }
    LastRevision=INDEX_NONE;
}
bool FAnimNode_GuLiGroundMechGrounding::IsValidToEvaluate(const USkeleton*,const FBoneContainer& Bones)
{
    if (!Pelvis.IsValidToEvaluate(Bones) || LegBones.Num()!=8) return false;
    for (const auto& B:LegBones) if (!B.IsValidToEvaluate(Bones)) return false;
    return true;
}
void FAnimNode_GuLiGroundMechGrounding::EvaluateSkeletalControl_AnyThread(FComponentSpacePoseContext& Output,TArray<FBoneTransform>& Out)
{
    const auto& S=Ground.Settings; const auto& Bones=Output.Pose.GetPose().GetBoneContainer();
    if (Ground.Revision!=LastRevision)
    {
        Locks[0]=Locks[1]=FLock{}; PelvisOffset=0.f; LastRevision=Ground.Revision;
        Contacts[0]=Contacts[1]=PoleTwists[0]=PoleTwists[1]=0;
        AppliedOffsets[0]=AppliedOffsets[1]=FVector::ZeroVector; bHasPreviousPose=false;
    }
    if (Ground.BlinkAlpha>UE_SMALL_NUMBER)
    {
        Locks[0]=Locks[1]=FLock{}; Contacts[0]=Contacts[1]=PoleTwists[0]=PoleTwists[1]=0;
        PelvisOffset=0; bHasPreviousPose=false;
        EvaluateBlinkCrouch(Output,Out);
        return;
    }
    if (Ground.GroundAlpha<=UE_SMALL_NUMBER)
    { Locks[0]=Locks[1]=FLock{}; Contacts[0]=Contacts[1]=PoleTwists[0]=PoleTwists[1]=0; PelvisOffset=0; bHasPreviousPose=false; return; }
    const auto PelvisIndex=Pelvis.GetCompactPoseIndex(Bones);
    const FTransform World=Ground.ComponentToWorld;
#if WITH_EDITOR
    if (bCaptureStructure) { ++StructureFrame.Serial; StructureFrame.Ground=Ground; }
#endif
    FVector Targets[2]; FQuat FootRotations[2]; float Corrections[2]={0,0}, ContactWeights[2]={0,0};
    FTransform Original[2][4], ToeLocals[2]; TArray<FCompactPoseBoneIndex,TInlineAllocator<4>> Indices[2];
    auto PlaneDelta=[](const FVector& Point,const FVector& OnPlane,const FVector& Normal)
    { return FVector::DotProduct(OnPlane-Point,Normal)/FMath::Max(.1,Normal.Z); };
    auto ReachAtAngle=[&](int32 Side,float Angle)
    { return FMath::Sqrt(FMath::Square(UpperLengths[Side])+FMath::Square(LowerLengths[Side])
        -2.f*UpperLengths[Side]*LowerLengths[Side]*FMath::Cos(FMath::DegreesToRadians(Angle))); };
    auto ConstrainTarget=[&](int32 Side,const FVector& Hip,FVector Target)
    {
        const float Sign=Side?-1.f:1.f;
        Target.X=Sign*FMath::Max(Sign*Target.X,S.MinFootSeparation*.5f);
        FVector Axis=(Target-Hip).GetSafeNormal(); if (Axis.IsNearlyZero()) Axis=RestAxis[Side];
        const FQuat Swing=FQuat::FindBetweenNormals(RestAxis[Side],Axis);
        const double Angle=Swing.GetAngle(),Limit=FMath::DegreesToRadians(S.MaxLegSwingDegrees);
        if (Angle>Limit) Axis=FQuat::Slerp(FQuat::Identity,Swing,Limit/Angle).RotateVector(RestAxis[Side]);
        return Hip+Axis*FMath::Clamp(float(FVector::Distance(Hip,Target)),ReachAtAngle(Side,S.MinKneeAngle),ReachAtAngle(Side,S.MaxKneeAngle));
    };
    for (int32 Side=0;Side<2;++Side)
    {
        for (int32 B=0;B<4;++B)
        { Indices[Side].Add(LegBones[Side*4+B].GetCompactPoseIndex(Bones)); Original[Side][B]=Output.Pose.GetComponentSpaceTransform(Indices[Side][B]); }
        const auto& Sample=Side?Ground.Right:Ground.Left;
        const float Plant=FMath::Clamp(Output.Curve.Get(Side?FName(TEXT("GroundPlant_R")):FName(TEXT("GroundPlant_L"))),0.f,1.f);
        // Full mechanical sole contact begins at the planting threshold. Do not
        // freeze a partially rolled heel when the support latch first engages.
        const float WantedContact=S.Enabled && Sample.bHit?FMath::SmoothStep(S.ReleaseThreshold,S.PlantThreshold,Plant)*Ground.GroundAlpha:0.f;
        Contacts[Side]=FMath::FInterpConstantTo(Contacts[Side],WantedContact,Ground.DeltaSeconds,
            1.f/(WantedContact>Contacts[Side]?S.BlendInSeconds:S.BlendOutSeconds));
        const float Contact=Contacts[Side];
#if WITH_EDITOR
        if (bCaptureStructure)
        {
            StructureFrame.Plant[Side]=Plant; StructureFrame.Contact[Side]=Contact;
            StructureFrame.LockedBefore[Side]=Locks[Side].bLocked;
            for (int32 B=0;B<4;++B) StructureFrame.Input[Side][B]=Original[Side][B];
        }
#endif
        ContactWeights[Side]=Contact;
        FTransform Foot=Original[Side][2];
        ToeLocals[Side]=Original[Side][3].GetRelativeTransform(Foot);
        ToeLocals[Side].SetRotation(FQuat::Slerp(ToeLocals[Side].GetRotation(),RestToeLocal[Side].GetRotation(),Contact).GetNormalized());
        FVector Target=Foot.GetLocation();
        const FVector Along=Ground.MoveDirection*FVector::DotProduct(Target-RestAnkle[Side],Ground.MoveDirection);
        Target+=Along*(Ground.StrideScale-1.f)*Ground.GroundAlpha;
        FootRotations[Side]=Foot.GetRotation();
        auto& Lock=Locks[Side];
        if (Plant<S.ReleaseThreshold) Lock.bBlockedUntilSwing=false;
        if (Sample.bHit && S.Enabled)
        {
        const FVector NormalCS=World.InverseTransformVectorNoScale(Sample.bHeelHit?Sample.HeelNormal:Sample.ToeNormal).GetSafeNormal();
        const FVector CurrentUp=Foot.TransformVectorNoScale(LocalUp[Side]).GetSafeNormal();
        FQuat Tilt=FQuat::FindBetweenNormals(CurrentUp,NormalCS);
        const double Angle=Tilt.GetAngle(), Max=FMath::DegreesToRadians(S.MaxTiltDegrees);
        if (Angle>Max) Tilt=FQuat::Slerp(FQuat::Identity,Tilt,Max/Angle);
        FootRotations[Side]=(FQuat::Slerp(FQuat::Identity,Tilt,Contact)*Foot.GetRotation()).GetNormalized();
        Foot.SetLocation(Target); Foot.SetRotation(FootRotations[Side]);
        const FTransform Toe=ToeLocals[Side]*Foot;
        const FVector HeelWorld=World.TransformPosition(Foot.TransformPosition(Side?S.HeelRight:S.HeelLeft));
        const FVector ToeWorld=World.TransformPosition(Toe.TransformPosition(Side?S.ToeRight:S.ToeLeft));
        // The heel owns the ankle translation; the articulated toe is solved
        // separately below. Averaging two stair heights would float the heel.
        const float ContactDelta=Sample.bHeelHit?PlaneDelta(HeelWorld,Sample.Heel,Sample.HeelNormal):PlaneDelta(ToeWorld,Sample.Toe,Sample.ToeNormal);
        const float TerrainDelta=(Sample.bHeelHit?Sample.Heel.Z:Sample.Toe.Z)-Ground.FloorHeight;
        const float Correction=FMath::Clamp(FMath::Lerp(TerrainDelta,ContactDelta,Contact)+S.SoleOffset,-S.MaxFootCorrection,S.MaxFootCorrection);
        Corrections[Side]=Correction;
        Target+=World.InverseTransformVector(FVector(0,0,Correction*Ground.GroundAlpha));
        }
        const bool SameSurface=Sample.bCanLock && Lock.SurfaceId==Sample.SurfaceId && Lock.Revision==Sample.SurfaceRevision;
        if (SameSurface) Lock.Surface=Sample.Surface;
        const FVector Anchored=World.InverseTransformPosition(Lock.Surface.TransformPosition(Lock.Position));
        const FVector LockedForward=Lock.Surface.TransformVectorNoScale(Lock.BodyDirection);
        const float LockYaw=FMath::Abs(FMath::FindDeltaAngleDegrees(LockedForward.Rotation().Yaw,World.TransformVectorNoScale(FVector::RightVector).Rotation().Yaw));
        const bool UnsafeAnchor=FVector::Distance(Anchored,Target)>S.LockReleaseDistance || LockYaw>S.LockMaxYawDegrees
            || FVector::Distance(Anchored,ConstrainTarget(Side,Original[Side][0].GetLocation()+World.InverseTransformVector(FVector(0,0,PelvisOffset)),Anchored))>S.ContactHeightTolerance;
        if (Lock.bLocked && (Plant<S.ReleaseThreshold || !SameSurface || UnsafeAnchor || !S.Enabled))
        {
            Lock.bLocked=false;
            // A structural release cannot latch again in the same stance phase.
            Lock.bBlockedUntilSwing=Plant>=S.ReleaseThreshold;
        }
        Lock.Weight=FMath::FInterpConstantTo(Lock.Weight,Lock.bLocked?1.f:0.f,Ground.DeltaSeconds,1.f/S.LockReleaseSeconds);
        if (Lock.Weight>UE_SMALL_NUMBER)
        {
            // Independent of the sampled contact curve: dropping from 1 to 0
            // must not snap from a world anchor back to the animation trajectory.
            Target=FMath::Lerp(Target,Anchored,Lock.Weight);
            FootRotations[Side]=FQuat::Slerp(FootRotations[Side],World.GetRotation().Inverse()*Lock.Surface.GetRotation()*Lock.Rotation,Lock.Weight).GetNormalized();
        }
        FVector Offset=Target-Original[Side][2].GetLocation();
        AppliedOffsets[Side]=FMath::VInterpConstantTo(AppliedOffsets[Side],Offset,Ground.DeltaSeconds,S.MaxCorrectionSpeed);
        Targets[Side]=Original[Side][2].GetLocation()+AppliedOffsets[Side];
        if (bHasPreviousPose)
        {
            const FQuat Previous=World.GetRotation().Inverse()*PreviousFootWorld[Side];
            const double Angle=Previous.AngularDistance(FootRotations[Side]);
            if (Angle>UE_SMALL_NUMBER) FootRotations[Side]=FQuat::Slerp(Previous,FootRotations[Side],
                FMath::Min(1.,FMath::DegreesToRadians(S.MaxFootAngularSpeed)*Ground.DeltaSeconds/Angle)).GetNormalized();
        }
    }
    // Intersect the planted legs' reachable pelvis-height intervals before IK.
    // The old final ankle clamp silently lifted a foot after its lock was made.
    float Low=-S.PelvisDown,High=S.PelvisUp;
    for (int32 Side=0;Side<2;++Side) if (ContactWeights[Side]>UE_KINDA_SMALL_NUMBER)
    {
        const FVector Hip=World.TransformPosition(Original[Side][0].GetLocation()), Target=World.TransformPosition(Targets[Side]);
        const float Length=ReachAtAngle(Side,S.MaxKneeAngle)*World.GetScale3D().GetAbsMax();
        const float Height=FMath::Sqrt(FMath::Max(0.f,float(FMath::Square(Length)-FVector::DistSquared2D(Hip,Target))));
        Low=FMath::Max(Low,float(Target.Z-Hip.Z)-Height); High=FMath::Min(High,float(Target.Z-Hip.Z)+Height);
    }
    float DesiredPelvis=S.Enabled?FMath::Clamp(FMath::Min(Corrections[0],Corrections[1]),-S.PelvisDown,S.PelvisUp)*Ground.GroundAlpha:0.f;
    if (Low<=High) DesiredPelvis=FMath::Clamp(DesiredPelvis,Low,High);
    const float SmoothedPelvis=FMath::FInterpTo(PelvisOffset,DesiredPelvis,Ground.DeltaSeconds,S.InterpSpeed);
    PelvisOffset=FMath::FInterpConstantTo(PelvisOffset,SmoothedPelvis,Ground.DeltaSeconds,S.MaxPelvisSpeed);
    // Never hard-clamp the body to a changing reach interval after smoothing.
    // An unreachable foot releases instead of teleporting the entire torso.
#if WITH_EDITOR
    if (bCaptureStructure)
    {
        StructureFrame.Pelvis=PelvisOffset; StructureFrame.DesiredPelvis=DesiredPelvis;
        StructureFrame.Low=Low; StructureFrame.High=High;
    }
#endif
    const FVector PelvisDelta=World.InverseTransformVector(FVector(0,0,PelvisOffset));
    FTransform NewPelvis=Output.Pose.GetComponentSpaceTransform(PelvisIndex); NewPelvis.AddToTranslation(PelvisDelta);
    Out.Emplace(PelvisIndex,NewPelvis);
    for (int32 Side=0;Side<2;++Side)
    {
        const auto& Sample=Side?Ground.Right:Ground.Left; auto& Lock=Locks[Side];
        const float Contact=ContactWeights[Side];
        const FVector OldHip=Original[Side][0].GetLocation(), OldKnee=Original[Side][1].GetLocation(), OldAnkle=Original[Side][2].GetLocation();
        const float UpperLength=UpperLengths[Side], LowerLength=LowerLengths[Side];
        const FVector Hip=OldHip+PelvisDelta;
        const FVector Ankle=ConstrainTarget(Side,Hip,Targets[Side]);
        const bool bReachable=FVector::Distance(Ankle,Targets[Side])<.1f;
        if (!bReachable) { Lock.bLocked=false; Lock.bBlockedUntilSwing=true; }
        const FVector Axis=(Ankle-Hip).GetSafeNormal(); const float Reach=FVector::Distance(Hip,Ankle);
        FVector Pole=(OldKnee-OldHip)-Axis*FVector::DotProduct(OldKnee-OldHip,Axis);
        const FVector Hinge=(RestPole[Side]-Axis*FVector::DotProduct(RestPole[Side],Axis)).GetSafeNormal();
        if (!Pole.Normalize()) Pole=Hinge;
        const double Twist=FMath::Atan2(FVector::DotProduct(Axis,FVector::CrossProduct(Hinge,Pole)),FVector::DotProduct(Hinge,Pole));
        const double TwistLimit=FMath::DegreesToRadians(S.MaxKneeTwistDegrees);
        PoleTwists[Side]=FMath::FInterpTo(PoleTwists[Side],float(FMath::Clamp(Twist,-TwistLimit,TwistLimit)),Ground.DeltaSeconds,S.InterpSpeed);
        Pole=FQuat(Axis,PoleTwists[Side]).RotateVector(Hinge);
        const float Along=(UpperLength*UpperLength+Reach*Reach-LowerLength*LowerLength)/(2.f*Reach);
        const FVector Knee=Hip+Axis*Along+Pole*FMath::Sqrt(FMath::Max(0.f,UpperLength*UpperLength-Along*Along));
        const FVector HingeNormal=FVector::CrossProduct(Knee-Hip,Ankle-Knee).GetSafeNormal();
        const FQuat AlignHinge=FQuat::FindBetweenNormals(RestHingeNormal[Side],HingeNormal);
        FTransform Upper=Original[Side][0], Lower=Original[Side][1], Foot=Original[Side][2];
        // Reconstruct both rigid links on one hinge plane. Carrying arbitrary
        // input axial twist would still twist the armor despite safe joint positions.
        Upper.SetLocation(Hip); Upper.SetRotation((FQuat::FindBetweenVectors(AlignHinge.RotateVector(RestUpperDirection[Side]),Knee-Hip)
            *AlignHinge*RestUpperRotation[Side]).GetNormalized());
        Lower.SetLocation(Knee); Lower.SetRotation((FQuat::FindBetweenVectors(AlignHinge.RotateVector(RestLowerDirection[Side]),Ankle-Knee)
            *AlignHinge*RestLowerRotation[Side]).GetNormalized());
        Foot.SetLocation(Ankle); Foot.SetRotation(FootRotations[Side]);
        FTransform Toe=ToeLocals[Side]*Foot;
        if (Sample.bToeHit && Contact>UE_KINDA_SMALL_NUMBER)
        {
            const FVector Joint=World.TransformPosition(Toe.GetLocation());
            const FVector End=World.TransformPosition(Toe.TransformPosition(Side?S.ToeRight:S.ToeLeft));
            const FVector Offset=End-Joint,Normal=Sample.ToeNormal.GetSafeNormal();
            const double NormalDistance=FVector::DotProduct(Sample.Toe-Joint,Normal)+S.SoleOffset*Normal.Z;
            const FVector Tangent=(Offset-Normal*FVector::DotProduct(Offset,Normal)).GetSafeNormal();
            if (FMath::Abs(NormalDistance)<=Offset.Size() && !Tangent.IsNearlyZero())
            {
                const FVector Wanted=Tangent*FMath::Sqrt(FMath::Max(0.,Offset.SizeSquared()-NormalDistance*NormalDistance))+Normal*NormalDistance;
                FQuat Correction=FQuat::FindBetweenVectors(Offset,Wanted);
                const double Angle=Correction.GetAngle(),Limit=FMath::DegreesToRadians(S.MaxTiltDegrees);
                if (Angle>Limit) Correction=FQuat::Slerp(FQuat::Identity,Correction,Limit/Angle);
                const FQuat LocalCorrection=World.GetRotation().Inverse()*Correction*World.GetRotation();
                Toe.SetRotation((FQuat::Slerp(FQuat::Identity,LocalCorrection,Contact)*Toe.GetRotation()).GetNormalized());
            }
        }
        if (Lock.Weight>0) Toe.SetRotation(FQuat::Slerp(Toe.GetRotation(),World.GetRotation().Inverse()*Lock.Surface.GetRotation()*Lock.ToeRotation,Lock.Weight).GetNormalized());
        // Commit an anchor only after the no-stretch solve and both sole contacts.
        if (!Lock.bLocked && !Lock.bBlockedUntilSwing && Lock.Weight<=UE_SMALL_NUMBER && Sample.bCanLock
            && Contact>=Ground.GroundAlpha-UE_KINDA_SMALL_NUMBER && Contact>0 && bReachable)
        {
            const FVector Heel=World.TransformPosition(Foot.TransformPosition(Side?S.HeelRight:S.HeelLeft));
            const FVector Tip=World.TransformPosition(Toe.TransformPosition(Side?S.ToeRight:S.ToeLeft));
            if (FMath::Abs(PlaneDelta(Heel,Sample.Heel,Sample.HeelNormal)+S.SoleOffset)<=S.ContactHeightTolerance
                && FMath::Abs(PlaneDelta(Tip,Sample.Toe,Sample.ToeNormal)+S.SoleOffset)<=S.ContactHeightTolerance)
            {
                Lock.bLocked=true; Lock.SurfaceId=Sample.SurfaceId; Lock.Revision=Sample.SurfaceRevision;
                Lock.Surface=Sample.Surface;
                Lock.BodyDirection=Sample.Surface.InverseTransformVectorNoScale(World.TransformVectorNoScale(FVector::RightVector));
                Lock.Position=Sample.Surface.InverseTransformPosition(World.TransformPosition(Ankle));
                Lock.Rotation=(Sample.Surface.GetRotation().Inverse()*World.GetRotation()*Foot.GetRotation()).GetNormalized();
                Lock.ToeRotation=(Sample.Surface.GetRotation().Inverse()*World.GetRotation()*Toe.GetRotation()).GetNormalized();
            }
        }
        Out.Emplace(Indices[Side][0],Upper); Out.Emplace(Indices[Side][1],Lower);
        Out.Emplace(Indices[Side][2],Foot); Out.Emplace(Indices[Side][3],Toe);
        AppliedOffsets[Side]=Ankle-OldAnkle;
        PreviousFootWorld[Side]=(World.GetRotation()*Foot.GetRotation()).GetNormalized();
#if WITH_EDITOR
        if (bCaptureStructure)
        {
            StructureFrame.Targets[Side]=Targets[Side]; StructureFrame.LockedAfter[Side]=Lock.bLocked;
            StructureFrame.Result[Side][0]=Upper; StructureFrame.Result[Side][1]=Lower;
            StructureFrame.Result[Side][2]=Foot; StructureFrame.Result[Side][3]=Toe;
        }
#endif
    }
    bHasPreviousPose=true;
    Out.Sort(FCompareBoneTransformIndex());
}

void FAnimNode_GuLiGroundMechGrounding::EvaluateBlinkCrouch(FComponentSpacePoseContext& Output,TArray<FBoneTransform>& Out)
{
    const auto& Bones=Output.Pose.GetPose().GetBoneContainer();
    const auto PelvisIndex=Pelvis.GetCompactPoseIndex(Bones);
    const int32 Count=Bones.GetCompactPoseNumBones();
    TArray<FTransform> Old,New; Old.SetNum(Count); New.SetNum(Count);
    for (int32 I=0;I<Count;++I) Old[I]=Output.Pose.GetComponentSpaceTransform(FCompactPoseBoneIndex(I));
    auto Descends=[&](FCompactPoseBoneIndex Index)
    { for (;Index.IsValid();Index=Bones.GetParentBoneIndex(Index)) if (Index==PelvisIndex) return true; return false; };
    const float Angle=FMath::Clamp(Ground.BlinkKneeAngleDegrees,Ground.Settings.MinKneeAngle,Ground.Settings.MaxKneeAngle);
    float Drop=Ground.BlinkCrouchDepthCm/FMath::Max(.001f,float(FMath::Abs(Ground.ComponentToWorld.GetScale3D().Z)));
    // A common pelvis shift keeps both soles fixed. Limit it by each rigid chain's
    // target knee angle instead of translating the feet or stretching the legs.
    for (int32 Side=0;Side<2;++Side)
    {
        const FVector Delta=Old[LegBones[Side*4].GetCompactPoseIndex(Bones).GetInt()].GetLocation()
            -Old[LegBones[Side*4+2].GetCompactPoseIndex(Bones).GetInt()].GetLocation();
        const double ReachSquared=FMath::Square(UpperLengths[Side])+FMath::Square(LowerLengths[Side])
            -2.*UpperLengths[Side]*LowerLengths[Side]*FMath::Cos(FMath::DegreesToRadians(Angle));
        const double Height=FMath::Sqrt(FMath::Max(0.,ReachSquared-Delta.SizeSquared2D()));
        Drop=FMath::Min(Drop,float(FMath::Max(0.,Delta.Z-Height)));
    }
    const FVector Shift(0,0,-Drop*FMath::Clamp(Ground.BlinkAlpha,0.f,1.f));
    TArray<FBoneTransform,TInlineAllocator<9>> Overrides;
    FTransform Body=Old[PelvisIndex.GetInt()]; Body.AddToTranslation(Shift); Overrides.Emplace(PelvisIndex,Body);
    for (int32 Side=0;Side<2;++Side)
    {
        const auto U=LegBones[Side*4].GetCompactPoseIndex(Bones), L=LegBones[Side*4+1].GetCompactPoseIndex(Bones);
        const auto F=LegBones[Side*4+2].GetCompactPoseIndex(Bones), T=LegBones[Side*4+3].GetCompactPoseIndex(Bones);
        FTransform Upper=Old[U.GetInt()],Lower=Old[L.GetInt()];
        const FVector Hip=Upper.GetLocation()+Shift,Ankle=Old[F.GetInt()].GetLocation();
        const FVector Axis=(Ankle-Hip).GetSafeNormal();
        const double Distance=FMath::Clamp(FVector::Distance(Hip,Ankle),
            double(FMath::Abs(UpperLengths[Side]-LowerLengths[Side])+.001f),
            double(UpperLengths[Side]+LowerLengths[Side]-.001f));
        const double Along=(FMath::Square(UpperLengths[Side])-FMath::Square(LowerLengths[Side])+Distance*Distance)/(2.*Distance);
        const double Height=FMath::Sqrt(FMath::Max(0.,FMath::Square(UpperLengths[Side])-Along*Along));
        FVector Pole=FQuat::FindBetweenNormals(RestAxis[Side],Axis).RotateVector(RestPole[Side]);
        Pole=(Pole-Axis*FVector::DotProduct(Pole,Axis)).GetSafeNormal();
        const FVector Knee=Hip+Axis*Along+Pole*Height;
        const FVector Hinge=FVector::CrossProduct(Knee-Hip,Ankle-Knee).GetSafeNormal();
        const FQuat Align=FQuat::FindBetweenNormals(RestHingeNormal[Side],Hinge);
        Upper.SetLocation(Hip); Upper.SetRotation((FQuat::FindBetweenVectors(Align.RotateVector(RestUpperDirection[Side]),Knee-Hip)
            *Align*RestUpperRotation[Side]).GetNormalized());
        Lower.SetLocation(Knee); Lower.SetRotation((FQuat::FindBetweenVectors(Align.RotateVector(RestLowerDirection[Side]),Ankle-Knee)
            *Align*RestLowerRotation[Side]).GetNormalized());
        Overrides.Emplace(U,Upper); Overrides.Emplace(L,Lower);
        Overrides.Emplace(F,Old[F.GetInt()]); Overrides.Emplace(T,Old[T.GetInt()]);
    }
    for (int32 I=0;I<Count;++I)
    {
        const FCompactPoseBoneIndex Index(I); const auto Parent=Bones.GetParentBoneIndex(Index);
        if (const auto* Override=Overrides.FindByPredicate([&](const FBoneTransform& B){return B.BoneIndex==Index;})) New[I]=Override->Transform;
        else New[I]=Parent.IsValid()?Old[I].GetRelativeTransform(Old[Parent.GetInt()])*New[Parent.GetInt()]:Old[I];
        if (Descends(Index)) Out.Emplace(Index,New[I]);
    }
    Out.Sort(FCompareBoneTransformIndex());
}
