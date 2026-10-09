#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GuLiModelPresentationComponent.generated.h"

/** One per assembled actor, never one per Mass instance. Registered paths are relative to the actor. */
UCLASS(ClassGroup=(GuLi), meta=(BlueprintSpawnableComponent))
class GULISTRIKE_API UGuLiModelPresentationComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UGuLiModelPresentationComponent();
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Models") int32 ModelId = 0;
	UFUNCTION(BlueprintCallable, Category="Models") bool SetModelId(int32 NewModelId);
	UFUNCTION(BlueprintCallable, Category="Models") bool RefreshModel();
	/** Existing child actors resolve Parts from their registered presentation class. */
	static void InstallForPresentationActor(AActor* Actor);
protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(EEndPlayReason::Type Reason) override;
};
