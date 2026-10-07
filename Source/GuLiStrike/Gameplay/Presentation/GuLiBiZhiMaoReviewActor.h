#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Battle/Network/GuLiBattleTypes.h"
#include "Gameplay/Presentation/GuLiVATAnimation.h"
#include "GuLiBiZhiMaoReviewActor.generated.h"

class UStaticMesh;
class UInstancedStaticMeshComponent;

/** An explicit art-review ISM instance. It is never registered as a combat unit. */
UCLASS()
class GULISTRIKE_API AGuLiBiZhiMaoReviewActor : public AActor
{
	GENERATED_BODY()
public:
	AGuLiBiZhiMaoReviewActor();
	virtual void BeginPlay() override;
	virtual void Tick(float Dt) override;
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual bool ShouldTickIfViewportsOnly() const override { return true; }
	UPROPERTY(EditAnywhere, Category="Review") TObjectPtr<UStaticMesh> Mesh;
	UPROPERTY(EditAnywhere, Category="Review") TObjectPtr<UGuLiVATDefinition> Animation;
	UPROPERTY(EditInstanceOnly, Category="Review") TObjectPtr<AActor> Target;
	UPROPERTY(EditAnywhere, Category="Review") FVector LocalVelocity = FVector::ZeroVector;
	UPROPERTY(EditAnywhere, Category="Review", meta=(ClampMin="0.1")) float ModelScale = 2;
	/** Explicitly authored only in the review map; never creates the artillery directly. */
	UPROPERTY(EditAnywhere, Category="Review|Construction") bool bPrepareConstructionReview = false;
	UPROPERTY(EditAnywhere, Category="Review|Construction") EGuLiTeam ConstructionReviewTeam = EGuLiTeam::Red;
	UPROPERTY(EditAnywhere, Category="Review|Construction") TArray<FVector> BuilderGroundLocations;
	UFUNCTION(CallInEditor, Category="Review", meta=(DisplayName="待机 / S停止")) void StopPreview();
	UFUNCTION(CallInEditor, Category="Review", meta=(DisplayName="向前平移 144cm/s")) void ForwardPreview();
	UFUNCTION(CallInEditor, Category="Review", meta=(DisplayName="向后平移 144cm/s")) void BackwardPreview();
	UFUNCTION(CallInEditor, Category="Review", meta=(DisplayName="向左平移 144cm/s")) void LeftPreview();
	UFUNCTION(CallInEditor, Category="Review", meta=(DisplayName="向右平移 144cm/s")) void RightPreview();
	UFUNCTION(CallInEditor, Category="Review", meta=(DisplayName="斜移 144cm/s")) void DiagonalPreview();
	UFUNCTION(CallInEditor, Category="Review", meta=(DisplayName="重置审核样机")) void ResetPreview();
private:
	UPROPERTY(VisibleAnywhere) TObjectPtr<UInstancedStaticMeshComponent> Instances;
	FGuLiVATPlayback Playback;
	FGuLiMechanicalAnimationState Aim;
	FVector Offset = FVector::ZeroVector;
	bool bConstructionReviewPrepared = false;
	int32 BuildersPrepared = 0;
};
