#pragma once
#include "CoreMinimal.h"
#include "Commander/Network/GuLiCommanderTypes.h"
#include "Subsystems/WorldSubsystem.h"
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
private:
	struct FAdvanceGroup
	{
		EGuLiTeam Team;
		TArray<FGuLiSoldierId> Soldiers;
		int32 Source = INDEX_NONE;
		int32 Target = INDEX_NONE;
		TSet<int32> Unreachable;
		double NextDecisionTime = 0;
	};
	TArray<FAdvanceGroup> Groups;
	uint32 DecisionCursor = 0;
	TArray<EGuLiTeam> ObservedOwners;
	UFUNCTION() void OnNavigationGenerated(ANavigationData* Data);
};
