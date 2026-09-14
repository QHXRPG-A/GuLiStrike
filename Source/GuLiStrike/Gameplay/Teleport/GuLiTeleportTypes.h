#pragma once

#include "CoreMinimal.h"
#include "Battle/Network/GuLiBattleTypes.h"
#include "GuLiTeleportTypes.generated.h"

UENUM(BlueprintType)
enum class EGuLiTeleportPhase : uint8 { Idle, Windup, AwaitingDestination, Recovery, Returning, Finished };

USTRUCT(BlueprintType)
struct GULISTRIKE_API FGuLiTeleportFieldConfig
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly) int32 Level = 1;
	UPROPERTY(BlueprintReadOnly) float RadiusCentimeters = 0;
	UPROPERTY(BlueprintReadOnly) bool bAllowPlayerVehicles = false;
	UPROPERTY(BlueprintReadOnly) float WindupSeconds = 3;
	UPROPERTY(BlueprintReadOnly) float RecoverySeconds = .5f;
	UPROPERTY(BlueprintReadOnly) float BeamHeightCentimeters = 50000;
	UPROPERTY(BlueprintReadOnly) float MaxTargetWaitSeconds = 10;
	UPROPERTY(BlueprintReadOnly) float MaxShipHeightCentimeters = 10000;
	bool IsValid() const;
};

/** Full semantic state is reliable; visuals never decide pickup or release. */
USTRUCT(BlueprintType)
struct GULISTRIKE_API FGuLiTeleportCastState
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly) FGuid CastId;
	UPROPERTY(BlueprintReadOnly) FGuid CommanderId;
	UPROPERTY(BlueprintReadOnly) int32 MatchEpoch = 0;
	UPROPERTY(BlueprintReadOnly) EGuLiTeam Team = EGuLiTeam::Unassigned;
	UPROPERTY(BlueprintReadOnly) FGuLiTeleportFieldConfig Config;
	UPROPERTY(BlueprintReadOnly) EGuLiTeleportPhase Phase = EGuLiTeleportPhase::Idle;
	UPROPERTY(BlueprintReadOnly) FVector Source = FVector::ZeroVector;
	UPROPERTY(BlueprintReadOnly) FVector Destination = FVector::ZeroVector;
	UPROPERTY(BlueprintReadOnly) double PhaseStartTime = 0;
	UPROPERTY(BlueprintReadOnly) double Deadline = 0;
	UPROPERTY(BlueprintReadOnly) int32 Revision = 0;
	UPROPERTY(BlueprintReadOnly) int32 ParticipantCount = 0;
	UPROPERTY(BlueprintReadOnly) bool bLanded = false;
	UPROPERTY(BlueprintReadOnly) float FinalProgress = 1;
	UPROPERTY(BlueprintReadOnly) FString Message;
	bool IsActive() const { return Phase != EGuLiTeleportPhase::Idle && Phase != EGuLiTeleportPhase::Finished; }
};

namespace GuLiTeleport
{
	GULISTRIKE_API bool IsInsideDisc(const FVector& Location, const FVector& Center, float Radius);
	GULISTRIKE_API bool CanCollectVehicle(const FGuLiTeleportFieldConfig& Config, bool bShip, double Height);
	GULISTRIKE_API float WindupProgress(const FGuLiTeleportCastState& State, double ServerTime);
}
