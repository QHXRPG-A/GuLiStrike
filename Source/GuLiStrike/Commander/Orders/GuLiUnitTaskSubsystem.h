#pragma once
#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Commander/Orders/GuLiUnitTaskTypes.h"
#include "GuLiUnitTaskSubsystem.generated.h"
class UGuLiSpecialTaskCatalog;
class UGuLiBuildingLifecycleComponent;

/** Authority-only queue owner. Business components cannot grant or resurrect tasks. */
UCLASS()
class GULISTRIKE_API UGuLiUnitTaskSubsystem final : public UTickableWorldSubsystem
{
	GENERATED_BODY()
public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	void RegisterActor(APawn& Pawn);
	void RegisterSoldiers(EGuLiTeam Team, TConstArrayView<FGuLiSoldierId> Soldiers, int32 SourceTerritory = INDEX_NONE);
	void UnregisterActor(FGuLiControllableActorId Id);
	bool Submit(AGuLiBattlePlayerState& Owner, const FGuLiCommanderSelectionState& Selection,
		const FGuLiUnitTaskCommand& Command, FString& OutMessage, int32& OutAccepted, int32& OutRejected,
		TSet<FGuLiTaskUnitId>* OutAcceptedUnits = nullptr);
	bool BuildContextCommand(const FGuLiCommanderSelectionState& Selection, FGuLiUnitTaskCommand& Command) const;
	void BuildSummary(const FGuLiCommanderSelectionState& Selection, TArray<FGuLiUnitTaskSummary>& Out) const;
	bool HasState(FGuLiTaskUnitId Unit) const { return States.Contains(Unit); }
	bool MayAdvance(FGuLiSoldierId Unit) const;
	bool WantsAdvanceYield(FGuLiSoldierId Unit) const;
	void NotifyAdvanceStage(FGuLiSoldierId Unit);
	void UpdateAdvanceTarget(FGuLiSoldierId Unit, int32 Territory, const FVector& Target);
	const FGuLiUnitTaskState* FindState(FGuLiTaskUnitId Unit) const { return States.Find(Unit); }
	/** Already-validated policy boundary, exercised by production admission and automation. */
	static bool Admit(FGuLiUnitTaskState& State, const FGuLiUnitTaskCommand& Command, int32 Capacity);
	/** Pure admission policy; callers refresh an active move's terminal status first. */
	static bool ReuseMove(FGuLiUnitTaskState& State, const FGuLiUnitTaskCommand& Command, float RadiusCentimeters);
	/** External control invalidates a pending replacement without restarting it after release. */
	void CancelPendingMove(FGuLiTaskUnitId Unit);
private:
	TMap<FGuLiTaskUnitId, FGuLiUnitTaskState> States;
	// Stable IDs are never reused within this World. Keep a tombstone after death/unregistration.
	TSet<FGuLiTaskUnitId> GrantedUnits;
	UPROPERTY(Transient) TObjectPtr<UGuLiSpecialTaskCatalog> Catalog;
	uint32 NextExecutionId = 1;
	double Accumulator = 0;
	struct FMoveBatch
	{
		TWeakObjectPtr<AGuLiBattlePlayerState> Owner;
		uint32 Id = 0;
		TMap<FGuLiTaskUnitId,uint64> Versions;
		TSet<FGuLiTaskUnitId> Replacements;
		FGuLiCommanderSelectionState Selection;
	};
	TArray<FMoveBatch> Planning;
	void Grant(FGuLiUnitTaskState& State);
	bool RefreshContext(FGuLiUnitTaskState& State) const;
	bool Validate(const FGuLiUnitTaskState& State, const FGuLiUnitTaskCommand& Command, FString& Error) const;
	bool Cancel(FGuLiUnitTaskState& State);
	void DiscardPendingMove(FGuLiUnitTaskState& State);
	void LogMoveFailure(const FGuLiUnitTaskState& State, const FGuLiTaskExecution& Task, const TCHAR* Reason) const;
	bool StartVehicleMove(FGuLiUnitTaskState& State, FGuLiTaskExecution& Task);
	void Advance(FGuLiUnitTaskState& State, double Now);
	EGuLiTaskStatus Start(FGuLiUnitTaskState& State);
	EGuLiTaskStatus Poll(FGuLiUnitTaskState& State);
	void TickMoveBatches();
	void StartMoveBatches();
	void WakeAutomaticBuilders(UGuLiBuildingLifecycleComponent& Building);
	uint32 AllocateExecutionId();
};
