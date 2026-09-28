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
