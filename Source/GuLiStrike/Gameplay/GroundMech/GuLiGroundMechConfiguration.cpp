#include "Gameplay/GroundMech/GuLiGroundMechConfiguration.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimMontage.h"
#include "Animation/BlendSpace.h"
#include "Animation/Skeleton.h"
#include "Engine/DataTable.h"
#include "Misc/SecureHash.h"

bool FGuLiGroundMechConfiguration::Load(FString& Error)
{
    bReady=false; Gaits.Reset(); Assets.Reset(); FString Canonical;
    auto Table=[&](const TCHAR* S,UScriptStruct* Type)->UDataTable*
    {
        const FString Name=FString(TEXT("DT_GuLiStrikeMech_"))+S;
        auto* T=LoadObject<UDataTable>(nullptr,*(TEXT("/Game/GuLiStrike/Data/")+Name+TEXT(".")+Name));
        if (!T || T->GetRowStruct()!=Type) { Error=TEXT("Missing Ground tuning table/schema: ")+Name; return nullptr; }
        TArray<FName> Names=T->GetRowNames(); Names.Sort(FNameLexicalLess());
        Canonical+=Name;
        for (FName N:Names)
        {
            FString Text; Type->ExportText(Text,T->GetRowMap().FindChecked(N),nullptr,nullptr,PPF_None,nullptr);
            Canonical+=N.ToString()+TEXT(":")+Text+TEXT("\n");
        }
        return T;
    };
    auto* M=Table(TEXT("Mobility"),FGuLiStrikeMechMobilityRow::StaticStruct());
    auto* I=Table(TEXT("GroundIK"),FGuLiStrikeMechGroundIKRow::StaticStruct());
    auto* C=Table(TEXT("Camera"),FGuLiStrikeMechCameraRow::StaticStruct());
    auto* G=Table(TEXT("Gait"),FGuLiStrikeMechGaitRow::StaticStruct());
    auto* Skills=Table(TEXT("Skills"),FGuLiStrikeMechSkillsRow::StaticStruct());
    auto* Upgrades=Table(TEXT("Upgrades"),FGuLiStrikeMechUpgradesRow::StaticStruct());
    if (!M || !I || !C || !G || !Skills || !Upgrades) return false;
    const auto* Rocket=Skills->FindRow<FGuLiStrikeMechSkillsRow>(TEXT("RocketJump"),TEXT("Ground air tuning"));
    if (!Rocket || !FMath::IsFinite(Rocket->AirSpeedMultiplier) || Rocket->AirSpeedMultiplier<=0
        || !FMath::IsFinite(Rocket->JetForwardSpeedMultiplier) || Rocket->JetForwardSpeedMultiplier<=0)
    { Error=TEXT("Ground air tuning requires positive base/jet-forward speed multipliers"); return false; }
    const auto* MR=M->FindRow<FGuLiStrikeMechMobilityRow>(TEXT("Default"),TEXT("Ground tuning"));
    const auto* IR=I->FindRow<FGuLiStrikeMechGroundIKRow>(TEXT("Default"),TEXT("Ground tuning"));
    const auto* FC=C->FindRow<FGuLiStrikeMechCameraRow>(TEXT("Free"),TEXT("Ground tuning"));
    const auto* AC=C->FindRow<FGuLiStrikeMechCameraRow>(TEXT("Aim"),TEXT("Ground tuning"));
    if (!MR || !IR || !FC || !AC) { Error=TEXT("Ground tuning has missing Default/Free/Aim row"); return false; }
    Mobility=*MR; IK=*IR; FreeCamera=*FC; AimCamera=*AC;
    if (!(Mobility.BlinkDistance>0 && Mobility.BlinkDistance<=5000 && Mobility.BlinkCooldownSeconds<=60
        && Mobility.BlinkWindupSeconds>0 && Mobility.BlinkWindupSeconds<=1 && Mobility.BlinkRecoverySeconds>0 && Mobility.BlinkRecoverySeconds<=1
        && Mobility.BlinkLoopSeconds>0 && Mobility.BlinkLoopSeconds<=1 && Mobility.BlinkLoopSwayDegrees>=0 && Mobility.BlinkLoopSwayDegrees<=5
        && Mobility.BlinkCooldownSeconds>=Mobility.BlinkWindupSeconds+Mobility.BlinkLoopSeconds+Mobility.BlinkRecoverySeconds
        && Mobility.BlinkArmBackDegrees>=90 && Mobility.BlinkArmBackDegrees<=150 && Mobility.BlinkBodyLeanDegrees>=0 && Mobility.BlinkBodyLeanDegrees<=30
        && Mobility.BlinkAfterimageCount>=1 && Mobility.BlinkAfterimageCount<=6 && Mobility.BlinkAfterimageSeconds>0 && Mobility.BlinkAfterimageSeconds<=1
        && Mobility.BlinkAfterimageOpacity>0 && Mobility.BlinkAfterimageOpacity<=1))
    { Error=TEXT("Invalid Ground blink distance/timing/pose/afterimage tuning"); return false; }
    if (!FMath::IsFinite(Mobility.SprintSpeed*Rocket->JetForwardSpeedMultiplier)
        || Mobility.SprintSpeed*Rocket->JetForwardSpeedMultiplier<Mobility.WalkSpeed*Rocket->AirSpeedMultiplier)
    { Error=TEXT("Ground jet-forward speed must not be below ordinary air speed"); return false; }
    for (const auto& Pair:G->GetRowMap())
    {
        const auto& Row=*reinterpret_cast<const FGuLiStrikeMechGaitRow*>(Pair.Value);
        UObject* Loaded=Row.Asset.LoadSynchronous();
        const FString Name=Pair.Key.ToString();
        const bool TypeOK=Name.EndsWith(TEXT("Blend"))?Cast<UBlendSpace>(Loaded)!=nullptr:
            Name.StartsWith(TEXT("Turn"))?Cast<UAnimMontage>(Loaded)!=nullptr:Cast<UAnimSequence>(Loaded)!=nullptr;
        if (!TypeOK || !FMath::IsFinite(Row.ReferenceSpeed) || Row.ReferenceSpeed<=0 || Row.MinStrideScale<=0
            || Row.MaxStrideScale<Row.MinStrideScale || Row.MinPlayRate<=0 || Row.MaxPlayRate<Row.MinPlayRate
            || Row.TransitionSeconds<=0 || Row.SpeedSmoothingSeconds<=0 || Row.BasePlayRate<=0
            || Row.HorizontalMotionScale<=0 || Row.SwingLiftScale<=0 || Row.PelvisMotionScale<=0
            || Row.SafeMaxStrideScale<Row.MinStrideScale || Row.SafeMaxStrideScale>Row.MaxStrideScale
            || Row.MovingTurnMaxWeight<0 || Row.MovingTurnMaxWeight>1)
        { Error=TEXT("Invalid Ground gait reference/units/ranges: ")+Name; return false; }
        if (Name==TEXT("Shoot") && (!FMath::IsFinite(Row.ArmRecoilDegrees) || Row.ArmRecoilDegrees<0 || Row.ArmRecoilDegrees>30
            || !FMath::IsFinite(Row.ArmRecoilAttackSeconds) || Row.ArmRecoilAttackSeconds<=0 || Row.ArmRecoilAttackSeconds>.1f
            || !FMath::IsFinite(Row.ArmRecoilHoldSeconds) || Row.ArmRecoilHoldSeconds<0 || Row.ArmRecoilHoldSeconds>.1f
            || !FMath::IsFinite(Row.ArmRecoilRecoverySeconds) || Row.ArmRecoilRecoverySeconds<=Row.ArmRecoilAttackSeconds || Row.ArmRecoilRecoverySeconds>1.f))
        { Error=TEXT("Invalid whole-arm recoil angle/timing (degrees/seconds)"); return false; }
        if (auto* Montage=Cast<UAnimMontage>(Loaded); Montage && (Montage->SlotAnimTracks.Num()!=1
            || Montage->SlotAnimTracks[0].SlotName!=TEXT("DefaultSlot") || FMath::Abs(Row.TurnDegrees)<1
            || Montage->SlotAnimTracks[0].AnimTrack.AnimSegments.Num()!=1
            || !Cast<UAnimSequence>(Montage->SlotAnimTracks[0].AnimTrack.AnimSegments[0].GetAnimReference())))
        { Error=TEXT("Ground turn requires calibrated yaw and DefaultSlot: ")+Name; return false; }
        Gaits.Add(Pair.Key,Row); Assets.Add(Pair.Key,Loaded);
        if (Name.StartsWith(TEXT("Turn")))
        {
            auto* Additive=Cast<UAnimSequence>(Row.MovingTurnAsset.LoadSynchronous());
            if (!Additive || !Additive->IsValidAdditive() || Additive->GetSkeleton()!=CastChecked<UAnimationAsset>(Loaded)->GetSkeleton())
            { Error=TEXT("Ground moving turn requires a derived local additive: ")+Name; return false; }
            Assets.Add(FName(TEXT("Moving")+Name),Additive);
        }
    }
    for (const TCHAR* Name:{TEXT("Idle"),TEXT("WalkForward"),TEXT("WalkBackward"),TEXT("StrafeLeft"),TEXT("StrafeRight"),
        TEXT("RunForward"),TEXT("RunBackward"),TEXT("TurnLeft"),TEXT("TurnRight"),TEXT("JumpStart"),TEXT("JumpLoop"),TEXT("Shoot"),TEXT("WalkBlend"),TEXT("RunBlend")})
        if (!Gaits.Contains(Name)) { Error=TEXT("Missing Ground gait: ")+FString(Name); return false; }
    const auto* Skeleton=Asset<UBlendSpace>(TEXT("WalkBlend"))->GetSkeleton();
    for (const auto& Pair:Assets)
        if (!Skeleton || CastChecked<UAnimationAsset>(Pair.Value)->GetSkeleton()!=Skeleton)
        { Error=TEXT("Ground gait assets must share the production skeleton: ")+Pair.Key.ToString(); return false; }
    const auto& Bones=Skeleton->GetReferenceSkeleton();
    for (const TCHAR* Suffix:{TEXT("L"),TEXT("R")})
    {
        const int32 Hip=Bones.FindBoneIndex(FName(FString(TEXT("UpperLeg_"))+Suffix));
        const int32 Knee=Bones.FindBoneIndex(FName(FString(TEXT("Leg_"))+Suffix));
        const int32 Foot=Bones.FindBoneIndex(FName(FString(TEXT("Foot_"))+Suffix));
        const int32 Toe=Bones.FindBoneIndex(FName(FString(TEXT("FootEnd_"))+Suffix));
        if (Hip<0 || Knee<0 || Foot<0 || Toe<0 || Bones.GetParentIndex(Knee)!=Hip || Bones.GetParentIndex(Foot)!=Knee || Bones.GetParentIndex(Toe)!=Foot)
        { Error=TEXT("Ground IK requires UpperLeg > Leg > Foot > FootEnd on both sides"); return false; }
    }
    auto CameraOK=[](const FGuLiStrikeMechCameraRow& R)
    { return R.MinDistance>0 && R.MinDistance<=R.ArmLength && R.ArmLength<=R.MaxDistance && R.FieldOfView>=10
        && R.FieldOfView<=120 && R.MinPitchDegrees<R.MaxPitchDegrees && R.TransitionSeconds>0 && R.Sensitivity>0
        && R.BlinkHoldSeconds>=0 && R.BlinkHoldSeconds<=.25f && R.BlinkCatchupSeconds>=.1f && R.BlinkCatchupSeconds<=2.f; };
    if (!(Mobility.WalkSpeed>0 && Mobility.SprintSpeed>=Mobility.WalkSpeed && Mobility.Acceleration>0 && Mobility.Braking>0
        && Mobility.TurnStartDegrees>Mobility.TurnStopDegrees && Mobility.TurnStopDegrees>0 && Mobility.MovingTurnRate>0
        && Mobility.GunReturnSeconds>0 && Mobility.GunYawLimit>0 && Mobility.GunYawLimit<180 && Mobility.GunPitchUpLimit>0 && Mobility.GunPitchUpLimit<90
        && Mobility.GunPitchDownLimit>0 && Mobility.GunPitchDownLimit<90 && CameraOK(FreeCamera) && CameraOK(AimCamera)
        && IK.TraceUp>0 && IK.TraceDown>0 && IK.MaxFootCorrection>0 && IK.PelvisDown>=0 && IK.PelvisUp>=0
        && IK.PlantThreshold>IK.ReleaseThreshold && IK.BlendInSeconds>0 && IK.BlendOutSeconds>0 && IK.InterpSpeed>0
        && IK.MinKneeAngle>=5 && IK.MinKneeAngle<IK.MaxKneeAngle && IK.MaxKneeAngle<=170
        && IK.MaxLegSwingDegrees>0 && IK.MaxLegSwingDegrees<=60 && IK.MaxKneeTwistDegrees>=0 && IK.MaxKneeTwistDegrees<=45
        && IK.MinFootSeparation>0 && IK.MinFootSeparation<=100 && IK.LockMaxYawDegrees>0 && IK.LockMaxYawDegrees<=60
        && IK.LockReleaseSeconds>0 && IK.LockReleaseSeconds<=1 && IK.MaxPelvisSpeed>0 && IK.MaxPelvisSpeed<=2000
        && IK.MaxCorrectionSpeed>0 && IK.MaxCorrectionSpeed<=2000 && IK.MaxFootAngularSpeed>0 && IK.MaxFootAngularSpeed<=2000
        && !IK.HeelLeft.IsNearlyZero() && !IK.ToeLeft.IsNearlyZero() && !IK.HeelRight.IsNearlyZero() && !IK.ToeRight.IsNearlyZero()))
    { Error=TEXT("Invalid or uncalibrated Ground mobility/IK/camera contract"); return false; }
    Fingerprint=FMD5::HashAnsiString(*Canonical); bReady=true; return true;
}
