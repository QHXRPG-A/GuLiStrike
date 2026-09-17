// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Commander/Network/GuLiCommanderTypes.h"

/** Local presentation data only; control cohorts and the wire contract remain unchanged. */
struct GULISTRIKE_API FGuLiCommanderUnitTypeSummary
{
	int32 SelectedCount = 0;
	int32 AliveCount = 0;
	uint16 UnitTypeId = 0;
	bool bMixedUnitTypes = false;
	float TotalHealth = 0.0f;
	float TotalMaxHealth = 0.0f;
	bool bSyncing = false;
	FText CommandStatus;

	bool IsVisible() const { return SelectedCount > 0 && (bSyncing || AliveCount > 0); }
	float GetHealthFraction() const;
};

/** Deduplicates selection membership, then visits reliable soldier states once. */
// 本地聚合选择、ACK 和名册为 UI 摘要；bReliableStateReady 为 false 时返回同步中，不回写网络状态。
GULISTRIKE_API FGuLiCommanderUnitTypeSummary BuildGuLiCommanderUnitTypeSummary(
	const FGuLiCommanderSelectionState& Selection,
	const FGuLiCommandAck& Ack,
	TConstArrayView<FGuLiSoldierStateItem> SoldierStates,
	bool bReliableStateReady);
