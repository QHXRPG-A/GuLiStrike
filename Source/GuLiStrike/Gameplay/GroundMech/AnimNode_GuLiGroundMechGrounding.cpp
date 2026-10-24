#include "Gameplay/GroundMech/AnimNode_GuLiGroundMechGrounding.h"

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
    for (int32 Side=0;Side<2;++Side)
    {
        auto Index=LegBones[Side*4+2].GetCompactPoseIndex(Bones);
        FTransform Ref=Bones.GetRefPoseTransform(Index);
        while ((Index=Bones.GetParentBoneIndex(Index))!=INDEX_NONE) Ref=Ref*Bones.GetRefPoseTransform(Index);
        RestAnkle[Side]=Ref.GetLocation(); LocalUp[Side]=Ref.InverseTransformVectorNoScale(FVector::UpVector);
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
    { Locks[0]=Locks[1]=FLock{}; PelvisOffset=0.f; LastRevision=Ground.Revision; }
    if (Ground.GroundAlpha<=UE_SMALL_NUMBER)
    { Locks[0].bLocked=Locks[1].bLocked=false; PelvisOffset=0; return; }
    const auto PelvisIndex=Pelvis.GetCompactPoseIndex(Bones);
    const FTransform World=Ground.ComponentToWorld;
    FVector Targets[2]; FQuat FootRotations[2]; float Corrections[2]={0,0};
    FTransform Original[2][4]; TArray<FCompactPoseBoneIndex,TInlineAllocator<4>> Indices[2];
    for (int32 Side=0;Side<2;++Side)
    {
        for (int32 B=0;B<4;++B)
        { Indices[Side].Add(LegBones[Side*4+B].GetCompactPoseIndex(Bones)); Original[Side][B]=Output.Pose.GetComponentSpaceTransform(Indices[Side][B]); }
        const auto& Sample=Side?Ground.Right:Ground.Left;
        const float Plant=FMath::Clamp(Output.Curve.Get(Side?FName(TEXT("GroundPlant_R")):FName(TEXT("GroundPlant_L"))),0.f,1.f);
        FTransform Foot=Original[Side][2]; FTransform Toe=Original[Side][3];
        FVector Target=Foot.GetLocation();
        const FVector Along=Ground.MoveDirection*FVector::DotProduct(Target-RestAnkle[Side],Ground.MoveDirection);
        Target+=Along*(Ground.StrideScale-1.f)*Ground.GroundAlpha;
        FootRotations[Side]=Foot.GetRotation();
        auto& Lock=Locks[Side];
        if (!Sample.bHit || !S.Enabled)
        { Lock.bLocked=false; Targets[Side]=Target; continue; }
        const FVector NormalCS=World.InverseTransformVectorNoScale(Sample.Normal).GetSafeNormal();
        const FVector CurrentUp=Foot.TransformVectorNoScale(LocalUp[Side]).GetSafeNormal();
        FQuat Tilt=FQuat::FindBetweenNormals(CurrentUp,NormalCS);
        const double Angle=Tilt.GetAngle(); const double Max=FMath::DegreesToRadians(S.MaxTiltDegrees);
        if (Angle>Max) Tilt=FQuat::Slerp(FQuat::Identity,Tilt,Max/Angle);
        FootRotations[Side]=(FQuat::Slerp(FQuat::Identity,Tilt,Plant*Ground.GroundAlpha)*Foot.GetRotation()).GetNormalized();
        // Measure contact AFTER foot rotation/stride. Measuring the old rotated sole produces floating feet.
        const FTransform ToeLocal=Toe.GetRelativeTransform(Foot);
        Foot.SetLocation(Target); Foot.SetRotation(FootRotations[Side]); Toe=ToeLocal*Foot;
        const FVector HeelWorld=World.TransformPosition(Foot.TransformPosition(Side?S.HeelRight:S.HeelLeft));
        const FVector ToeWorld=World.TransformPosition(Toe.TransformPosition(Side?S.ToeRight:S.ToeLeft));
        const auto PlaneHeight=[&](const FVector& P,const FVector& OnPlane)
        { return OnPlane.Z-((P.X-OnPlane.X)*Sample.Normal.X+(P.Y-OnPlane.Y)*Sample.Normal.Y)/FMath::Max(.1,Sample.Normal.Z); };
        // Swing retains the authored lift; a planted sole converges to both sampled contact points.
        const float ContactDelta=FMath::Max(PlaneHeight(HeelWorld,Sample.Heel)-HeelWorld.Z,PlaneHeight(ToeWorld,Sample.Toe)-ToeWorld.Z);
        const float TerrainDelta=(Sample.Heel.Z+Sample.Toe.Z)*.5f-Ground.FloorHeight;
        const float Correction=FMath::Clamp(FMath::Lerp(TerrainDelta,ContactDelta,Plant)+S.SoleOffset,-S.MaxFootCorrection,S.MaxFootCorrection);
        Corrections[Side]=Correction;
        Target+=World.InverseTransformVector(FVector(0,0,Correction*Ground.GroundAlpha));
        if (Lock.bLocked && (Plant<S.ReleaseThreshold || !Sample.bCanLock || Lock.SurfaceId!=Sample.SurfaceId
            || Lock.Revision!=Sample.SurfaceRevision || FVector::Distance(Sample.Surface.TransformPosition(Lock.Position),World.TransformPosition(Target))>S.LockReleaseDistance)) Lock.bLocked=false;
        if (!Lock.bLocked && Sample.bCanLock && Plant>=S.PlantThreshold)
        {
            Lock.bLocked=true; Lock.SurfaceId=Sample.SurfaceId; Lock.Revision=Sample.SurfaceRevision;
            Lock.Position=Sample.Surface.InverseTransformPosition(World.TransformPosition(Target));
            Lock.Rotation=(Sample.Surface.GetRotation().Inverse()*World.GetRotation()*FootRotations[Side]).GetNormalized();
        }
        if (Lock.bLocked)
        {
            const FVector Anchored=World.InverseTransformPosition(Sample.Surface.TransformPosition(Lock.Position));
            Target=FMath::Lerp(Target,Anchored,Plant*Ground.GroundAlpha);
            FootRotations[Side]=FQuat::Slerp(FootRotations[Side],World.GetRotation().Inverse()*Sample.Surface.GetRotation()*Lock.Rotation,Plant*Ground.GroundAlpha).GetNormalized();
        }
        Targets[Side]=Target;
    }
    const float DesiredPelvis=S.Enabled?FMath::Clamp(FMath::Min(Corrections[0],Corrections[1]),-S.PelvisDown,S.PelvisUp)*Ground.GroundAlpha:0.f;
    PelvisOffset=FMath::FInterpTo(PelvisOffset,DesiredPelvis,Ground.DeltaSeconds,S.InterpSpeed);
    const FVector PelvisDelta=World.InverseTransformVector(FVector(0,0,PelvisOffset));
    FTransform NewPelvis=Output.Pose.GetComponentSpaceTransform(PelvisIndex); NewPelvis.AddToTranslation(PelvisDelta);
    Out.Emplace(PelvisIndex,NewPelvis);
    for (int32 Side=0;Side<2;++Side)
    {
        const FVector OldHip=Original[Side][0].GetLocation(), OldKnee=Original[Side][1].GetLocation(), OldAnkle=Original[Side][2].GetLocation();
        const float UpperLength=FVector::Distance(OldHip,OldKnee), LowerLength=FVector::Distance(OldKnee,OldAnkle);
        const FVector Hip=OldHip+PelvisDelta;
        FVector Axis=(Targets[Side]-Hip).GetSafeNormal(); if (Axis.IsNearlyZero()) Axis=(OldAnkle-OldHip).GetSafeNormal();
        const float Reach=FMath::Clamp(float(FVector::Distance(Hip,Targets[Side])),FMath::Abs(UpperLength-LowerLength)+.01f,UpperLength+LowerLength-.01f);
        const FVector Ankle=Hip+Axis*Reach;
        FVector Pole=(OldKnee-OldHip)-Axis*FVector::DotProduct(OldKnee-OldHip,Axis);
        if (!Pole.Normalize()) { Pole=FVector::CrossProduct(Axis,FVector::RightVector).GetSafeNormal(); }
        const float Along=(UpperLength*UpperLength+Reach*Reach-LowerLength*LowerLength)/(2.f*Reach);
        const FVector Knee=Hip+Axis*Along+Pole*FMath::Sqrt(FMath::Max(0.f,UpperLength*UpperLength-Along*Along));
        FTransform Upper=Original[Side][0], Lower=Original[Side][1], Foot=Original[Side][2];
        Upper.SetLocation(Hip); Upper.SetRotation((FQuat::FindBetweenVectors(OldKnee-OldHip,Knee-Hip)*Upper.GetRotation()).GetNormalized());
        Lower.SetLocation(Knee); Lower.SetRotation((FQuat::FindBetweenVectors(OldAnkle-OldKnee,Ankle-Knee)*Lower.GetRotation()).GetNormalized());
        Foot.SetLocation(Ankle); Foot.SetRotation(FootRotations[Side]);
        const FTransform Toe=Original[Side][3].GetRelativeTransform(Original[Side][2])*Foot;
        Out.Emplace(Indices[Side][0],Upper); Out.Emplace(Indices[Side][1],Lower);
        Out.Emplace(Indices[Side][2],Foot); Out.Emplace(Indices[Side][3],Toe);
    }
    Out.Sort(FCompareBoneTransformIndex());
}
