#include "Gameplay/Teleport/GuLiTeleportFieldActor.h"
#include "Components/DecalComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "GameFramework/GameStateBase.h"
#include "Engine/World.h"
#include "UObject/ConstructorHelpers.h"

AGuLiTeleportFieldActor::AGuLiTeleportFieldActor()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = false;
	bAlwaysRelevant = true;
	// The short destination window must be visible while a joining client loads troop state.
	NetPriority = 4.f;
	SetNetUpdateFrequency(10);
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
}
void AGuLiTeleportFieldActor::EnsureMaterials()
{
	if (GetNetMode() == NM_DedicatedServer) { return; }
	if (!Ground)
	{
		Ground = NewObject<UDecalComponent>(this,TEXT("RangeAndProgress")); Ground->SetupAttachment(GetRootComponent());
		Ground->SetRelativeRotation(FRotator(-90,0,0)); Ground->FadeScreenSize = 0; Ground->RegisterComponent();
		Beam = NewObject<UStaticMeshComponent>(this,TEXT("TeleportBeam")); Beam->SetupAttachment(GetRootComponent());
		Beam->SetCollisionEnabled(ECollisionEnabled::NoCollision); Beam->SetCastShadow(false); Beam->SetReceivesDecals(false);
		Beam->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Game/GuLiStrike/FX/CommanderTeleport/SM_TeleportCylinder.SM_TeleportCylinder")));
		Beam->RegisterComponent();
	}
	if (!GroundMID)
	{
		auto* M = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/GuLiStrike/FX/CommanderTeleport/M_TeleportGround.M_TeleportGround"));
		if (M) { GroundMID = UMaterialInstanceDynamic::Create(M,this); Ground->SetDecalMaterial(GroundMID); }
	}
	if (!BeamMID)
	{
		auto* M = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/GuLiStrike/FX/CommanderTeleport/M_TeleportBeam.M_TeleportBeam"));
		if (M) { BeamMID = UMaterialInstanceDynamic::Create(M,this); Beam->SetMaterial(0,BeamMID); }
	}
}
void AGuLiTeleportFieldActor::SetPreview(const FVector& Point, const float Radius, const bool bValid)
{
	bPreview = true; SetActorLocation(Point); EnsureMaterials();
	Ground->DecalSize = FVector(200,Radius/.94f,Radius/.94f);
	Beam->SetVisibility(false);
	if (GroundMID)
	{
		GroundMID->SetScalarParameterValue(TEXT("Progress"),1);
		GroundMID->SetScalarParameterValue(TEXT("Opacity"),.5f);
		GroundMID->SetVectorParameterValue(TEXT("Tint"), bValid ? FLinearColor(.0f,.7f,1) : FLinearColor(1,.08f,.03f));
	}
}
void AGuLiTeleportFieldActor::TickVisuals()
{
	if (GetNetMode() == NM_DedicatedServer || bPreview || State.Phase == EGuLiTeleportPhase::Idle) { return; }
	EnsureMaterials();
	const auto* GS = GetWorld()->GetGameState();
	const double Now = GS ? GS->GetServerWorldTimeSeconds() : GetWorld()->GetTimeSeconds();
	const double Elapsed = FMath::Max(0.0,Now-State.PhaseStartTime);
	const bool bLanding = State.bLanded;
	const bool bBeam = bLanding || State.Phase == EGuLiTeleportPhase::AwaitingDestination || State.Phase == EGuLiTeleportPhase::Returning;
	const float Alpha = State.Phase == EGuLiTeleportPhase::Finished ? FMath::Clamp(1.f-float(Elapsed)/.5f,0.f,1.f) : 1.f;
	SetActorLocation(bLanding ? State.Destination : State.Source);
	Ground->DecalSize = FVector(200,State.Config.RadiusCentimeters/.94f,State.Config.RadiusCentimeters/.94f);
	if (GroundMID)
	{
		const float Progress = State.Phase == EGuLiTeleportPhase::Windup ? GuLiTeleport::WindupProgress(State,Now) : (State.Phase == EGuLiTeleportPhase::Finished ? State.FinalProgress : 1.f);
		GroundMID->SetScalarParameterValue(TEXT("Progress"),Progress);
		GroundMID->SetScalarParameterValue(TEXT("Opacity"),Alpha);
		GroundMID->SetVectorParameterValue(TEXT("Tint"),FLinearColor(0,.7f,1));
	}
	if (State.Phase != EGuLiTeleportPhase::Finished)
	{
		Beam->SetVisibility(bBeam);
		const float FullHeight = State.Config.BeamHeightCentimeters;
		const float Height = FullHeight * FMath::Clamp(float(Elapsed)/.25f,0.001f,1.f);
		Beam->SetRelativeScale3D(FVector(State.Config.RadiusCentimeters/50,State.Config.RadiusCentimeters/50,Height/100));
		Beam->SetRelativeLocation(FVector(0,0,bLanding ? FullHeight-Height*.5f : Height*.5f));
	}
	if (BeamMID) { BeamMID->SetScalarParameterValue(TEXT("Opacity"),Alpha); }
}
