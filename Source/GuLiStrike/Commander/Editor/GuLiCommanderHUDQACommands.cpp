// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_EDITOR

#include "Blueprint/SlateBlueprintLibrary.h"
#include "Commander/Framework/GuLiCommanderNetSyncComponent.h"
#include "Commander/Framework/GuLiCommanderPlayerController.h"
#include "Commander/Framework/GuLiCommanderPlayerState.h"
#include "Commander/Mass/GuLiBattleAuthoritySubsystem.h"
#include "Commander/Network/GuLiSoldierStateReplicator.h"
#include "Commander/UI/GuLiCommanderHUDWidget.h"
#include "Components/Button.h"
#include "Components/Image.h"
#include "Components/InputComponent.h"
#include "Components/TextBlock.h"
#include "Dom/JsonObject.h"
#include "Editor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/UObjectIterator.h"

// Explicit, local PIE-only QA. Nothing here runs at startup or in packaged games.
// Select/damage cases alter the disposable PIE match; end PIE to reset them.
namespace GuLiCommanderHUDQA
{
	void Run(const TArray<FString>& Args)
	{
		UWorld* World = GEditor ? GEditor->PlayWorld : nullptr;
		AGuLiCommanderPlayerController* PC = World
			? Cast<AGuLiCommanderPlayerController>(World->GetFirstPlayerController()) : nullptr;
		if (!World || World->GetNetMode() != NM_Standalone || !PC || !PC->HasAuthority())
		{
			UE_LOG(LogTemp, Error, TEXT("Commander HUD QA requires a standalone local PIE match."));
			return;
		}
		AGuLiCommanderPlayerState* PS = PC->GetPlayerState<AGuLiCommanderPlayerState>();
		UGuLiCommanderNetSyncComponent* Sync = PC->GetCommanderNetSyncComponent();
		UGuLiBattleAuthoritySubsystem* Authority = World->GetSubsystem<UGuLiBattleAuthoritySubsystem>();
		AGuLiSoldierStateReplicator* Replicator = nullptr;
		for (TActorIterator<AGuLiSoldierStateReplicator> It(World); It; ++It) { Replicator = *It; break; }
		UGuLiCommanderHUDWidget* HUD = nullptr;
		for (TObjectIterator<UGuLiCommanderHUDWidget> It; It; ++It)
		{
			if (It->GetWorld() == World && It->IsInViewport()) { HUD = *It; break; }
		}
		if (!PS || !Sync || !Authority || !Replicator || !HUD)
		{
			UE_LOG(LogTemp, Error, TEXT("Commander HUD QA sources are not ready."));
			return;
		}

		const FString Action = Args.Num() > 0 ? Args[0].ToLower() : TEXT("status");
		const FString EvidenceDirectory = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("outputs/commander-ring-hud-20260830"));
		FString Output = FPaths::ConvertRelativePathToFull(Args.Num() > 1 ? Args[1].TrimQuotes() : EvidenceDirectory / TEXT("hud-qa.json"));
		FPaths::CollapseRelativeDirectories(Output);
		if (!FPaths::IsUnderDirectory(Output, EvidenceDirectory) || FPaths::GetExtension(Output) != TEXT("json"))
		{
			UE_LOG(LogTemp, Error, TEXT("Commander HUD QA reports must be JSON files under %s"), *EvidenceDirectory);
			return;
		}
		TSharedRef<FJsonObject> Report = MakeShared<FJsonObject>();
		bool bSuccess = true;
		if (Action == TEXT("select") || Action == TEXT("selectmany") || Action == TEXT("clear"))
		{
			FGuLiSelectionRequest Request;
			Request.ClientRequestId = PC->AllocateEditorQASelectionRequestId();
			Request.KnownSelectionRevision = Sync->GetSelectionState().SelectionRevision;
			Request.Modifier = Action == TEXT("clear") ? EGuLiSelectionModifier::Clear : EGuLiSelectionModifier::Replace;
			Request.RadiusPreset = Action == TEXT("selectmany") ? EGuLiSelectionRadiusPreset::Large : EGuLiSelectionRadiusPreset::Small;
			bool bFound = Action == TEXT("clear");
			for (const FGuLiSoldierStateItem& Item : Replicator->GetItems())
			{
				FTransform Transform;
				if (Item.Team == PS->GetTeam() && Item.IsAlive() && Authority->TryGetSoldierTransform(Item.SoldierId, Transform))
				{
					Request.Center = Transform.GetLocation();
					bFound = true;
					break;
				}
			}
			if (bFound) { Sync->ServerRequestSelection(Request); }
			else { bSuccess = false; }
		}
		else if (Action == TEXT("kill5") || Action == TEXT("killall") || Action == TEXT("injure"))
		{
			TSet<FGuLiSoldierId> Members;
			for (const FGuLiControlCohortDescriptor& Cohort : Sync->GetSelectionState().Cohorts)
			{
				for (const FGuLiSoldierId Id : Cohort.MemberIds) { Members.Add(Id); }
			}
			const int32 Limit = Action == TEXT("kill5") ? 5 : Action == TEXT("injure") ? 1 : MAX_int32;
			int32 Changed = 0;
			for (const FGuLiSoldierStateItem& Item : Replicator->GetItems())
			{
				if (Changed < Limit && Members.Contains(Item.SoldierId) && Item.IsAlive()
					&& Authority->ApplyDamage(Item.SoldierId, Action == TEXT("injure") ? 50u : 255u))
				{
					++Changed;
				}
			}
			Report->SetNumberField(TEXT("damage_applied_count"), Changed);
			Report->SetStringField(TEXT("note"), TEXT("Read status after the next reliable snapshot; this command does not force HUD refresh."));
			bSuccess = Changed > 0;
		}
		else if (Action == TEXT("keys"))
		{
			const auto Invoke = [PC](const FInputChord& Chord)
			{
				if (!PC->InputComponent) { return false; }
				for (const FInputKeyBinding& Binding : PC->InputComponent->KeyBindings)
				{
					if (Binding.KeyEvent == IE_Pressed && Binding.Chord == Chord)
					{
						Binding.KeyDelegate.Execute(Chord.Key);
						return true;
					}
				}
				return false;
			};
			const EGuLiSelectionRadiusPreset OriginalRadius = PC->GetSelectionRadiusPreset();
			const EGuLiCommanderToolMode OriginalMode = PC->GetCommanderToolMode();
			PC->ActivateSelectionTool();
			PC->SetSelectionRadiusPreset(EGuLiSelectionRadiusPreset::Small);
			bSuccess &= Invoke(FInputChord(EKeys::Add)) && PC->GetSelectionRadiusPreset() == EGuLiSelectionRadiusPreset::Medium;
			bSuccess &= Invoke(FInputChord(EKeys::Equals, true, false, false, false)) && PC->GetSelectionRadiusPreset() == EGuLiSelectionRadiusPreset::Large;
			bSuccess &= Invoke(FInputChord(EKeys::Add)) && PC->GetSelectionRadiusPreset() == EGuLiSelectionRadiusPreset::Small;
			bSuccess &= !Invoke(FInputChord(EKeys::Equals));
			Report->SetBoolField(TEXT("plus_cycle_and_bare_equals"), bSuccess);
			const bool bMoveArmed = PC->ArmMoveTool();
			Invoke(FInputChord(EKeys::Add));
			Invoke(FInputChord(EKeys::Equals, true, false, false, false));
			const bool bMoveGate = bMoveArmed && PC->GetSelectionRadiusPreset() == EGuLiSelectionRadiusPreset::Small;
			Report->SetBoolField(TEXT("move_mode_ignores_both_plus_keys"), bMoveGate);
			bSuccess &= bMoveGate;
			UButton* SelectButton = Cast<UButton>(HUD->GetWidgetFromName(TEXT("BTN_Cmd_Select")));
			if (SelectButton) { SelectButton->OnClicked.Broadcast(); }
			const bool bAfterClick = SelectButton && !SelectButton->GetIsFocusable()
				&& PC->GetCommanderToolMode() == EGuLiCommanderToolMode::Select
				&& Invoke(FInputChord(EKeys::Add)) && PC->GetSelectionRadiusPreset() == EGuLiSelectionRadiusPreset::Medium;
			Report->SetBoolField(TEXT("plus_after_select_button_delegate"), bAfterClick);
			Report->SetStringField(TEXT("input_test_scope"), TEXT("Actual controller key bindings and button delegate; no OS input injection."));
			bSuccess &= bAfterClick;
			PC->SetSelectionRadiusPreset(OriginalRadius);
			PC->ActivateSelectionTool();
			if (OriginalMode == EGuLiCommanderToolMode::Move) { PC->ArmMoveTool(); }
		}
		else if (Action == TEXT("syncing")) { PS->SetServerSyncReady(false); }
		else if (Action == TEXT("ready")) { PS->SetServerSyncReady(true); }
		else if (Action != TEXT("status")) { bSuccess = false; }

		Report->SetStringField(TEXT("action"), Action);
		Report->SetBoolField(TEXT("success"), bSuccess);
		Report->SetNumberField(TEXT("cohort_count"), Sync->GetSelectionState().Cohorts.Num());
		Report->SetNumberField(TEXT("snapshot_revision"), Replicator->GetSnapshotRevision());
		Report->SetNumberField(TEXT("match_epoch"), Replicator->GetSnapshotMatchEpoch());
		Report->SetBoolField(TEXT("sync_ready"), PS->IsSyncReady());
		for (const TCHAR* Name : {TEXT("TXT_UnitTypeName"), TEXT("TXT_UnitTypeCount"), TEXT("TXT_UnitTypeHealth"), TEXT("TXT_UnitTypeStatus")})
		{
			if (const UTextBlock* Text = Cast<UTextBlock>(HUD->GetWidgetFromName(Name))) { Report->SetStringField(Name, Text->GetText().ToString()); }
		}
		if (const UWidget* Card = HUD->GetWidgetFromName(TEXT("C_UnitTypeCard")))
		{
			Report->SetBoolField(TEXT("card_visible"), Card->GetVisibility() != ESlateVisibility::Collapsed);
		}
		if (const UImage* Fill = Cast<UImage>(HUD->GetWidgetFromName(TEXT("I_UnitTypeHealthFill"))))
		{
			Report->SetNumberField(TEXT("health_fraction"), Fill->GetRenderTransform().Scale.X);
		}
		Report->SetBoolField(TEXT("blocking_geometry_ready"), HUD->HasValidBlockingGeometry());
		for (const TCHAR* Name : {TEXT("SB_TopStatus"), TEXT("SB_MapDesign"), TEXT("SB_DockDesign")})
		{
			if (const UWidget* Island = HUD->GetWidgetFromName(Name))
			{
				FVector2D Pixel, Viewport;
				const FGeometry& Geometry = Island->GetCachedGeometry();
				USlateBlueprintLibrary::LocalToViewport(HUD, Geometry, Geometry.GetLocalSize() * 0.5f, Pixel, Viewport);
				Report->SetBoolField(FString(Name) + TEXT("_center_blocks_world"), HUD->IsScreenPositionBlocked(Pixel));
			}
		}
		int32 ViewWidth = 0, ViewHeight = 0;
		PC->GetViewportSize(ViewWidth, ViewHeight);
		Report->SetNumberField(TEXT("viewport_width"), ViewWidth);
		Report->SetNumberField(TEXT("viewport_height"), ViewHeight);
		Report->SetBoolField(TEXT("world_center_open"), !HUD->IsScreenPositionBlocked(FVector2D(ViewWidth, ViewHeight) * 0.5f));
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(Output), true);
		FString Json;
		FJsonSerializer::Serialize(Report, TJsonWriterFactory<>::Create(&Json));
		if (!FFileHelper::SaveStringToFile(Json, *Output, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
		{
			UE_LOG(LogTemp, Error, TEXT("Commander HUD QA could not save %s"), *Output);
			return;
		}
		UE_LOG(LogTemp, Display, TEXT("Commander HUD QA %s success=%d report=%s"), *Action, bSuccess, *Output);
	}

	FAutoConsoleCommand Command(
		TEXT("gs.Commander.QA.HUD"),
		TEXT("Standalone PIE only: status|select|selectmany|kill5|injure|killall|clear|keys|syncing|ready [report.json]. End PIE to discard QA changes."),
		FConsoleCommandWithArgsDelegate::CreateStatic(&Run));
}

#endif
