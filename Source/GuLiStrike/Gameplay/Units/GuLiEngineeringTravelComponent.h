#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "AITypes.h"
#include "NavigationPath.h"
#include "UObject/Interface.h"
#include "Gameplay/Units/GuLiEngineeringCommandTypes.h"
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
	virtual int32 GetUnitTypeId() const = 0;
	virtual FGuLiControllableActorId GetStableActorId() const = 0;
	virtual float GetEngineeringBaseSpeed() const = 0;
	virtual void SetEngineeringPresentationVisible(bool bVisible) = 0;
	virtual FBox GetEngineeringTravelBounds() const = 0;
	virtual EGuLiTransitOrderResult IssueStrongholdTransit(const FGuLiStrongholdTransitOrder& Order, EGuLiTeam RequestingTeam) = 0;
};
DECLARE_MULTICAST_DELEGATE(FGuLiEngineeringTransportEvent);

/** Prepared synchronously at the command boundary, committed by the vehicle task owner. */
struct FGuLiPreparedTransit
{
	TArray<int32> Route;
	int32 FieldId = 0;
	FVector ClickLocation = FVector::ZeroVector;
};

/** A complete path prepared without disturbing the currently executing move. */
struct FGuLiPreparedGroundMove
{
	FAIMoveRequest Request;
	FNavPathSharedPtr Path;
	bool bAlreadyAtGoal = false;
};

/** Shared travel entry; vehicle work remains in its owning task component. */
UCLASS()
class GULISTRIKE_API UGuLiEngineeringTravelComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UGuLiEngineeringTravelComponent();
	bool BeginMove(const FVector& Target, float AcceptanceRadius);
	bool PrepareGroundMove(const FVector& Target, float AcceptanceRadius, FGuLiPreparedGroundMove& Out) const;
	bool CommitGroundMove(const FGuLiPreparedGroundMove& Prepared);
	bool StopAtSafePoint();
	bool FindGroundPath(const FVector& Target, float& OutLength) const;
	EGuLiTransitOrderResult PrepareTransport(const FGuLiStrongholdTransitOrder& Order, FGuLiPreparedTransit& Out) const;
	void BeginTransport(const FGuLiPreparedTransit& Prepared);
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
	float ExitQueryAccumulator = 0;
	UGuLiResourceWorldSubsystem& Resources() const;
	IGuLiEngineeringVehicle& Vehicle() const;
	AAIController& Controller() const;
	bool MoveOnGround(const FVector& Target, float AcceptanceRadius);
	void ResolveDisruption(double Now);
	void StartAirRoute(const TArray<int32>& Route, const FVector& From, double Now, float Ascent, float InitialSpeed);
	bool FindExit(FTransform& Transform) const;
	void ExitTransit(const FTransform& Transform, double Now);
	void PublishPhase(EGuLiTransitPhase Phase);
	UFUNCTION() void OnRep_State();
};
