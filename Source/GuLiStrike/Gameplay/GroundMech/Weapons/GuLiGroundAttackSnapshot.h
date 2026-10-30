#pragma once
#include "CoreMinimal.h"
#include "GuLiNetEffects.h"
#include "Battle/Combat/GuLiCombatDamageLedger.h"

/** Immutable per-admitted-shot data. Secondary hits retain this payload, independently of the Pawn. */
struct FGuLiGroundAttackSnapshot
{
    FGuid Player;
    uint64 Revision=1;
    float Damage=0;
    int32 BulletVfxId=0,ImpactVfxId=0,MuzzleVfxId=0;
    GuLiNet::Effects::Set Effects;
    GuLiNet::Effects::Ricochet GetRicochet() const { return GuLiNet::Effects::GetRicochet(Effects); }
};
struct FGuLiGroundWeaponHit
{
    FGuid Shot,Event;
    FGuLiTargetHandle Target;
    FVector Position=FVector::ZeroVector;
    int32 Segment=0;
    float DirectDamageMultiplier=1;
    /** On-hit consumers use their original payload values, without the direct-damage multiplier. */
    bool bCanTriggerRicochet=false;
};
DECLARE_MULTICAST_DELEGATE_TwoParams(FGuLiGroundWeaponHitEvent,const FGuLiGroundWeaponHit&,const FGuLiGroundAttackSnapshot&);
