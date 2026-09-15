#include "Gameplay/CommanderSkills/GuLiUnitSkillExecution.h"

FGuLiActiveSkillUnitResult GuLiUnitSkillExecution::Execute(const FGuLiActiveSkillDefinition* Definition,
	const FGuLiUnitSkillCaster& Caster, FGuLiActiveSkillRuntime& Runtime, double SimulationSeconds,
	double ServerSeconds, FExecute ExecuteEffect)
{
	FGuLiActiveSkillUnitResult Result; Result.SoldierId = Caster.Context.SoldierId;
	if (!Definition) return Result;
	Result.SkillId = Definition->SkillId;
	if (!Caster.bEligible) { Result.Code = EGuLiActiveSkillResultCode::Ineligible; return Result; }
	Result.ReadyAtServerSeconds = ServerSeconds + FMath::Max(0., Runtime.ReadyAt - SimulationSeconds);
	if (Runtime.ReadyAt > SimulationSeconds) { Result.Code = EGuLiActiveSkillResultCode::Cooldown; return Result; }
	if (Definition->TargetMode == EGuLiActiveSkillTargetMode::GroundPoint)
	{
		if (!Caster.bHasGroundPoint) { Result.Code = EGuLiActiveSkillResultCode::InvalidGround; return Result; }
		if (FVector::DistSquared(Caster.Context.SourceTransform.GetLocation(), Caster.Context.GroundPoint) > FMath::Square(Definition->RangeCentimeters))
		{ Result.Code = EGuLiActiveSkillResultCode::OutOfRange; return Result; }
	}
	Result.Execution = ExecuteEffect(Caster.Context, Definition->Configuration);
	Result.Code = Result.Execution.bSucceeded ? EGuLiActiveSkillResultCode::Succeeded : EGuLiActiveSkillResultCode::ExecutionFailed;
	if (Result.Execution.bSucceeded)
	{
		Runtime.SkillId = Definition->SkillId;
		Runtime.ReadyAt = SimulationSeconds + Definition->CooldownSeconds;
		Runtime.ActiveCastId = Result.Execution.CastId;
		Result.ReadyAtServerSeconds = ServerSeconds + Definition->CooldownSeconds;
	}
	return Result;
}
