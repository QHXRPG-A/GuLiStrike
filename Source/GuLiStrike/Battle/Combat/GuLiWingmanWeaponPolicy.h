#pragma once

#include "CoreMinimal.h"
#include "Battle/Combat/GuLiCombatDamageLedger.h"
#include "Battle/Relay/GuLiWingmanRelayTypes.h"

namespace GuLiWingmanAttackAuthority
{
	/** Air bursts have one geometric start boundary; range, facing, LOS and flight mode are irrelevant. */
	GULISTRIKE_API bool IsAirBurstStartEligible(const FVector& SourceLocation,
		const FGuLiCombatTargetSnapshot& LiveTarget, float StartDistanceCentimeters);
}

/** Canonical wire representation for the free-look camera aim. */
namespace GuLiWingmanMissileAim
{
	constexpr int32 QuantizedUnit = 1000;
	GULISTRIKE_API bool Quantize(const FVector& Direction, FIntVector& OutDirectionMilli);
	GULISTRIKE_API bool Decode(const FIntVector& DirectionMilli, FVector& OutDirection);
}

/** Weapon input executed by the Ship simulation owner. */
struct GULISTRIKE_API FGuLiWingmanMissileSalvoRequest
{
	FGuid ActivationId;
	FGuLiWeaponBindingKey Binding;
	FName SkillId;
	FGameplayTag MissileAbilityId;
	uint32 AbilitySetRevision = 0u;
	uint32 LoadoutRevision = 0u;
	uint32 ProfileRevision = 0u;
	uint32 MissileDefinitionRevision = 0u;
	/** Client camera forward, normalized then rounded to 1/1000 before the owner command. */
	FIntVector AimDirectionMilli = FIntVector::ZeroValue;

	bool IsWellFormed() const;
};

struct GULISTRIKE_API FGuLiWingmanMissileSalvoResult
{
	EGuLiWingmanRejectReason RejectReason = EGuLiWingmanRejectReason::InvalidIdentity;
	uint8 FlightIndex = MAX_uint8;
	int32 LaunchedCount = 0;
	bool bSharedCooldownStarted = false;
	/** Authority-selected target. No client-provided target handle participates. */
	FGuLiTargetHandle SelectedTarget;

	bool WasLaunched() const
	{
		return RejectReason == EGuLiWingmanRejectReason::None && LaunchedCount > 0 && bSharedCooldownStarted;
	}
};

namespace GuLiWingmanMissilePolicy
{
GULISTRIKE_API EGuLiWingmanRejectReason SelectTarget(const FVector& Origin,const FVector& Aim,
    const FGuLiWingmanWeaponRuntimeConfig& Runtime,const FGuLiTargetHandle& Source,EGuLiTeam Team,
    TConstArrayView<FGuLiCombatTargetSnapshot> Targets,
    TFunctionRef<bool(const FVector&,const FGuLiCombatTargetSnapshot&)> Visible,FGuLiCombatTargetSnapshot& OutTarget);
}
