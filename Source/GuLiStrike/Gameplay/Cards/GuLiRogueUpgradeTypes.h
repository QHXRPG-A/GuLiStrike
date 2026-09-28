#pragma once
#include "CoreMinimal.h"
#include "Commander/Network/GuLiCommanderTypes.h"
#include "GuLiRogueUpgradeTypes.generated.h"
class UNiagaraSystem;

/** Authority-authored, short-lived cosmetic batch. Never carries gameplay attribute changes. */
USTRUCT()
struct FGuLiRogueUpgradeCue
{
	GENERATED_BODY()
	UPROPERTY() FGuid Session;
	UPROPERTY() uint32 MatchEpoch=0;
	UPROPERTY() uint16 BatchIndex=0;
	UPROPERTY() EGuLiTeam Team=EGuLiTeam::Unassigned;
	UPROPERTY() uint16 UnitTypeId=0;
	UPROPERTY() float StartTime=0.f;
	UPROPERTY() TSoftObjectPtr<UNiagaraSystem> System;
	UPROPERTY() float Scale=1.f;
	UPROPERTY() FLinearColor Color=FLinearColor(20.f,6.930114f,.933554f,1.f);
	UPROPERTY() TArray<FGuLiSoldierId> Soldiers;
};

namespace GuLiRogueUpgrade
{
	constexpr int32 BlockSize=1024;
	constexpr int32 NetworkBatchSize=128;
	constexpr float Duration=1.f;
	/** Same syntax as the Excel value; RGB is linear HDR, alpha is normalized. */
	GULISTRIKE_API bool ParseColor(const FString& Text, FLinearColor& OutColor);
}
