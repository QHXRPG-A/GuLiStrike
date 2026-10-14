#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Commander/Network/GuLiCommanderTypes.h"
#include "GuLiConstructedUnitComponent.generated.h"

class UGuLiBuildingLifecycleComponent;
class ANavigationData;
struct FGuLiHybridObjectState;

/** One construction-to-Mass handover; ordinary placed buildings keep this dormant. */
UCLASS()
class GULISTRIKE_API UGuLiConstructedUnitComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UGuLiConstructedUnitComponent();
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

	/** Called immediately before the new Mass visual is submitted, independent of object-state arrival order. */
	void RetireVisualForMass(FGuLiSoldierId Id);
	void AdvanceHybrid(uint64 Tick);
	bool CaptureHybrid(TArray<uint8>& Bytes) const;
	bool RestoreHybrid(TConstArrayView<uint8> Bytes,bool Apply);
	void ActivateHybridRestore();
	bool ApplyHybridLedger(const FGuLiHybridObjectState& State);
	UFUNCTION(BlueprintPure, Category="Construction") FGuLiSoldierId GetConvertedSoldierId() const { return ConvertedSoldierId; }
private:

	void BeginConversion();
	void RefreshHandoverVisual();
	UPROPERTY() FGuLiSoldierId ConvertedSoldierId;

	UFUNCTION() void HandleNavigationRebuilt(ANavigationData* Data);
	TWeakObjectPtr<UGuLiBuildingLifecycleComponent> Lifecycle;
	bool bConversionStarted = false;
	bool bNavigationRebuilt = false;
	bool bVisualRetired = false;
	uint64 EarliestConversionTick=0;
	FGuid HybridConversionEvent;
};
