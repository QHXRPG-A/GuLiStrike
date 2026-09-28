#pragma once
#include "CoreMinimal.h"
#include "GuLiCommanderOperationTypes.generated.h"

// Keep the existing serialized values. New lifecycle operations are appended.
UENUM()
enum class EGuLiCommanderBehaviorStep : uint8
{
	Wait, CancelPending, ReplaceMove, RunTask, TakeManual, TakeAutomatic,
	MiningSelect, MiningMove, MiningExtract, MiningFactory, MiningReturn, MiningEnter, MiningUnload, MiningExit,
	MiningFinish, MiningRetry, MiningFail, MiningComplete,
	ConstructionMove, ConstructionWork,
	AdvanceSelect, AdvanceMove, AdvanceCapture, AdvanceComplete, AdvanceReject, AdvanceWait,
	MiningReposition, ConstructionReserve, ConstructionRetry,
	StartOrder, ObserveOrder, FinishOrder
};

UENUM()
enum class EGuLiCommanderControlState : uint8 { Normal, SafeExit, Stopped, ReplacementMove };

enum class EGuLiCommanderOperationResult : uint8 { None, Queued, Deferred, Applied, Failed, Stale };

/** Authority-only identity. An exited StateTree task cannot submit work for its successor. */
struct FGuLiCommanderOperationToken
{
	uint64 Version = 0;
	uint32 ExecutionId = 0;
	uint64 Serial = 0;
	bool IsValid() const { return Serial != 0; }
	bool operator==(const FGuLiCommanderOperationToken& Other) const
	{ return Version == Other.Version && ExecutionId == Other.ExecutionId && Serial == Other.Serial; }
};

struct FGuLiCommanderOperationReceipt
{
	FGuLiCommanderOperationToken Token;
	EGuLiCommanderOperationResult Result = EGuLiCommanderOperationResult::None;
};
