#include "Gameplay/Presentation/GuLiBiZhiMaoReviewActor.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "UObject/Package.h"
#include "Gameplay/Economy/GuLiTeamEconomySubsystem.h"
#include "Gameplay/Resources/GuLiResourceWorldSubsystem.h"

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
#if !UE_BUILD_SHIPPING
	if (bPrepareConstructionReview && GetWorld()->GetNetMode()!=NM_Client && HasAuthority() && GetWorld()->IsGameWorld()
		&& GetWorld()->GetPackage()->GetName().EndsWith(TEXT("LVL_CommanderMassPrototype")))
	{
		auto* Resources=GetWorld()->GetSubsystem<UGuLiResourceWorldSubsystem>();
		auto* Economy=GetWorld()->GetSubsystem<UGuLiTeamEconomySubsystem>();
		if (Resources && Resources->IsRuntimeReady() && Economy && Economy->AreTransactionsOpen()
			&& GuLiEconomy::IsPlayableTeam(ConstructionReviewTeam))
		{
			if (!bConstructionReviewPrepared)
			{
				Economy->Credit(ConstructionReviewTeam,EGuLiResourceType::Blue,120);
				Economy->Credit(ConstructionReviewTeam,EGuLiResourceType::Red,60);
				bConstructionReviewPrepared=true;
			}
			while (BuilderGroundLocations.IsValidIndex(BuildersPrepared))
			{
				if (!Resources->SpawnConstructionVehicle(ConstructionReviewTeam,BuilderGroundLocations[BuildersPrepared])) break;
				++BuildersPrepared;
			}
		}
	}
#endif
	if (!Animation || !Animation->IsValidDefinition() || !Mesh || Instances->GetInstanceCount()!=1) return;
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
