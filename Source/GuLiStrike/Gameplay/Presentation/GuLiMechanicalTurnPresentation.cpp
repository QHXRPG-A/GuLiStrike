#include "Gameplay/Presentation/GuLiMechanicalAnimation.h"

namespace
{
	float DampedAngle(float Current, float Target, float Dt, const FGuLiMechanicalAnimationConfig& C, float Scale)
	{
		const float Delta = FMath::FindDeltaAngleDegrees(Current, Target);
		const float Step = Delta * (1.0f - FMath::Exp(-C.VisualYawDamping * Scale * Dt));
		return FRotator::NormalizeAxis(FMath::Abs(Delta - Step) < C.VisualYawSnapDegrees ? Target : Current + Step);
	}
	float Bank(float Current, float Target, float Dt, float Enter, float Return)
	{
		const bool bReturning = FMath::Abs(Target) < FMath::Abs(Current) && Current * Target >= 0;
		const float Value = FMath::Lerp(Current, Target, 1.0f - FMath::Exp(-2.995732274f * Dt / (bReturning ? Return : Enter)));
		return Target == 0 && FMath::Abs(Value) < .0001f ? 0 : Value;
	}
	FQuat UpperRotation(const FGuLiMechanicalVisualState& V)
	{
		return FQuat(FVector::ForwardVector, V.UpperBankRadians)
			* FRotator(0, FMath::FindDeltaAngleDegrees(V.Root.Rotator().Yaw, V.UpperYawDegrees), 0).Quaternion();
	}
	FVector2D DiscTilt(const FGuLiMechanicalAnimationConfig& C, const FGuLiMechanicalAnimationState& S,
		const FGuLiMechanicalVisualState& V)
	{
		FVector2D Tilt = S.DiscTiltRadians + FVector2D(V.DiscBankRadians, 0);
		const double Limit = FMath::DegreesToRadians(C.MaximumDiscTilt);
		if (Tilt.SizeSquared() > Limit * Limit) Tilt = Tilt.GetSafeNormal() * Limit;
		return Tilt;
	}
	FQuat TiltRotation(const FVector2D& Tilt)
	{
		const double Angle = Tilt.Size();
		return Angle > UE_SMALL_NUMBER ? FQuat(FVector(Tilt.X, Tilt.Y, 0) / Angle, Angle) : FQuat::Identity;
	}
	FVector LegTranslation(const FGuLiMechanicalAnimationConfig& C, const FGuLiMechanicalVisualState& V, int32 I)
	{
		if (!C.bHasTurnRig) return FVector::ZeroVector;
		const FQuat Hip(C.LegAxes[I], -FMath::Sign(C.LegEnds[I].Y) * V.LegBankRadians);
		// The ankle counter-rotates around its pin; its neutral orientation is preserved.
		return C.LegRoots[I] + Hip.RotateVector(C.LegEnds[I] - C.LegRoots[I]) - C.LegEnds[I];
	}
}

void GuLiMechanicalAnimation::StepVisualTurn(const FGuLiMechanicalAnimationConfig& C,
	const FGuLiMechanicalAnimationState& S, const FTransform& Logical, double Now, bool bReset,
	FGuLiMechanicalVisualState& V, float YawResponseScale)
{
	if (C.Model != EGuLiMechanicalModel::WarMachine || Logical.ContainsNaN() || !FMath::IsFinite(Now)) return;
	const float TargetUpper = S.bInitialized ? S.UpperYawDegrees : Logical.Rotator().Yaw;
	const float Scale = FMath::IsFinite(YawResponseScale) ? FMath::Max(0.f, YawResponseScale) : 1.f;
	const bool bInitialize = bReset || !V.bInitialized || Now < V.PoseTime;
	const float Dt = bInitialize ? 0 : float(FMath::Max(0.0, Now - V.PoseTime));
	if (bInitialize)
	{
		V = {};
		V.Root = Logical;
		V.UpperYawDegrees = TargetUpper;
		V.bInitialized = true;
	}
	else if (Dt > UE_SMALL_NUMBER)
	{
		const float Before = V.Root.Rotator().Yaw;
		const float Yaw = DampedAngle(Before, Logical.Rotator().Yaw, Dt, C, Scale);
		V.Root = FTransform(FRotator(0, Yaw, 0), Logical.GetLocation());
		V.UpperYawDegrees = DampedAngle(V.UpperYawDegrees, TargetUpper, Dt, C, Scale);
		// Do not turn the final sub-degree snap (or quantized idle jitter) into a bank impulse.
		const bool bMicroTurn = FMath::Abs(FMath::FindDeltaAngleDegrees(Before, Logical.Rotator().Yaw)) <= 2 * C.VisualYawSnapDegrees;
		V.YawRate = bMicroTurn ? 0 : FMath::FindDeltaAngleDegrees(Before, Yaw) / Dt;
		const float Strength = FMath::Sign(V.YawRate) * FMath::Clamp(
			(FMath::Abs(V.YawRate) - C.TurnRateDeadZone) / (C.TurnFullBankRate - C.TurnRateDeadZone), 0.0f, 1.0f);
		// +yaw turns toward local +Y. Negative mathematical X roll leans into +Y.
		V.DiscBankRadians = Bank(V.DiscBankRadians, -Strength * FMath::DegreesToRadians(C.TurnDiscDegrees), Dt, C.TurnDiscEnterSeconds, C.TurnDiscReturnSeconds);
		V.LegBankRadians = Bank(V.LegBankRadians, Strength * FMath::DegreesToRadians(C.TurnLegDegrees), Dt, C.TurnLegEnterSeconds, C.TurnLegReturnSeconds);
		V.UpperBankRadians = Bank(V.UpperBankRadians, -Strength * FMath::DegreesToRadians(C.TurnUpperDegrees), Dt, C.TurnUpperEnterSeconds, C.TurnUpperReturnSeconds);
	}
	V.Root.SetLocation(Logical.GetLocation());
	V.PoseTime = Now;
	const FQuat Hover = HoverBodyTransform(C, S).GetRotation();
	const FQuat DisplayParent = V.Root.GetRotation() * Hover * UpperRotation(V);
	const FQuat AuthorityUpper = Logical.GetRotation() * Hover
		* FRotator(0, FMath::FindDeltaAngleDegrees(Logical.Rotator().Yaw, TargetUpper), 0).Quaternion();
	for (int32 Side = 0; Side < 2; ++Side)
	{
		const FQuat Desired = AuthorityUpper * FRotator(S.GunPitchDegrees[Side], 0, 0).Quaternion();
		V.GunRotation[Side] = (DisplayParent.Inverse() * Desired).GetNormalized();
	}
}

FGuLiMechanicalAnimationFrame GuLiMechanicalAnimation::BuildVisualFrame(const FGuLiMechanicalAnimationConfig& C,
	const FGuLiMechanicalAnimationState& S, const FGuLiMechanicalVisualState& V, float Now)
{
	auto Frame = BuildFrame(C, S, V.Root.Rotator().Yaw, Now);
	if (C.Model != EGuLiMechanicalModel::WarMachine || !V.bInitialized) return Frame;
	Frame.Values[0] = FMath::DegreesToRadians(FMath::FindDeltaAngleDegrees(V.Root.Rotator().Yaw, V.UpperYawDegrees));
	const auto Tilt = DiscTilt(C, S, V);
	Frame.Values[5] = Tilt.X; Frame.Values[6] = Tilt.Y;
	Frame.Values[14] = V.UpperBankRadians;
	Frame.Values[15] = C.bHasTurnRig ? V.LegBankRadians : 0;
	for (int32 Side = 0; Side < 2; ++Side)
	{
		const auto& Q = V.GunRotation[Side];
		const int32 I = 16 + Side * 4;
		Frame.Values[I] = Q.X; Frame.Values[I+1] = Q.Y; Frame.Values[I+2] = Q.Z; Frame.Values[I+3] = Q.W;
	}
	return Frame;
}

bool GuLiMechanicalAnimation::ResolveVisualMuzzle(const FGuLiMechanicalAnimationConfig& C,
	const FGuLiMechanicalAnimationState& S, const FGuLiMechanicalVisualState& V,
	FName Slot, int32 Side, FTransform& Out)
{
	if (!V.bInitialized || C.Model != EGuLiMechanicalModel::WarMachine || Side < 0 || Side > 1) return false;
	const bool bMissile = Slot == TEXT("MissileLauncher");
	if (!bMissile && Slot != TEXT("BasicAttack")) return false;
	const auto& Neutral = bMissile ? C.MissileMuzzles[Side] : C.BasicMuzzles[Side];
	FVector P = Neutral.GetLocation();
	FQuat Q = Neutral.GetRotation();
	if (!bMissile)
	{
		// V.PoseTime freezes with the final WPO frame on death/phasing.
		P -= Neutral.GetUnitAxis(EAxis::X) * RecoilAt(C, S, Side, float(V.PoseTime));
		P = C.GunPivots[Side] + V.GunRotation[Side].RotateVector(P - C.GunPivots[Side]);
		Q = V.GunRotation[Side] * Q;
	}
	const FQuat Upper = UpperRotation(V);
	P = C.UpperPivot + Upper.RotateVector(P - C.UpperPivot);
	const auto Body = HoverBodyTransform(C, S);
	Out = FTransform(V.Root.GetRotation() * Body.GetRotation() * Upper * Q,
		V.Root.TransformPosition(Body.TransformPosition(P)));
	return !Out.ContainsNaN();
}

bool GuLiMechanicalAnimation::ResolveVisualNozzle(const FGuLiMechanicalAnimationConfig& C,
	const FGuLiMechanicalAnimationState& S, const FGuLiMechanicalVisualState& V, int32 Disc, FTransform& Out)
{
	if (!V.bInitialized || C.Model != EGuLiMechanicalModel::WarMachine || !C.bHasHoverNozzles || Disc < 0 || Disc >= 4) return false;
	const FQuat Tilt = TiltRotation(DiscTilt(C, S, V));
	const FVector P = C.DiscPivots[Disc] + Tilt.RotateVector(C.HoverNozzles[Disc] - C.DiscPivots[Disc]) + LegTranslation(C, V, Disc);
	const auto Body = HoverBodyTransform(C, S);
	const FVector Down = V.Root.TransformVectorNoScale(Body.TransformVectorNoScale(Tilt.RotateVector(-FVector::UpVector))).GetSafeNormal();
	Out = FTransform(Down.Rotation(), V.Root.TransformPosition(Body.TransformPosition(P)));
	return !Out.ContainsNaN();
}
