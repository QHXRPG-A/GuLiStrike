#include "Gameplay/Presentation/GuLiBiZhiMaoReviewActor.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "UObject/Package.h"
#include "Gameplay/Resources/GuLiResourceWorldSubsystem.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/ScopeExit.h"

AGuLiBiZhiMaoReviewActor::AGuLiBiZhiMaoReviewActor()
{
	PrimaryActorTick.bCanEverTick=true;
	Instances=CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("ReviewVertexVAT"));
	SetRootComponent(Instances);
	Instances->SetMobility(EComponentMobility::Movable);
	Instances->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Instances->SetCanEverAffectNavigation(false);
	Instances->SetNumCustomDataFloats(GuLiVATAnimation::CustomDataFloatCount);
	Instances->SetCullDistances(0,0);
	bReplicates=false;
}
void AGuLiBiZhiMaoReviewActor::BeginPlay()
{
	Super::BeginPlay();
	FString AcceptanceRun;
	if (FParse::Value(FCommandLine::Get(),TEXT("GuLiAcceptanceRun="),AcceptanceRun) && !AcceptanceRun.IsEmpty())
	{
		// The acceptance cohort and input script own their load and resources.
		// This unrelated authoring preview must not inject extra actors or gifts.
		Instances->ClearInstances(); SetActorHiddenInGame(true); SetActorTickEnabled(false); return;
	}
	ResetPreview();
}
void AGuLiBiZhiMaoReviewActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	Instances->SetStaticMesh(Mesh);
	ResetPreview();
}
void AGuLiBiZhiMaoReviewActor::ResetPreview()
{
	Offset=FVector::ZeroVector;Playback={};Aim={};
	Instances->ClearInstances();
	Instances->AddInstance(FTransform(FQuat::Identity,Offset,FVector(ModelScale)));
	Tick(0);
}
void AGuLiBiZhiMaoReviewActor::Tick(float Dt)
{
	Super::Tick(Dt);

	if (!Animation || !Animation->IsValidDefinition() || !Mesh || Instances->GetInstanceCount()!=1) return;
#if WITH_EDITOR
	// ISM setters call Modify even for derived preview frames. Keep those writes
	// out of map dirtiness/undo, without clearing authored changes on the package.
	const bool bEditorPreview = GetWorld() && !GetWorld()->IsGameWorld();
	const EObjectFlags SavedPreviewFlags = Instances->GetFlags() & (RF_Transient | RF_Transactional);
	if (bEditorPreview)
	{
		Instances->ClearFlags(RF_Transactional);
		Instances->SetFlags(RF_Transient);
	}
	ON_SCOPE_EXIT
	{
		if (bEditorPreview)
		{
			Instances->ClearFlags(RF_Transient | RF_Transactional);
			Instances->SetFlags(SavedPreviewFlags);
		}
	};
#endif
	const float SafeDt=FMath::Clamp(Dt,0.f,.1f);
	Offset+=LocalVelocity*SafeDt;
	const FTransform LocalRoot(FQuat::Identity,Offset,FVector(ModelScale));
	const FTransform WorldRoot=LocalRoot*GetActorTransform();
	const FVector Velocity=GetActorQuat().RotateVector(LocalVelocity);
	const auto Previous=Playback;
	GuLiVATAnimation::Step(*Animation,Velocity,GetActorRotation().Yaw,true,SafeDt,Playback);
	const FVector Position=Target ? Target->GetActorLocation() : FVector::ZeroVector;
	GuLiVATAnimation::StepAim(*Animation,Playback,WorldRoot,Target ? &Position : nullptr,SafeDt,Aim);
	Instances->UpdateInstanceTransform(0,LocalRoot,false,true,false);
	GuLiVATAnimation::WriteInstance(*Instances,0,*Animation,Playback,Previous,Aim,GetActorRotation().Yaw,Dt==0,Velocity);
}
void AGuLiBiZhiMaoReviewActor::StopPreview() { LocalVelocity=FVector::ZeroVector; }
void AGuLiBiZhiMaoReviewActor::ForwardPreview() { LocalVelocity=FVector(144,0,0); }
void AGuLiBiZhiMaoReviewActor::BackwardPreview() { LocalVelocity=FVector(-144,0,0); }
void AGuLiBiZhiMaoReviewActor::LeftPreview() { LocalVelocity=FVector(0,-144,0); }
void AGuLiBiZhiMaoReviewActor::RightPreview() { LocalVelocity=FVector(0,144,0); }
void AGuLiBiZhiMaoReviewActor::DiagonalPreview() { LocalVelocity=FVector(1,1,0).GetSafeNormal()*144; }
