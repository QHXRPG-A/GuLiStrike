#pragma once
#include "CoreMinimal.h"
#include "GuLiCommanderWorkTypes.generated.h"

/** Authority-only observations; these enums are not replicated. */
UENUM()
enum class EGuLiCommanderWorkPhase : uint8
{
	Any, MiningIdle, MiningMoving, MiningExtracting, MiningReturning, MiningEntering, MiningUnloading, MiningExiting,
	ConstructionPrepared, ConstructionMoving, ConstructionWorking, AdvanceSelecting, AdvanceMoving, AdvanceCapturing, AdvanceWaiting,
	MiningWaitingPosition, MiningReserved, MiningWaitingPath, ConstructionWaitingPosition, ConstructionReserved, ConstructionWaitingPath
};

UENUM()
enum class EGuLiCommanderWorkResult : uint8
{
	Any, None, Running, TargetReady, NoTarget, CanMine, TargetLost, OutOfRange, Full, Depleted,
	FactoryReady, FactoryLost, AtFactory, Entered, Unloaded, Exited, Arrived, Complete, Failed, WaitingPosition, WaitingPath,
	FactoryUnreachable
};

enum class EGuLiMiningBehaviorAction : uint8
{
	SelectTarget, MoveToTarget, Extract, SelectFactory, ReturnToFactory, EnterFactory, Unload, ExitFactory,
	FinishCycle, Retry, Fail, Complete, Reposition
};
