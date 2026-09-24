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
struct FPathFollowingResult;
enum class EGuLiEngineeringMoveStatus : uint8 { Idle, WaitingForPath, Moving, Arrived, Failed };
enum class EGuLiEngineeringPathPreparation : uint8 { Pending, Ready, Failed };

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
	bool bNeedsPath = false;
};

/** Shared travel entry; vehicle work remains in its owning task component. */
UCLASS()
class GULISTRIKE_API UGuLiEngineeringTravelComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UGuLiEngineeringTravelComponent();
	bool BeginMove(const FVector& Target, float AcceptanceRadius);
	bool BeginWorkMove(const FVector& Target, float AcceptanceRadius);
	double EstimateWorkDistance(const FVector& Target) const;
	EGuLiEngineeringMoveStatus GetMoveStatus() const { return GroundStatus; }
	bool IsWaitingForPath() const { return GroundStatus == EGuLiEngineeringMoveStatus::WaitingForPath; }
	bool HasQueuedPath() const;
	EGuLiEngineeringPathPreparation PrepareReplacementMove(uint32 TaskId, const FVector& Target, float Radius, FGuLiPreparedGroundMove& Out);
	void CancelReplacementMove();
	bool IsWorkJourney() const { return bWorkJourney; }
	bool WasPathUnreachable() const { return bPathUnreachable; }
	bool ProcessQueuedPath();
	double GetLastQueueWaitMilliseconds() const { return LastQueueWaitMilliseconds; }
	void CancelGroundMove();
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	bool PrepareGroundMove(const FVector& Target, float AcceptanceRadius, FGuLiPreparedGroundMove& Out) const;
	bool CommitGroundMove(const FGuLiPreparedGroundMove& Prepared);
	bool StopAtSafePoint();
	bool FindGroundPath(const FVector& Target, float& OutLength) const;
	EGuLiTransitOrderResult PrepareTransport(const FGuLiStrongholdTransitOrder& Order, FGuLiPreparedTransit& Out) const;
	void BeginTransport(const FGuLiPreparedTransit& Prepared);
	bool IsInTransit() const { return State.IsPhased(); }
	bool IsRouting() const { return State.IsRouting(); }
	UFUNCTION(BlueprintPure) FGuLiStrongholdTransitState GetTransitState() const { return State; }
	UFUNCTION(BlueprintPure, Category="Engineering|Navigation") FString GetTravelDebug() const;
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
	mutable int32 ExitCandidateCursor = 0;
	EGuLiEngineeringMoveStatus GroundStatus = EGuLiEngineeringMoveStatus::Idle;
	FVector GroundGoal = FVector::ZeroVector;
	float GroundAcceptance = 100;
	FNavPathSharedPtr ActiveGroundPath;
	FAIRequestID GroundRequestId;
	double QueuedAt = 0;
	double LastQueueWaitMilliseconds = 0;
	bool bWorkJourney = false;
	bool bPathUnreachable = false;
	bool bGroundRepath = false;
	FVector WorkGoal = FVector::ZeroVector;
	float WorkAcceptance = 100;
	struct FTransitLeg { int32 Source; int32 Target; FVector Entry; };
	TArray<FTransitLeg> WorkLegs;
	mutable uint32 RouteCacheRevision = 0;
	mutable FVector RouteCacheStart = FVector::ZeroVector;
	mutable TArray<double> RouteCosts;
	mutable TArray<int32> RouteParents;
	mutable TArray<int32> RouteSources;
	mutable EGuLiTeam RouteCacheTeam = EGuLiTeam::Unassigned;
	struct FFailedPath { FVector Start; FVector Goal; double RetryAt; uint32 Revision; };
	TArray<FFailedPath> FailedPaths;
	uint32 FailureProfile = 0;
	uint32 ReplacementTaskId = 0;
	FVector ReplacementGoal = FVector::ZeroVector;
	float ReplacementRadius = 100;
	double ReplacementQueuedAt = 0;
	EGuLiEngineeringPathPreparation ReplacementStatus = EGuLiEngineeringPathPreparation::Pending;
	FGuLiPreparedGroundMove ReplacementPath;
	bool ComputeQueuedPath(const FVector& Target, float Radius, double RequestTime, FGuLiPreparedGroundMove& Out, bool& bSuccess);
	double PlanWorkRoute(const FVector& Target, TArray<FTransitLeg>* Out) const;
	void AdvanceWorkRoute();
	void TickGroundMove();
	void OnGroundMoveFinished(FAIRequestID RequestId, const FPathFollowingResult& Result);
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
