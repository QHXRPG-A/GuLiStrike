#pragma once
#include "CoreMinimal.h"
#include "GuLiStrongholdTransitTypes.generated.h"

UENUM(BlueprintType)
enum class EGuLiTransitPhase : uint8
{
	Ground, ApproachingGate, Ascending, Accelerating, Cruising, Decelerating, WaitingForExit, ExitFlash
};

USTRUCT()
struct FGuLiTransitTiming
{
	GENERATED_BODY()
	UPROPERTY() float InitialSpeed = 0;
	UPROPERTY() float PeakSpeed = 0;
	UPROPERTY() float Acceleration = 0;
	UPROPERTY() float Cruise = 0;
	UPROPERTY() float Deceleration = 0;
	UPROPERTY() double Length = 0;
	void Initialize(double InLength, float InPeakSpeed, float AccelerationSeconds, float DecelerationSeconds, float InInitialSpeed = 0);
	double Duration() const { return Acceleration + Cruise + Deceleration; }
	double DistanceAt(double Seconds) const;
	float SpeedAt(double Seconds) const;
};

USTRUCT()
struct FGuLiTransitRoutePoint
{
	GENERATED_BODY()
	UPROPERTY() int32 TerritoryIndex = INDEX_NONE;
	UPROPERTY() FVector_NetQuantize10 Position = FVector::ZeroVector;
};

USTRUCT(BlueprintType)
struct FGuLiStrongholdTransitState
{
	GENERATED_BODY()
	UPROPERTY() FGuid JourneyId;
	UPROPERTY(BlueprintReadOnly) EGuLiTransitPhase Phase = EGuLiTransitPhase::Ground;
	UPROPERTY(BlueprintReadOnly) int32 GateFieldId = 0;
	UPROPERTY(BlueprintReadOnly) int32 DestinationTerritory = INDEX_NONE;
	UPROPERTY(BlueprintReadOnly) bool bEmergencyExit = false;
	UPROPERTY() TArray<FGuLiTransitRoutePoint> Route;
	UPROPERTY() FGuLiTransitTiming Timing;
	UPROPERTY() double StartServerTime = 0;
	UPROPERTY() double ExitServerTime = 0;
	UPROPERTY() float AscentSeconds = 0;
	UPROPERTY() float FlashSeconds = .2f;
	UPROPERTY() FVector_NetQuantize10 EntryPosition = FVector::ZeroVector;
	UPROPERTY() FVector_NetQuantize10 FinalGroundTarget = FVector::ZeroVector;
	UPROPERTY() FVector_NetQuantize10 ExitCenter = FVector::ZeroVector;
	UPROPERTY() FVector_NetQuantize10 ExitPosition = FVector::ZeroVector;
	bool IsPhased() const { return Phase >= EGuLiTransitPhase::Ascending && Phase <= EGuLiTransitPhase::WaitingForExit; }
	bool IsRouting() const { return Phase >= EGuLiTransitPhase::ApproachingGate && Phase <= EGuLiTransitPhase::WaitingForExit; }
	FVector SamplePosition(double ServerTime, int32* OutLeg = nullptr) const;
};
