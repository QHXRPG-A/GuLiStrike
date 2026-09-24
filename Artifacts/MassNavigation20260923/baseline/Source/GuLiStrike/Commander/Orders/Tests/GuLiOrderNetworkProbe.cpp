// Opt-in real-network regression driver; never runs in an ordinary match.
#include "CoreMinimal.h"
#if !UE_BUILD_SHIPPING
#include "Commander/Framework/GuLiCommanderNetSyncComponent.h"
#include "Commander/Framework/GuLiCommanderPlayerController.h"
#include "Commander/Orders/GuLiUnitTaskSubsystem.h"
#include "Commander/Mass/GuLiBattleAuthoritySubsystem.h"
#include "Commander/Network/GuLiSoldierStateReplicator.h"
#include "Commander/Presentation/GuLiCommanderPresentationActor.h"
#include "Commander/Presentation/GuLiCommanderCameraPawn.h"
#include "Commander/UI/GuLiCommanderHUDWidget.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "GameFramework/SpringArmComponent.h"
#include "InputKeyEventArgs.h"
#include "UnrealClient.h"
#include "Misc/App.h"
#include "Battle/Framework/GuLiBattlePlayerState.h"
#include "Gameplay/Building/GuLiBuildingProductionComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/NetDriver.h"
#include "EngineUtils.h"
#include "Containers/Ticker.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"

namespace GuLiOrderNetworkProbe
{
struct FRun
{
	bool Client = false, Finished = false, SawTwo = false;
	FString Path;
	double Started = FPlatformTime::Seconds(), Ready = 0, Next = 0;
	int32 Stage = 0; uint32 Sequence = 0, Generation = 0, NextCommand = 500000;
	TWeakObjectPtr<UWorld> World;
	TWeakObjectPtr<AGuLiCommanderPlayerController> PC;
	TWeakObjectPtr<AGuLiCommanderPresentationActor> Presentation;
	TWeakObjectPtr<AGuLiSoldierStateReplicator> Roster;
	TArray<FGuLiSoldierId> Units;
	FVector Origin;
	FVector SavedCamera;
	float SavedZoom = 0, SavedYaw = 0;
	uint32 SequenceAfterKey = 0;
	FGuLiUnitTaskCommand Replay;
	TSharedRef<FJsonObject> Checks = MakeShared<FJsonObject>();
	TArray<FString> Errors;
	TArray<TSharedPtr<FJsonValue>> Samples;
	void Check(const TCHAR* Name, bool Value) { Checks->SetBoolField(Name,Value); if (!Value) Errors.Add(Name); }
	void Finish(const FString& Error = {})
	{
		if (!Error.IsEmpty()) Errors.Add(Error);
		auto Report=MakeShared<FJsonObject>(); Report->SetBoolField(TEXT("passed"),Errors.IsEmpty());
		Report->SetObjectField(TEXT("checks"),Checks); Report->SetNumberField(TEXT("stages"),Stage);
		TArray<TSharedPtr<FJsonValue>> Values; for (const auto& E:Errors) Values.Add(MakeShared<FJsonValueString>(E));
		Report->SetArrayField(TEXT("errors"),Values);
		Report->SetArrayField(TEXT("samples"),Samples);
		FString Json; FJsonSerializer::Serialize(Report,TJsonWriterFactory<>::Create(&Json));
		FFileHelper::SaveStringToFile(Json,*Path,FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
		Finished=true; FPlatformMisc::RequestExit(false);
	}
	bool FindWorld()
	{
		for (const auto& Context:GEngine->GetWorldContexts())
		{
			auto* W=Context.World(); if (!W || W->WorldType!=EWorldType::Game || !W->HasBegunPlay() || (W->GetNetMode()==NM_Client)!=Client) continue;
			World=W;
			if (!Client) return W->GetSubsystem<UGuLiBattleAuthoritySubsystem>()->HasSpawnedAuthorityPopulation();
			for (auto It=W->GetPlayerControllerIterator();It;++It)
				if (auto* C=Cast<AGuLiCommanderPlayerController>(It->Get()); C && C->IsLocalController()) PC=C;
			for (TActorIterator<AGuLiCommanderPresentationActor> It(W);It;++It) Presentation=*It;
			for (TActorIterator<AGuLiSoldierStateReplicator> It(W);It;++It) Roster=*It;
			return PC.IsValid() && Presentation.IsValid() && Roster.IsValid() && PC->CanIssueCommanderOrders();
		}
		return false;
	}
	void Select(int32 Index)
	{
		FTransform Pose; Presentation->TryGetPresentedSoldierTransform(Units[Index],Pose);
		FGuLiSelectionRequest R; R.Kind=EGuLiSelectionKind::Point; R.ClientRequestId=100000+Stage*10+Index;
		R.SeedSoldierId=Units[Index]; R.RayOrigin=Pose.GetLocation()+FVector(0,0,10000); R.RayDirection=FVector(0,0,-1);
		PC->GetCommanderNetSyncComponent()->SubmitOrderedSelection(R);
	}
	void Submit(EGuLiTaskDisposition How, FVector Target = FVector::ZeroVector)
	{
		FGuLiUnitTaskCommand C; C.CommandId=NextCommand++; C.SelectionRevision=1; C.Disposition=How; C.Target=Target;
		auto* Sync=PC->GetCommanderNetSyncComponent(); Sequence=Sync->NextOrderedSequence; Generation=Sync->GetConnectionGeneration(); Replay=C;
		Sync->SubmitOrderedTask(C);
	}
	void Key(FKey Key, EInputEvent Event)
	{ PC->InputKey(FInputKeyEventArgs::CreateSimulated(Key, Event, Event == IE_Released ? 0 : 1)); }
	void Screenshot(const TCHAR* Suffix)
	{
		if (FApp::CanEverRender()) FScreenshotRequest::RequestScreenshot(FPaths::ChangeExtension(Path, Suffix), true, false);
	}
	bool Tick(float)
	{
		if (Finished) return false;
		const double Now=FPlatformTime::Seconds();
		if (Now-Started>120) { Finish(TEXT("timeout")); return false; }
		if (!FindWorld()) return true;
		if (!Ready)
		{
			Ready=Now; Next=Now+3;
			if (!Client)
			{
				for (TActorIterator<AActor> It(World.Get());It;++It)
					if (auto* Production=It->FindComponentByClass<UGuLiBuildingProductionComponent>()) Production->SetComponentTickEnabled(false);
				UE_LOG(LogTemp,Display,TEXT("OrderProbe server ready"));
			}
		}
		if (!Client)
		{
			if (Now>=Next)
			{
				Next=Now+2;
				for (auto It=World->GetPlayerControllerIterator();It;++It) if (auto* C=Cast<AGuLiCommanderPlayerController>(It->Get()))
				{
					auto* S=C->GetCommanderNetSyncComponent(); auto Snap=MakeShared<FJsonObject>();
					Snap->SetNumberField(TEXT("sequence"),S->LastOrderedSequence); Snap->SetNumberField(TEXT("summary_count"),S->TaskSummaries.Num());
					Snap->SetNumberField(TEXT("group_count"),S->ControlGroupCounts.Num()); Snap->SetBoolField(TEXT("tick_enabled"),S->IsComponentTickEnabled());
					Snap->SetBoolField(TEXT("dedicated_tick"),S->PrimaryComponentTick.bAllowTickOnDedicatedServer);
					Samples.Add(MakeShared<FJsonValueObject>(Snap));
				}
			}
			const auto* Driver=World->GetNetDriver(); if (Driver && Driver->ClientConnections.Num()>=2) SawTwo=true;
			if (SawTwo && Driver && Driver->ClientConnections.IsEmpty())
			{
				Check(TEXT("two_independent_connections"),true);
				auto* Authority=World->GetSubsystem<UGuLiBattleAuthoritySubsystem>();
				auto* Tasks=World->GetSubsystem<UGuLiUnitTaskSubsystem>();
				TArray<FGuLiSoldierStateItem> Soldiers; Authority->BuildSoldierStateSnapshot(Soldiers);
				int32 Consumed=0, Retained=0; bool Stable=true;
				for (const auto& Soldier:Soldiers) if (Soldier.IsAlive())
				{
					const auto* Before=Tasks->FindState(FGuLiTaskUnitId::Soldier(Soldier.SoldierId)); if (!Before) continue;
					const bool Used=Before->AutomaticBehaviors.ContainsByPredicate([](const auto& G){ return G.Lifetime==EGuLiTaskLifetime::InitialOnce && G.bConsumed; });
					if (Used) ++Consumed; else ++Retained;
					const bool WasStopped=Before->bStopped;
					Tasks->RegisterSoldiers(Soldier.Team,MakeArrayView(&Soldier.SoldierId,1));
					const auto* After=Tasks->FindState(FGuLiTaskUnitId::Soldier(Soldier.SoldierId));
					Stable &= After && After->bStopped==WasStopped && Used==After->AutomaticBehaviors.ContainsByPredicate([](const auto& G){ return G.Lifetime==EGuLiTaskLifetime::InitialOnce && G.bConsumed; });
				}
				Check(TEXT("registration_and_disconnect_do_not_restore_initial_grants"),Stable && Consumed>0);
				Check(TEXT("other_members_keep_initial_eligibility"),Retained>0);
				Finish(); return false;
			}
			return true;
		}
		if (Now<Next) return true; Next=Now+2;
		auto* Sync=PC->GetCommanderNetSyncComponent(); const auto& Views=Sync->GetTaskSummaries();
		auto Snapshot=MakeShared<FJsonObject>(); Snapshot->SetNumberField(TEXT("stage"),Stage); Snapshot->SetNumberField(TEXT("summaries"),Views.Num());
		Snapshot->SetNumberField(TEXT("cohorts"),Sync->SelectionState.Cohorts.Num()); Snapshot->SetNumberField(TEXT("group_counts"),Sync->ControlGroupCounts.Num());
		Snapshot->SetStringField(TEXT("feedback"),Sync->GetLastTaskFeedback());
		if (!Views.IsEmpty()) { Snapshot->SetNumberField(TEXT("members"),Views[0].UnitCount); Snapshot->SetNumberField(TEXT("tasks"),Views[0].Tasks.Num()); Snapshot->SetBoolField(TEXT("stopped"),Views[0].bStopped); }
		Samples.Add(MakeShared<FJsonValueObject>(Snapshot));
		auto Stopped=[&]() { return Views.Num()==1 && Views[0].UnitCount==1 && Views[0].bStopped; };
		auto Count=[&](int32 I) { const auto& G=Sync->GetControlGroupCounts(); return G.IsValidIndex(I)?G[I]:-1; };
		if (Stage==0)
		{
			for (const auto& Unit:Roster->GetItems()) if (Unit.IsAlive() && Unit.Team==PC->GetPlayerState<AGuLiBattlePlayerState>()->GetTeam())
			{
				FTransform Pose; if (!Presentation->TryGetPresentedSoldierTransform(Unit.SoldierId,Pose)) continue;
				if (Units.IsEmpty()) Origin=Pose.GetLocation(); Units.Add(Unit.SoldierId); if (Units.Num()==2) break;
			}
			if (Units.Num()<2) { Units.Reset(); return true; }
			Select(0); Submit(EGuLiTaskDisposition::Stop); Sync->SubmitControlGroup(1,true,false,false,false);
		}
		else if (Stage==1)
		{
			Check(TEXT("ordered_select_then_stop"),Stopped()); Check(TEXT("group_created"),Count(1)==1);
			Check(TEXT("per_member_queue_admission_feedback"), Sync->GetLastCommandAck().CohortResults.Num() == 1
				&& Sync->GetLastCommandAck().CohortResults[0].IsMemberAccepted(0));
			Submit(EGuLiTaskDisposition::Replace,Origin+FVector(30000,0,0));
			Submit(EGuLiTaskDisposition::Append,Origin+FVector(30000,10000,0));
			Submit(EGuLiTaskDisposition::Append,Origin+FVector(0,10000,0));
			Select(1);
			// Preserve the previous sequence to replay the third append on the real channel.
			FGuLiUnitTaskCommand Stop; Stop.CommandId=NextCommand++; Stop.SelectionRevision=1; Stop.Disposition=EGuLiTaskDisposition::Stop;
			Sync->SubmitOrderedTask(Stop); Sync->SubmitControlGroup(2,true,false,false,false);
		}
		else if (Stage==2)
		{
			Check(TEXT("selection_change_did_not_redirect_stop"),Stopped());
			Sync->SubmitControlGroup(1,false,false,false,false);
			Sync->ServerOrderedTask(Replay,Sequence,Generation);
		}
		else if (Stage==3)
		{
			Check(TEXT("three_appends_preserved_after_selection_and_replay"),Views.Num()==1 && Views[0].Tasks.Num()==3 && !Views[0].bStopped);
			Check(TEXT("initial_task_not_recoverable"),Views.Num()==1 && Views[0].RecoverableTasks.IsEmpty());
			Sync->SubmitControlGroup(2,false,true,false,false);
			Screenshot(TEXT("queue.png"));
		}
		else if (Stage==4)
		{
			Check(TEXT("overlapping_groups"),Count(1)==1 && Count(2)==2);
			Sync->SubmitControlGroup(3,true,false,true,true);
		}
		else if (Stage==5)
		{
			Check(TEXT("steal_removes_only_selected_members"),Count(1)==0 && Count(2)==1 && Count(3)==1);
			Sync->SubmitControlGroup(1,false,false,false,false); Submit(EGuLiTaskDisposition::Stop);
		}
		else if (Stage==6)
		{
			Check(TEXT("empty_group_recall_keeps_selection"),Stopped());
			FGuLiUnitTaskCommand Invalid; Invalid.CommandId=NextCommand++; Invalid.SelectionRevision=1;
			Invalid.Kind=EGuLiUnitTaskKind::Special; Invalid.SpecialTaskId=99999; Sync->SubmitOrderedTask(Invalid);
		}
		else if (Stage==7)
		{
			Check(TEXT("invalid_command_does_not_release_stop"),Stopped());
			Submit(EGuLiTaskDisposition::Append,Origin+FVector(10000,0,0));
		}
		else if (Stage==8)
		{
			Check(TEXT("valid_command_releases_stop_without_initial_restore"),Views.Num()==1 && !Views[0].bStopped && Views[0].RecoverableTasks.IsEmpty());
			Submit(EGuLiTaskDisposition::Stop);
		}
		else if (Stage==9)
		{
			Check(TEXT("final_stop_persists"),Stopped());
			auto* Camera = PC->GetPawn<AGuLiCommanderCameraPawn>();
			if (!Camera) { Finish(TEXT("missing camera")); return false; }
			Camera->SetActorTickEnabled(false);
			Camera->SetCameraDebugEnabled(true);
			TArray<UUserWidget*> Widgets; UWidgetBlueprintLibrary::GetAllWidgetsOfClass(World.Get(), Widgets, UGuLiCommanderHUDWidget::StaticClass(), false);
			bool FoundPanel = false;
			for (auto* Widget : Widgets)
			{
				auto* HUD = CastChecked<UGuLiCommanderHUDWidget>(Widget);
				if (!HUD->TaskText || !HUD->FocusButton || !HUD->StopButton) continue;
				FoundPanel = HUD->TaskText->GetText().ToString().Contains(TEXT("持续停止"));
				HUD->FocusButton->OnClicked.Broadcast();
			}
			Check(TEXT("hud_task_panel_and_stop_status"), FoundPanel);
			const FVector Expected = PC->FindConfirmedSelectionCenter();
			Check(TEXT("unit_info_focus_button"), FVector::Dist2D(Camera->GetActorLocation(), Expected) < 1 || Camera->GetCameraDebugSnapshot().bFootprintClamped);
			SavedCamera = Camera->GetActorLocation();
			Key(EKeys::LeftControl, IE_Pressed); Key(EKeys::F5, IE_Pressed);
		}
		else if (Stage==10)
		{
			Check(TEXT("ctrl_f5_saves_scene_position"), PC->SceneBookmarks[0].IsSet() && FVector::Dist2D(*PC->SceneBookmarks[0], SavedCamera) < 1);
			Key(EKeys::F5, IE_Released); Key(EKeys::LeftControl, IE_Released);
			auto* Camera = PC->GetPawn<AGuLiCommanderCameraPawn>();
			Camera->JumpToWorldLocation(SavedCamera + FVector(25000,15000,0));
			Camera->AddYawInput(.1f); Camera->AddZoomInput(1); Camera->Tick(.1f);
			SavedZoom = Camera->GetCameraDebugSnapshot().DesiredArmLength;
			SavedYaw = Camera->GetActorRotation().Yaw;
			Key(EKeys::S, IE_Pressed);
		}
		else if (Stage==11)
		{
			Check(TEXT("s_key_reaches_stop_scheduler"), Stopped());
			SequenceAfterKey = Sync->NextOrderedSequence;
			Key(EKeys::S, IE_Repeat); Key(EKeys::F5, IE_Pressed);
		}
		else if (Stage==12)
		{
			Check(TEXT("keyboard_repeat_does_not_submit_again"), Sync->NextOrderedSequence == SequenceAfterKey);
			auto* Camera = PC->GetPawn<AGuLiCommanderCameraPawn>();
			Check(TEXT("f5_recalls_position_preserving_zoom_and_yaw"), FVector::Dist2D(Camera->GetActorLocation(), SavedCamera) < 1
				&& FMath::IsNearlyEqual(SavedZoom, Camera->GetCameraDebugSnapshot().DesiredArmLength)
				&& FMath::IsNearlyEqual(SavedYaw, Camera->GetActorRotation().Yaw));
			Key(EKeys::S, IE_Released); Key(EKeys::F5, IE_Released); Key(EKeys::Q, IE_Pressed);
			Screenshot(TEXT("stopped.png"));
		}
		else if (Stage==13)
		{
			Key(EKeys::Q, IE_Released); Check(TEXT("q_does_not_release_persistent_stop"), Stopped()); Finish(); return false;
		}
		++Stage; return true;
	}
};
void Start(const TArray<FString>& Args,UWorld*)
{
	if (Args.Num()!=2) return;
	auto Run=MakeShared<FRun>(); Run->Client=Args[0]==TEXT("client"); Run->Path=FPaths::ConvertRelativePathToFull(Args[1]);
	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Run](float Dt){return Run->Tick(Dt);}));
}
FAutoConsoleCommandWithWorldAndArgs Probe(TEXT("gs.Commander.OrderProbe"),TEXT("Opt-in task regression: server|client report.json"),FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Start));
}
#endif
