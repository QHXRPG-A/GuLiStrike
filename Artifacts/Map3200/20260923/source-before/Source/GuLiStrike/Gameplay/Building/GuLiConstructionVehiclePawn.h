#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Gameplay/Units/GuLiEngineeringTravelComponent.h"
#include "GuLiConstructionVehiclePawn.generated.h"
class UChildActorComponent;
class UGuLiConstructionWorkComponent;
class UGuLiBuildingLifecycleComponent;
struct FGuLiSoldierDefinition;

UCLASS(NotPlaceable)
class GULISTRIKE_API AGuLiConstructionVehiclePawn : public ACharacter, public IGuLiEngineeringVehicle
{
	GENERATED_BODY()
public:
	AGuLiConstructionVehiclePawn(const FObjectInitializer& Initializer = FObjectInitializer::Get());
	void InitializeVehicle(EGuLiTeam InTeam, FGuLiControllableActorId InId, const FGuLiSoldierDefinition& Definition);
	UFUNCTION(BlueprintCallable,BlueprintAuthorityOnly) bool IssueMove(const FVector& Target);
	UFUNCTION(BlueprintCallable,BlueprintAuthorityOnly) bool IssueConstruction(UGuLiBuildingLifecycleComponent* Building);
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly)
	virtual EGuLiTransitOrderResult IssueStrongholdTransit(const FGuLiStrongholdTransitOrder& Order, EGuLiTeam RequestingTeam) override;
	UFUNCTION(BlueprintPure, Category = "Construction")
	virtual EGuLiTeam GetTeam() const override { return Team; }
	virtual int32 GetUnitTypeId() const override { return UnitTypeId; }
	virtual FGuLiControllableActorId GetStableActorId() const override { return StableId; }
	virtual float GetEngineeringBaseSpeed() const override { return BaseSpeed; }
	virtual void SetEngineeringPresentationVisible(bool bVisible) override;
	virtual FBox GetEngineeringTravelBounds() const override { return TravelBounds; }
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
private:
	FBox TravelBounds = FBox(ForceInit);
	uint32 LastTransitRequestId = 0;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UChildActorComponent> Presentation;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UGuLiEngineeringTravelComponent> Travel;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UGuLiConstructionWorkComponent> Work;
	UPROPERTY(Replicated) EGuLiTeam Team = EGuLiTeam::Unassigned;
	UPROPERTY(Replicated) FGuLiControllableActorId StableId;
	UPROPERTY(ReplicatedUsing=OnRep_Definition) TSubclassOf<AActor> PresentationClass;
	UPROPERTY(ReplicatedUsing=OnRep_Definition) float PresentationScale = 1;
	UPROPERTY(Replicated) float BaseSpeed = 900;
	UPROPERTY(Replicated) int32 UnitTypeId = 0;
	UFUNCTION() void OnRep_Definition();
};
