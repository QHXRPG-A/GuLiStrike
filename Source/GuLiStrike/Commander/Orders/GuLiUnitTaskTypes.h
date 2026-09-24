#pragma once
#include "CoreMinimal.h"
#include "Commander/Network/GuLiCommanderTypes.h"
#include "GameplayTagContainer.h"
#include "GuLiUnitTaskTypes.generated.h"

class APawn;
class AGuLiBattlePlayerState;
class UGuLiCommanderStateTreeComponent;
struct FGuLiSharedMoveIntent;

UENUM()
enum class EGuLiTaskLifetime : uint8 { InitialOnce, Persistent };
UENUM()
enum class EGuLiTaskDisposition : uint8 { Replace, Append, Stop };
UENUM()
enum class EGuLiUnitTaskKind : uint8 { Move, Special, ReturnToFactory, Transit };
UENUM()
enum class EGuLiTaskStatus : uint8 { Waiting, Running, WorkUnitComplete, Completed, Failed, WaitingSafeExit, Stopped };

/** IDs never repeat within a match; generation prevents a future respawn from inheriting consumed grants. */
USTRUCT()
struct FGuLiTaskUnitId
{
	GENERATED_BODY()
	UPROPERTY() uint32 Id = 0;
	UPROPERTY() uint32 Generation = 1;
	UPROPERTY() bool bActor = false;
	bool IsValid() const { return Id != 0 && Generation != 0; }
	friend bool operator==(const FGuLiTaskUnitId& A, const FGuLiTaskUnitId& B) { return A.Id == B.Id && A.Generation == B.Generation && A.bActor == B.bActor; }
	friend uint32 GetTypeHash(const FGuLiTaskUnitId& A) { return HashCombine(HashCombine(::GetTypeHash(A.Id), ::GetTypeHash(A.Generation)), ::GetTypeHash(A.bActor)); }
	static FGuLiTaskUnitId Soldier(FGuLiSoldierId Id) { FGuLiTaskUnitId V; V.Id = Id.Value; return V; }
	static FGuLiTaskUnitId Actor(FGuLiControllableActorId Id) { FGuLiTaskUnitId V; V.Id = Id.Value; V.bActor = true; return V; }
};

USTRUCT()
struct FGuLiUnitTaskCommand
{
	GENERATED_BODY()
	/** Authority-only context; intentionally absent from reflection and network serialization. */
	TSharedPtr<FGuLiSharedMoveIntent> SharedMoveIntent;
	UPROPERTY() uint32 CommandId = 0;
	UPROPERTY() uint32 SelectionRevision = 0;
	UPROPERTY() EGuLiTaskDisposition Disposition = EGuLiTaskDisposition::Replace;
	UPROPERTY() EGuLiUnitTaskKind Kind = EGuLiUnitTaskKind::Move;
	UPROPERTY() int32 SpecialTaskId = 0;
	UPROPERTY() FVector_NetQuantize Target = FVector::ZeroVector;
	UPROPERTY() uint32 BuildingId = 0;
	UPROPERTY() uint16 ClusterId = 0;
	UPROPERTY() FName TerritoryId;
	/** UI ground-move intent must not turn into contextual construction at the destination. */
	UPROPERTY() bool bGroundMoveOnly = false;
	/** Optional UI target-mode constraint, validated before any unit is interrupted. */
	UPROPERTY() FName TargetIntent;
	bool IsWellFormed() const;
};

USTRUCT()
struct FGuLiUnitTaskView
{
	GENERATED_BODY()
	UPROPERTY() FGuLiUnitTaskCommand Command;
	UPROPERTY() EGuLiTaskStatus Status = EGuLiTaskStatus::Waiting;
	UPROPERTY() bool bAutomatic = false;
	UPROPERTY() FString DisplayName;
	/** Presentation target resolved by authority. Zero coordinates alone do not imply a valid target. */
	UPROPERTY() bool bHasLocation = false;
	UPROPERTY() FVector_NetQuantize Location = FVector::ZeroVector;
};

/** Identical queues share one owner-only view; member lists are not replicated in the summary. */
USTRUCT()
struct FGuLiUnitTaskSummary
{
	GENERATED_BODY()
	UPROPERTY() int32 UnitCount = 0;
	UPROPERTY() int32 ManualTaskCount = 0;
	UPROPERTY() TArray<FGuLiUnitTaskView> Tasks;
	UPROPERTY() bool bStopped = false;
	UPROPERTY() bool bWaitingSafeExit = false;
	UPROPERTY() FString Error;
	UPROPERTY() TArray<FString> RecoverableTasks;
};

USTRUCT()
struct FGuLiCommanderControlGroup
{
	GENERATED_BODY()
	UPROPERTY() TArray<FGuLiSoldierId> Soldiers;
	UPROPERTY() TArray<FGuLiControllableActorId> Actors;
};

struct FGuLiTaskUnitContext
{
	FGuLiTaskUnitId Unit;
	EGuLiTeam Team = EGuLiTeam::Unassigned;
	uint16 UnitTypeId = 0;
	TWeakObjectPtr<APawn> Pawn;
	FVector Location = FVector::ZeroVector;
	int32 SourceTerritory = INDEX_NONE;
};

struct FGuLiTaskExecution
{
	FGuLiUnitTaskCommand Command;
	uint64 Version = 0;
	uint32 ExecutionId = 0;
	uint32 WorkSerial = 0;
	int32 CurrentObjective = INDEX_NONE;
	bool bAutomatic = false;
	bool bStarted = false;
	bool bYieldRequested = false;
	bool bPlanning = false;
	bool bWaitingForTarget = false;
	EGuLiTaskStatus Status = EGuLiTaskStatus::Waiting;
	FString Error;
};

/** Pure lifecycle state is also used by the authority regression tests. */
struct FGuLiCommanderAutomaticBehavior
{
	int32 BehaviorId = 0;
	EGuLiTaskLifetime Lifetime = EGuLiTaskLifetime::Persistent;
	bool bConsumed = false;
	void ConsumeIfInitial() { bConsumed |= Lifetime == EGuLiTaskLifetime::InitialOnce; }
};

struct FGuLiUnitTaskState
{
	FGuLiTaskUnitContext Context;
	TWeakObjectPtr<AGuLiBattlePlayerState> Owner;
	TArray<FGuLiCommanderAutomaticBehavior> AutomaticBehaviors;
	TWeakObjectPtr<UGuLiCommanderStateTreeComponent> ActorTree;
	bool bBehaviorStepDone = true;
	bool bUnregisterPending = false;
	uint64 LastBehaviorRequestRound = 0;
	TArray<FGuLiUnitTaskCommand> Queue;
	TOptional<FGuLiTaskExecution> Active;
	/** New move being prepared while Active continues along its committed route. Server only. */
	TOptional<FGuLiTaskExecution> PendingMove;
	uint64 Version = 0;
	bool bStopped = false;
	bool bCancelPending = false;
	double NextAutomaticTime = 0;
	FString Error;
	void ConsumeInitialBehaviors() { for (auto& Grant : AutomaticBehaviors) Grant.ConsumeIfInitial(); }
	int32 ManualTaskCount() const { return Queue.Num() + (PendingMove.IsSet() || (Active.IsSet() && !Active->bAutomatic && !bCancelPending) ? 1 : 0); }
};
