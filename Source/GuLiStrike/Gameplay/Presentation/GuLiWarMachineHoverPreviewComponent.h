#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Gameplay/Presentation/GuLiMechanicalAnimation.h"
#include "GuLiWarMachineHoverPreviewComponent.generated.h"

class UInstancedStaticMeshComponent;
class UGuLiWarMachineHoverComponent;

/** Attached only to the explicitly tagged isolated review actor in the Mass test map. */
UCLASS()
class GULISTRIKE_API UGuLiWarMachineHoverPreviewComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UGuLiWarMachineHoverPreviewComponent();
	virtual void BeginPlay() override;
	virtual void TickComponent(float Dt, ELevelTick TickType, FActorComponentTickFunction* Function) override;
private:
	UPROPERTY(Transient) TObjectPtr<UInstancedStaticMeshComponent> Models;
	UPROPERTY(Transient) TObjectPtr<UGuLiWarMachineHoverComponent> Effects;
	struct FSample
	{
		FTransform Origin, Previous;
		FGuLiMechanicalAnimationState State;
		FGuLiMechanicalAnimationFrame Frame;
		float Travel = 0, LastShot = -100;
		int32 LastStage = -1, ShotSide = 0;
	};
	TArray<FSample> Samples;
	FGuLiMechanicalAnimationConfig Config;
	double PreviewSeconds = 0;
};
