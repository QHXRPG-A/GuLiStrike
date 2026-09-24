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
	void AdvanceCapture(int32 Red, int32 Blue, float Seconds, float NeutralCaptureSeconds);
	/** Authority fact only; progression is deliberately not a dependency. */
	FGuLiCaptureRewardRequested OnCaptureRewardRequested;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	UFUNCTION(BlueprintPure, Category="Stronghold") float GetCaptureProgress() const { return Progress; }
	UFUNCTION(BlueprintPure, Category="Stronghold") int32 GetRedCount() const { return RedCount; }
	UFUNCTION(BlueprintPure, Category="Stronghold") int32 GetBlueCount() const { return BlueCount; }
private:
	UPROPERTY(Replicated) float Progress = 0;
	UPROPERTY(Replicated) int32 RedCount = 0;
	UPROPERTY(Replicated) int32 BlueCount = 0;
	int32 TerritoryIndex = INDEX_NONE;
};
