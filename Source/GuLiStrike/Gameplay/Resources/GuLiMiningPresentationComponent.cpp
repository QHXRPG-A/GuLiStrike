#include "Gameplay/Resources/GuLiMiningPresentationComponent.h"

#include "Components/PrimitiveComponent.h"
#include "GameFramework/Actor.h"
#include "UObject/StructOnScope.h"
#include "UObject/UnrealType.h"

bool UGuLiMiningPresentationComponent::InitializePresentation(AActor* Actor)
{
	Presentation = Actor;
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
	ApplyMining(false, FVector::ZeroVector);
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
	AActor* Actor = Presentation.Get();
	if (!Actor || !IsReady()) return;
	if (bActive) AimAt(Target);
	// Dedicated servers still compute the same muzzle geometry, but never activate Niagara.
	const bool bShow = bActive && Actor->GetNetMode() != NM_DedicatedServer;
	if (UFunction* Function = Actor->FindFunction(bShow ? TEXT("StartMiningAt") : TEXT("StopMining")))
	{
		FStructOnScope Parameters(Function);
		if (bShow)
		{
			const FStructProperty* Parameter = FindFProperty<FStructProperty>(Function, TEXT("TargetWorldLocation"));
			if (!ensure(Parameter && Parameter->Struct == TBaseStructure<FVector>::Get())) return;
			*Parameter->ContainerPtrToValuePtr<FVector>(Parameters.GetStructMemory()) = Target;
		}
		Actor->ProcessEvent(Function, Parameters.GetStructMemory());
	}
	if (!bActive)
		for (int32 Side = 0; Side < 2; ++Side) Pivots[Side]->SetRelativeRotation(TravelRotations[Side]);
}
