#include "Gameplay/Presentation/GuLiVATAnimation.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/Texture2D.h"

bool UGuLiVATDefinition::IsValidDefinition() const
{
	if (!BonePosition || !BoneRotation || Bones.IsEmpty() || Clips.IsEmpty() || FramesPerSecond <= 0
		|| !GameplayBounds.IsValid || !RuntimeRenderBounds.IsValid || Muzzles.Num() != 2) return false;
	const int32 Frames = BoneDeltas.Num() / Bones.Num();
	if (Frames * Bones.Num() != BoneDeltas.Num()) return false;
	for (const auto& Clip : Clips)
		if (Clip.FrameCount < 2 || Clip.FirstFrame < 0 || Clip.FirstFrame + Clip.FrameCount > Frames
			|| !FMath::IsFinite(Clip.DurationSeconds) || Clip.DurationSeconds <= 0) return false;
	for (const auto& Muzzle : Muzzles)
		if (!Bones.IsValidIndex(Muzzle.BoneIndex) || Muzzle.ReferencePosition.ContainsNaN()) return false;
	return true;
}

TArray<FString> UGuLiVATDefinition::ValidateImportedTextures() const
{
	TArray<FString> Errors;
	if (!IsValidDefinition()) { Errors.Add(TEXT("Invalid VAT definition.")); return Errors; }
#if WITH_EDITOR
	const int32 Frames = BoneDeltas.Num() / Bones.Num();
	for (int32 TextureIndex = 0; TextureIndex < 2; ++TextureIndex)
	{
		UTexture2D* Texture = TextureIndex == 0 ? BonePosition : BoneRotation;
		if (Texture->Source.GetSizeX() != Bones.Num() || Texture->Source.GetSizeY() != Frames
			|| Texture->Source.GetFormat() != TSF_RGBA32F || Texture->SRGB || Texture->CompressionSettings != TC_HDR_F32
			|| Texture->Filter != TF_Nearest || Texture->MipGenSettings != TMGS_NoMipmaps)
		{ Errors.Add(Texture->GetName()+TEXT(": expected nearest RGBA32F, no sRGB/mips, one texel per bone/frame.")); continue; }
		TArray64<uint8> Raw;
		if (!Texture->Source.GetMipData(Raw,0) || Raw.Num() != int64(BoneDeltas.Num()) * 4 * sizeof(float))
		{ Errors.Add(Texture->GetName()+TEXT(": invalid source pixels.")); continue; }
		const float* Values = reinterpret_cast<const float*>(Raw.GetData());
		for (int32 Index = 0; Index < BoneDeltas.Num(); ++Index)
		{
			const auto& Delta = BoneDeltas[Index];
			const FVector P = Delta.GetTranslation(); const FQuat Q = Delta.GetRotation();
			const double Expected[4] = {TextureIndex == 0 ? P.X : Q.X, TextureIndex == 0 ? P.Y : Q.Y,
				TextureIndex == 0 ? P.Z : Q.Z, TextureIndex == 0 ? 1 : Q.W};
			for (int32 Channel = 0; Channel < 4; ++Channel)
				if (!FMath::IsFinite(Values[Index*4+Channel]) || FMath::Abs(Values[Index*4+Channel]-Expected[Channel]) > .001)
				{
					Errors.Add(FString::Printf(TEXT("%s: CPU/GPU source differs at frame %d bone %d channel %d."),
						*Texture->GetName(),Index/Bones.Num(),Index%Bones.Num(),Channel));
					Index=BoneDeltas.Num(); break;
				}
		}
	}
#endif
	return Errors;
}

int32 UGuLiVATDefinition::FindClip(FName Name) const
{
	return Clips.IndexOfByPredicate([Name](const auto& Clip) { return Clip.Name == Name; });
}

float UGuLiVATDefinition::TextureFrame(uint8 Clip, float Phase) const
{
	if (!Clips.IsValidIndex(Clip)) return 0;
	return Clips[Clip].FirstFrame + FMath::Clamp(Phase, 0.0f, 1.0f) * (Clips[Clip].FrameCount - 1);
}

FTransform UGuLiVATDefinition::SampleBone(int32 Bone, float Frame) const
{
	if (!Bones.IsValidIndex(Bone) || BoneDeltas.IsEmpty() || !FMath::IsFinite(Frame)) return FTransform::Identity;
	const int32 Count = BoneDeltas.Num() / Bones.Num();
	const float Safe = FMath::Clamp(Frame, 0.0f, float(Count - 1));
	const int32 First = FMath::FloorToInt(Safe), Next = FMath::Min(First + 1, Count - 1);
	const auto& A = BoneDeltas[First * Bones.Num() + Bone];
	const auto& B = BoneDeltas[Next * Bones.Num() + Bone];
	const float Alpha = Safe - First;
	// Normalized quaternion lerp is identical to the vertex shader, including hemisphere fix.
	const FQuat Q = FQuat::FastLerp(A.GetRotation(), B.GetRotation(), Alpha).GetNormalized();
	return FTransform(Q, FMath::Lerp(A.GetTranslation(), B.GetTranslation(), Alpha));
}

void GuLiVATAnimation::Step(const UGuLiVATDefinition& D, const FVector& Velocity,
	float BodyYaw, bool bAlive, float Dt, FGuLiVATPlayback& P)
{
	const FVector Local = FRotator(0, BodyYaw, 0).UnrotateVector(Velocity);
	const float Speed = Velocity.Size2D();
	FName Name = TEXT("Idle");
	if (!bAlive) Name = TEXT("Death");
	else if (Speed > 5)
		Name = FMath::Abs(Local.X) >= FMath::Abs(Local.Y)
			? (Local.X >= 0 ? TEXT("Forward") : TEXT("Backward"))
			: (Local.Y >= 0 ? TEXT("Right") : TEXT("Left"));
	const int32 Clip = D.FindClip(Name);
	if (!D.Clips.IsValidIndex(Clip)) return;
	if (!P.bInitialized || P.Clip != Clip) { P.Clip = uint8(Clip); P.Phase = 0; P.bInitialized = true; }
	const auto& C = D.Clips[Clip];
	P.Rate = C.StrideCentimeters > 0 ? Speed / C.StrideCentimeters : 1 / C.DurationSeconds;
	P.Phase += FMath::Max(0.0f, Dt) * P.Rate;
	P.Phase = C.bLoop ? FMath::Frac(P.Phase) : FMath::Min(P.Phase, 1.0f);
}

void GuLiVATAnimation::StepAim(const UGuLiVATDefinition& D, const FGuLiVATPlayback& P,
	const FTransform& Root, const FVector* Target, float Dt, FGuLiMechanicalAnimationState& Aim)
{
	if (!Aim.bInitialized) { Aim.UpperYawDegrees = Root.Rotator().Yaw; Aim.bInitialized = true; }
	const int32 Top = D.Bones.IndexOfByPredicate([](const auto& B) { return B.Name == TEXT("Top_M"); });
	const FVector Pivot = Root.TransformPosition(D.SampleBone(Top, D.TextureFrame(P.Clip, P.Phase)).TransformPosition(D.UpperPivot));
	const FVector Direction = Target ? *Target - Pivot : Root.GetRotation().GetForwardVector();
	const FRotator Wanted = Direction.Rotation();
	Aim.UpperYawDegrees = FMath::FixedTurn(Aim.UpperYawDegrees, Wanted.Yaw, 180 * Dt);
	for (int32 Side = 0; Side < 2; ++Side)
		Aim.GunPitchDegrees[Side] = FMath::FInterpConstantTo(Aim.GunPitchDegrees[Side], Target ? FMath::Clamp(Wanted.Pitch, -80.0f, 80.0f) : 0.0f, Dt, 90);
}

bool GuLiVATAnimation::ResolveMuzzle(const UGuLiVATDefinition& D, const FGuLiVATPlayback& P,
	const FGuLiMechanicalAnimationState& Aim, const FTransform& Root, int32 Side, FTransform& Out)
{
	if (!D.Muzzles.IsValidIndex(Side)) return false;
	const auto& M = D.Muzzles[Side];
	const float Frame = D.TextureFrame(P.Clip, P.Phase);
	const FTransform Delta = D.SampleBone(M.BoneIndex, Frame);
	const int32 Top = D.Bones.IndexOfByPredicate([](const auto& B) { return B.Name == TEXT("Top_M"); });
	const FVector Upper = D.SampleBone(Top, Frame).TransformPosition(D.UpperPivot);
	const FVector Pivot = Delta.TransformPosition(M.PitchPivot);
	FVector Position = Delta.TransformPosition(M.ReferencePosition);
	FVector Direction = Delta.TransformVectorNoScale(M.ReferenceDirection);
	const FQuat Pitch = FRotator(Aim.GunPitchDegrees[Side], 0, 0).Quaternion();
	Position = Pivot + Pitch.RotateVector(Position - Pivot);
	Direction = Pitch.RotateVector(Direction);
	const FQuat Yaw = FRotator(0, FMath::FindDeltaAngleDegrees(Root.Rotator().Yaw, Aim.UpperYawDegrees), 0).Quaternion();
	Position = Upper + Yaw.RotateVector(Position - Upper);
	Direction = Yaw.RotateVector(Direction);
	Out = FTransform(Root.TransformVectorNoScale(Direction).Rotation(), Root.TransformPosition(Position));
	return !Out.ContainsNaN();
}

void GuLiVATAnimation::WriteInstance(UInstancedStaticMeshComponent& C, int32 Index,
	const UGuLiVATDefinition& D, const FGuLiVATPlayback& P, const FGuLiVATPlayback& Before,
	const FGuLiMechanicalAnimationState& Aim, float BodyYaw, bool bReset)
{
	if (C.NumCustomDataFloats < CustomDataFloatCount || Index < 0) return;
	const float Values[4] = {D.TextureFrame(P.Clip, P.Phase),
		FMath::DegreesToRadians(FMath::FindDeltaAngleDegrees(BodyYaw, Aim.UpperYawDegrees)),
		FMath::DegreesToRadians(Aim.GunPitchDegrees[0]), FMath::DegreesToRadians(Aim.GunPitchDegrees[1])};
	for (int32 Field = 0; Field < 4; ++Field)
	{
		const float Previous = bReset ? Values[Field] : C.PerInstanceSMCustomData.IsValidIndex(Index * C.NumCustomDataFloats + FirstCustomData + Field)
			? C.PerInstanceSMCustomData[Index * C.NumCustomDataFloats + FirstCustomData + Field] : Values[Field];
		C.SetCustomDataValue(Index, FirstCustomData + 4 + Field, Previous, false);
		C.SetCustomDataValue(Index, FirstCustomData + Field, Values[Field], false);
	}
}
