#include "Gameplay/Resources/GuLiMiningPresentationComponent.h"
#include "Commander/Presentation/GuLiCommanderLODSubsystem.h"
#include "Gameplay/Performance/GuLiPerformanceSubsystem.h"
#include "Engine/World.h"
#include "NiagaraComponent.h"
#include "Gameplay/Vfx/GuLiClientPresentationPolicy.h"

#include "Gameplay/Resources/GuLiQuadrupedMinerPresentation.h"
#include "Gameplay/Presentation/GuLiEngineeringPoseComponent.h"

bool UGuLiMiningPresentationComponent::IsReady() const
{ return Presentation.IsValid() && Presentation->IsReady(); }

bool UGuLiMiningPresentationComponent::InitializePresentation(AActor* Actor)
{
    if (GetWorld()) GetWorld()->GetTimerManager().ClearTimer(VisibilityTimer);
    if (Presentation.IsValid()) Presentation->SetMiningState(false,FVector::ZeroVector);
    Presentation=Cast<AGuLiQuadrupedMinerPresentation>(Actor);
    bAppliedVisible=false; bLatestActive=false; LatestTarget=FVector::ZeroVector; Visibility={};
    if (!IsReady() || !Presentation->BodyVAT->Animation->IsValidData()) return false;
    Actor->SetActorEnableCollision(false);
    LocalMuzzle=GetOwner()->GetActorTransform().InverseTransformPosition(Presentation->GetReferenceMuzzleWorld());
    if (GetWorld() && GuLiClientPresentation::CanPresent(GetWorld()))
        GetWorld()->GetTimerManager().SetTimer(VisibilityTimer,this,&ThisClass::PollVisibility,
            FGuLiPersistentEffectVisibility::CheckSeconds,true);
    return true;
}

bool UGuLiMiningPresentationComponent::CalculateMuzzle(const FTransform& VehiclePose, FVector& Muzzle) const
{
    if (!IsReady()) return false;
    Muzzle=VehiclePose.TransformPosition(LocalMuzzle);
    return !Muzzle.ContainsNaN();
}

void UGuLiMiningPresentationComponent::RefreshDisplayedAim()
{
    if (auto* Actor=Presentation.Get())
    {
        const auto* Pose=GetOwner()->FindComponentByClass<UGuLiEngineeringPoseComponent>();
        Actor->NotifyDisplayedPose(Pose && Pose->IsDisplacementReset()); Actor->RefreshBeam();
    }
}

void UGuLiMiningPresentationComponent::ApplyMining(bool Active, const FVector& Target)
{
    if (!IsReady() || Target.ContainsNaN()) return;
    const bool Changed=bLatestActive!=Active;
    bLatestActive=Active; LatestTarget=Target;
    Presentation->SetMiningState(Active,Target);
    if (GuLiClientPresentation::CanPresent(GetWorld()) && Changed) PollVisibility();
}

void UGuLiMiningPresentationComponent::ApplyVisibleMining(bool Show)
{
    if (auto* Actor=Presentation.Get()) Actor->SetBeamVisible(Show);
    bAppliedVisible=Show;
}

void UGuLiMiningPresentationComponent::PollVisibility()
{
	FGuLiPerformanceScope Timing(GetWorld(),TEXT("Mining.VisibilityMs"));
	AActor* Actor=Presentation.Get(); if (!Actor || !IsReady() || !GetWorld()) return;
	if (!bLatestActive)
	{
		Visibility.Update(false,false,GetWorld()->GetTimeSeconds()); ApplyVisibleMining(false); return;
	}
	FBox Bounds(ForceInit); Bounds+=LatestTarget;
	Bounds+=Presentation->GetDisplayedMuzzleWorld();
	TInlineComponentArray<UNiagaraComponent*> Beams(Actor);
	for (auto* Beam : Beams) if (Beam->Bounds.GetBox().IsValid) Bounds+=Beam->Bounds.GetBox();
	// Include endpoint sprites even before the first Niagara bounds update.
	Bounds=Bounds.ExpandBy(500.f);
	auto* LOD=GetWorld()->GetSubsystem<UGuLiCommanderLODSubsystem>();
	FGuLiCommanderLODDecision Decision; Decision.bVisible=true; Decision.TargetLevel=EGuLiCommanderLODLevel::Full;
	if (LOD) Decision=LOD->EvaluateWorldEffectBounds(Visibility.DetailQuery(Bounds),20000);
	Visibility.ApplyDetail(Decision,GetWorld()->GetTimeSeconds());
	for (auto* Beam : Beams) GuLiClientPresentation::ApplyEndpointDetail(Beam,Visibility.DetailLevel.Get(EGuLiCommanderLODLevel::Full));
	const bool Visible=!Actor->IsHidden() && Decision.bVisible;
	Visibility.Update(bLatestActive,Visible,GetWorld()->GetTimeSeconds());
	ApplyVisibleMining(Visibility.State==EGuLiPersistentEffectVisibility::Visible);
}

void UGuLiMiningPresentationComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	if (GetWorld()) GetWorld()->GetTimerManager().ClearTimer(VisibilityTimer);
	ApplyVisibleMining(false); Presentation.Reset(); Super::EndPlay(Reason);
}

FString UGuLiMiningPresentationComponent::GetPresentationDiagnosticsJson() const
{
 const auto* Actor=Presentation.Get();
 return FString::Printf(TEXT("{\"state\":%d,\"active\":%s,\"applied_visible\":%s,\"presentation\":\"%s\",\"tick_enabled\":%s,\"timer_active\":%s,\"target\":[%.3f,%.3f,%.3f]}"),
  int32(Visibility.State),bLatestActive ? TEXT("true") : TEXT("false"),bAppliedVisible ? TEXT("true") : TEXT("false"),
  Actor ? *Actor->GetName() : TEXT(""),Actor && Actor->IsActorTickEnabled() ? TEXT("true") : TEXT("false"),
  GetWorld() && GetWorld()->GetTimerManager().IsTimerActive(VisibilityTimer) ? TEXT("true") : TEXT("false"),LatestTarget.X,LatestTarget.Y,LatestTarget.Z);
}
