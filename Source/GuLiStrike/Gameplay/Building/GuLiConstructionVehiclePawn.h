#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Gameplay/Units/GuLiEngineeringTravelComponent.h"
#include "GuLiConstructionVehiclePawn.generated.h"
class UChildActorComponent;
class UGuLiConstructionWorkComponent;
class UGuLiConstructionPresentationComponent;
class UGuLiEngineeringPoseComponent;
class UGuLiBuildingLifecycleComponent;
struct FGuLiSoldierDefinition;

UCLASS(NotPlaceable)
class GULISTRIKE_API AGuLiConstructionVehiclePawn : public ACharacter, public IGuLiEngineeringVehicle
{
	GENERATED_BODY()
	friend class FGuLiHybridEngineerAdapter;
public:
	AGuLiConstructionVehiclePawn(const FObjectInitializer& Initializer = FObjectInitializer::Get());
	void InitializeVehicle(EGuLiTeam InTeam, FGuLiControllableActorId InId, const FGuLiSoldierDefinition& Definition);
	UFUNCTION(BlueprintCallable,BlueprintAuthorityOnly) bool IssueMove(const FVector& Target);
	UFUNCTION(BlueprintCallable,BlueprintAuthorityOnly) bool IssueConstruction(UGuLiBuildingLifecycleComponent* Building, bool bAutomatic = false);
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly)
	virtual EGuLiTransitOrderResult IssueStrongholdTransit(const FGuLiStrongholdTransitOrder& Order, EGuLiTeam RequestingTeam) override;
	UFUNCTION(BlueprintPure, Category = "Construction")
	virtual EGuLiTeam GetTeam() const override { return Team; }
	virtual int32 GetUnitTypeId() const override { return UnitTypeId; }
	virtual FGuLiControllableActorId GetStableActorId() const override { return StableId; }
	virtual float GetEngineeringBaseSpeed() const override { return BaseSpeed; }
	virtual void SetEngineeringPresentationVisible(bool bVisible) override;
	virtual FBox GetEngineeringTravelBounds() const override { return TravelBounds; }

private:
	FBox TravelBounds = FBox(ForceInit);
	uint32 LastTransitRequestId = 0;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UChildActorComponent> Presentation;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UGuLiEngineeringPoseComponent> MovementPresentation;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UGuLiEngineeringTravelComponent> Travel;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UGuLiConstructionWorkComponent> Work;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UGuLiConstructionPresentationComponent> ConstructionPresentation;
	UPROPERTY() EGuLiTeam Team = EGuLiTeam::Unassigned;
	UPROPERTY() FGuLiControllableActorId StableId;
	UPROPERTY() TSubclassOf<AActor> PresentationClass;
	UPROPERTY() float PresentationScale = 1;
	UPROPERTY() float BaseSpeed = 900;
	UPROPERTY() int32 UnitTypeId = 0;
	UFUNCTION() void OnRep_Definition();
};
