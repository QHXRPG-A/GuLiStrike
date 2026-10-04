#include "Gameplay/CommanderSkills/GuLiSummonSkill.h"
#include "Battle/Framework/GuLiBattlePlayerState.h"
#include "Commander/Mass/GuLiBattleAuthoritySubsystem.h"
#include "Gameplay/Data/GuLiCommanderDataSubsystem.h"
#include "Engine/World.h"

bool UGuLiSummonSkillExecutor::ValidateDefinition(const FGuLiActiveSkillDefinition& D, FString& Error) const
{
	if (!Super::ValidateDefinition(D, Error)) return false;
	const auto* C = Cast<UGuLiSummonSkillConfiguration>(D.Configuration);
	if (D.Scope == EGuLiActiveSkillScope::Unit && D.TargetMode == EGuLiActiveSkillTargetMode::Self
		&& C && C->UnitTypeId > 0 && C->UnitTypeId <= MAX_uint16 && C->Count > 0 && C->Count <= 32
		&& FMath::IsFinite(C->ClearanceCentimeters) && C->ClearanceCentimeters >= 0
		&& C->OuterRings > 0 && C->OuterRings <= 3 && C->CandidatesPerRing > 0 && C->CandidatesPerRing <= 36) return true;
	Error = TEXT("Summon requires a self-targeted unit skill and bounded placement configuration.");
	return false;
}

bool UGuLiSummonSkillExecutor::IsAvailable(UWorld& World, EGuLiTeam Team, uint16 UnitTypeId, const UDataAsset* Configuration) const
{
	const auto* C = Cast<UGuLiSummonSkillConfiguration>(Configuration);
	const auto* Data = World.GetSubsystem<UGuLiCommanderDataSubsystem>();
	const auto* Unit = C && Data ? Data->FindSoldierDefinition(uint16(C->UnitTypeId)) : nullptr;
	return Unit && Unit->UsesMass() && Unit->bSummonOnly;
}

FGuLiActiveSkillExecutionResult UGuLiSummonSkillExecutor::Execute(
	const FGuLiActiveSkillExecutionContext& Context, const UDataAsset* Configuration) const
{
	FGuLiActiveSkillExecutionResult Result;
	const auto* C = Cast<UGuLiSummonSkillConfiguration>(Configuration);
	UWorld* World = Context.Commander ? Context.Commander->GetWorld() : nullptr;
	auto* Authority = World ? World->GetSubsystem<UGuLiBattleAuthoritySubsystem>() : nullptr;
	TArray<FGuLiSoldierId> Spawned;
	if (C && Authority)
		Result.bSucceeded = Authority->SummonSoldierBatch(Context.Commander->GetTeam(), Context.SoldierId,
			uint16(C->UnitTypeId), C->Count, C->ClearanceCentimeters, C->OuterRings, C->CandidatesPerRing, Spawned, Result.Error);
	else Result.Error = TEXT("Summon authority or configuration is unavailable.");
	return Result;
}
