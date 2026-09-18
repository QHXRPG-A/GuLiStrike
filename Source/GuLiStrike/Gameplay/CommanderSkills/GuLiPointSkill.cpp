#include "Gameplay/CommanderSkills/GuLiPointSkill.h"
#include "Battle/Framework/GuLiBattlePlayerState.h"
#include "Battle/Framework/GuLiBattleGameState.h"
#include "Gameplay/CombatEffects/GuLiCombatEffectRuntimeSubsystem.h"
#include "Gameplay/Data/GuLiSpellFieldDataSubsystem.h"
#include "Gameplay/CombatEffects/GuLiGroundWarningSubsystem.h"
#include "Gameplay/Skills/GuLiSkillTargeting.h"
#include "Engine/World.h"

bool UGuLiPointSkillExecutor::ValidateDefinition(const FGuLiActiveSkillDefinition& Definition, FString& Error) const
{
	const auto* Config = Cast<UGuLiPointSkillConfiguration>(Definition.Configuration);
	FGuLiSpellFieldConfig Field;
	if (Definition.Scope != EGuLiActiveSkillScope::Unit || Definition.TargetMode != EGuLiActiveSkillTargetMode::GroundPoint
		|| !Config || !Config->Projectile || !Config->Projectile->IsValidDefinition()
		|| !UGuLiSpellFieldDataSubsystem::ResolveAuthoredConfig(Config->FieldConfigId, Field) || Config->LaunchOffset.ContainsNaN()
		|| !FMath::IsFinite(Config->TargetAreaDiameterCentimeters) || Config->TargetAreaDiameterCentimeters < 0
		|| (Config->GroundWarningStyle && !Config->GroundWarningStyle->IsValidStyle()))
	{ Error = TEXT("Point projectile executor requires a unit, ground target, projectile and existing field row."); return false; }
	return true;
}
FGuLiActiveSkillExecutionResult UGuLiPointSkillExecutor::Execute(const FGuLiActiveSkillExecutionContext& Context, const UDataAsset* Configuration) const
{
	FGuLiActiveSkillExecutionResult Result;
	const auto* Candidate = Cast<UGuLiPointSkillConfiguration>(Configuration);
	if (!Candidate || !Context.Commander || !Context.Commander->HasAuthority() || !Context.Commander->GetWorld())
	{ Result.Error = TEXT("Point projectile requires authoritative caster and configuration."); return Result; }
	const auto& Config = *Candidate;
	auto& World = *Context.Commander->GetWorld();
	FGuLiCombatAttackRequest Request;
	const auto* GameState = World.GetGameState<AGuLiBattleGameState>();
	if (!GameState || !Config.Projectile || !Config.Projectile->ResolveMotionSettings(Request.Motion))
	{ Result.Error = TEXT("Effect configuration is unavailable."); return Result; }
	Request.Context.Source = Context.Source;
	Request.Context.MatchEpoch = GameState->GetMatchEpoch();
	Request.Context.RootEventId = Context.RequestId;
	Request.Context.ShotId = FGuid::NewGuid();
	Request.SourceTransform = Context.SourceTransform;
	Request.TargetLocation = Context.GroundPoint;
	// Admission checks the circle center. The sampled impact may overshoot weapon range.
	if (Config.TargetAreaDiameterCentimeters > 0)
	{
		const float Radius = Config.TargetAreaDiameterCentimeters * 0.5f;
		FRandomStream Random(HashCombine(GetTypeHash(Context.RequestId),
			HashCombine(GetTypeHash(Context.SoldierId.Value), GetTypeHash(Context.ShotOrdinal))));
		for (int32 Attempt = 0; Attempt < 8; ++Attempt)
		{
			const float Angle = Random.FRand() * UE_TWO_PI;
			const float Distance = Radius * FMath::Sqrt(Random.FRand());
			const FVector ScatterPoint = Context.GroundPoint + FVector(FMath::Cos(Angle) * Distance, FMath::Sin(Angle) * Distance, 0);
			FVector Ground;
			if (GuLiSkillTargeting::ResolveGround(World, ScatterPoint, Ground)
				&& FVector::DistSquared2D(Ground, Context.GroundPoint) <= FMath::Square(Radius))
			{
				Request.TargetLocation = Ground;
				break;
			}
		}
	}
	Request.Projectile = Config.Projectile;
	Request.bUseAuthoredPointTrajectory = Config.bUseAuthoredTrajectory;
	Request.GroundWarningStyle = Config.GroundWarningStyle;
	if (!ConfigurePayload(Context, Config, Request, Result.Error)) return Result;
	Result.EffectId = World.GetSubsystem<UGuLiCombatEffectRuntimeSubsystem>()->LaunchPointProjectile(Request);
	Result.bSucceeded = Result.EffectId.IsValid();
	if (!Result.bSucceeded) Result.Error = TEXT("Projectile execution rejected the cast.");
	return Result;
}

bool UGuLiPointSkillExecutor::ConfigurePayload(const FGuLiActiveSkillExecutionContext& Context,
	const UGuLiPointSkillConfiguration& Config, FGuLiCombatAttackRequest& Request, FString& Error) const
{
	const auto* Fields = Context.Commander->GetWorld()->GetSubsystem<UGuLiSpellFieldDataSubsystem>();
	const auto* Field = Fields ? Fields->FindCombatField(Config.FieldConfigId) : nullptr;
	if (!Field) { Error = TEXT("Point projectile field is unavailable."); return false; }
	Request.MuzzleOffset = Config.LaunchOffset;
	Request.Context.SkillId = Context.SkillId;
	Request.Context.EffectConfigId = Config.FieldConfigId;
	Request.Context.Damage = Field->Damage;
	Request.FrozenField = *Field;
	return true;
}
