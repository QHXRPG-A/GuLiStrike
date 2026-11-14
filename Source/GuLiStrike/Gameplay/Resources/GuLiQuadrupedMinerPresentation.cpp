#include "Gameplay/Resources/GuLiQuadrupedMinerPresentation.h"
#include "Gameplay/Vfx/GuLiVfxRegistrySubsystem.h"
#include "Gameplay/Vfx/GuLiClientPresentationPolicy.h"
#include "Commander/Presentation/GuLiCommanderLODSubsystem.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#if WITH_EDITOR
#include "Engine/StaticMesh.h"
#include "MeshDescription.h"
#include "StaticMeshAttributes.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"
#endif

FString UGuLiQuadrupedMinerVATData::BuildApprovedMesh(UStaticMesh* Mesh,const FString& SourceDirectory)
{
#if WITH_EDITOR
	const FString Expected=FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()/TEXT("ArtSource/QuadrupedMiner_10k_v5"));
	if (!Mesh || !Mesh->GetPathName().StartsWith(TEXT("/Game/GuLiStrike/Commander/Units/ElectromagneticMiner/QuadrupedMiner_10k_v5/"))
		|| !FPaths::IsSamePath(SourceDirectory,Expected)) return TEXT("Rejected mesh or source directory");
	TArray<TSharedPtr<FJsonObject>> Data;
	for (int32 LOD=0;LOD<3;++LOD)
	{
		FString Text; TSharedPtr<FJsonObject> J;
		if (!FFileHelper::LoadFileToString(Text,*(Expected/FString::Printf(TEXT("lod%d.json"),LOD)))
			|| !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),J) || !J) return TEXT("Invalid source JSON");
		const int32 Corners=J->GetArrayField(TEXT("triangles")).Num()*3;
		if (J->GetIntegerField(TEXT("lod"))!=LOD || J->GetArrayField(TEXT("colors")).Num()!=Corners
			|| J->GetArrayField(TEXT("normals")).Num()!=Corners || J->GetArrayField(TEXT("uvs")).Num()!=3
			|| Corners>3*(LOD==0?9854:LOD==1?5000:3000)) return TEXT("Invalid corner count or budget");
		Data.Add(J);
	}
	Mesh->Modify(); Mesh->SetNumSourceModels(3);
	for (int32 LOD=0;LOD<3;++LOD)
	{
		const auto& J=Data[LOD];
		FMeshDescription* D=Mesh->CreateMeshDescription(LOD);
		D->Empty(); FStaticMeshAttributes A(*D); A.Register();
		auto Positions=A.GetVertexPositions(); auto Normals=A.GetVertexInstanceNormals();
		auto Tangents=A.GetVertexInstanceTangents(); auto Signs=A.GetVertexInstanceBinormalSigns();
		auto Colors=A.GetVertexInstanceColors(); auto UVs=A.GetVertexInstanceUVs(); UVs.SetNumChannels(3);
		TArray<FVertexID> V;
		for (const auto& P:J->GetArrayField(TEXT("positions")))
		{
			const auto& C=P->AsArray(); const auto ID=D->CreateVertex();
			Positions[ID]=FVector3f(C[0]->AsNumber(),C[1]->AsNumber(),C[2]->AsNumber()); V.Add(ID);
		}
		TArray<FPolygonGroupID> Groups;
		for (const auto& Name:J->GetArrayField(TEXT("material_names")))
		{ const auto ID=D->CreatePolygonGroup(); A.GetPolygonGroupMaterialSlotNames()[ID]=FName(*Name->AsString()); Groups.Add(ID); }
		int32 Corner=0;
		for (const auto& Triangle:J->GetArrayField(TEXT("triangles")))
		{
			const auto& T=Triangle->AsArray(); TArray<FVertexInstanceID> VI;
			for (int32 I=0;I<3;++I,++Corner)
			{
				const int32 Vertex=int32(T[I]->AsNumber()); if (!V.IsValidIndex(Vertex)) return TEXT("Invalid vertex index");
				const auto ID=D->CreateVertexInstance(V[Vertex]); VI.Add(ID);
				const auto& N=J->GetArrayField(TEXT("normals"))[Corner]->AsArray();
				const FVector3f Normal(N[0]->AsNumber(),N[1]->AsNumber(),N[2]->AsNumber()); Normals[ID]=Normal;
				const FVector3f Axis=FMath::Abs(Normal.Z)<.9f?FVector3f(0,0,1):FVector3f(0,1,0);
				Tangents[ID]=FVector3f::CrossProduct(Axis,Normal).GetSafeNormal(); Signs[ID]=1;
				const auto& C=J->GetArrayField(TEXT("colors"))[Corner]->AsArray();
				Colors[ID]=FVector4f(C[0]->AsNumber(),C[1]->AsNumber(),C[2]->AsNumber(),C[3]->AsNumber());
				for (int32 Channel=0;Channel<3;++Channel)
				{
					const auto& U=J->GetArrayField(TEXT("uvs"))[Channel]->AsArray()[Corner]->AsArray();
					UVs.Set(ID,Channel,FVector2f(U[0]->AsNumber(),Channel==0?LOD:U[1]->AsNumber()));
				}
			}
			const int32 Group=int32(T[3]->AsNumber()); if (!Groups.IsValidIndex(Group)) return TEXT("Invalid material index");
			D->CreatePolygon(Groups[Group],VI);
		}
		auto& Build=Mesh->GetSourceModel(LOD).BuildSettings;
		Build.bRecomputeNormals=false; Build.bRecomputeTangents=false; Build.bGenerateLightmapUVs=false;
		Build.bUseFullPrecisionUVs=true; Build.bRemoveDegenerates=false;
		Mesh->GetSourceModel(LOD).ReductionSettings.PercentTriangles=1.f;
		Mesh->CommitMeshDescription(LOD);
	}
	Mesh->Build(false); Mesh->MarkPackageDirty();
	return TEXT("OK");
#else
	return TEXT("Editor-only authoring");
#endif
}

bool UGuLiQuadrupedMinerVATData::IsValidData() const
{
	if (LODs.Num()!=3 || TotalFrames!=103 || MuzzleSamples.Num()!=TotalFrames || TextureWidth<=0
		|| WalkFirstFrame!=1 || WalkFrameCount!=29 || MiningFirstFrame!=30 || MiningFrameCount!=73
		|| !FMath::IsFinite(WalkCycleDistanceCm) || WalkCycleDistanceCm<=0
		|| !FMath::IsFinite(WalkDurationSeconds) || WalkDurationSeconds<=0
		|| !FMath::IsFinite(MiningDurationSeconds) || MiningDurationSeconds<=0 || !RenderBounds.IsValid
		|| ReferenceMuzzle.ContainsNaN() || !HitMaterial || !WreckMaterial) return false;
	for (const auto& L : LODs)
		if (!L.Position || !L.Normal || L.VertexCount<=0 || L.NormalCount<=0
			|| L.PositionRows!=FMath::DivideAndRoundUp(L.VertexCount,TextureWidth)
			|| L.NormalRows!=FMath::DivideAndRoundUp(L.NormalCount,TextureWidth)) return false;
	for (const auto& P : MuzzleSamples) if (P.ContainsNaN()) return false;
	return true;
}

FVector UGuLiQuadrupedMinerVATData::SampleMuzzle(float Frame) const
{
	if (MuzzleSamples.IsEmpty()) return ReferenceMuzzle;
	const float Safe=FMath::Clamp(Frame,0.f,float(MuzzleSamples.Num()-1));
	const int32 A=FMath::FloorToInt(Safe), B=FMath::Min(A+1,MuzzleSamples.Num()-1);
	return FMath::Lerp(MuzzleSamples[A],MuzzleSamples[B],Safe-A);
}

AGuLiQuadrupedMinerPresentation::AGuLiQuadrupedMinerPresentation()
{
	bReplicates=false;
	SetCanBeDamaged(false);
	PrimaryActorTick.bCanEverTick=true;
	PrimaryActorTick.bStartWithTickEnabled=false;
	PrimaryActorTick.TickGroup=TG_PostUpdateWork;
	BodyVAT=CreateDefaultSubobject<UGuLiMiningVATMeshComponent>(TEXT("BodyVAT"));
	SetRootComponent(BodyVAT);
	BodyVAT->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	BodyVAT->SetCanEverAffectNavigation(false);
	BodyVAT->SetGenerateOverlapEvents(false);
	MiningLaser=CreateDefaultSubobject<UNiagaraComponent>(TEXT("MiningLaser"));
	MiningLaser->SetupAttachment(BodyVAT);
	MiningLaser->SetAutoActivate(false);
	MiningLaser->SetCanEverAffectNavigation(false);
	MiningLaser->SetVisibility(false);
}

bool AGuLiQuadrupedMinerPresentation::IsReady() const
{ return BodyVAT && BodyVAT->GetStaticMesh() && BodyVAT->Animation && BodyVAT->Animation->LODs.Num()==3
	&& BodyVAT->Animation->MuzzleSamples.Num()==BodyVAT->Animation->TotalFrames; }

void AGuLiQuadrupedMinerPresentation::BeginPlay()
{
	Super::BeginPlay();
	LastPosition=GetActorLocation(); LastYaw=GetActorRotation().Yaw;
	LastTickTime=LastPoseTime=WalkEpoch=MiningEpoch=GetWorld()->GetTimeSeconds();
	if (!GuLiClientPresentation::CanPresent(GetWorld())) return;
	MiningLaser->SetAsset(GuLiVfx::Load<UNiagaraSystem>(this,GuLiVfxIds::MiningLaser));
	MiningLaser->SetWorldScale3D(GuLiVfx::Scale(this,GuLiVfxIds::MiningLaser));
	WritePose(true);
}

void AGuLiQuadrupedMinerPresentation::Wake()
{
	if (!GuLiClientPresentation::CanPresent(GetWorld()) || !bPresentationVisible || !IsReady()) return;
	if (!IsActorTickEnabled())
	{
		LastTickTime=GetWorld()->GetTimeSeconds()-GetWorld()->GetDeltaSeconds();
		SetActorTickEnabled(true);
	}
}

void AGuLiQuadrupedMinerPresentation::NotifyDisplayedPose(bool bTeleport)
{
	if (!GetWorld()) return;
	const double Now=GetWorld()->GetTimeSeconds();
	const double Dt=FMath::Max(0.0,Now-LastPoseTime);
	const FVector Position=GetActorLocation();
	const float Yaw=GetActorRotation().Yaw;
	const double Distance=FVector::Dist2D(Position,LastPosition);
	const float Turn=FMath::Abs(FMath::FindDeltaAngleDegrees(LastYaw,Yaw));
	bTeleport|=Distance>FMath::Max(1000.0,900.0*Dt*4.0);
	if (bTeleport)
	{
		LastMotionTime=Now-.2; WalkEpoch=Now; bResetPoseHistory=true;
	}
	else if (Distance>FMath::Max(.001,5.0*Dt) || Turn>FMath::Max(.001,1.0*Dt)) LastMotionTime=Now;
	LastPosition=Position; LastYaw=Yaw; LastPoseTime=Now;
	if (bTeleport || Now-LastMotionTime<.2 || Clip!=0 || Blend<1) Wake();
}

void AGuLiQuadrupedMinerPresentation::SetPresentationVisible(bool Visible)
{
	if (bPresentationVisible==Visible) return;
	bPresentationVisible=Visible;
	LastPosition=GetActorLocation(); LastYaw=GetActorRotation().Yaw;
	LastPoseTime=LastTickTime=GetWorld()->GetTimeSeconds();
	bInitialized=false;
	if (!Visible) { SetBeamVisible(false); SetActorTickEnabled(false); }
	else { WritePose(true); Wake(); }
}

FVector AGuLiQuadrupedMinerPresentation::GetReferenceMuzzleWorld() const
{ return BodyVAT->GetComponentTransform().TransformPosition(BodyVAT->Animation->ReferenceMuzzle); }

FVector AGuLiQuadrupedMinerPresentation::GetDisplayedMuzzleWorld() const
{
	if (!IsReady()) return GetActorLocation();
	const auto& D=*BodyVAT->Animation;
	return BodyVAT->GetComponentTransform().TransformPosition(FMath::Lerp(D.SampleMuzzle(FromFrame),D.SampleMuzzle(Frame),Blend));
}

void AGuLiQuadrupedMinerPresentation::SetMiningState(bool Active, const FVector& Target)
{
	if (Target.ContainsNaN()) return;
	const bool Changed=bMining!=Active;
	bMining=Active; TargetWorld=Target;
	if (!Active) SetBeamVisible(false);
	if (Changed) { if (Active) MiningEpoch=GetWorld()->GetTimeSeconds(); Wake(); }
	RefreshBeam();
}

void AGuLiQuadrupedMinerPresentation::SetBeamVisible(bool Visible)
{
	Visible=Visible && bMining && bPresentationVisible && !IsHidden() && BodyVAT->IsVisible()
		&& GuLiClientPresentation::CanPresent(GetWorld());
	if (Visible==bBeamVisible) { if (Visible) RefreshBeam(); return; }
	bBeamVisible=Visible;
	if (!Visible) { MiningLaser->DeactivateImmediate(); MiningLaser->SetVisibility(false); }
	else
	{
		RefreshBeam(); MiningLaser->SetVisibility(true); MiningLaser->Activate(false); Wake();
	}
}

void AGuLiQuadrupedMinerPresentation::RefreshBeam()
{
	if (!bBeamVisible || !IsReady()) return;
	const FVector Muzzle=GetDisplayedMuzzleWorld();
	MiningLaser->SetWorldLocationAndRotation(Muzzle,(TargetWorld-Muzzle).Rotation());
	MiningLaser->SetVariablePosition(TEXT("User.Beam End"),TargetWorld);
}

void AGuLiQuadrupedMinerPresentation::WritePose(bool Reset)
{
	const float A=FromFrame, B=Frame, T=Blend;
	if (Reset) { PreviousA=A; PreviousB=B; PreviousBlend=T; }
	const float Values[]={A,B,T,PreviousA,PreviousB,PreviousBlend};
	for (int32 I=0; I<6; ++I) BodyVAT->SetCustomPrimitiveDataFloat(19+I,Values[I]);
	PreviousA=A; PreviousB=B; PreviousBlend=T;
	bInitialized=true;
}

void AGuLiQuadrupedMinerPresentation::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!IsReady() || !bPresentationVisible || IsHidden() || !BodyVAT->IsVisible())
	{ SetBeamVisible(false); SetActorTickEnabled(false); LastPosition=GetActorLocation(); return; }
	const double Now=GetWorld()->GetTimeSeconds();
	const float Dt=FMath::Max(0.f,float(Now-LastTickTime)); LastTickTime=Now;
	NotifyDisplayedPose();
	const uint8 Desired=bMining ? 2 : Now-LastMotionTime<.2 ? 1 : 0;
	if (Desired!=Clip)
	{
		FromFrame=Blend<.5f ? FromFrame : Frame;
		Clip=Desired; TransitionTime=0; Blend=0;
	}
	const auto& D=*BodyVAT->Animation;
	// Analytic clocks survive coarse visibility updates and never reset on a turn
	// or a single stationary network sample. Only a teleport resets walking history.
	Phase=Clip==1 ? float(FMath::Frac(FMath::Max(0.0,Now-WalkEpoch)/D.WalkDurationSeconds))
		: Clip==2 ? float(FMath::Frac(FMath::Max(0.0,Now-MiningEpoch)/D.MiningDurationSeconds)) : 0.f;
	Frame=Clip==1 ? D.WalkFirstFrame+Phase*(D.WalkFrameCount-1)
		: Clip==2 ? D.MiningFirstFrame+Phase*(D.MiningFrameCount-1) : 0;
	TransitionTime+=Dt; Blend=FMath::Min(TransitionTime/.15f,1.f);
	if (Blend>=1) FromFrame=Frame;
	WritePose(!bInitialized || bResetPoseHistory); bResetPoseHistory=false;
	RefreshBeam();
	auto* LOD=GetWorld()->GetSubsystem<UGuLiCommanderLODSubsystem>();
	const bool Offscreen=GuLiClientPresentation::OffscreenUnitsEnabled() && LOD && !LOD->IsBoundsVisible(BodyVAT->Bounds.GetBox());
	SetActorTickInterval(Offscreen ? float(GuLiClientPresentation::OffscreenUnitInterval) : 0.f);
	if (Clip==0 && Blend>=1) SetActorTickEnabled(false);
}

void AGuLiQuadrupedMinerPresentation::EndPlay(EEndPlayReason::Type Reason)
{ SetBeamVisible(false); Super::EndPlay(Reason); }

FString AGuLiQuadrupedMinerPresentation::GetVATDiagnosticsJson() const
{
	return FString::Printf(TEXT("{\"ready\":%s,\"clip\":%u,\"phase\":%.6f,\"frame\":%.6f,\"blend\":%.6f,\"mining\":%s,\"beam\":%s}"),
		IsReady()?TEXT("true"):TEXT("false"),Clip,Phase,Frame,Blend,bMining?TEXT("true"):TEXT("false"),bBeamVisible?TEXT("true"):TEXT("false"));
}
