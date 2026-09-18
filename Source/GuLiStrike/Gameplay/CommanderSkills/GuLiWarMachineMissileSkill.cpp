#include "Gameplay/CommanderSkills/GuLiWarMachineMissileSkill.h"
#include "Battle/Framework/GuLiBattlePlayerState.h"
#include "Gameplay/CombatEffects/GuLiCombatEffectTypes.h"
#include "Gameplay/Data/GuLiCommanderDataSubsystem.h"
#include "Gameplay/Data/GuLiSpellFieldDataSubsystem.h"
#include "Gameplay/Skills/GuLiArmySkillSubsystem.h"
#include "Engine/World.h"

bool UGuLiWarMachineMissileSkillExecutor::ValidateDefinition(const FGuLiActiveSkillDefinition& Definition, FString& Error) const
{
	if (!Super::ValidateDefinition(Definition, Error)) return false;
	const auto* Config = CastChecked<UGuLiPointSkillConfiguration>(Definition.Configuration);
	if (!Config->SourceWeaponSlot.IsNone() && Config->bUseAuthoredTrajectory) return true;
	Error = TEXT("War Machine missile requires a source weapon slot and authored trajectory.");
	return false;
}

bool UGuLiWarMachineMissileSkillExecutor::ConfigurePayload(const FGuLiActiveSkillExecutionContext& Context,
	const UGuLiPointSkillConfiguration& Config, FGuLiCombatAttackRequest& Request, FString& Error) const
{
	auto& World = *Context.Commander->GetWorld();
	const auto* Army = World.GetSubsystem<UGuLiArmySkillSubsystem>();
	const auto* Data = World.GetSubsystem<UGuLiCommanderDataSubsystem>();
	const auto* Weapon = Army ? Army->FindResolvedSkill(Context.Commander->GetTeam(), Context.UnitTypeId, Config.SourceWeaponSlot) : nullptr;
	const auto* Mount = Data && Data->IsWeaponMountCatalogValid() ? Data->FindWeaponMountConfig(Context.UnitTypeId, Config.SourceWeaponSlot) : nullptr;
	const auto* Skill = Weapon && Data ? Data->FindSkillDefinition(Weapon->SkillId) : nullptr;
	if (!Weapon || !Weapon->bUnlocked || !Weapon->bEquipped || Weapon->TriggerMode != EGuLiWeaponTriggerMode::Active
		|| Weapon->ExecutorId != TEXT("LaunchProjectile") || !Mount || !Mount->IsValid() || !Skill || Skill->EffectConfigId.IsNone())
	{ Error = TEXT("Active source weapon or calibrated launch mount is unavailable."); return false; }
	const auto* Fields = World.GetSubsystem<UGuLiSpellFieldDataSubsystem>();
	const auto* Field = Fields ? Fields->FindCombatField(Skill->EffectConfigId) : nullptr;
	if (!Field) { Error = TEXT("Missile explosion field is unavailable."); return false; }
	Request.MuzzleOffset = Mount->Muzzles[Context.ShotOrdinal % Mount->Muzzles.Num()];
	Request.Context.WeaponBinding = FGuLiWeaponBindingKey::Army(Request.Context.MatchEpoch, Context.Commander->GetTeam(), Context.UnitTypeId, Config.SourceWeaponSlot);
	Request.Context.ProfileRevision = Weapon->Revision;
	Request.Context.SkillId = Weapon->SkillId;
	Request.Context.EffectConfigId = Skill->EffectConfigId;
	Request.Context.Damage = Weapon->Damage;
	Request.FrozenField = *Field;
	return true;
}
