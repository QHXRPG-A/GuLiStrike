// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Battle/Network/GuLiBattleTypes.h"
#include "GuLiEconomyTypes.generated.h"

/** A fungible resource understood by the match economy. */
UENUM(BlueprintType)
enum class EGuLiResourceType : uint8
{
	Blue = 0,
	Red
};

/** Replication-friendly balance value shared by producers, consumers and owner-only UI state. */
USTRUCT(BlueprintType)
struct GULISTRIKE_API FGuLiResourceAmounts
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Economy")
	int32 Blue = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Economy")
	int32 Red = 0;

	UPROPERTY(VisibleAnywhere, Category = "Economy")
	uint32 Revision = 0u;

	int32 Get(EGuLiResourceType Type) const;
	bool CanAfford(EGuLiResourceType Type, int32 Amount) const;
	bool CanAfford(const FGuLiResourceAmounts& Cost) const;
	bool Add(EGuLiResourceType Type, int32 Amount);
	bool Remove(EGuLiResourceType Type, int32 Amount);
	void AddChecked(const FGuLiResourceAmounts& Amounts);
	void RemoveChecked(const FGuLiResourceAmounts& Cost);
	bool HasPositiveAmount() const { return Blue > 0 || Red > 0; }
	void Sanitize();
};

namespace GuLiEconomy
{
	inline bool IsPlayableTeam(const EGuLiTeam Team)
	{
		return Team == EGuLiTeam::Red || Team == EGuLiTeam::Blue;
	}
}
