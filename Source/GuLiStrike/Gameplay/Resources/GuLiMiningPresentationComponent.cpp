#include "Gameplay/Resources/GuLiMiningPresentationComponent.h"
#include "Commander/Presentation/GuLiCommanderLODSubsystem.h"
#include "Gameplay/Performance/GuLiPerformanceSubsystem.h"
#include "Engine/World.h"
#include "NiagaraComponent.h"
#include "Gameplay/Vfx/GuLiClientPresentationPolicy.h"

#include "Components/PrimitiveComponent.h"
#include "GameFramework/Actor.h"
#include "UObject/StructOnScope.h"
#include "UObject/UnrealType.h"

bool UGuLiMiningPresentationComponent::InitializePresentation(AActor* Actor)
{
	if (GetWorld()) GetWorld()->GetTimerManager().ClearTimer(VisibilityTimer);
	if (Presentation.IsValid() && bAppliedVisible) ApplyVisibleMining(false);
	for (int32 Side=0;Side<2;++Side) { Pivots[Side].Reset(); Muzzles[Side].Reset(); }
	Presentation = Actor; bAppliedVisible=false; bLatestActive=false; LatestTarget=FVector::ZeroVector; Visibility={};
	if (!Actor || !Actor->FindFunction(TEXT("StartMiningAt")) || !Actor->FindFunction(TEXT("StopMining"))) return false;
	Actor->SetActorEnableCollision(false);
	TInlineComponentArray<USceneComponent*> Components(Actor);
	for (USceneComponent* Component : Components)
	{
		Component->SetCanEverAffectNavigation(false);
		for (int32 Side = 0; Side < 2; ++Side)
		{
			const FString Suffix = Side == 0 ? TEXT("L") : TEXT("R");
			if (Component->GetFName() == FName(TEXT("CollectorPivot_") + Suffix)) Pivots[Side] = Component;
			if (Component->GetFName() == FName(TEXT("LaserMuzzle_") + Suffix)) Muzzles[Side] = Component;
		}
	}
	for (int32 Side = 0; Side < 2; ++Side)
	{
		if (!Pivots[Side].IsValid() || !Muzzles[Side].IsValid()) return false;
		LocalPivots[Side] = GetOwner()->GetActorTransform().InverseTransformPosition(Pivots[Side]->GetComponentLocation());
		BarrelLengths[Side] = FVector::Distance(Pivots[Side]->GetComponentLocation(), Muzzles[Side]->GetComponentLocation());
		TravelRotations[Side] = Pivots[Side]->GetRelativeRotation();
	}
	bLatestActive=false; LatestTarget=FVector::ZeroVector; bAppliedVisible=true; ApplyVisibleMining(false);
	if (GetWorld() && Actor->GetNetMode()!=NM_DedicatedServer)
		GetWorld()->GetTimerManager().SetTimer(VisibilityTimer,this,&ThisClass::PollVisibility,
			FGuLiPersistentEffectVisibility::CheckSeconds,true);
	return true;
}

bool UGuLiMiningPresentationComponent::CalculateMuzzles(
	const FVector& Target, const FTransform& VehiclePose, FVector& Left, FVector& Right) const
{
	if (!IsReady()) return false;
	FVector* Results[] = { &Left, &Right };
	for (int32 Side = 0; Side < 2; ++Side)
	{
		const FVector Pivot = VehiclePose.TransformPosition(LocalPivots[Side]);
		*Results[Side] = Pivot + (Target - Pivot).GetSafeNormal() * BarrelLengths[Side];
	}
	return true;
}

void UGuLiMiningPresentationComponent::AimAt(const FVector& Target)
{
	if (!IsReady()) return;
	for (const TWeakObjectPtr<USceneComponent>& Pivot : Pivots)
		Pivot->SetWorldRotation((Target - Pivot->GetComponentLocation()).Rotation());
}

void UGuLiMiningPresentationComponent::ApplyMining(const bool bActive, const FVector& Target)
{
	AActor* Actor=Presentation.Get(); if (!Actor || !IsReady()) return;
	const bool Changed=bLatestActive!=bActive;
	bLatestActive=bActive; LatestTarget=Target;
	// Authority muzzle geometry remains independent of cosmetic visibility.
	if (bActive && (Actor->GetNetMode()==NM_DedicatedServer || bAppliedVisible)) AimAt(Target);
	if (Actor->GetNetMode()==NM_DedicatedServer) return;
	if (Changed) PollVisibility();
}

void UGuLiMiningPresentationComponent::ApplyVisibleMining(const bool bShow)
{
	AActor* Actor=Presentation.Get(); if (!Actor) return;
	if (!bShow && !bAppliedVisible) return;
	if (UFunction* Function=Actor->FindFunction(bShow ? TEXT("StartMiningAt") : TEXT("StopMining")))
	{
		FStructOnScope Parameters(Function);
		if (bShow)
		{
			const FStructProperty* Parameter=FindFProperty<FStructProperty>(Function,TEXT("TargetWorldLocation"));
			if (!ensure(Parameter && Parameter->Struct==TBaseStructure<FVector>::Get())) return;
			*Parameter->ContainerPtrToValuePtr<FVector>(Parameters.GetStructMemory())=LatestTarget;
		}
		Actor->ProcessEvent(Function,Parameters.GetStructMemory());
	}
	Actor->SetActorTickEnabled(bShow);
	bAppliedVisible=bShow;
	if (!bShow)
	{
		TInlineComponentArray<UNiagaraComponent*> Beams(Actor);
		for (auto* Beam : Beams) { Beam->DeactivateImmediate(); Beam->SetVisibility(false); }
		for (int32 Side=0; Side<2; ++Side) if (Pivots[Side].IsValid()) Pivots[Side]->SetRelativeRotation(TravelRotations[Side]);
	}
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
	for (const auto& Muzzle : Muzzles) if (Muzzle.IsValid()) Bounds+=Muzzle->GetComponentLocation();
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
