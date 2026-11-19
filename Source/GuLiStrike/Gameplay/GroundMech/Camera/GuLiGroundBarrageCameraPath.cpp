#include "Gameplay/GroundMech/Camera/GuLiGroundBarrageCameraPath.h"
#include "Gameplay/Data/Generated/GuLiStrikeRogueCardsTableRows.h"

namespace
{
void SetClampedCameraTangents(FInterpCurveVector& Curve)
{
    // UE 5.7's clamped vector helper reads contiguous floats. The camera packs
    // three independent channels in an LWC FVector, whose components are doubles.
    // AddPoint initializes both endpoints with stationary tangents.
    for (int32 Index = 1; Index + 1 < Curve.Points.Num(); ++Index)
    {
        const auto& Previous = Curve.Points[Index - 1];
        auto& Key = Curve.Points[Index];
        const auto& Next = Curve.Points[Index + 1];
        for (int32 Axis = 0; Axis < 3; ++Axis)
        {
            const float Tangent = ClampFloatTangent(
                static_cast<float>(Previous.OutVal[Axis]), Previous.InVal,
                static_cast<float>(Key.OutVal[Axis]), Key.InVal,
                static_cast<float>(Next.OutVal[Axis]), Next.InVal);
            Key.ArriveTangent[Axis] = Tangent;
            Key.LeaveTangent[Axis] = Tangent;
        }
    }
}
}

bool FGuLiGroundBarrageCameraPath::Load(const FGuLiStrikeRogueCardsCardsRow& Card)
{
    *this = {};
    const auto* Table = Cast<UDataTable>(Card.CastCamera.LoadSynchronous());
    using FKey = FGuLiStrikeRogueCardsBarrageCameraRow;
    if (!Table || Table->GetRowStruct() != FKey::StaticStruct() || Card.CastCameraReturnSeconds <= 0.f) return false;
    const float Lengths[] = {Card.CastFoldSeconds, Card.CastCrossedHoldSeconds, Card.CastOpenSeconds, Card.CastCameraReturnSeconds};
    const float Start[] = {0.f, Lengths[0], Lengths[0]+Lengths[1], Lengths[0]+Lengths[1]+Lengths[2]};
    TArray<FKey*> Keys;
    Table->GetAllRows(TEXT("Ranger barrage camera"), Keys);
    for (const FKey* K : Keys)
    {
        const int32 Phase = K->Phase==TEXT("Fold") ? 0 : K->Phase==TEXT("Hold") ? 1 :
            K->Phase==TEXT("Open") ? 2 : K->Phase==TEXT("Return") ? 3 : INDEX_NONE;
        if (Phase==INDEX_NONE || !FMath::IsWithinInclusive(K->PhaseAlpha,0.f,1.f)) return false;
        if (Lengths[Phase] <= 0.f) continue;
        const float T = Start[Phase] + K->PhaseAlpha*Lengths[Phase];
        auto Add = [T](FInterpCurveVector& Curve, FVector Value)
        {
            const int32 Index = Curve.AddPoint(T,Value);
            Curve.Points[Index].InterpMode = CIM_CurveAutoClamped;
        };
        Add(Orbit,{K->AzimuthDegrees,K->RadiusCm,K->EyeHeightCm});
        Add(Focus,{K->FocusForwardCm,K->FocusRightCm,K->FocusHeightCm});
        Add(Lens,{K->FieldOfViewDegrees,K->RollDegrees,K->ShotWeight});
        const float CueTime = Phase == 3 && Card.CastCameraReturnCueSeconds > 0.f
            ? Start[Phase] + K->PhaseAlpha * Card.CastCameraReturnCueSeconds : T;
        Effects.AddPoint(CueTime,{K->FlashAlpha,K->ChargeArcScale});
    }
    const float End = Start[3]+Lengths[3];
    if (Orbit.Points.Num()<2 || !FMath::IsNearlyZero(Orbit.Points[0].InVal) ||
        !FMath::IsNearlyEqual(Orbit.Points.Last().InVal,End)) return false;
    SetClampedCameraTangents(Orbit);
    SetClampedCameraTangents(Focus);
    SetClampedCameraTangents(Lens);
    Duration = End;
    return true;
}

bool FGuLiGroundBarrageCameraPath::Sample(float Seconds, FGuLiGroundBarrageCameraShot& Out) const
{
    if (Duration<=0.f || Seconds<0.f || Seconds>Duration) return false;
    const FVector O = Orbit.Eval(Seconds), L = Lens.Eval(Seconds);
    double Sine, Cosine;
    FMath::SinCos(&Sine,&Cosine,FMath::DegreesToRadians(O.X));
    Out.EyeFromFeet = FVector(O.Y*Cosine,O.Y*Sine,O.Z);
    Out.Rotation = (Focus.Eval(Seconds)-Out.EyeFromFeet).Rotation();
    Out.Rotation.Roll = L.Y;
    Out.Fov = L.X;
    Out.Weight = FMath::Clamp(float(L.Z),0.f,1.f);
    Out.Flash = FMath::Clamp(float(Effects.Eval(Seconds).X),0.f,1.f);
    return true;
}

float FGuLiGroundBarrageCameraPath::ChargeArcScale(float Seconds) const
{
    return Duration>0.f && Seconds>=0.f && Seconds<Duration ? FMath::Max(0.,Effects.Eval(Seconds).Y) : 0.f;
}
