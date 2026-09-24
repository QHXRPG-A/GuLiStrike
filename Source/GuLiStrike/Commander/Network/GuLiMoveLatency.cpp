#include "Commander/Network/GuLiMoveLatency.h"
#include "Battle/Framework/GuLiBattleGameState.h"
#include "Battle/Framework/GuLiBattlePlayerState.h"
#include "Components/ActorComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "GuLiStrike.h"

namespace
{
TAutoConsoleVariable<int32> CVarMoveLatency(TEXT("guli.Commander.MoveLatencyDiagnostics"),0,
	TEXT("Trace move input/admission/planning/commit/send/apply/render-submit. Clock values are process-local."));
}
bool GuLiMoveLatency::IsEnabled() { return CVarMoveLatency.GetValueOnAnyThread() != 0; }
GuLiMoveLatency::FContext GuLiMoveLatency::Context(const UObject* Object, uint32 Command, uint32 Batch)
{
	FContext Result; Result.Command=Command; Result.Batch=Batch;
	if (!Object) return Result;
	const auto* World=Object->GetWorld();
	if (const auto* State=World ? World->GetGameState<AGuLiBattleGameState>() : nullptr) Result.Epoch=State->GetMatchEpoch();
	const auto* Player=Cast<AGuLiBattlePlayerState>(Object);
	const auto* Component=Cast<UActorComponent>(Object);
	const auto* Controller=Cast<APlayerController>(Component ? Component->GetOwner() : Object);
	if (!Player && Controller) Player=Controller->GetPlayerState<AGuLiBattlePlayerState>();
	if (Player) Result.Player=Player->GetPlayerGuid();
	return Result;
}
void GuLiMoveLatency::Record(const TCHAR* Stage, const FContext& C, int32 Members, const TCHAR* Detail)
{
	if (!IsEnabled()) return;
	UE_LOG(LogGuLiStrike,Display,TEXT("MassMoveLatency stage=%s clock=%.6f player=%s epoch=%u command=%u execution=%u plan=%llu batch=%u members=%d %s"),
		Stage,FPlatformTime::Seconds(),*C.Player.ToString(EGuidFormats::Digits),C.Epoch,C.Command,C.Execution,C.Plan,C.Batch,Members,Detail);
}
