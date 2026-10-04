#include "Gameplay/Presentation/GuLiMechanicalAnimation.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshSocket.h"
#include "Misc/ConfigCacheIni.h"
#include "Materials/MaterialInstanceDynamic.h"

namespace
{
	const TCHAR* WheelNames[] = {TEXT("FL"), TEXT("FR"), TEXT("RL"), TEXT("RR")};
	FTransform SocketTransform(const UStaticMeshSocket& Socket, float Scale)
	{
		return FTransform(Socket.RelativeRotation, Socket.RelativeLocation * Scale);
	}
	float ReadSetting(const TCHAR* Section, const TCHAR* Key, float Default, float Min, float Max)
	{
		float Value = Default;
		if (GConfig) GConfig->GetFloat(Section, Key, Value, GGameIni);
		return FMath::IsFinite(Value) ? FMath::Clamp(Value, Min, Max) : Default;
	}
}

FGuLiMechanicalAnimationConfig FGuLiMechanicalAnimationConfig::FromStaticMesh(const UStaticMesh* Mesh, float Scale)
{
	FGuLiMechanicalAnimationConfig C;
	if (!Mesh || !FMath::IsFinite(Scale) || Scale <= 0.0f) return C;
	const auto* Gun = Mesh->FindSocket(TEXT("Rigid_GunPitch_01"));
	const auto* Muzzle = Mesh->FindSocket(TEXT("FX_Muzzle_Basic_01"));
	if (!Gun || !Muzzle) return C;
	const auto* Upper = Mesh->FindSocket(TEXT("Rigid_UpperYaw"));
	C.ModelScale = Scale;
	C.Model = Upper ? EGuLiMechanicalModel::WarMachine : EGuLiMechanicalModel::Sweeper;
	C.GunPivots[0] = Gun->RelativeLocation * Scale;
	C.BasicMuzzles[0] = SocketTransform(*Muzzle, Scale);
	if (Upper)
	{
		const auto* Gun2 = Mesh->FindSocket(TEXT("Rigid_GunPitch_02"));
		const auto* Muzzle2 = Mesh->FindSocket(TEXT("FX_Muzzle_Basic_02"));
		if (!Gun2 || !Muzzle2) return {};
		C.UpperPivot = Upper->RelativeLocation * Scale;
		C.GunPivots[1] = Gun2->RelativeLocation * Scale;
		C.BasicMuzzles[1] = SocketTransform(*Muzzle2, Scale);
		for (int32 Side = 0; Side < 2; ++Side)
			if (const auto* Socket = Mesh->FindSocket(FName(*FString::Printf(TEXT("FX_Missile_%02d"), Side + 1))))
				C.MissileMuzzles[Side] = SocketTransform(*Socket, Scale);
		C.bHasHoverNozzles = true;
		C.bHasTurnRig = true;
		for (int32 Index = 0; Index < 4; ++Index)
		{
			const auto* LegRoot = Mesh->FindSocket(FName(*FString::Printf(TEXT("Rigid_LegRoot_%s"), WheelNames[Index])));
			const auto* LegEnd = Mesh->FindSocket(FName(*FString::Printf(TEXT("Rigid_LegEnd_%s"), WheelNames[Index])));
			if (LegRoot && LegEnd)
			{
				C.LegRoots[Index] = LegRoot->RelativeLocation * Scale;
				C.LegEnds[Index] = LegEnd->RelativeLocation * Scale;
				C.LegAxes[Index] = LegRoot->RelativeRotation.Vector();
			}
			else C.bHasTurnRig = false;
			const auto* Disc = Mesh->FindSocket(FName(*FString::Printf(TEXT("Rigid_Disc_%s"), WheelNames[Index])));
			const auto* Nozzle = Mesh->FindSocket(FName(*FString::Printf(TEXT("FX_Hover_%s"), WheelNames[Index])));
			if (!Disc || !Nozzle || Nozzle->RelativeScale.X <= 0) { C.bHasHoverNozzles = false; continue; }
			C.DiscPivots[Index] = Disc->RelativeLocation * Scale;
			C.HoverNozzles[Index] = Nozzle->RelativeLocation * Scale;
			// The socket's X scale encodes the authored disc diameter; it is not a transform scale.
			C.DiscDiameters[Index] = Nozzle->RelativeScale.X * Scale;
		}
	}
	else
	{
		C.MaximumPitch = 60.0f;
		C.RecoilDistance = 0.0f;
		for (int32 Index = 0; Index < 4; ++Index)
		{
			const auto* Wheel = Mesh->FindSocket(FName(*FString::Printf(TEXT("Rigid_Wheel_%s"), WheelNames[Index])));
			if (!Wheel || Wheel->RelativeScale.X <= 0.0f) return {};
			C.WheelPivots[Index] = Wheel->RelativeLocation * Scale;
			C.WheelRadii[Index] = Wheel->RelativeScale.X * Scale;
		}
	}
	const TCHAR* Section = Upper ? TEXT("GuLiMechanicalAnimation.WarMachine") : TEXT("GuLiMechanicalAnimation.Sweeper");
	C.UpperTurnRate = ReadSetting(Section, TEXT("UpperTurnRate"), C.UpperTurnRate, 1, 3600);
	C.LowerTurnRate = ReadSetting(Section, TEXT("LowerTurnRate"), C.LowerTurnRate, 1, 3600);
	C.BaseMoveSpeed = ReadSetting(Section, TEXT("BaseMoveSpeed"), C.BaseMoveSpeed, 1, 100000);
	C.PitchRate = ReadSetting(Section, TEXT("PitchRate"), C.PitchRate, 1, 3600);
	C.MinimumPitch = ReadSetting(Section, TEXT("MinimumPitch"), C.MinimumPitch, -89, 0);
	C.MaximumPitch = ReadSetting(Section, TEXT("MaximumPitch"), C.MaximumPitch, 0, 89);
	C.DiscTiltTransitionSeconds = ReadSetting(Section, TEXT("DiscTiltTransitionSeconds"), C.DiscTiltTransitionSeconds, .01f, 10);
	if (Upper)
	{
		C.MinimumDiscTilt = ReadSetting(Section, TEXT("MinimumDiscTilt"), C.MinimumDiscTilt, 0, 45);
		C.MaximumDiscTilt = ReadSetting(Section, TEXT("MaximumDiscTilt"), C.MaximumDiscTilt, C.MinimumDiscTilt, 45);
		C.RecoilDistance = ReadSetting(Section, TEXT("RecoilDistance"), C.RecoilDistance, 0, 100);
		C.RecoilKickSeconds = ReadSetting(Section, TEXT("RecoilKickSeconds"), C.RecoilKickSeconds, .001f, 1);
		C.RecoilReturnSeconds = ReadSetting(Section, TEXT("RecoilReturnSeconds"), C.RecoilReturnSeconds, .001f, 2);
		C.HoverHeight = ReadSetting(Section, TEXT("HoverHeight"), C.HoverHeight, 0, 1000);
		C.HoverBobAmplitude = ReadSetting(Section, TEXT("HoverBobAmplitude"), C.HoverBobAmplitude, 0, 100);
		C.HoverPitchAmplitude = ReadSetting(Section, TEXT("HoverPitchAmplitude"), C.HoverPitchAmplitude, 0, 15);
		C.HoverRollAmplitude = ReadSetting(Section, TEXT("HoverRollAmplitude"), C.HoverRollAmplitude, 0, 15);
		C.HoverBobPeriod = ReadSetting(Section, TEXT("HoverBobPeriod"), C.HoverBobPeriod, .1f, 60);
		C.HoverPitchPeriod = ReadSetting(Section, TEXT("HoverPitchPeriod"), C.HoverPitchPeriod, .1f, 60);
		C.HoverRollPeriod = ReadSetting(Section, TEXT("HoverRollPeriod"), C.HoverRollPeriod, .1f, 60);
		C.HoverBlendSeconds = ReadSetting(Section, TEXT("HoverBlendSeconds"), C.HoverBlendSeconds, .01f, 10);
		C.HoverIdleEnterSpeed = ReadSetting(Section, TEXT("HoverIdleEnterSpeed"), C.HoverIdleEnterSpeed, 0, 100);
		C.HoverIdleExitSpeed = ReadSetting(Section, TEXT("HoverIdleExitSpeed"), C.HoverIdleExitSpeed, C.HoverIdleEnterSpeed, 200);
		C.HoverIdleHoldSeconds = ReadSetting(Section, TEXT("HoverIdleHoldSeconds"), C.HoverIdleHoldSeconds, 0, 2);
		C.VisualYawDamping = ReadSetting(Section, TEXT("VisualYawDamping"), C.VisualYawDamping, .1f, 30);
		C.VisualYawSnapDegrees = ReadSetting(Section, TEXT("VisualYawSnapDegrees"), C.VisualYawSnapDegrees, .001f, 1);
		C.TurnFullBankRate = ReadSetting(Section, TEXT("TurnFullBankRate"), C.TurnFullBankRate, 5, 720);
		C.TurnRateDeadZone = ReadSetting(Section, TEXT("TurnRateDeadZone"), C.TurnRateDeadZone, 0, C.TurnFullBankRate*.5f);
		C.TurnDiscDegrees = ReadSetting(Section, TEXT("TurnDiscDegrees"), C.TurnDiscDegrees, 0, 15);
		C.TurnLegDegrees = ReadSetting(Section, TEXT("TurnLegDegrees"), C.TurnLegDegrees, 0, 8);
		C.TurnUpperDegrees = ReadSetting(Section, TEXT("TurnUpperDegrees"), C.TurnUpperDegrees, 0, 15);
		C.TurnDiscEnterSeconds = ReadSetting(Section, TEXT("TurnDiscEnterSeconds"), C.TurnDiscEnterSeconds, .01f, 5);
		C.TurnLegEnterSeconds = ReadSetting(Section, TEXT("TurnLegEnterSeconds"), C.TurnLegEnterSeconds, .01f, 5);
		C.TurnUpperEnterSeconds = ReadSetting(Section, TEXT("TurnUpperEnterSeconds"), C.TurnUpperEnterSeconds, .01f, 5);
		C.TurnDiscReturnSeconds = ReadSetting(Section, TEXT("TurnDiscReturnSeconds"), C.TurnDiscReturnSeconds, .01f, 5);
		C.TurnLegReturnSeconds = ReadSetting(Section, TEXT("TurnLegReturnSeconds"), C.TurnLegReturnSeconds, .01f, 5);
		C.TurnUpperReturnSeconds = ReadSetting(Section, TEXT("TurnUpperReturnSeconds"), C.TurnUpperReturnSeconds, .01f, 5);
	}
	return C;
}

float GuLiMechanicalAnimation::HoverWeight(const FGuLiMechanicalAnimationConfig& C,
	const FGuLiMechanicalAnimationState& S, double Now)
{
	const float T = float(FMath::Clamp((Now - double(S.HoverBlendStartMilliseconds) * .001) / C.HoverBlendSeconds, 0.0, 1.0));
	return FMath::Lerp(float(S.HoverBlendFromWeight) / 255.0f, S.bHoverIdleTarget ? 1.0f : 0.0f, T*T*(3-2*T));
}

void GuLiMechanicalAnimation::StepHover(const FGuLiMechanicalAnimationConfig& C, uint32 Id,
	float Speed, float Dt, double Now, FGuLiMechanicalAnimationState& S)
{
	if (C.Model != EGuLiMechanicalModel::WarMachine) return;
	if (!S.bHoverInitialized)
	{
		S.bHoverInitialized = true;
		S.HoverBlendStartMilliseconds = uint32(FMath::Max(0.0, Now * 1000.0));
	}
	S.HoverStoppedSeconds = Speed < C.HoverIdleEnterSpeed ? S.HoverStoppedSeconds + FMath::Max(0.0f, Dt) : 0;
	const bool bIdle = Speed > C.HoverIdleExitSpeed ? false
		: (S.HoverStoppedSeconds + UE_SMALL_NUMBER >= C.HoverIdleHoldSeconds ? true : S.bHoverIdleTarget);
	if (bIdle != S.bHoverIdleTarget)
	{
		S.HoverBlendFromWeight = uint8(FMath::RoundToInt(HoverWeight(C, S, Now)*255));
		S.HoverBlendStartMilliseconds = uint32(FMath::Max(0.0, Now * 1000.0));
		S.bHoverIdleTarget = bIdle;
	}
	EvaluateHover(C, Id, Now, S);
}

void GuLiMechanicalAnimation::EvaluateHover(const FGuLiMechanicalAnimationConfig& C, uint32 Id,
	double Now, FGuLiMechanicalAnimationState& S)
{
	S.HoverBobCentimeters = S.HoverPitchRadians = S.HoverRollRadians = 0;
	if (C.Model != EGuLiMechanicalModel::WarMachine) return;
	// Fixed integer mixing: identical on authority, late joiners and clients; never a random stream.
	uint32 Seed = Id; Seed ^= Seed >> 16; Seed *= 0x7feb352du; Seed ^= Seed >> 15; Seed *= 0x846ca68bu; Seed ^= Seed >> 16;
	const double Phase = double(Seed) / 4294967296.0 * 2.0 * UE_DOUBLE_PI;
	const float W = HoverWeight(C, S, Now);
	S.HoverBobCentimeters = W * C.HoverBobAmplitude * FMath::Sin(Now * 2.0 * UE_DOUBLE_PI / C.HoverBobPeriod + Phase);
	S.HoverPitchRadians = W * FMath::DegreesToRadians(C.HoverPitchAmplitude) * FMath::Sin(Now * 2.0 * UE_DOUBLE_PI / C.HoverPitchPeriod + Phase + 1.3);
	S.HoverRollRadians = W * FMath::DegreesToRadians(C.HoverRollAmplitude) * FMath::Sin(Now * 2.0 * UE_DOUBLE_PI / C.HoverRollPeriod + Phase * 1.73 + 2.1);
}

FTransform GuLiMechanicalAnimation::HoverBodyTransform(const FGuLiMechanicalAnimationConfig& C, const FGuLiMechanicalAnimationState& S)
{
	if (C.Model != EGuLiMechanicalModel::WarMachine) return FTransform::Identity;
	// HLSL applies positive-axis X roll, then UE pitch (+X toward +Z).
	const FQuat Q = FRotator(FMath::RadiansToDegrees(S.HoverPitchRadians), 0, 0).Quaternion()
		* FQuat(FVector::ForwardVector, S.HoverRollRadians);
	return FTransform(Q, C.UpperPivot - Q.RotateVector(C.UpperPivot) + FVector(0, 0, C.HoverHeight + S.HoverBobCentimeters));
}

bool GuLiMechanicalAnimation::ResolveHoverNozzle(const FGuLiMechanicalAnimationConfig& C,
	const FGuLiMechanicalAnimationState& S, const FTransform& Root, int32 Disc, FTransform& Out)
{
	if (C.Model != EGuLiMechanicalModel::WarMachine || !C.bHasHoverNozzles || Disc < 0 || Disc >= 4) return false;
	const float Angle = S.DiscTiltRadians.Size();
	const FQuat Tilt = Angle > UE_SMALL_NUMBER ? FQuat(FVector(S.DiscTiltRadians.X, S.DiscTiltRadians.Y, 0) / Angle, Angle) : FQuat::Identity;
	const auto Body = HoverBodyTransform(C, S);
	const FVector Point = C.DiscPivots[Disc] + Tilt.RotateVector(C.HoverNozzles[Disc] - C.DiscPivots[Disc]);
	const FVector Down = Root.TransformVectorNoScale(Body.TransformVectorNoScale(Tilt.RotateVector(-FVector::UpVector))).GetSafeNormal();
	Out = FTransform(Down.Rotation(), Root.TransformPosition(Body.TransformPosition(Point)));
	return !Out.ContainsNaN();
}

void GuLiMechanicalAnimation::ConfigureOverlay(UInstancedStaticMeshComponent& Component, UMaterialInterface* Material)
{
	const auto Config = FGuLiMechanicalAnimationConfig::FromStaticMesh(Component.GetStaticMesh(), 1.0f);
	if (Material && Config.IsEnabled())
	{
		auto* Instance = UMaterialInstanceDynamic::Create(Material, &Component);
		Instance->SetScalarParameterValue(TEXT("RigidKind"), Config.Model == EGuLiMechanicalModel::WarMachine ? 1.0f : 2.0f);
		Instance->SetVectorParameterValue(TEXT("RigidUpperPivot"), FLinearColor(Config.UpperPivot.X, Config.UpperPivot.Y, Config.UpperPivot.Z, 0));
		for (int32 I = 0; I < 4; ++I)
		{
			const auto Set = [Instance, I](const TCHAR* Name, const FVector& V)
			{
				Instance->SetVectorParameterValue(FName(*FString::Printf(TEXT("%s%d"), Name, I)), FLinearColor(V.X, V.Y, V.Z, 0));
			};
			Set(TEXT("RigidLegRoot"), Config.LegRoots[I]);
			Set(TEXT("RigidLegEnd"), Config.LegEnds[I]);
			Set(TEXT("RigidLegAxis"), Config.LegAxes[I]);
		}
		Material = Instance;
	}
	for (int32 Slot = 0; Slot < Component.GetNumMaterials(); ++Slot) Component.SetMaterial(Slot, Material);
}

void GuLiMechanicalAnimation::StepAim(const FGuLiMechanicalAnimationConfig& C, const FTransform& Root,
	const FVector* Target, float FinalMoveSpeed, float Dt, FGuLiMechanicalAnimationState& S)
{
	if (!C.IsEnabled() || Dt < 0 || !FMath::IsFinite(Dt)) return;
	const float LowerYaw = Root.Rotator().Yaw;
	if (!S.bInitialized) { S.UpperYawDegrees = LowerYaw; S.bInitialized = true; }
	const bool bWarMachine = C.Model == EGuLiMechanicalModel::WarMachine;
	const FVector LocalTarget = Target ? HoverBodyTransform(C, S).InverseTransformPosition(Root.InverseTransformPosition(*Target)) : FVector::ZeroVector;
	const FVector ToTarget = LocalTarget - C.UpperPivot;
	const float DesiredYaw = Target && !ToTarget.IsNearlyZero() ? LowerYaw + ToTarget.Rotation().Yaw : LowerYaw;
	const float Rate = C.UpperTurnRate * FMath::Max(0.0f, FinalMoveSpeed) / C.BaseMoveSpeed;
	S.UpperYawDegrees = bWarMachine ? FMath::FixedTurn(S.UpperYawDegrees, DesiredYaw, Rate * Dt) : LowerYaw;
	const FQuat Yaw = FRotator(0, FMath::FindDeltaAngleDegrees(LowerYaw, S.UpperYawDegrees), 0).Quaternion();
	for (int32 Side = 0; Side < (bWarMachine ? 2 : 1); ++Side)
	{
		const FVector Pivot = bWarMachine ? C.UpperPivot + Yaw.RotateVector(C.GunPivots[Side] - C.UpperPivot) : C.GunPivots[Side];
		const FVector Delta = Target ? LocalTarget - Pivot : FVector::ForwardVector;
		const float Pitch = Target ? FMath::RadiansToDegrees(FMath::Atan2(Delta.Z, Delta.Size2D())) : 0;
		S.GunPitchDegrees[Side] = FMath::FInterpConstantTo(S.GunPitchDegrees[Side],
			FMath::Clamp(Pitch, C.MinimumPitch, C.MaximumPitch), Dt, C.PitchRate);
	}
}

void GuLiMechanicalAnimation::StepLocomotion(const FGuLiMechanicalAnimationConfig& C,
	const FTransform& Before, const FTransform& Root, float Dt, bool bReset, FGuLiMechanicalAnimationState& S)
{
	if (bReset)
	{
		for (float& Phase : S.WheelRadians) Phase = 0;
		S.DiscTiltRadians = FVector2D::ZeroVector;
		S.DiscTiltFrom = S.DiscTiltTarget = FVector2D::ZeroVector;
		S.DiscTiltElapsed = 0;
		for (int32 Side = 0; Side < 2; ++Side) { S.RecoilStartTime[Side] = -1000; S.RecoilFromCentimeters[Side] = 0; }
		return;
	}
	if (Dt <= UE_SMALL_NUMBER) return;
	if (C.Model == EGuLiMechanicalModel::Sweeper)
	{
		const FVector Forward = (Before.GetUnitAxis(EAxis::X) + Root.GetUnitAxis(EAxis::X)).GetSafeNormal();
		for (int32 Index = 0; Index < 4; ++Index)
		{
			const FVector Travel = Root.TransformPosition(C.WheelPivots[Index]) - Before.TransformPosition(C.WheelPivots[Index]);
			S.WheelRadians[Index] = FMath::Fmod(S.WheelRadians[Index] + FVector::DotProduct(Travel, Forward) / C.WheelRadii[Index], 2*PI);
		}
	}
	else if (C.Model == EGuLiMechanicalModel::WarMachine)
	{
		const FVector Velocity = (Root.GetLocation() - Before.GetLocation()) / Dt;
		const float Speed = Velocity.Size2D();
		const FVector Local = Root.InverseTransformVectorNoScale(Velocity).GetSafeNormal2D();
		const float Tilt = Speed > 1 ? FMath::Lerp(C.MinimumDiscTilt, C.MaximumDiscTilt,
			FMath::Clamp((Speed - C.BaseMoveSpeed) / C.BaseMoveSpeed, 0.0f, 1.0f)) : 0;
		const FVector2D Target(-Local.Y * FMath::DegreesToRadians(Tilt), Local.X * FMath::DegreesToRadians(Tilt));
		if (!Target.Equals(S.DiscTiltTarget, 0.0001))
		{
			S.DiscTiltFrom = S.DiscTiltRadians; S.DiscTiltTarget = Target; S.DiscTiltElapsed = 0;
		}
		S.DiscTiltElapsed += Dt;
		const float Alpha = FMath::Clamp(S.DiscTiltElapsed / C.DiscTiltTransitionSeconds, 0.0f, 1.0f);
		S.DiscTiltRadians = FMath::Lerp(S.DiscTiltFrom, S.DiscTiltTarget, Alpha*Alpha*(3-2*Alpha));
	}
}

float GuLiMechanicalAnimation::RecoilAt(const FGuLiMechanicalAnimationConfig& C,
	const FGuLiMechanicalAnimationState& S, int32 Side, float Now)
{
	if (Side < 0 || Side > 1 || C.RecoilDistance <= 0) return 0;
	const float Age = Now - S.RecoilStartTime[Side];
	if (Age < 0 || Age >= C.RecoilKickSeconds + C.RecoilReturnSeconds) return 0;
	const auto Smooth = [](float T) { T = FMath::Clamp(T, 0.0f, 1.0f); return T*T*(3-2*T); };
	if (Age < C.RecoilKickSeconds)
		return FMath::Lerp(FMath::Clamp(S.RecoilFromCentimeters[Side], 0.0f, C.RecoilDistance), C.RecoilDistance, Smooth(Age / C.RecoilKickSeconds));
	return C.RecoilDistance * (1-Smooth((Age-C.RecoilKickSeconds) / C.RecoilReturnSeconds));
}

void GuLiMechanicalAnimation::AcceptShot(const FGuLiMechanicalAnimationConfig& C,
	FGuLiMechanicalAnimationState& S, int32 Side, float Now)
{
	if (Side < 0 || Side > 1) return;
	S.RecoilFromCentimeters[Side] = RecoilAt(C, S, Side, Now);
	S.RecoilStartTime[Side] = Now;
}

bool GuLiMechanicalAnimation::ResolveMuzzle(const FGuLiMechanicalAnimationConfig& C,
	const FGuLiMechanicalAnimationState& S, const FTransform& Root, FName Slot, int32 Side, float Now, FTransform& Out)
{
	if (!C.IsEnabled() || Side < 0 || Side > 1 || Root.ContainsNaN()) return false;
	const bool bMissile = Slot == TEXT("MissileLauncher");
	if ((!bMissile && Slot != TEXT("BasicAttack")) || (C.Model == EGuLiMechanicalModel::Sweeper && (Side != 0 || bMissile))) return false;
	const FTransform& Neutral = bMissile ? C.MissileMuzzles[Side] : C.BasicMuzzles[Side];
	FVector Point = Neutral.GetLocation();
	FQuat Rotation = Neutral.GetRotation();
	if (!bMissile)
	{
		Point -= Neutral.GetUnitAxis(EAxis::X) * RecoilAt(C, S, Side, Now);
		const FQuat Pitch = FRotator(S.GunPitchDegrees[Side], 0, 0).Quaternion();
		Point = C.GunPivots[Side] + Pitch.RotateVector(Point-C.GunPivots[Side]);
		Rotation = Pitch * Rotation;
	}
	if (C.Model == EGuLiMechanicalModel::WarMachine)
	{
		const float YawDegrees = S.bInitialized ? FMath::FindDeltaAngleDegrees(Root.Rotator().Yaw, S.UpperYawDegrees) : 0;
		const FQuat Yaw = FRotator(0, YawDegrees, 0).Quaternion();
		Point = C.UpperPivot + Yaw.RotateVector(Point-C.UpperPivot);
		Rotation = Yaw * Rotation;
	}
	const auto Body = HoverBodyTransform(C, S);
	Point = Body.TransformPosition(Point);
	Rotation = Body.GetRotation() * Rotation;
	Out = FTransform(Root.GetRotation()*Rotation, Root.TransformPosition(Point));
	return !Out.ContainsNaN();
}

FGuLiMechanicalAnimationFrame GuLiMechanicalAnimation::BuildFrame(const FGuLiMechanicalAnimationConfig& C,
	const FGuLiMechanicalAnimationState& S, float LowerYaw, float Now)
{
	FGuLiMechanicalAnimationFrame F;
	F.Values[0] = C.Model == EGuLiMechanicalModel::WarMachine && S.bInitialized
		? FMath::DegreesToRadians(FMath::FindDeltaAngleDegrees(LowerYaw, S.UpperYawDegrees)) : 0;
	F.Values[1] = FMath::DegreesToRadians(S.GunPitchDegrees[0]);
	F.Values[2] = FMath::DegreesToRadians(S.GunPitchDegrees[1]);
	F.Values[3] = RecoilAt(C, S, 0, Now) / C.ModelScale;
	F.Values[4] = RecoilAt(C, S, 1, Now) / C.ModelScale;
	F.Values[5] = S.DiscTiltRadians.X; F.Values[6] = S.DiscTiltRadians.Y;
	for (int32 Index = 0; Index < 4; ++Index) F.Values[7+Index] = S.WheelRadians[Index];
	if (C.Model == EGuLiMechanicalModel::WarMachine)
	{
		F.Values[11] = (C.HoverHeight + S.HoverBobCentimeters) / C.ModelScale;
		F.Values[12] = S.HoverPitchRadians;
		F.Values[13] = S.HoverRollRadians;
	}
	return F;
}

bool GuLiMechanicalAnimation::WriteInstance(UInstancedStaticMeshComponent& Component, int32 Index,
	const FGuLiMechanicalAnimationFrame& Current, const FGuLiMechanicalAnimationFrame& Previous, float HitTime)
{
	if (Component.NumCustomDataFloats < FGuLiMechanicalAnimationFrame::CustomDataFloats || !Component.IsValidInstance(Index)) return false;
	bool bChanged = false;
	const int32 Start = Index * Component.NumCustomDataFloats;
	TArray<float, TInlineAllocator<59>> Data;
	Data.Append(Component.PerInstanceSMCustomData.GetData() + Start, Component.NumCustomDataFloats);
	Data[0] = HitTime;
	Data[29] = Current.MissilePodVisible;
	Data[30] = Previous.MissilePodVisible;
	for (int32 I = 0; I < FGuLiMechanicalAnimationFrame::PoseFloats; ++I)
	{
		Data[FGuLiMechanicalAnimationFrame::PoseIndex(I, false)] = Current.Values[I];
		Data[FGuLiMechanicalAnimationFrame::PoseIndex(I, true)] = Previous.Values[I];
	}
	for (int32 Slot = 0; Slot < Component.NumCustomDataFloats; ++Slot)
	{
		const float Value = Data[Slot];
		if (!FMath::IsNearlyEqual(Component.PerInstanceSMCustomData[Start+Slot], Value, 0.00001f))
		{
			bChanged = true;
		}
	}
	// UE's instance data manager uploads dirty instance ranges without recreating the scene proxy.
	if (bChanged) Component.SetCustomData(Index, MakeArrayView(Data), false);
	return bChanged;
}
