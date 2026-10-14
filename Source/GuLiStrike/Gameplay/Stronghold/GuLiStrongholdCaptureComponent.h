#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Gameplay/Resources/GuLiResourceTypes.h"
#include "GuLiStrongholdCaptureComponent.generated.h"
DECLARE_MULTICAST_DELEGATE_TwoParams(FGuLiCaptureRewardRequested, int32, EGuLiTeam);

/** Ground dominance is sampled once by the world; this component owns only capture progress. */
UCLASS()
class GULISTRIKE_API UGuLiStrongholdCaptureComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UGuLiStrongholdCaptureComponent();
	void InitializeCapture(int32 InTerritoryIndex, EGuLiTeam Owner);
	void SetOwnerEndpoint(EGuLiTeam Owner);

    void ApplyHybridCapture(float Value,int32 Red,int32 Blue)
    {Progress=Value;RedCount=Red;BlueCount=Blue;}
	/** Authority fact only; progression is deliberately not a dependency. */
	FGuLiCaptureRewardRequested OnCaptureRewardRequested;

	UFUNCTION(BlueprintPure, Category="Stronghold") float GetCaptureProgress() const { return Progress; }
	UFUNCTION(BlueprintPure, Category="Stronghold") int32 GetRedCount() const { return RedCount; }
	UFUNCTION(BlueprintPure, Category="Stronghold") int32 GetBlueCount() const { return BlueCount; }
private:
	UPROPERTY() float Progress = 0;
	UPROPERTY() int32 RedCount = 0;
	UPROPERTY() int32 BlueCount = 0;
	int32 TerritoryIndex = INDEX_NONE;
};
