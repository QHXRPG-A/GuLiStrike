#include "Gameplay/CommanderSkills/GuLiCommanderSkillDefinition.h"
#include "Battle/Framework/GuLiBattlePlayerState.h"
#include "Battle/Framework/GuLiBattleGameState.h"
#include "Gameplay/CombatEffects/GuLiCombatEffectRuntimeSubsystem.h"
#include "Gameplay/Data/GuLiSpellFieldDataSubsystem.h"
#include "Gameplay/Teleport/GuLiTeleportFieldActor.h"
#include "Engine/World.h"

bool FGuLiActiveSkillRequest::HasSameContent(const FGuLiActiveSkillRequest& Other) const
{
	return MatchEpoch == Other.MatchEpoch && SelectionRevision == Other.SelectionRevision && GlobalSkillId == Other.GlobalSkillId
		&& Command == Other.Command && CastId == Other.CastId && bHasGroundPoint == Other.bHasGroundPoint && GroundPoint == Other.GroundPoint;
}
const FGuLiActiveSkillDefinition* UGuLiCommanderSkillCatalog::FindSkill(FName Id) const
{ return Skills.FindByPredicate([Id](const auto& Entry) { return Entry.SkillId == Id; }); }
const FGuLiActiveSkillDefinition* UGuLiCommanderSkillCatalog::FindUnitSkill(uint16 UnitTypeId) const
{
	const FName* Id = UnitSkills.Find(UnitTypeId); return Id ? FindSkill(*Id) : nullptr;
}
bool UGuLiCommanderSkillCatalog::Validate(FString& Error) const
{
	TSet<FName> Seen;
	for (const auto& Definition : Skills)
	{
		if (Definition.SkillId.IsNone() || Seen.Contains(Definition.SkillId) || !Definition.ExecutorClass
			|| Definition.ExecutorClass->HasAnyClassFlags(CLASS_Abstract) || !FMath::IsFinite(Definition.CooldownSeconds)
			|| Definition.CooldownSeconds < 0 || !FMath::IsFinite(Definition.RangeCentimeters) || Definition.RangeCentimeters < 0 || Definition.MaximumLevel < 1)
		{ Error = TEXT("Skill requires a unique ID, concrete executor and valid cooldown/range/level."); return false; }
		if (!Definition.ExecutorClass->GetDefaultObject<UGuLiCommanderSkillExecutor>()->ValidateDefinition(Definition, Error)) return false;
		Seen.Add(Definition.SkillId);
	}
	for (const auto& Pair : UnitSkills)
	{
		if (Pair.Key < 1 || Pair.Key > MAX_uint16) { Error = TEXT("Unit skill mapping has an invalid UnitTypeId."); return false; }
		if (Pair.Value.IsNone()) continue;
		const auto* Definition = FindSkill(Pair.Value);
		if (!Definition || Definition->Scope != EGuLiActiveSkillScope::Unit || Definition->TargetMode == EGuLiActiveSkillTargetMode::TwoPoint)
		{ Error = TEXT("Unit skill mapping requires a unit-scoped, single-command skill."); return false; }
		if (Definition->TargetMode == EGuLiActiveSkillTargetMode::GroundPoint && Definition->RangeCentimeters <= 0)
		{ Error = TEXT("Ground-targeted unit skills require positive range."); return false; }
	}
	Seen.Reset();
	for (FName Id : GlobalSkills)
	{
		const auto* Definition = FindSkill(Id);
		if (!Definition || Definition->Scope != EGuLiActiveSkillScope::Global || Seen.Contains(Id))
		{ Error = TEXT("Global skills require unique global-scoped definitions."); return false; }
		Seen.Add(Id);
	}
	return true;
}
bool UGuLiCommanderSkillExecutor::ValidateDefinition(const FGuLiActiveSkillDefinition&, FString&) const { return true; }
FGuLiActiveSkillExecutionResult UGuLiCommanderSkillExecutor::Execute(const FGuLiActiveSkillExecutionContext&, const UDataAsset*) const
{
	FGuLiActiveSkillExecutionResult Result; Result.Error = TEXT("Executor has no implementation."); return Result;
}
bool UGuLiPointSkillExecutor::ValidateDefinition(const FGuLiActiveSkillDefinition& Definition, FString& Error) const
{
	const auto* Config = Cast<UGuLiPointSkillConfiguration>(Definition.Configuration);
	FGuLiSpellFieldConfig Field;
	if (Definition.Scope != EGuLiActiveSkillScope::Unit || Definition.TargetMode != EGuLiActiveSkillTargetMode::GroundPoint
		|| !Config || !Config->Projectile || !Config->Projectile->IsValidDefinition()
		|| !UGuLiSpellFieldDataSubsystem::ResolveAuthoredConfig(Config->FieldConfigId, Field) || Config->LaunchOffset.ContainsNaN())
	{ Error = TEXT("Point projectile executor requires a unit, ground target, projectile and existing field row."); return false; }
	return true;
}
FGuLiActiveSkillExecutionResult UGuLiPointSkillExecutor::Execute(const FGuLiActiveSkillExecutionContext& Context, const UDataAsset* Configuration) const
{
	const auto& Config = *CastChecked<UGuLiPointSkillConfiguration>(Configuration);
	FGuLiActiveSkillExecutionResult Result;
	auto& World = *Context.Commander->GetWorld();
	const auto* Field = World.GetSubsystem<UGuLiSpellFieldDataSubsystem>()->FindCombatField(Config.FieldConfigId);
	FGuLiCombatAttackRequest Request;
	if (!Field || !Config.Projectile->ResolveMotionSettings(Request.Motion)) { Result.Error = TEXT("Effect configuration is unavailable."); return Result; }
	Request.Context.Source = Context.Source;
	Request.Context.SkillId = Context.SkillId;
	Request.Context.EffectConfigId = Config.FieldConfigId;
	Request.Context.Damage = Field->Damage;
	Request.Context.MatchEpoch = World.GetGameState<AGuLiBattleGameState>()->GetMatchEpoch();
	Request.Context.RootEventId = Context.RequestId;
	Request.Context.ShotId = FGuid::NewGuid();
	Request.SourceTransform = Context.SourceTransform;
	Request.MuzzleOffset = Config.LaunchOffset;
	Request.TargetLocation = Context.GroundPoint;
	Request.Projectile = Config.Projectile;
	Request.FrozenField = *Field;
	Result.EffectId = World.GetSubsystem<UGuLiCombatEffectRuntimeSubsystem>()->LaunchPointProjectile(Request);
	Result.bSucceeded = Result.EffectId.IsValid();
	if (!Result.bSucceeded) Result.Error = TEXT("Projectile execution rejected the cast.");
	return Result;
}
bool UGuLiSelfSkillExecutor::ValidateDefinition(const FGuLiActiveSkillDefinition& Definition, FString& Error) const
{
	if (Definition.TargetMode == EGuLiActiveSkillTargetMode::Self) return true;
	Error = TEXT("Self-effect executor requires Self targeting."); return false;
}
bool UGuLiSelfSkillExecutor::ApplySelfEffect_Implementation(const FGuLiActiveSkillExecutionContext&, const UDataAsset*) const { return false; }
FGuLiActiveSkillExecutionResult UGuLiSelfSkillExecutor::Execute(const FGuLiActiveSkillExecutionContext& Context, const UDataAsset* Configuration) const
{
	FGuLiActiveSkillExecutionResult Result;
	Result.bSucceeded = ApplySelfEffect(Context, Configuration);
	if (!Result.bSucceeded) Result.Error = TEXT("Self effect did not apply.");
	return Result;
}
bool UGuLiTeleportSkillExecutor::ValidateDefinition(const FGuLiActiveSkillDefinition& Definition, FString& Error) const
{
	if (Definition.Scope == EGuLiActiveSkillScope::Global && Definition.TargetMode == EGuLiActiveSkillTargetMode::TwoPoint
		&& Definition.MaximumLevel <= 4) return true;
	Error = TEXT("Teleport requires a global two-point skill with at most four authored levels."); return false;
}
FGuLiActiveSkillExecutionResult UGuLiTeleportSkillExecutor::Execute(const FGuLiActiveSkillExecutionContext& Context, const UDataAsset*) const
{
	FGuLiActiveSkillExecutionResult Result;
	auto& Commander = *Context.Commander;
	if (Context.Command == EGuLiActiveSkillCommand::Activate)
	{
		if (auto* Field = AGuLiTeleportFieldActor::StartCast(Commander, Context.Level, Context.GroundPoint, Result.Error))
		{ Result.CastId = Field->GetCastState().CastId; Result.bSucceeded = true; }
	}
	else if (auto* Field = AGuLiTeleportFieldActor::FindCast(*Commander.GetWorld(), Commander.GetPlayerGuid());
		Field && Field->GetCastState().CastId == Context.CastId)
	{
		Result.CastId = Context.CastId;
		if (Context.Command == EGuLiActiveSkillCommand::Continue) Result.bSucceeded = Field->SubmitDestination(Commander, Context.GroundPoint, Result.Error);
		else { Field->Cancel(Commander); Result.bSucceeded = true; }
	}
	else Result.Error = TEXT("Teleport cast is no longer owned by this commander.");
	return Result;
}
