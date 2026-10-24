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
#if !UE_BUILD_SHIPPING
    bool PrepareDevelopmentProductionCycle();
#endif
	bool CaptureHybridProduction(FGuLiHybridProductionPrivateState& Out) const;
	bool ValidateHybridProduction(const FGuLiHybridProductionPrivateState& State) const;
	bool RestoreHybridProduction(const FGuLiHybridProductionPrivateState& State);
    void RecoverGridBirths();
    void ResetResourceRequests(uint64 Epoch);
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
    int32 GridRemaining=0,GridCandidate=0,GridJobCursor=0;
    double GridRetryAt=0;
    void AdvanceGridProduction(float DeltaSeconds);
    void ReleaseGridBirth(const FGuLiHybridObjectKey& Target,bool bWithdraw=true);
	UFUNCTION() void HandleHybridTransaction(const FGuLiHybridTransactionResult& Result);
public:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
};
