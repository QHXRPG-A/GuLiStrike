#include "Gameplay/CommanderSkills/GuLiWarMachineMissileSkill.h"
#include "Battle/Framework/GuLiBattlePlayerState.h"
#include "Gameplay/CombatEffects/GuLiCombatEffectTypes.h"
#include "Gameplay/CombatEffects/GuLiCombatEffectRuntimeSubsystem.h"
#include "Gameplay/Data/GuLiCommanderDataSubsystem.h"
#include "Gameplay/Data/GuLiSpellFieldDataSubsystem.h"
#include "Gameplay/Skills/GuLiArmySkillSubsystem.h"
#include "Engine/World.h"

bool UGuLiWarMachineMissileSkillExecutor::ValidateDefinition(const FGuLiActiveSkillDefinition& Definition, FString& Error) const
{
	if (!Super::ValidateDefinition(Definition, Error)) return false;
	const auto* Config = CastChecked<UGuLiPointSkillConfiguration>(Definition.Configuration);
	if (Config->SourceWeaponSlot == TEXT("MissileLauncher") && Config->bUseAuthoredTrajectory
		&& Config->MaxProjectilesPerActivation > 0 && Config->TargetAreaDiameterCentimeters > 0
		&& Config->GroundWarningStyle) return true;
	Error = TEXT("WM01 missile requires MissileLauncher, authored trajectory, positive guidance capacity/area and warning style.");
	return false;
}

bool UGuLiWarMachineMissileSkillExecutor::IsAvailable(UWorld& World, EGuLiTeam Team, uint16 UnitTypeId, const UDataAsset* Configuration) const
{
	const auto* Config=Cast<UGuLiPointSkillConfiguration>(Configuration);
	const auto* Army=World.GetSubsystem<UGuLiArmySkillSubsystem>();
	const auto* Weapon=Army && Config ? Army->FindResolvedSkill(Team,UnitTypeId,Config->SourceWeaponSlot) : nullptr;
	return Weapon && Weapon->bUnlocked && Weapon->bEquipped && Weapon->ProjectileCount>0
		&& Weapon->TriggerMode==EGuLiWeaponTriggerMode::Active && Weapon->ExecutorId==TEXT("LaunchProjectile");
}

FGuLiActiveSkillExecutionResult UGuLiWarMachineMissileSkillExecutor::Execute(
	const FGuLiActiveSkillExecutionContext& Context, const UDataAsset* Configuration) const
{
	FGuLiActiveSkillExecutionResult Result;
	const auto* Config=Cast<UGuLiPointSkillConfiguration>(Configuration);
	UWorld* World=Context.Commander ? Context.Commander->GetWorld() : nullptr;
	const auto* Army=World ? World->GetSubsystem<UGuLiArmySkillSubsystem>() : nullptr;
	const auto* Weapon=Army && Config ? Army->FindResolvedSkill(Context.Commander->GetTeam(),Context.UnitTypeId,Config->SourceWeaponSlot) : nullptr;
	auto* Runtime=World ? World->GetSubsystem<UGuLiCombatEffectRuntimeSubsystem>() : nullptr;
	if (!Weapon || !Weapon->bUnlocked || !Weapon->bEquipped || Weapon->ProjectileCount<1 || !Runtime)
	{ Result.Error=TEXT("Missile launcher is unavailable."); return Result; }
	if (Context.UnitTypeId != 2 || Config->MaxProjectilesPerActivation <= 0
		|| Weapon->ProjectileCount > Config->MaxProjectilesPerActivation
		|| Context.RemainingGuidanceCapacity < 0 || !Context.GuidanceBatchId.IsValid())
	{ Result.Error=TEXT("Missile salvo exceeds its configured capacity or has no authoritative guidance batch."); return Result; }
	if (Weapon->ProjectileCount > Context.RemainingGuidanceCapacity)
	{ Result.bDeferredByGuidanceCapacity = true; return Result; }
	TArray<FGuLiCombatAttackRequest> Requests;
	Requests.Reserve(Weapon->ProjectileCount);
	for (int32 Index=0; Index<Weapon->ProjectileCount; ++Index)
	{
		FGuLiActiveSkillExecutionContext Shot=Context;
		Shot.ShotOrdinal=Context.ShotOrdinal+static_cast<uint32>(Index);
		if (!BuildRequest(Shot,Configuration,Requests.AddDefaulted_GetRef(),Result.Error)) return Result;
	}
	TArray<FGuid> Effects;
	Result.bSucceeded=Runtime->LaunchPointProjectiles(Requests,Effects);
	if (Result.bSucceeded)
	{
		Result.EffectId=Effects[0];
		Result.LaunchedProjectileCount=Effects.Num();
	}
	else Result.Error=TEXT("Missile salvo preflight rejected the cast.");
	return Result;
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
	if (const auto* Definition = Data->FindSoldierDefinition(Context.UnitTypeId);
		Definition && Context.bHasMechanicalPose)
	{
		FTransform Muzzle;
		if (GuLiMechanicalAnimation::ResolveMuzzle(Definition->MechanicalAnimation, Context.MechanicalPose,
			Context.SourceTransform, Config.SourceWeaponSlot, Context.ShotOrdinal % Mount->Muzzles.Num(), World.GetTimeSeconds(), Muzzle))
			Request.MuzzleOffset = Context.SourceTransform.InverseTransformPosition(Muzzle.GetLocation());
	}
	Request.Context.WeaponBinding = FGuLiWeaponBindingKey::Army(Request.Context.MatchEpoch, Context.Commander->GetTeam(), Context.UnitTypeId, Config.SourceWeaponSlot);
	Request.Context.ProfileRevision = Weapon->Revision;
	Request.Context.SkillId = Weapon->SkillId;
	Request.Context.EffectConfigId = Skill->EffectConfigId;
	Request.Context.Damage = Weapon->Damage;
	Request.FrozenField = *Field;
	Request.GuidanceBatchId = Context.GuidanceBatchId;
	Request.GuidanceCenter = Context.GroundPoint;
	Request.GuidanceRadius = Config.TargetAreaDiameterCentimeters * 0.5f;
	return true;
}
