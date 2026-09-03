// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Algo/AllOf.h"
#include "CoreMinimal.h"
#include "Development/GuLiWingmanQAEvidence.h"

/** Result of one synchronous, isolated production-API probe for a formal QA role. */
struct GULISTRIKE_API FGuLiWingmanQARoleProbeResult
{
	TSet<FName> PassedGateIds;
	TArray<FGuLiWingmanQAEvent> Events;
	FString Error;
	bool bRecognizedRole = false;

	bool PassedAll(const TArray<FName>& RequiredGateIds) const
	{
		return bRecognizedRole && Error.IsEmpty()
			&& Algo::AllOf(RequiredGateIds, [this](const FName Gate)
			{
				return PassedGateIds.Contains(Gate);
			});
	}
};

/**
 * Deterministic role probes exercise the same Relay, presentation, projectile and
 * Damage Ledger entry points used by production. They never mutate the campaign
 * World and never manufacture movement on a Dedicated Server.
 */
namespace GuLiWingmanQARoleProbes
{
	GULISTRIKE_API bool Supports(FName RoleId);
	/** Presentation probes need a client process; every other probe is authoritative. */
	GULISTRIKE_API bool RequiresClientExecutionDomain(FName RoleId);
	GULISTRIKE_API FGuLiWingmanQARoleProbeResult Run(FName RoleId);
}
