#include "Gameplay/CommanderSkills/GuLiSummonSkill.h"
#include "Battle/Framework/GuLiBattlePlayerState.h"
#include "Commander/Mass/GuLiBattleAuthoritySubsystem.h"
#include "Gameplay/Data/GuLiCommanderDataSubsystem.h"
#include "Engine/World.h"
#include "Network/GuLiHybridManifest.h"
#include "Network/Private/GuLiHybridObjectConversions.h"
#include "GuLiNetObjectBatch.h"

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
	UWorld* World = Context.GetExecutionWorld();
	auto* Authority = World ? World->GetSubsystem<UGuLiBattleAuthoritySubsystem>() : nullptr;
    if (World)
    {
        auto* H=World->GetSubsystem<UGuLiHybridWorldSubsystem>();FGuLiHybridObjectKey Key;FGuLiHybridObjectState O;TArray<FVector> Locations;
        if (!C || !Authority || !H || !H->ResolveTarget(Context.Source,Key) || !H->GetObject(Key,O) || !H->HasSimulation(Key)
            || !Authority->PlanSummonPlacement(Context.GetExecutionTeam(),Context.SoldierId,uint16(C->UnitTypeId),C->Count,C->ClearanceCentimeters,C->OuterRings,C->CandidatesPerRing,Locations,Result.Error)) return Result;
        std::vector<GuLiNet::ObjectBatch::Placement> Placements;
        for (const auto& P:Locations) Placements.push_back({P.X,P.Y,P.Z,float(Context.SourceTransform.Rotator().Yaw)});
        const auto Payload=GuLiNet::ObjectBatch::Encode(Placements);
        const auto Event=FGuLiHybridManifest::MakeStableKey(TEXT("/Game/GuLiStrike/Summon"),EGuLiHybridObjectKind::BusinessWork,
            Context.RequestId.ToString(EGuidFormats::Digits)+TEXT("/")+Key.StableId.ToString(EGuidFormats::Digits)).StableId;
        const FName Rule(*(TEXT("summon/")+O.Definition.ToString()+TEXT("/")+Context.SkillId.ToString().ToLower()));
        Result.EffectId=H->SubmitCapability(Key,Event,Rule,O.RelationRevision,MakeArrayView(Payload.data(),int32(Payload.size())),O.Team);
        Result.bSucceeded=Result.EffectId.IsValid();if (!Result.bSucceeded) Result.Error=TEXT("Summon batch could not enter the transaction queue");return Result;
    }
	Result.Error=TEXT("Summon world is unavailable.");
	return Result;
}
