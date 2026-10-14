#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Gameplay/Resources/GuLiResourceTypes.h"
#include "Network/GuLiHybridWorldSubsystem.h"
#include "GuLiBuildingProductionComponent.generated.h"

struct FGuLiHybridProductionPrivateState;

UCLASS()
class GULISTRIKE_API UGuLiBuildingProductionComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UGuLiBuildingProductionComponent();
	void InitializeProduction();
	void AdvanceHybridProduction(float DeltaSeconds);
	bool CaptureHybridProduction(FGuLiHybridProductionPrivateState& Out) const;
	bool ValidateHybridProduction(const FGuLiHybridProductionPrivateState& State) const;
	bool RestoreHybridProduction(const FGuLiHybridProductionPrivateState& State);
	bool ValidateHybridProductionMotion(float Cycle,bool Producing) const;
	bool ApplyHybridProductionMotion(float Cycle,bool Producing);
	UFUNCTION(BlueprintPure, Category="Building") float GetCycleSeconds() const { return CycleSeconds; }
	UFUNCTION(BlueprintPure, Category="Building") bool IsProducing() const { return bProducing; }
private:
	UPROPERTY() float CycleSeconds = 0;
	UPROPERTY() bool bProducing = false;
	EGuLiTeam ObservedTeam = EGuLiTeam::Unassigned;
	struct FHybridJob { FGuid Event; FGuLiHybridObjectKey Target; FVector Location; float FacingYaw=0; bool bSubmitted=false; };
	TArray<FHybridJob> HybridJobs;
	UFUNCTION() void HandleHybridTransaction(const FGuLiHybridTransactionResult& Result);
public:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
};
