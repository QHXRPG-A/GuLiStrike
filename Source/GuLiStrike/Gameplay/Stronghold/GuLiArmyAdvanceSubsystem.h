#pragma once
#include "CoreMinimal.h"
#include "Commander/Network/GuLiCommanderTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "Commander/Behavior/GuLiCommanderWorkTypes.h"
#include "GuLiArmyAdvanceSubsystem.generated.h"
class ANavigationData;

/** Automatic objective policy survives the barracks. Mass remains the movement/combat authority. */
UCLASS()
class GULISTRIKE_API UGuLiArmyAdvanceSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()
public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;
	virtual void Tick(float Dt) override;
	virtual TStatId GetStatId() const override;
	void RegisterBatch(EGuLiTeam Team, TConstArrayView<FGuLiSoldierId> Soldiers, int32 SourceTerritory);
	void ActivateTaskMember(EGuLiTeam Team, FGuLiSoldierId Soldier, int32 SourceTerritory);
	EGuLiCommanderWorkPhase GetBehaviorPhase(FGuLiSoldierId Soldier) const;
	EGuLiCommanderWorkResult GetBehaviorResult(FGuLiSoldierId Soldier) const;
	void SelectBehaviorTarget(FGuLiSoldierId Soldier);
	void MoveToBehaviorTarget(FGuLiSoldierId Soldier);
	void WaitForBehaviorCapture(FGuLiSoldierId Soldier);
	void CompleteBehaviorStage(FGuLiSoldierId Soldier);
	void RejectBehaviorTarget(FGuLiSoldierId Soldier);
	void WaitForBehaviorTarget(FGuLiSoldierId Soldier);
private:
	struct FAdvanceGroup
	{
		EGuLiTeam Team;
		TArray<FGuLiSoldierId> Soldiers;
		int32 Source = INDEX_NONE;
		int32 Target = INDEX_NONE;
		TSet<int32> Unreachable;
		double NextDecisionTime = 0;
		EGuLiCommanderWorkPhase Phase = EGuLiCommanderWorkPhase::AdvanceSelecting;
		EGuLiCommanderWorkResult Result = EGuLiCommanderWorkResult::None;
		FVector Center = FVector::ZeroVector;
		FVector Approach = FVector::ZeroVector;
		uint64 PendingPlan = 0;
		uint32 PendingPlanEpoch = 0;
		bool bPlanCommitted = false;
		bool bMoving = false;
		bool bLocked = false;
	};
	FAdvanceGroup* FindGroup(FGuLiSoldierId Soldier);
	const FAdvanceGroup* FindGroup(FGuLiSoldierId Soldier) const;
	void ObserveGroup(FAdvanceGroup& Group);
	TArray<FAdvanceGroup> Groups;
	uint32 DecisionCursor = 0;
	int32 RemainingMoveQueries = 4;
	TArray<EGuLiTeam> ObservedOwners;
	UFUNCTION() void OnNavigationGenerated(ANavigationData* Data);
};
