#pragma once
#include "Battle/Combat/GuLiCombatDamageLedger.h"
#include "Network/GuLiHybridWorldSubsystem.h"

struct FGuLiHybridManifestData;
namespace GuLiHybridTargets
{
template<EGuLiTargetKind Kind,bool UsesOrdinal=false>
bool Actor(const FGuLiHybridManifestData&,const FGuLiHybridObjectState& Object,uint32,FGuLiTargetHandle& Out)
{
    Out={};Out.Kind=Kind;Out.AuthorityId=Object.Key.StableId;Out.Generation=Object.Key.LifeGeneration;
    if constexpr (UsesOrdinal)
    {
        if (!Object.Ordinal) return false;
        Out.LocalId=Object.Ordinal;
    }
    return Out.IsValid();
}
inline bool Mass(const FGuLiHybridManifestData&,const FGuLiHybridObjectState& Object,uint32 Epoch,FGuLiTargetHandle& Out)
{
    if (!Epoch || !Object.Ordinal) return false;
    Out=GuLiCombatTargets::MakeCommanderSoldierTargetHandle(Epoch,Object.Ordinal,Object.Key.LifeGeneration);
    return Out.IsValid();
}
}
