#include "Gameplay/Cards/GuLiRogueCardEffect.h"
#include "Gameplay/Data/Generated/GuLiStrikeRogueCardsTableRows.h"
#include "Gameplay/Skills/GuLiArmySkillSubsystem.h"
#include "Commander/Mass/GuLiBattleAuthoritySubsystem.h"
#include "Battle/Framework/GuLiBattlePlayerState.h"
#include "Engine/World.h"

namespace
{
    void DescribePercent(const FGuLiStrikeRogueCardsCardsRow& R,FGuLiRogueEffectDefinition& Out,FName Slot,EGuLiSkillAttribute Attribute)
    {
        auto& M=Out.Source.Modifiers.AddDefaulted_GetRef();M.Target.UnitTypeIds.Add(uint16(R.UnitTypeId));M.Target.SlotId=Slot;
        M.Attribute=Attribute;M.Operation=EGuLiSkillModifierOperation::AddPercent;M.Magnitude=R.BonusPercent;
    }
}
bool UGuLiRogueCardEffect::Describe(const FGuLiStrikeRogueCardsCardsRow&,FGuLiRogueEffectDefinition&,FString& Error) const
{Error=TEXT("Card effect has no portable capability description: ")+GetClass()->GetPathName();return false;}
bool UGuLiRogueCardFireRateEffect::Describe(const FGuLiStrikeRogueCardsCardsRow& R,FGuLiRogueEffectDefinition& Out,FString& Error) const
{if (!Validate(R,Error)) return false;DescribePercent(R,Out,TEXT("BasicAttack"),EGuLiSkillAttribute::AttackRate);return true;}
bool UGuLiRogueCardMoveSpeedEffect::Describe(const FGuLiStrikeRogueCardsCardsRow& R,FGuLiRogueEffectDefinition& Out,FString& Error) const
{if (!Validate(R,Error)) return false;Out.MovementPercent.Add(uint16(R.UnitTypeId),R.BonusPercent);return true;}
bool UGuLiRogueCardMissileDamageEffect::Describe(const FGuLiStrikeRogueCardsCardsRow& R,FGuLiRogueEffectDefinition& Out,FString& Error) const
{if (!Validate(R,Error)) return false;DescribePercent(R,Out,TEXT("MissileLauncher"),EGuLiSkillAttribute::Damage);return true;}
bool UGuLiRogueCardMissilePodEffect::Describe(const FGuLiStrikeRogueCardsCardsRow& R,FGuLiRogueEffectDefinition& Out,FString& Error) const
{if (!Validate(R,Error)) return false;auto& U=Out.Source.Unlocks.AddDefaulted_GetRef();U.Target.UnitTypeIds.Add(uint16(R.UnitTypeId));U.Target.SlotId=TEXT("MissileLauncher");return true;}
bool UGuLiRogueCardMissileCountEffect::Describe(const FGuLiStrikeRogueCardsCardsRow& R,FGuLiRogueEffectDefinition& Out,FString& Error) const
{
    if (!Validate(R,Error)) return false;auto& M=Out.Source.Modifiers.AddDefaulted_GetRef();M.Target.UnitTypeIds.Add(uint16(R.UnitTypeId));M.Target.SlotId=TEXT("MissileLauncher");
    M.Attribute=EGuLiSkillAttribute::ProjectileCount;M.Operation=EGuLiSkillModifierOperation::AddFlat;M.IntegerMagnitude=R.BonusCount;return true;
}
bool UGuLiRogueCardEffect::Validate(const FGuLiStrikeRogueCardsCardsRow& R, FString& Error) const
{
	if (FMath::IsFinite(R.BonusPercent) && R.BonusPercent>0 && R.BonusCount==0) return true;
	Error=TEXT("Percentage effect requires positive BonusPercent and zero BonusCount"); return false;
}
FText UGuLiRogueCardEffect::FormatDescription(const FGuLiStrikeRogueCardsCardsRow& R, const FText& Pattern) const
{
	FNumberFormattingOptions Options; Options.SetMaximumFractionalDigits(2);
	return FText::Format(Pattern,FText::AsNumber(R.BonusPercent*100.f,&Options));
}
bool UGuLiRogueCardMissilePodEffect::Validate(const FGuLiStrikeRogueCardsCardsRow& R, FString& Error) const
{
	if (R.BonusPercent==0 && R.BonusCount==0 && R.MaxAcquisitions==1) return true;
	Error=TEXT("Unlock effect requires zero bonuses and MaxAcquisitions=1"); return false;
}
bool UGuLiRogueCardMissileCountEffect::Validate(const FGuLiStrikeRogueCardsCardsRow& R, FString& Error) const
{
	if (R.BonusCount>0 && R.BonusPercent==0) return true;
	Error=TEXT("Projectile count effect requires positive BonusCount and zero BonusPercent"); return false;
}
FText UGuLiRogueCardMissileCountEffect::FormatDescription(const FGuLiStrikeRogueCardsCardsRow& R, const FText& Pattern) const
{ return FText::Format(Pattern,FText::AsNumber(R.BonusCount)); }
