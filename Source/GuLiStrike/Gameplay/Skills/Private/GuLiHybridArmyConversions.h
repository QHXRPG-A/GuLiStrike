#pragma once
#include "GuLiNetArmy.h"
#include "Gameplay/Skills/GuLiSkillTypes.h"
#include "Network/Private/GuLiNetConversions.h"

namespace GuLiArmyBridge
{
inline std::string Text(FName N) {return N.IsNone()?std::string{}:std::string(TCHAR_TO_UTF8(*N.ToString().ToLower()));}
inline GuLiNet::Army::Selector Selector(const FGuLiSkillTargetSelector& V)
{
    GuLiNet::Army::Selector S;S.SlotName=Text(V.SlotId);S.Skill=Text(V.RequiredSkillId);
    for (auto U:V.UnitTypeIds) S.Units.push_back(U);
    TArray<FGameplayTag> Tags;V.RequiredTags.GetGameplayTagArray(Tags);for (auto T:Tags) S.Tags.push_back(Text(T.GetTagName()));return S;
}
inline GuLiNet::Army::Source Source(const FGuLiSkillSource& V)
{
    GuLiNet::Army::Source S;S.Identity=GuLiNetBridge::ToCore(V.SourceInstanceId);
    for (const auto& Modifier:V.Modifiers) S.Modifiers.push_back({Selector(Modifier.Target),uint8(Modifier.Attribute),uint8(Modifier.Operation),Modifier.Magnitude,Modifier.IntegerMagnitude});
    for (const auto& Replacement:V.Replacements) S.Replacements.push_back({Selector(Replacement.Target),Text(Replacement.SkillId),Replacement.Priority});
    for (const auto& Unlock:V.Unlocks) S.Unlocks.push_back(Selector(Unlock.Target));return S;
}
inline FGuLiResolvedSkillProfile Profile(const GuLiNet::Army::Profile& P,EGuLiTeam Team,uint32 Revision)
{
    FGuLiResolvedSkillProfile V;V.Team=Team;V.UnitTypeId=P.Key.Unit;V.SlotId=FName(UTF8_TO_TCHAR(P.Key.Name.c_str()));
    V.SkillId=FName(UTF8_TO_TCHAR(P.Skill.c_str()));V.ExecutorId=FName(UTF8_TO_TCHAR(P.Executor.c_str()));
    for (const auto& T:P.Tags) V.Tags.AddTag(FGameplayTag::RequestGameplayTag(FName(UTF8_TO_TCHAR(T.c_str())),false));
    V.Damage=P.Damage;V.AttackRatePerSecond=P.Rate;V.RangeCentimeters=P.Range;V.ProjectileCount=P.Count;
    V.ProjectileSpeedCentimetersPerSecond=P.Speed;V.ProjectileLifetimeSeconds=P.Lifetime;V.ProjectileSweepRadiusCentimeters=P.Sweep;V.ProjectileSpreadAngleDegrees=P.Spread;
    V.bUnlocked=P.Unlocked;V.bEquipped=P.Equipped;V.TriggerMode=P.Active?EGuLiWeaponTriggerMode::Active:EGuLiWeaponTriggerMode::Automatic;V.Revision=Revision;return V;
}
}
