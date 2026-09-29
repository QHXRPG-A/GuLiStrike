#include "Gameplay/Cards/GuLiRogueCardEffect.h"
#include "Gameplay/Data/Generated/GuLiStrikeRogueCardsTableRows.h"
#include "Gameplay/Skills/GuLiArmySkillSubsystem.h"
#include "Commander/Mass/GuLiBattleAuthoritySubsystem.h"
#include "Battle/Framework/GuLiBattlePlayerState.h"
#include "Engine/World.h"

namespace
{
	bool AddSkillSource(const AGuLiBattlePlayerState& Commander, const FGuLiStrikeRogueCardsCardsRow& Card,
		FGuid SourceId, FName Slot, EGuLiSkillAttribute Attribute, FString& Error)
	{
		auto* Skills=Commander.GetWorld()->GetSubsystem<UGuLiArmySkillSubsystem>();
		if (!Skills) { Error=TEXT("Skill subsystem unavailable"); return false; }
		FGuLiSkillSource Source; Source.SourceInstanceId=SourceId; Source.DebugLabel=TEXT("RogueCard:")+Card.Id;
		auto& Modifier=Source.Modifiers.AddDefaulted_GetRef();
		Modifier.Target.UnitTypeIds.Add(static_cast<uint16>(Card.UnitTypeId)); Modifier.Target.SlotId=Slot;
		Modifier.Attribute=Attribute; Modifier.Operation=EGuLiSkillModifierOperation::AddPercent;
		Modifier.Magnitude=Card.BonusPercent;
		return Skills->UpsertSource(Commander, Source, Error);
	}
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
bool UGuLiRogueCardMissilePodEffect::Apply(const AGuLiBattlePlayerState& C, const FGuLiStrikeRogueCardsCardsRow& R, FGuid Id, FString& Error) const
{
	auto* Skills=C.GetWorld()->GetSubsystem<UGuLiArmySkillSubsystem>();
	if (!Skills || !Validate(R,Error)) return false;
	FGuLiSkillSource Source; Source.SourceInstanceId=Id; Source.DebugLabel=TEXT("RogueCard:")+R.Id;
	auto& Unlock=Source.Unlocks.AddDefaulted_GetRef();
	Unlock.Target.UnitTypeIds.Add(static_cast<uint16>(R.UnitTypeId)); Unlock.Target.SlotId=TEXT("MissileLauncher");
	return Skills->UpsertSource(C,Source,Error);
}
bool UGuLiRogueCardMissileCountEffect::Validate(const FGuLiStrikeRogueCardsCardsRow& R, FString& Error) const
{
	if (R.BonusCount>0 && R.BonusPercent==0) return true;
	Error=TEXT("Projectile count effect requires positive BonusCount and zero BonusPercent"); return false;
}
FText UGuLiRogueCardMissileCountEffect::FormatDescription(const FGuLiStrikeRogueCardsCardsRow& R, const FText& Pattern) const
{ return FText::Format(Pattern,FText::AsNumber(R.BonusCount)); }
bool UGuLiRogueCardMissileCountEffect::Apply(const AGuLiBattlePlayerState& C, const FGuLiStrikeRogueCardsCardsRow& R, FGuid Id, FString& Error) const
{
	auto* Skills=C.GetWorld()->GetSubsystem<UGuLiArmySkillSubsystem>();
	if (!Skills || !Validate(R,Error)) return false;
	FGuLiSkillSource Source; Source.SourceInstanceId=Id; Source.DebugLabel=TEXT("RogueCard:")+R.Id;
	auto& Modifier=Source.Modifiers.AddDefaulted_GetRef();
	Modifier.Target.UnitTypeIds.Add(static_cast<uint16>(R.UnitTypeId)); Modifier.Target.SlotId=TEXT("MissileLauncher");
	Modifier.Attribute=EGuLiSkillAttribute::ProjectileCount; Modifier.Operation=EGuLiSkillModifierOperation::AddFlat;
	Modifier.IntegerMagnitude=R.BonusCount;
	return Skills->UpsertSource(C,Source,Error);
}
bool UGuLiRogueCardFireRateEffect::Apply(const AGuLiBattlePlayerState& C, const FGuLiStrikeRogueCardsCardsRow& R, FGuid Id, FString& Error) const
{ return AddSkillSource(C,R,Id,TEXT("BasicAttack"),EGuLiSkillAttribute::AttackRate,Error); }
bool UGuLiRogueCardMissileDamageEffect::Apply(const AGuLiBattlePlayerState& C, const FGuLiStrikeRogueCardsCardsRow& R, FGuid Id, FString& Error) const
{ return AddSkillSource(C,R,Id,TEXT("MissileLauncher"),EGuLiSkillAttribute::Damage,Error); }
bool UGuLiRogueCardMoveSpeedEffect::Apply(const AGuLiBattlePlayerState& C, const FGuLiStrikeRogueCardsCardsRow& R, FGuid Id, FString& Error) const
{
	auto* Authority=C.GetWorld()->GetSubsystem<UGuLiBattleAuthoritySubsystem>();
	if (!Authority) { Error=TEXT("Movement authority unavailable"); return false; }
	return Authority->ApplyRogueMovementSource(C.GetTeam(), static_cast<uint16>(R.UnitTypeId), Id, R.BonusPercent, Error);
}
