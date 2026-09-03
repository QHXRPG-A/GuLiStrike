// Copyright Epic Games, Inc. All Rights Reserved.

#include "Battle/Framework/GuLiBattleGameMode.h"

#include "Battle/Framework/GuLiBattleGameState.h"
#include "Battle/Framework/GuLiBattlePlayerController.h"
#include "Battle/Framework/GuLiBattlePlayerState.h"
#include "Battle/Network/GuLiPlayerNetSyncComponent.h"
#include "Battle/Network/Relay/GuLiWingmanRelayComponent.h"
#include "Battle/Relay/GuLiWingmanRelayAuthorityRegistry.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GuLiStrike.h"

AGuLiBattleGameMode::AGuLiBattleGameMode()
{
	GameStateClass = AGuLiBattleGameState::StaticClass();
	PlayerStateClass = AGuLiBattlePlayerState::StaticClass();
	PlayerControllerClass = AGuLiBattlePlayerController::StaticClass();
	// 未配置的角色不可偷偷生成默认飞行 Pawn；出生路径只认 RolePawnClasses。
	DefaultPawnClass = nullptr;
	InitialRolePriority = {EGuLiCommanderRole::Commander, EGuLiCommanderRole::Ground, EGuLiCommanderRole::Air};
	bUseSeamlessTravel = true;
}

void AGuLiBattleGameMode::BeginPlay()
{
	Super::BeginPlay();
	if (HasAuthority())
	{
		if (AGuLiBattleGameState* BattleState = GetGameState<AGuLiBattleGameState>())
		{
			BattleState->InitializeServerMatchState();
		}
	}
}

void AGuLiBattleGameMode::GenericPlayerInitialization(AController* Controller)
{
	APlayerController* PlayerController = Cast<APlayerController>(Controller);
	if (HasAuthority() && IsValid(PlayerController))
	{
		ExitingPlayers.Remove(TWeakObjectPtr<APlayerController>(PlayerController));
		CancelPlayerRespawn(*PlayerController);
		// 此时引擎已恢复/复制旧 PlayerState，角色必须先于 Pawn 类查询确定。
		AssignPlayerRole(*PlayerController);
	}

	Super::GenericPlayerInitialization(Controller);

	if (HasAuthority() && IsValid(PlayerController))
	{
		if (UGuLiPlayerNetSyncComponent* NetSync = PlayerController->FindComponentByClass<UGuLiPlayerNetSyncComponent>())
		{
			NetSync->EnsureServerConnectionBootstrap();
		}
		TryAssignWaitingWingmanGroups();
	}
}

void AGuLiBattleGameMode::AssignPlayerRole(APlayerController& PlayerController)
{
	AGuLiBattlePlayerState* PlayerState = PlayerController.GetPlayerState<AGuLiBattlePlayerState>();
	AGuLiBattleGameState* BattleState = GetGameState<AGuLiBattleGameState>();
	if (!PlayerState || !BattleState)
	{
		MovePlayerToObserver(PlayerController, TEXT("Battle GameState/PlayerState is missing"));
		return;
	}

	// 登录可以早于 BeginPlay；初始化幂等，不能因每次登录重建战局标识。
	BattleState->InitializeServerMatchState();
	PlayerState->EnsureServerPlayerGuid();
	uint8 SlotIndex = AGuLiBattlePlayerState::InvalidSlotIndex;
	EGuLiTeam Team = EGuLiTeam::Unassigned;
	EGuLiCommanderRole AssignedRole = EGuLiCommanderRole::Observer;
	if (!BattleState->ClaimRoleSlotWithPriority(
		PlayerState->GetPlayerGuid(), PlayerState->GetBattleSlotIndex(), InitialRolePriority,
		SlotIndex, Team, AssignedRole))
	{
		MovePlayerToObserver(PlayerController, TEXT("No gameplay role slot is available"));
		return;
	}

	// PostLogin / seamless 初始化在进入本函数前已经计数；恢复观察者转为玩法角色时也要迁移计数桶。
	// 必须先移除旧桶再清原生观察者标志，否则 RemovePlayerControllerFromPlayerCount 会减错桶。
	if (MustSpectate(&PlayerController))
	{
		RemovePlayerControllerFromPlayerCount(&PlayerController);
		if (GetWorld()->IsInSeamlessTravel() || PlayerController.HasClientLoadedCurrentWorld())
		{
			++NumPlayers;
		}
		else
		{
			++NumTravellingPlayers;
		}
	}
	PlayerState->SetIsOnlyASpectator(false);
	PlayerState->SetIsSpectator(false);
	PlayerState->SetServerRoleAssignment(Team, AssignedRole, SlotIndex);
	BattleState->SetRoleSlotBattleReady(SlotIndex, PlayerState->GetPlayerGuid(), false);
	BattleState->SetRoleSlotSyncReady(SlotIndex, PlayerState->GetPlayerGuid(), false);
	if (!ResolveConfiguredPawnClass(AssignedRole))
	{
		MovePlayerToObserver(PlayerController, TEXT("RolePawnClasses entry is missing, abstract or invalid"));
	}
}

UClass* AGuLiBattleGameMode::ResolveConfiguredPawnClass(const EGuLiCommanderRole AssignedRole) const
{
	const TSubclassOf<APawn>* ConfiguredClass = RolePawnClasses.Find(AssignedRole);
	UClass* PawnClass = ConfiguredClass ? ConfiguredClass->Get() : nullptr;
	return IsValid(PawnClass)
		&& PawnClass->IsChildOf(APawn::StaticClass())
		&& !PawnClass->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists)
		? PawnClass : nullptr;
}

bool AGuLiBattleGameMode::HasCurrentRoleSlot(const AGuLiBattlePlayerState& PlayerState) const
{
	const AGuLiBattleGameState* BattleState = GetGameState<AGuLiBattleGameState>();
	if (!BattleState || !PlayerState.GetPlayerGuid().IsValid())
	{
		return false;
	}

	for (const FGuLiCommanderRoleSlotState& Slot : BattleState->GetRoleSlots())
	{
		if (Slot.SlotIndex == PlayerState.GetBattleSlotIndex())
		{
			return Slot.bOccupied && Slot.PlayerGuid == PlayerState.GetPlayerGuid()
				&& Slot.Team == PlayerState.GetTeam() && Slot.Role == PlayerState.GetBattleRole();
		}
	}
	return false;
}

bool AGuLiBattleGameMode::CanSpawnPlayer(APlayerController& PlayerController) const
{
	return HasAuthority() && !bStopSpawning && GetWorld()
		&& !HasMatchEnded() && GetMatchState() != MatchState::Aborted
		&& IsValid(&PlayerController) && !PlayerController.IsActorBeingDestroyed()
		&& PlayerController.GetWorld() == GetWorld()
		&& !ExitingPlayers.Contains(TWeakObjectPtr<APlayerController>(&PlayerController));
}

void AGuLiBattleGameMode::MovePlayerToObserver(APlayerController& PlayerController, const TCHAR* Reason)
{
	CancelPlayerRespawn(PlayerController);
	AGuLiBattlePlayerState* PlayerState = PlayerController.GetPlayerState<AGuLiBattlePlayerState>();
	UE_LOG(LogGuLiStrike, Warning, TEXT("Battle spawn rejected: Player=%s Role=%d Reason=%s"),
		*PlayerController.GetName(), PlayerState ? static_cast<int32>(PlayerState->GetBattleRole()) : -1, Reason);

	if (PlayerState)
	{
		if (AGuLiBattleGameState* BattleState = GetGameState<AGuLiBattleGameState>())
		{
			BattleState->ReleaseRoleSlot(PlayerState->GetBattleSlotIndex(), PlayerState->GetPlayerGuid());
		}
		PlayerState->SetServerObserver();
	}

	// 先撤销身份并解除占有，再清旧 Pawn，不把配置错误或角色退出当作战斗死亡排队。
	if (APawn* OldPawn = PlayerController.GetPawn())
	{
		PlayerController.UnPossess();
		OldPawn->Destroy();
	}
	if (PlayerController.GetPlayerState<APlayerState>())
	{
		// StartSpectatingOnly 只改 Controller/PS，不维护 AGameMode 的人数。
		// 在改变原生标志前迁移一次；重复失败或已是观察者时不能再次减玩家数。
		if (!MustSpectate(&PlayerController))
		{
			PlayerSwitchedToSpectatorOnly(&PlayerController);
		}
		PlayerController.StartSpectatingOnly();
	}
	else
	{
		PlayerController.ChangeState(NAME_Spectating);
	}
}

UClass* AGuLiBattleGameMode::GetDefaultPawnClassForController_Implementation(AController* InController)
{
	APlayerController* PlayerController = Cast<APlayerController>(InController);
	const AGuLiBattlePlayerState* PlayerState = PlayerController
		? PlayerController->GetPlayerState<AGuLiBattlePlayerState>() : nullptr;
	if (!PlayerState || PlayerState->GetBattleRole() == EGuLiCommanderRole::Observer
		|| PlayerState->GetBattleRole() == EGuLiCommanderRole::Unassigned)
	{
		return nullptr;
	}

	// UE 无缝初始化会先为恢复后的 PS 查询 PlayerStart，再进入 GenericPlayerInitialization。
	// 此时新 GameState 尚未认领席位；这里只解析 Pawn 类，不能清掉待恢复的角色/席位。
	return ResolveConfiguredPawnClass(PlayerState->GetBattleRole());
}

void AGuLiBattleGameMode::RestartPlayer(AController* NewPlayer)
{
	APlayerController* PlayerController = Cast<APlayerController>(NewPlayer);
	if (!PlayerController || !CanSpawnPlayer(*PlayerController)
		|| PendingRespawns.Contains(TWeakObjectPtr<APlayerController>(PlayerController)))
	{
		return;
	}

	const AGuLiBattlePlayerState* PlayerState = PlayerController->GetPlayerState<AGuLiBattlePlayerState>();
	if (!PlayerState || PlayerState->GetBattleRole() == EGuLiCommanderRole::Observer
		|| PlayerState->GetBattleRole() == EGuLiCommanderRole::Unassigned)
	{
		return;
	}
	// 真正出生前仍须验证本局席位归属；提前查询类用于选出生点不代表获得出生权限。
	if (!HasCurrentRoleSlot(*PlayerState) || !GetDefaultPawnClassForController_Implementation(PlayerController))
	{
		MovePlayerToObserver(*PlayerController, TEXT("Pawn configuration or role-slot ownership is invalid"));
		return;
	}

	// 其他服务器路径已占有 Pawn 时，不因重复死亡通知/重启请求再生成一份。
	if (IsValid(PlayerController->GetPawn()))
	{
		return;
	}

	Super::RestartPlayer(PlayerController);
	if (!IsValid(PlayerController->GetPawn()))
	{
		MovePlayerToObserver(*PlayerController, TEXT("RestartPlayer failed to spawn or possess the configured Pawn"));
	}
}

void AGuLiBattleGameMode::SchedulePlayerRespawn(APlayerController& PlayerController)
{
	if (!bRespawnPlayers || !CanSpawnPlayer(PlayerController) || !IsMatchInProgress()
		|| PendingRespawns.Contains(TWeakObjectPtr<APlayerController>(&PlayerController)))
	{
		return;
	}

	const AGuLiBattlePlayerState* PlayerState = PlayerController.GetPlayerState<AGuLiBattlePlayerState>();
	const AGuLiBattleGameState* BattleState = GetGameState<AGuLiBattleGameState>();
	if (!PlayerState || !BattleState || !HasCurrentRoleSlot(*PlayerState)
		|| PlayerState->GetBattleRole() == EGuLiCommanderRole::Observer)
	{
		return;
	}

	const TWeakObjectPtr<APlayerController> WeakController(&PlayerController);
	const FGuid ExpectedGuid = PlayerState->GetPlayerGuid();
	const uint8 ExpectedSlot = PlayerState->GetBattleSlotIndex();
	const uint32 ExpectedEpoch = BattleState->GetMatchEpoch();
	FTimerDelegate RespawnDelegate = FTimerDelegate::CreateWeakLambda(this,
		[this, WeakController, ExpectedGuid, ExpectedSlot, ExpectedEpoch]()
		{
			PendingRespawns.Remove(WeakController);
			APlayerController* Controller = WeakController.Get();
			const AGuLiBattleGameState* CurrentState = GetGameState<AGuLiBattleGameState>();
			const AGuLiBattlePlayerState* CurrentPlayerState = Controller
				? Controller->GetPlayerState<AGuLiBattlePlayerState>() : nullptr;
			if (!Controller || !CanSpawnPlayer(*Controller) || !IsMatchInProgress()
				|| !CurrentState || CurrentState->GetMatchEpoch() != ExpectedEpoch
				|| !CurrentPlayerState || CurrentPlayerState->GetPlayerGuid() != ExpectedGuid
				|| CurrentPlayerState->GetBattleSlotIndex() != ExpectedSlot
				|| IsValid(Controller->GetPawn()))
			{
				return;
			}

			// 复活保留身份/席位，不重做角色分配；公共握手和士兵流资格各自仍按原生命周期维护。
			RestartPlayer(Controller);
		});

	FTimerHandle& Timer = PendingRespawns.Add(WeakController);
	if (FMath::IsFinite(PlayerRespawnDelaySeconds) && PlayerRespawnDelaySeconds > 0.0f)
	{
		GetWorldTimerManager().SetTimer(Timer, RespawnDelegate, PlayerRespawnDelaySeconds, false);
	}
	else
	{
		Timer = GetWorldTimerManager().SetTimerForNextTick(RespawnDelegate);
	}
}

void AGuLiBattleGameMode::CancelPlayerRespawn(APlayerController& PlayerController)
{
	if (FTimerHandle* Timer = PendingRespawns.Find(TWeakObjectPtr<APlayerController>(&PlayerController)))
	{
		if (GetWorld())
		{
			GetWorldTimerManager().ClearTimer(*Timer);
		}
		PendingRespawns.Remove(TWeakObjectPtr<APlayerController>(&PlayerController));
	}
}

void AGuLiBattleGameMode::Logout(AController* Exiting)
{
	if (HasAuthority() && Exiting)
	{
		if (APlayerController* PlayerController = Cast<APlayerController>(Exiting))
		{
			ExitingPlayers.Add(TWeakObjectPtr<APlayerController>(PlayerController));
			CancelPlayerRespawn(*PlayerController);
			if (const AGuLiBattlePlayerState* PlayerState =
				PlayerController->GetPlayerState<AGuLiBattlePlayerState>())
			{
				HandleWingmanOwnerDisconnected(*PlayerController, *PlayerState);
			}
		}
		if (const AGuLiBattlePlayerState* PlayerState = Exiting->GetPlayerState<AGuLiBattlePlayerState>())
		{
			if (AGuLiBattleGameState* BattleState = GetGameState<AGuLiBattleGameState>())
			{
				BattleState->ReleaseRoleSlot(PlayerState->GetBattleSlotIndex(), PlayerState->GetPlayerGuid());
			}
		}
	}
	Super::Logout(Exiting);
}

void AGuLiBattleGameMode::HandleWingmanOwnerDisconnected(
	APlayerController& ExitingController,
	const AGuLiBattlePlayerState& ExitingPlayerState)
{
	AGuLiBattleGameState* BattleState = GetGameState<AGuLiBattleGameState>();
	FGuLiWingmanRelayAuthorityRegistry* Registry = BattleState
		? BattleState->GetWingmanRelayAuthorityRegistry() : nullptr;
	const FGuid ExitingGuid = ExitingPlayerState.GetPlayerGuid();
	if (!Registry || !ExitingGuid.IsValid()
		|| ExitingPlayerState.GetTeam() == EGuLiTeam::Unassigned)
	{
		return;
	}

	struct FCandidate
	{
		FGuid PlayerGuid;
		TWeakObjectPtr<UGuLiWingmanRelayComponent> Transport;
		bool bNonAir = false;
	};
	TArray<FCandidate> AllCandidates;
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* Controller = It->Get();
		const AGuLiBattlePlayerState* PlayerState = Controller && Controller != &ExitingController
			? Controller->GetPlayerState<AGuLiBattlePlayerState>() : nullptr;
		UGuLiWingmanRelayComponent* Transport = Controller
			? Controller->FindComponentByClass<UGuLiWingmanRelayComponent>() : nullptr;
		if (!PlayerState || !Transport || !Transport->CanServerAttachPersistentGroup()
			|| PlayerState->GetTeam() != ExitingPlayerState.GetTeam()
			|| !PlayerState->GetPlayerGuid().IsValid()
			|| PlayerState->GetBattleRole() == EGuLiCommanderRole::Observer
			|| PlayerState->GetBattleRole() == EGuLiCommanderRole::Unassigned)
		{
			continue;
		}
		FCandidate& Candidate = AllCandidates.AddDefaulted_GetRef();
		Candidate.PlayerGuid = PlayerState->GetPlayerGuid();
		Candidate.Transport = Transport;
		// A Commander/Ground client has no Ship ASC, which exercises the intended projection-only backup path.
		Candidate.bNonAir = PlayerState->GetBattleRole() != EGuLiCommanderRole::Air;
	}
	AllCandidates.Sort([](const FCandidate& Lhs, const FCandidate& Rhs)
	{
		if (Lhs.bNonAir != Rhs.bNonAir)
		{
			return Lhs.bNonAir;
		}
		if (Lhs.PlayerGuid.A != Rhs.PlayerGuid.A) return Lhs.PlayerGuid.A < Rhs.PlayerGuid.A;
		if (Lhs.PlayerGuid.B != Rhs.PlayerGuid.B) return Lhs.PlayerGuid.B < Rhs.PlayerGuid.B;
		if (Lhs.PlayerGuid.C != Rhs.PlayerGuid.C) return Lhs.PlayerGuid.C < Rhs.PlayerGuid.C;
		return Lhs.PlayerGuid.D < Rhs.PlayerGuid.D;
	});

	TArray<FGuid> CandidateGuids;
	TMap<FGuid, TWeakObjectPtr<UGuLiWingmanRelayComponent>> TransportsByGuid;
	const bool bHasProjectionOnlyHost = AllCandidates.ContainsByPredicate(
		[](const FCandidate& Candidate) { return Candidate.bNonAir; });
	for (const FCandidate& Candidate : AllCandidates)
	{
		if (bHasProjectionOnlyHost && !Candidate.bNonAir)
		{
			continue;
		}
		CandidateGuids.Add(Candidate.PlayerGuid);
		TransportsByGuid.Add(Candidate.PlayerGuid, Candidate.Transport);
	}

	const TArray<FGuLiWingmanOwnerLossAssignment> Assignments =
		Registry->HandleOwnerDisconnected(
			ExitingGuid,
			CandidateGuids,
			GetWorld()->GetTimeSeconds());
	UGuLiWingmanRelayComponent* ExitingTransport =
		ExitingController.FindComponentByClass<UGuLiWingmanRelayComponent>();
	for (const FGuLiWingmanOwnerLossAssignment& Assignment : Assignments)
	{
		// Reliable public removal closes combat/presentation while the new owner applies the frozen cut.
		BattleState->ServerRevokeWingmanGroup(Assignment.Group);
		if (ExitingTransport)
		{
			ExitingTransport->ServerDetachPersistentGroup(Assignment.Group);
		}
		if (Assignment.Disposition == EGuLiWingmanOwnerLossDisposition::OfferStarted)
		{
			UGuLiWingmanRelayComponent* NewTransport =
				TransportsByGuid.FindRef(Assignment.NewOwnerPlayerGuid).Get();
			if (!NewTransport || !NewTransport->ServerDeliverLeaseOffer(Assignment.Group))
			{
				UE_LOG(LogGuLiStrike, Error,
					TEXT("Wingman Relay offer delivery failed: Group=%s Owner=%s"),
					*Assignment.Group.ShipInstanceId.ToString(),
					*Assignment.NewOwnerPlayerGuid.ToString());
			}
		}
	}
}

void AGuLiBattleGameMode::TryAssignWaitingWingmanGroups()
{
	AGuLiBattleGameState* BattleState = GetGameState<AGuLiBattleGameState>();
	FGuLiWingmanRelayAuthorityRegistry* Registry = BattleState
		? BattleState->GetWingmanRelayAuthorityRegistry() : nullptr;
	if (!Registry || Registry->GetAwaitingOwnerGroups().IsEmpty())
	{
		return;
	}

	struct FCandidate
	{
		FGuid PlayerGuid;
		TWeakObjectPtr<UGuLiWingmanRelayComponent> Transport;
		EGuLiTeam Team = EGuLiTeam::Unassigned;
	};
	TArray<FCandidate> Candidates;
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* Controller = It->Get();
		const AGuLiBattlePlayerState* PlayerState = Controller
			? Controller->GetPlayerState<AGuLiBattlePlayerState>() : nullptr;
		UGuLiWingmanRelayComponent* Transport = Controller
			? Controller->FindComponentByClass<UGuLiWingmanRelayComponent>() : nullptr;
		if (!PlayerState || !Transport || !Transport->CanServerAttachPersistentGroup()
			|| !PlayerState->GetPlayerGuid().IsValid()
			|| PlayerState->GetTeam() == EGuLiTeam::Unassigned
			|| PlayerState->GetBattleRole() == EGuLiCommanderRole::Air
			|| PlayerState->GetBattleRole() == EGuLiCommanderRole::Observer
			|| PlayerState->GetBattleRole() == EGuLiCommanderRole::Unassigned)
		{
			continue;
		}
		FCandidate& Candidate = Candidates.AddDefaulted_GetRef();
		Candidate.PlayerGuid = PlayerState->GetPlayerGuid();
		Candidate.Transport = Transport;
		Candidate.Team = PlayerState->GetTeam();
	}
	Candidates.Sort([](const FCandidate& Lhs, const FCandidate& Rhs)
	{
		if (Lhs.PlayerGuid.A != Rhs.PlayerGuid.A) return Lhs.PlayerGuid.A < Rhs.PlayerGuid.A;
		if (Lhs.PlayerGuid.B != Rhs.PlayerGuid.B) return Lhs.PlayerGuid.B < Rhs.PlayerGuid.B;
		if (Lhs.PlayerGuid.C != Rhs.PlayerGuid.C) return Lhs.PlayerGuid.C < Rhs.PlayerGuid.C;
		return Lhs.PlayerGuid.D < Rhs.PlayerGuid.D;
	});
	TMap<FGuid, TWeakObjectPtr<UGuLiWingmanRelayComponent>> TransportsByGuid;
	for (const FCandidate& Candidate : Candidates)
	{
		TransportsByGuid.Add(Candidate.PlayerGuid, Candidate.Transport);
	}

	TSet<FGuid> ConsumedOwners;
	for (const FGuLiWingmanGroupHandle& Group : Registry->GetAwaitingOwnerGroups())
	{
		const uint8 OwnerCohort = Registry->GetGroupOwnerCohort(Group);
		TArray<FGuid> CandidateGuids;
		for (const FCandidate& Candidate : Candidates)
		{
			if (static_cast<uint8>(Candidate.Team) == OwnerCohort
				&& !ConsumedOwners.Contains(Candidate.PlayerGuid))
			{
				CandidateGuids.Add(Candidate.PlayerGuid);
			}
		}
		FGuLiWingmanOwnerLossAssignment Assignment;
		if (!Registry->AssignAwaitingGroup(
			Group, CandidateGuids, GetWorld()->GetTimeSeconds(), Assignment))
		{
			continue;
		}
		ConsumedOwners.Add(Assignment.NewOwnerPlayerGuid);
		UGuLiWingmanRelayComponent* Transport =
			TransportsByGuid.FindRef(Assignment.NewOwnerPlayerGuid).Get();
		if (Transport)
		{
			Transport->ServerDeliverLeaseOffer(Assignment.Group);
		}
	}
}

void AGuLiBattleGameMode::StopPendingRespawns()
{
	bStopSpawning = true;
	if (GetWorld())
	{
		for (TPair<TWeakObjectPtr<APlayerController>, FTimerHandle>& Entry : PendingRespawns)
		{
			GetWorldTimerManager().ClearTimer(Entry.Value);
		}
	}
	PendingRespawns.Reset();
}

void AGuLiBattleGameMode::StartToLeaveMap()
{
	StopPendingRespawns();
	Super::StartToLeaveMap();
}

void AGuLiBattleGameMode::HandleMatchHasEnded()
{
	StopPendingRespawns();
	Super::HandleMatchHasEnded();
}

void AGuLiBattleGameMode::HandleLeavingMap()
{
	StopPendingRespawns();
	Super::HandleLeavingMap();
}

void AGuLiBattleGameMode::HandleMatchAborted()
{
	StopPendingRespawns();
	Super::HandleMatchAborted();
}

void AGuLiBattleGameMode::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	StopPendingRespawns();
	Super::EndPlay(EndPlayReason);
}
