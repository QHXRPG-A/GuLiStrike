#pragma once
#include "CoreMinimal.h"
#include "Gameplay/Data/Generated/GuLiStrikeMechTableRows.h"
#include "GuLiGroundMechConfiguration.generated.h"

/** Fixed-step ability state; snapshots preserve cooldown and never replay accepted input. */
USTRUCT(BlueprintType)
struct GULISTRIKE_API FGuLiGroundMechBlinkState
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly) uint8 Phase=0; // 0 ready/cooldown, 1 Begin, 2 Loop/travel, 3 End
    UPROPERTY(BlueprintReadOnly) int32 Serial=0;
    UPROPERTY() uint32 LastRequest=0;
    UPROPERTY(BlueprintReadOnly) float Elapsed=0.f;
    UPROPERTY(BlueprintReadOnly) float Cooldown=0.f;
    UPROPERTY() FVector Start=FVector::ZeroVector;
    UPROPERTY() FVector End=FVector::ZeroVector;
    UPROPERTY() FVector Direction=FVector::ForwardVector;
    UPROPERTY() float Yaw=0.f;
};

/** Immutable per-spawn copy of the Excel/DataTable contract, shared by physics and presentation. */
USTRUCT()
struct GULISTRIKE_API FGuLiGroundMechConfiguration
{
    GENERATED_BODY()
    UPROPERTY() FGuLiStrikeMechMobilityRow Mobility;
    UPROPERTY() FGuLiStrikeMechGroundIKRow IK;
    UPROPERTY() FGuLiStrikeMechCameraRow FreeCamera;
    UPROPERTY() FGuLiStrikeMechCameraRow AimCamera;
    UPROPERTY() TMap<FName,FGuLiStrikeMechGaitRow> Gaits;
    UPROPERTY() TMap<FName,TObjectPtr<UObject>> Assets;
    FString Fingerprint;
    bool bReady=false;
    bool Load(FString& Error);
    const FGuLiStrikeMechGaitRow& Gait(FName Name) const { return Gaits.FindChecked(Name); }
    template<class T> T* Asset(FName Name) const { const auto* V=Assets.Find(Name); return V?Cast<T>(V->Get()):nullptr; }
};

/** Authoritative fixed-step turn state. Montage evaluation never applies root motion to physics. */
USTRUCT(BlueprintType)
struct GULISTRIKE_API FGuLiGroundMechTurnState
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly) int32 Serial=0;
    UPROPERTY(BlueprintReadOnly) int32 Direction=0;
    UPROPERTY(BlueprintReadOnly) float Time=0.f;
    UPROPERTY(BlueprintReadOnly) float Duration=0.f;
    UPROPERTY(BlueprintReadOnly) float StartYaw=0.f;
    UPROPERTY(BlueprintReadOnly) float TargetDegrees=0.f;
    UPROPERTY(BlueprintReadOnly) float Progress=0.f;
};

/** Fixed-step return state; replicas and restored owners use the same component-space start poses. */
USTRUCT()
struct GULISTRIKE_API FGuLiGroundMechWeaponAimState
{
    GENERATED_BODY()
    UPROPERTY() bool bForward=false;
    UPROPERTY() float ReturnAlpha=1.f;
    UPROPERTY() FQuat RightStart=FQuat::Identity;
    UPROPERTY() FQuat LeftStart=FQuat::Identity;
};
