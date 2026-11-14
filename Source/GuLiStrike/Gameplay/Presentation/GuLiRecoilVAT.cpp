#include "Gameplay/Presentation/GuLiRecoilVAT.h"
#include "Gameplay/Presentation/GuLiMechanicalAnimation.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Materials/MaterialInstanceDynamic.h"

namespace
{
FGuLiRecoilVATSample Blend(const FGuLiRecoilVATSample& A,const FGuLiRecoilVATSample& B,float T)
{
	FGuLiRecoilVATSample S;
	S.UpperTranslation=FMath::Lerp(A.UpperTranslation,B.UpperTranslation,T);
	S.LeftGunTranslation=FMath::Lerp(A.LeftGunTranslation,B.LeftGunTranslation,T);
	S.RightGunTranslation=FMath::Lerp(A.RightGunTranslation,B.RightGunTranslation,T);
	FQuat Q=B.UpperRotation; if ((A.UpperRotation|Q)<0) Q=Q*-1.;
	S.UpperRotation=(A.UpperRotation*(1.-T)+Q*T).GetNormalized();
	return S;
}
bool Neutral(const FGuLiRecoilVATSample& S)
{
	return S.UpperTranslation.IsNearlyZero(.001) && S.LeftGunTranslation.IsNearlyZero(.001)
		&& S.RightGunTranslation.IsNearlyZero(.001) && S.UpperRotation.Equals(FQuat::Identity,.00001);
}
}

bool UGuLiRecoilVATDefinition::IsValidDefinition() const
{
	if (FramesPerSide<2 || FramesPerSide>256 || MissileFrames<0 || MissileFrames>512
		|| Samples.Num()!=FramesPerSide*2+MissileFrames
		|| !FMath::IsFinite(CycleSeconds) || CycleSeconds<=0 || CycleSeconds>2
		|| PositionTexture.IsNull() || RotationTexture.IsNull() || !RenderBounds.IsValid || UpperPivot.ContainsNaN()
		|| GunPivots.Num()!=2 || ReferenceMuzzles.Num()!=2) return false;
	for (const auto& P:GunPivots) if (P.ContainsNaN()) return false;
	for (const auto& P:ReferenceMuzzles) if (P.ContainsNaN()) return false;
	for (const auto& S:Samples)
		if (S.UpperTranslation.ContainsNaN() || S.LeftGunTranslation.ContainsNaN() || S.RightGunTranslation.ContainsNaN()
			|| S.UpperRotation.ContainsNaN() || !S.UpperRotation.IsNormalized()) return false;
	if (MissileFrames)
	{
		if (MissileFrames<2 || !FMath::IsFinite(MissileEnterSeconds) || MissileEnterSeconds<=0
			|| !FMath::IsFinite(MissileHoldSeconds) || MissileHoldSeconds<0
			|| !FMath::IsFinite(MissileReturnSeconds) || MissileReturnSeconds<=0
			|| !FMath::IsFinite(MissilePeakPitchDegrees) || MissilePeakPitchDegrees<=0
			|| !Neutral(Samples[FramesPerSide*2]) || !Neutral(Samples.Last())) return false;
		for (int32 I=FramesPerSide*2;I<Samples.Num();++I)
			if (!Samples[I].LeftGunTranslation.IsNearlyZero() || !Samples[I].RightGunTranslation.IsNearlyZero()) return false;
	}
	return Neutral(Samples[0]) && Neutral(Samples[FramesPerSide-1])
		&& Neutral(Samples[FramesPerSide]) && Neutral(Samples[FramesPerSide*2-1]);
}

bool UGuLiRecoilVATDefinition::BindMesh(UStaticMesh* Mesh)
{
#if WITH_EDITOR
	if (!Mesh || !IsValidDefinition()) return false;
	Mesh->Modify(); Mesh->RemoveUserDataOfClass(UGuLiRecoilVATBinding::StaticClass());
	auto* Binding=NewObject<UGuLiRecoilVATBinding>(Mesh); Binding->Definition=this;
	Mesh->AddAssetUserData(Binding); Mesh->MarkPackageDirty(); return true;
#else
	return false;
#endif
}

UGuLiRecoilVATDefinition* UGuLiRecoilVATDefinition::GetMeshDefinition(UStaticMesh* Mesh)
{
	return const_cast<UGuLiRecoilVATDefinition*>(GuLiRecoilVAT::Find(Mesh));
}

TArray<FString> UGuLiRecoilVATDefinition::ValidateImportedTextures() const
{
	TArray<FString> Errors;
	if (!IsValidDefinition()) { Errors.Add(TEXT("Invalid recoil definition")); return Errors; }
#if WITH_EDITOR
	for (int32 Role=0;Role<2;++Role)
	{
		UTexture2D* T=Role==0?PositionTexture.LoadSynchronous():RotationTexture.LoadSynchronous();
		const int32 Width=Role==0?3:1;
		if (!T || T->Source.GetSizeX()!=Width || T->Source.GetSizeY()!=Samples.Num()
			|| T->Source.GetFormat()!=TSF_RGBA32F || T->SRGB || T->CompressionSettings!=TC_HDR_F32
			|| T->Filter!=TF_Nearest || T->MipGenSettings!=TMGS_NoMipmaps)
		{ Errors.Add(TEXT("Recoil textures require nearest RGBA32F, authored dimensions, no sRGB/mips")); continue; }
		TArray64<uint8> Raw;
		if (!T->Source.GetMipData(Raw,0) || Raw.Num()!=int64(Samples.Num())*Width*4*sizeof(float))
		{ Errors.Add(TEXT("Invalid recoil texture source pixels")); continue; }
		const float* Values=reinterpret_cast<const float*>(Raw.GetData());
		for (int32 Frame=0;Frame<Samples.Num();++Frame)
		for (int32 Column=0;Column<Width;++Column)
		{
			const auto& S=Samples[Frame];
			const FVector P=Column==0?S.UpperTranslation:Column==1?S.LeftGunTranslation:S.RightGunTranslation;
			const FQuat Q=S.UpperRotation;
			const double Expected[4]={Role==0?P.X:Q.X,Role==0?P.Y:Q.Y,Role==0?P.Z:Q.Z,Role==0?1.:Q.W};
			for (int32 Channel=0;Channel<4;++Channel)
				if (!FMath::IsFinite(Values[(Frame*Width+Column)*4+Channel])
					|| FMath::Abs(Values[(Frame*Width+Column)*4+Channel]-Expected[Channel])>.001)
				{
					Errors.Add(FString::Printf(TEXT("%s CPU/GPU mismatch frame %d column %d channel %d"),*T->GetName(),Frame,Column,Channel));
					return Errors;
				}
		}
	}
#endif
	return Errors;
}

float UGuLiRecoilVATDefinition::Frame(int32 Side,double Age) const
{
	if (Age<0 || Age>=CycleSeconds) return float(Side*FramesPerSide);
	return float(Side*FramesPerSide+Age/CycleSeconds*(FramesPerSide-1));
}

float UGuLiRecoilVATDefinition::MissileFrame(const FGuLiMechanicalAnimationState& State,double Time) const
{
	if (!MissileFrames) return 0.f;
	const double Age=Time-State.MissileRecoilStartTime;
	const double Release=FMath::Max(double(State.MissileRecoilStartTime+MissileEnterSeconds+MissileHoldSeconds),
		double(State.MissileRecoilLastShotTime+MissileEnterSeconds+MissileHoldSeconds));
	const double Duration=MissileEnterSeconds+MissileHoldSeconds+MissileReturnSeconds;
	double ClipTime=0;
	if (Age>=0 && Time<Release+MissileReturnSeconds)
		ClipTime=Age<MissileEnterSeconds ? Age : Time<=Release ? MissileEnterSeconds
			: MissileEnterSeconds+MissileHoldSeconds+(Time-Release);
	return float(FramesPerSide*2+ClipTime/Duration*(MissileFrames-1));
}

void UGuLiRecoilVATDefinition::NotifyMissileShot(FGuLiMechanicalAnimationState& State,double Time) const
{
	if (!MissileFrames || Time<State.MissileRecoilLastShotTime) return;
	const double Release=State.MissileRecoilLastShotTime+MissileEnterSeconds+MissileHoldSeconds;
	if (Time>Release)
	{
		const auto Current=Sample(MissileFrame(State,Time));
		const double W=FMath::Clamp(2.*FMath::Acos(FMath::Clamp(FMath::Abs(Current.UpperRotation.W),0.,1.))
			/FMath::DegreesToRadians(double(MissilePeakPitchDegrees)),0.,1.);
		// Inverse smoothstep resumes a returning pose without snapping to neutral.
		const double Entry=.5-FMath::Sin(FMath::Asin(1.-2.*W)/3.);
		State.MissileRecoilStartTime=float(Time-Entry*MissileEnterSeconds);
	}
	State.MissileRecoilLastShotTime=float(Time);
}

FGuLiRecoilVATSample UGuLiRecoilVATDefinition::Sample(float Index) const
{
	if (Samples.IsEmpty() || !FMath::IsFinite(Index)) return {};
	Index=FMath::Clamp(Index,0.f,float(Samples.Num()-1));
	const int32 A=FMath::FloorToInt(Index), B=FMath::Min(A+1,Samples.Num()-1);
	return Blend(Samples[A],Samples[B],Index-A);
}

FGuLiRecoilVATSample UGuLiRecoilVATDefinition::Evaluate(const FGuLiMechanicalAnimationState& State,double Time) const
{
	const auto A=Sample(Frame(0,Time-State.RecoilStartTime[0]));
	const auto B=Sample(Frame(1,Time-State.RecoilStartTime[1]));
	FGuLiRecoilVATSample S;
	S.UpperTranslation=A.UpperTranslation+B.UpperTranslation;
	S.UpperRotation=(B.UpperRotation*A.UpperRotation).GetNormalized();
	S.LeftGunTranslation=A.LeftGunTranslation+B.LeftGunTranslation;
	S.RightGunTranslation=A.RightGunTranslation+B.RightGunTranslation;
	if (MissileFrames)
	{
		const auto M=Sample(MissileFrame(State,Time));
		const double W=FMath::Clamp(2.*FMath::Acos(FMath::Clamp(FMath::Abs(M.UpperRotation.W),0.,1.))
			/FMath::DegreesToRadians(double(MissilePeakPitchDegrees)),0.,1.);
		S.UpperTranslation=S.UpperTranslation*(1.-W)+M.UpperTranslation;
		FQuat Q=S.UpperRotation; if (Q.W<0) Q=Q*-1.;
		S.UpperRotation=(M.UpperRotation*(Q*(1.-W)+FQuat::Identity*W).GetNormalized()).GetNormalized();
	}
	return S;
}

void UGuLiRecoilVATDefinition::ConfigureMaterial(UMaterialInstanceDynamic& M) const
{
	M.SetTextureParameterValue(TEXT("RecoilPosition"),PositionTexture.LoadSynchronous());
	M.SetTextureParameterValue(TEXT("RecoilRotation"),RotationTexture.LoadSynchronous());
	M.SetScalarParameterValue(TEXT("RecoilFrames"),float(Samples.Num()));
	M.SetScalarParameterValue(TEXT("RecoilEnabled"),1.f);
	M.SetScalarParameterValue(TEXT("RecoilMirrorGunY"),bMirrorGunPivotY?1.f:0.f);
	M.SetScalarParameterValue(TEXT("RecoilMissileEnabled"),MissileFrames?1.f:0.f);
	M.SetScalarParameterValue(TEXT("RecoilMissilePeakRadians"),FMath::DegreesToRadians(MissilePeakPitchDegrees));
}

const UGuLiRecoilVATDefinition* GuLiRecoilVAT::Find(const UStaticMesh* Mesh)
{
	const auto* Binding=Mesh?Cast<UGuLiRecoilVATBinding>(const_cast<UStaticMesh*>(Mesh)->GetAssetUserDataOfClass(UGuLiRecoilVATBinding::StaticClass())):nullptr;
	return Binding?Binding->Definition.Get():nullptr;
}

void GuLiRecoilVAT::ConfigureBounds(UInstancedStaticMeshComponent& C)
{
	const UStaticMesh* Mesh=C.GetStaticMesh(); const auto* D=Find(Mesh);
	if (!D || !D->IsValidDefinition()) return;
	const FBoxSphereBounds Base=Mesh->GetBounds();
	const FVector Extent=D->RenderBounds.GetExtent()+(D->RenderBounds.GetCenter()-Base.Origin).GetAbs();
	const FVector Ratio=Extent/Base.BoxExtent.ComponentMax(FVector(1));
	C.SetBoundsScale(FMath::Max(1.f,float(Ratio.GetMax())));
}

void GuLiRecoilVAT::Update(const UGuLiRecoilVATDefinition& D,const FGuLiMechanicalAnimationState& State,
	double Time,bool Reset,FGuLiRecoilVATPlayback& P)
{
	for (int32 I=0;I<2;++I)
	{
		P.Previous[I]=P.Current[I]; P.Current[I]=D.Frame(I,Time-State.RecoilStartTime[I]);
		if (Reset) P.Previous[I]=P.Current[I];
	}
	P.MissilePrevious=P.MissileCurrent; P.MissileCurrent=D.MissileFrame(State,Time);
	if (Reset) P.MissilePrevious=P.MissileCurrent;
}

void GuLiRecoilVAT::Write(UInstancedStaticMeshComponent& C,int32 Index,const FGuLiRecoilVATPlayback& P,bool Reset)
{
	if (C.NumCustomDataFloats<CustomDataFloatCount || !C.PerInstanceSMData.IsValidIndex(Index)) return;
	TArray<float,TInlineAllocator<CustomDataFloatCount>> Row;
	Row.Append(C.PerInstanceSMCustomData.GetData()+Index*C.NumCustomDataFloats,C.NumCustomDataFloats);
	for (int32 I=0;I<2;++I) {Row[FirstCustomData+I]=P.Current[I];Row[FirstCustomData+2+I]=Reset?P.Current[I]:P.Previous[I];}
	Row[67]=P.MissileCurrent; Row[68]=Reset?P.MissileCurrent:P.MissilePrevious;
	C.SetCustomData(Index,MakeArrayView(Row),false);
}
