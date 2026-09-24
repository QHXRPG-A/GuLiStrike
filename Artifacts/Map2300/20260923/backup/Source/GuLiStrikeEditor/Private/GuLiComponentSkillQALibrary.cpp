#include "GuLiComponentSkillQALibrary.h"
#include "Battle/Framework/GuLiBattlePlayerController.h"
#include "Battle/Framework/GuLiBattleGameState.h"
#include "Commander/Framework/GuLiCommanderNetSyncComponent.h"
#include "Gameplay/CommanderSkills/GuLiCommanderSkillComponent.h"
#include "Editor.h"
#include "GameFramework/PlayerController.h"
#include "Settings/LevelEditorPlaySettings.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/StrongObjectPtr.h"
#include "EngineUtils.h"
#include "Battle/Network/Relay/GuLiWingmanRelayComponent.h"
#include "Gameplay/Ship/Build/GuLiShipBuildComponent.h"
#include "Gameplay/Ship/GuLiStrikeShip.h"
#include "Gameplay/Ship/GuLiShipMovementComponent.h"

void UGuLiComponentSkillQALibrary::SetGMPanelOpen(APlayerController* Controller, bool bOpen)
{
	auto& BattleController = *CastChecked<AGuLiBattlePlayerController>(Controller);
	if (BattleController.IsGMPanelOpen() != bOpen) BattleController.ToggleGMPanel();
}

void UGuLiComponentSkillQALibrary::FlushPlayerInput(APlayerController* Controller)
{
	Controller->FlushPressedKeys();
}

bool UGuLiComponentSkillQALibrary::StartPIE(int32 Mode, int32 Clients)
{
	if (!GEditor || GEditor->IsPlaySessionInProgress() || Mode < 0 || Mode > 2 || Clients < 1 || Clients > 4) return false;
	static TStrongObjectPtr<ULevelEditorPlaySettings> Settings;
	Settings.Reset(DuplicateObject<ULevelEditorPlaySettings>(GetDefault<ULevelEditorPlaySettings>(), GetTransientPackage()));
	Settings->SetPlayNetMode(EPlayNetMode(Mode)); Settings->SetPlayNumberOfClients(Clients);
	Settings->SetRunUnderOneProcess(true); Settings->bLaunchSeparateServer = false;
	Settings->NewWindowWidth = 640; Settings->NewWindowHeight = 360;
	FRequestPlaySessionParams Request;
	Request.EditorPlaySettings = Settings.Get(); Request.SessionDestination = EPlaySessionDestinationType::InProcess;
	Request.WorldType = EPlaySessionWorldType::PlayInEditor; Request.bAllowOnlineSubsystem = false;
	GEditor->RequestPlaySession(Request); return true;
}
bool UGuLiComponentSkillQALibrary::SelectRadius(APlayerController* Controller, FVector Center, int32 RequestId, bool bAdd)
{
	if (!Controller || !Controller->IsLocalController() || !Controller->GetWorld()->IsPlayInEditor()) return false;
	auto* Sync = Controller->FindComponentByClass<UGuLiCommanderNetSyncComponent>();
	if (!Sync || !Sync->IsSoldierStreamReady()) return false;
	FGuLiSelectionRequest Request;
	Request.Center = Center; Request.RadiusPreset = EGuLiSelectionRadiusPreset::Large;
	Request.ClientRequestId = RequestId; Request.KnownSelectionRevision = Sync->GetSelectionState().SelectionRevision;
	Request.Modifier = bAdd ? EGuLiSelectionModifier::Add : EGuLiSelectionModifier::Replace;
	TGuardValue<bool> ScriptGuard(GAllowActorScriptExecutionInEditor, false);
	Sync->SubmitSelectionRequest(Request); return true;
}
bool UGuLiComponentSkillQALibrary::SubmitSkill(UGuLiCommanderSkillComponent* Component, FGuid RequestId,
	FName GlobalSkillId, int64 SelectionRevision, bool bHasPoint, FVector Point)
{
	if (!Component || !Component->GetWorld()->IsPlayInEditor()) return false;
	auto* PC = Cast<APlayerController>(Component->GetOwner()->GetOwner());
	if (!PC || !PC->IsLocalController()) return false;
	FGuLiActiveSkillRequest Request;
	Request.RequestId = RequestId; Request.MatchEpoch = Component->GetWorld()->GetGameState<AGuLiBattleGameState>()->GetMatchEpoch();
	Request.GlobalSkillId = GlobalSkillId; Request.SelectionRevision = SelectionRevision;
	Request.bHasGroundPoint = bHasPoint; Request.GroundPoint = Point;
	TGuardValue<bool> ScriptGuard(GAllowActorScriptExecutionInEditor, false);
	Component->ServerRequestSkill(Request); return true;
}
void UGuLiComponentSkillQALibrary::PressQ(UGuLiCommanderSkillComponent* Component, bool bHasPoint, FVector Point)
{
	if (!Component || !Component->GetWorld()->IsPlayInEditor()) return;
	TGuardValue<bool> ScriptGuard(GAllowActorScriptExecutionInEditor, false);
	Component->ActivateSelectedUnits(bHasPoint, Point);
}
FString UGuLiComponentSkillQALibrary::SelectionSnapshot(APlayerController* Controller)
{
	FString Result;
	const auto Writer = TJsonWriterFactory<>::Create(&Result);
	Writer->WriteObjectStart();
	if (const auto* Sync = Controller ? Controller->FindComponentByClass<UGuLiCommanderNetSyncComponent>() : nullptr)
	{
		Writer->WriteValue(TEXT("ready"), Sync->IsSoldierStreamReady());
		Writer->WriteValue(TEXT("pending"), Sync->HasUnresolvedSelectionIntent());
		Writer->WriteValue(TEXT("revision"), Sync->GetSelectionState().SelectionRevision);
		Writer->WriteArrayStart(TEXT("members"));
		for (const auto& Cohort : Sync->GetSelectionState().Cohorts) for (auto Id : Cohort.MemberIds) Writer->WriteValue(Id.Value);
		Writer->WriteArrayEnd();
	}
	Writer->WriteObjectEnd(); Writer->Close(); return Result;
}

bool UGuLiComponentSkillQALibrary::CommitShipChoice(UGuLiShipBuildComponent* Component, FName NodeId, FString& Error)
{
	if (!Component || !Component->GetOwner()->HasAuthority() || !Component->GetWorld()->IsPlayInEditor()) return false;
	const auto State = Component->GetBuildState();
	// Python's editor callspace override also intercepts Client RPCs triggered by a server-local commit.
	TGuardValue<bool> ScriptGuard(GAllowActorScriptExecutionInEditor, false);
	const auto Result = Component->CommitConfirmedChoice(FGuid::NewGuid(), NodeId, State.MatchEpoch, State.BuildRevision);
	Error = Result.Reason;
	return Result.bCommitted;
}

FString UGuLiComponentSkillQALibrary::WingmanSnapshot(UObject* WorldContext)
{
	FString Result;
	const auto Writer = TJsonWriterFactory<>::Create(&Result);
	Writer->WriteArrayStart();
	for (TActorIterator<APlayerController> It(WorldContext->GetWorld()); It; ++It)
	{
		const auto* Relay = It->FindComponentByClass<UGuLiWingmanRelayComponent>();
		if (!Relay) continue;
		const auto& State = Relay->GetRelayState();
		Writer->WriteObjectStart();
		Writer->WriteValue(TEXT("controller"), It->GetName());
		Writer->WriteValue(TEXT("local"), It->IsLocalController());
		Writer->WriteValue(TEXT("lifecycle"), int32(State.Lease.Lifecycle));
		Writer->WriteValue(TEXT("carrier_valid"), State.LatestCarrierSource.IsValid());
		Writer->WriteValue(TEXT("config_usable"), State.AbilityConfig.IsUsableByLeaseOwner());
		Writer->WriteValue(TEXT("bootstrap_valid"), Relay->GetLastClientBootstrap().IsWellFormed());
		Writer->WriteValue(TEXT("accepted_sequence"), State.LastAcceptedCandidateSequence);
		Writer->WriteValue(TEXT("atomic_attempts"), Relay->GetListenSmokeAtomicBuildAttemptCount());
		Writer->WriteValue(TEXT("atomic_accepted"), Relay->GetListenSmokeAtomicAcceptedCount());
		Writer->WriteValue(TEXT("atomic_rejected"), Relay->GetListenSmokeAtomicRejectedCount());
		Writer->WriteValue(TEXT("atomic_reject_reason"), int32(Relay->GetListenSmokeLastAtomicResultRejectReason()));
		if (const auto* Ship = Cast<AGuLiStrikeShip>(It->GetPawn()))
		{
			const auto* Movement = Ship->FindComponentByClass<UGuLiShipMovementComponent>();
			Writer->WriteValue(TEXT("applied_loadout_revision"), Movement->GetAppliedLoadoutRevision());
		}
		Writer->WriteObjectEnd();
	}
	Writer->WriteArrayEnd(); Writer->Close(); return Result;
}
