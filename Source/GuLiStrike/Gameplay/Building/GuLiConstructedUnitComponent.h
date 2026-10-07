#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Commander/Network/GuLiCommanderTypes.h"
#include "GuLiConstructedUnitComponent.generated.h"

class UGuLiBuildingLifecycleComponent;
class ANavigationData;

/** One construction-to-Mass handover; ordinary placed buildings keep this dormant. */
UCLASS()
class GULISTRIKE_API UGuLiConstructedUnitComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UGuLiConstructedUnitComponent();
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void TickComponent(float Dt, ELevelTick Type, FActorComponentTickFunction* Function) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
	void SetPopulationReservation(const FGuid& Token);
	bool OwnsPopulationReservation(const FGuid& Token) const { return PopulationReservation.IsValid() && PopulationReservation == Token; }
	/** Called immediately before the new Mass visual is submitted, independent of actor replication order. */
	void RetireVisualForMass(FGuLiSoldierId Id);
	UFUNCTION(BlueprintPure, Category="Construction") FGuLiSoldierId GetConvertedSoldierId() const { return ConvertedSoldierId; }
private:
	void OnConstructionChanged();
	void BeginConversion();
	void RefreshHandoverVisual();
	UPROPERTY(ReplicatedUsing=OnRep_ConvertedSoldier) FGuLiSoldierId ConvertedSoldierId;
	UFUNCTION() void OnRep_ConvertedSoldier();
	UFUNCTION() void HandleNavigationRebuilt(ANavigationData* Data);
	TWeakObjectPtr<UGuLiBuildingLifecycleComponent> Lifecycle;
	FGuid PopulationReservation;
	FDelegateHandle ConstructionChanged;
	bool bConversionStarted = false;
	bool bNavigationRebuilt = false;
	bool bVisualRetired = false;
	double EarliestConversionTime = 0;
};
