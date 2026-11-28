#pragma once

#include "Battle/Combat/GuLiCombatDamageLedger.h"
#include "Gameplay/CombatEffects/GuLiHealthBarEvent.h"

/** Display history for a single life. The first eligible snapshot establishes its baseline. */
struct FGuLiUnitVitalHistory
{
	FGuLiCombatHealthState Health;
	EGuLiTeam Team = EGuLiTeam::Unassigned;
	double LastChange = -1;
	double LastDamage = -1;
	bool bInitialized = false;

	bool Observe(const FGuLiCombatHealthState& Next, EGuLiTeam NextTeam, bool bEligible, double Now)
	{
		const bool bSameLife = bInitialized && bEligible && Team == NextTeam;
		const bool bChanged = bSameLife && (Health.Health != Next.Health || Health.MaxHealth != Next.MaxHealth
			|| Health.Shield != Next.Shield || Health.MaxShield != Next.MaxShield);
		if (!bSameLife) LastChange = LastDamage = -1;
		else if (bChanged)
		{
			LastChange = Now;
			if (FGuLiHealthBarEvent::LostVitals(Health.Health, Health.Shield, Next.Health, Next.MaxHealth, Next.Shield, Next.MaxShield))
				LastDamage = Now;
		}
		Health = Next;
		Team = NextTeam;
		bInitialized = bEligible;
		return bChanged;
	}
};
