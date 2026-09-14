#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "UObject/Interface.h"
#include "Gameplay/Resources/GuLiResourceTypes.h"
#include "Gameplay/Stronghold/GuLiStrongholdTransitTypes.h"
#include "GuLiEngineeringTravelComponent.generated.h"
class UGuLiExternalUnitControlComponent;
class UGuLiStrongholdTransitPresentationComponent;
class UGuLiResourceWorldSubsystem;
class AAIController;

UINTERFACE(MinimalAPI)
class UGuLiEngineeringVehicle : public UInterface { GENERATED_BODY() };
class GULISTRIKE_API IGuLiEngineeringVehicle
{
	GENERATED_BODY()
public:
	virtual EGuLiTeam GetTeam() const = 0;
	virtual FGuLiControllableActorId GetStableActorId() const = 0;
	virtual float GetEngineeringBaseSpeed() const = 0;
	virtual void SetEngineeringPresentationVisible(bool bVisible) = 0;
	virtual FBox GetEngineeringTravelBounds() const = 0;
};
DECLARE_MULTICAST_DELEGATE(FGuLiEngineeringTransportEvent);

/** Shared travel entry; vehicle work remains in its owning task component. */
UCLASS()
class GULISTRIKE_API UGuLiEngineeringTravelComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UGuLiEngineeringTravelComponent();
	bool BeginMove(const FVector& Target, float AcceptanceRadius);
	void CancelApproach();
	bool IsInTransit() const { return State.IsPhased(); }
	bool IsRouting() const { return State.IsRouting(); }
	UFUNCTION(BlueprintPure) FGuLiStrongholdTransitState GetTransitState() const { return State; }
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	FGuLiEngineeringTransportEvent OnTransportStarted;
	FGuLiEngineeringTransportEvent OnTransportEnded;
private:
	UPROPERTY(ReplicatedUsing=OnRep_State) FGuLiStrongholdTransitState State;
	UPROPERTY(Transient) TObjectPtr<UGuLiExternalUnitControlComponent> Control;
	UPROPERTY(Transient) TObjectPtr<UGuLiStrongholdTransitPresentationComponent> Presentation;
	TArray<int32> PassedNodes;
	FVector GateEntry = FVector::ZeroVector;
	float GroundAcceptance = 500;
	int32 SourceTerritory = INDEX_NONE;
	float ExitQueryAccumulator = 0;
	UGuLiResourceWorldSubsystem& Resources() const;
	IGuLiEngineeringVehicle& Vehicle() const;
	AAIController& Controller() const;
	bool MoveOnGround(const FVector& Target, float AcceptanceRadius);
	void EnterGate(const TArray<int32>& Route);
	void ResolveDisruption(double Now);
	void StartAirRoute(const TArray<int32>& Route, const FVector& From, double Now, float Ascent, float InitialSpeed);
	bool FindExit(FTransform& Transform) const;
	void ExitTransit(const FTransform& Transform, double Now);
	void PublishPhase(EGuLiTransitPhase Phase);
	UFUNCTION() void OnRep_State();
};
