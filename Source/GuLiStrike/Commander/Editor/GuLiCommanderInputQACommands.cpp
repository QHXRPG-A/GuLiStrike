// Copyright Epic Games, Inc. All Rights Reserved.
#include "GuLiStrike.h"
#if WITH_EDITOR
#include "Commander/Framework/GuLiCommanderNetSyncComponent.h"
#include "Commander/Framework/GuLiCommanderPlayerController.h"
#include "Commander/UI/GuLiCommanderHUDWidget.h"
#include "Components/Button.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Editor.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Application/SlateUser.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "InputKeyEventArgs.h"
#include "Layout/WidgetPath.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "TimerManager.h"
#include "UObject/UObjectIterator.h"

namespace GuLiCommanderSlateInputQA
{
using EMode = EGuLiCommanderToolMode;
using ERadius = EGuLiSelectionRadiusPreset;
struct FRun;
TSharedPtr<FRun> ActiveRun;

// One callback per world frame lets the real PlayerInput process each key state.
struct FRun : TSharedFromThis<FRun>
{
	TWeakObjectPtr<UWorld> World;
	TWeakObjectPtr<AGuLiCommanderPlayerController> PC;
	TWeakObjectPtr<UGuLiCommanderHUDWidget> HUD;
	TWeakPtr<SWidget> OldFocus;
	FTimerHandle Timer;
	FDelegateHandle EndPIEHandle;
	TSet<FKey> HeldKeys;
	TSharedRef<FJsonObject> Report = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> Observations;
	FString Output;
	ERadius OldRadius = ERadius::Small;
	EMode OldMode = EMode::Select;
	uint32 User = 0, SelectionRevision = 0;
	int32 Step = 0, CheckCount = 0;
	bool bPassed = true;

	TSharedRef<FJsonObject> Observe(const FString& Action)
	{
		auto Row = MakeShared<FJsonObject>();
		Row->SetStringField(TEXT("action"), Action);
		Row->SetNumberField(TEXT("frame"), static_cast<double>(GFrameCounter));
		if (PC.IsValid())
		{
			Row->SetNumberField(TEXT("radius_m"), GuLiCommanderProtocol::GetSelectionRadiusCentimeters(PC->GetSelectionRadiusPreset()) / 100.0f);
			Row->SetStringField(TEXT("mode"), PC->GetCommanderToolMode() == EMode::Select ? TEXT("select") : TEXT("move"));
		}
		const auto Focus = FSlateApplication::Get().GetUserFocusedWidget(User);
		Row->SetStringField(TEXT("focus_widget"), Focus ? Focus->GetTypeAsString() : TEXT("none"));
		Observations.Add(MakeShared<FJsonValueObject>(Row));
		return Row;
	}
	void Key(const FKey InKey, const bool bDown, const bool bCleanupOnly = false)
	{
		if (bDown) HeldKeys.Add(InKey); else HeldKeys.Remove(InKey);
		const FModifierKeysState Mods(HeldKeys.Contains(EKeys::LeftShift), false, false, false, false, false, false, false, false);
		const FKeyEvent Event(InKey, Mods, User, false, 0, 0);
		auto& App = FSlateApplication::Get();
		const bool bHandled = bDown ? App.ProcessKeyDownEvent(Event) : App.ProcessKeyUpEvent(Event);
		// Only aborted-run cleanup may bypass focus, to avoid leaving the PC with a held key.
		if (bCleanupOnly && !bDown && PC.IsValid()) PC->InputKey(FInputKeyEventArgs(nullptr, Event.GetInputDeviceId(), InKey, IE_Released, Event.GetEventTimestamp()));
		auto Row = Observe((bDown ? TEXT("down ") : TEXT("up ")) + InKey.ToString());
		Row->SetBoolField(TEXT("handled"), bHandled); Row->SetBoolField(TEXT("cleanup_only"), bCleanupOnly);
	}
	void Click(const FName Name)
	{
		UButton* Button = HUD.IsValid() ? Cast<UButton>(HUD->GetWidgetFromName(Name)) : nullptr;
		const auto Slate = Button ? Button->GetCachedWidget() : TSharedPtr<SWidget>();
		FWidgetPath Path;
		auto& App = FSlateApplication::Get();
		if (!Slate || !Button->GetIsEnabled() || !App.GeneratePathToWidgetUnchecked(Slate.ToSharedRef(), Path))
		{
			Observe(TEXT("button path unavailable: ") + Name.ToString())->SetBoolField(TEXT("passed"), false);
			bPassed = false; return;
		}
		const FGeometry& Geometry = Path.Widgets.Last().Geometry;
		const FVector2D Position = Geometry.LocalToAbsolute(Geometry.GetLocalSize() * 0.5f);
		TSet<FKey> Buttons;
		const FPointerEvent MoveEvent(User, FSlateApplication::CursorPointerIndex, Position, Position, Buttons, FKey(), 0.0f, FModifierKeysState());
		// GeneratePathToWidgetUnchecked produces a layout/focus path with NO virtual
		// pointer entries. Slate pointer routing indexes one entry per widget. Use the
		// same weak-to-pointer-path conversion as UWidgetInteractionComponent.
		FWidgetPath PointerPath;
		if (FWeakWidgetPath(Path).ToWidgetPath(PointerPath, FWeakWidgetPath::EInterruptedPathHandling::ReturnInvalid, &MoveEvent)
			!= FWeakWidgetPath::EPathResolutionResult::Live || !PointerPath.IsValid()
			|| PointerPath.Widgets.Num() != Path.Widgets.Num() || PointerPath.GetLastWidget() != Slate.ToSharedRef())
		{
			Observe(TEXT("pointer path unavailable: ") + Name.ToString())->SetBoolField(TEXT("passed"), false);
			bPassed = false; return;
		}
		// SButton requires hover on mouse-up; use normal Slate movement before the click.
		App.RoutePointerMoveEvent(PointerPath, MoveEvent, false);
		Buttons.Add(EKeys::LeftMouseButton);
		const bool bDown = App.RoutePointerDownEvent(PointerPath, FPointerEvent(User, FSlateApplication::CursorPointerIndex, Position, Position, Buttons, EKeys::LeftMouseButton, 0.0f, FModifierKeysState())).IsEventHandled();
		Buttons.Reset();
		const bool bUp = App.RoutePointerUpEvent(PointerPath, FPointerEvent(User, FSlateApplication::CursorPointerIndex, Position, Position, Buttons, EKeys::LeftMouseButton, 0.0f, FModifierKeysState())).IsEventHandled();
		auto Row = Observe(TEXT("click ") + Name.ToString());
		Row->SetNumberField(TEXT("pointer_path_nodes"), PointerPath.Widgets.Num());
		Row->SetBoolField(TEXT("pointer_down_handled"), bDown);
		Row->SetBoolField(TEXT("pointer_up_handled"), bUp);
		bPassed &= bDown && bUp;
	}
	void Check(const TCHAR* Name, const ERadius Radius, const EMode Mode = EMode::Select)
	{
		const bool bMatch = PC->GetSelectionRadiusPreset() == Radius && PC->GetCommanderToolMode() == Mode
			&& PC->GetCommanderNetSyncComponent()->GetSelectionState().SelectionRevision == SelectionRevision;
		Observe(Name)->SetBoolField(TEXT("passed"), bMatch);
		bPassed &= bMatch; ++CheckCount;
	}
	void Finish(const FString& Reason = FString())
	{
		const TSharedRef<FRun> KeepAlive = AsShared();
		if (World.IsValid()) World->GetTimerManager().ClearTimer(Timer);
		FEditorDelegates::PrePIEEnded.Remove(EndPIEHandle);
		for (const FKey InKey : {EKeys::Add, EKeys::Equals, EKeys::LeftShift}) if (HeldKeys.Contains(InKey)) Key(InKey, false, true);
		auto& App = FSlateApplication::Get();
		if (const auto SlateUser = App.GetUser(User)) SlateUser->ReleaseCapture(FSlateApplication::CursorPointerIndex);
		if (PC.IsValid())
		{
			PC->SetSelectionRadiusPreset(OldRadius);
			PC->ActivateSelectionTool();
			if (OldMode == EMode::Move) PC->ArmMoveTool();
		}
		const auto PreviousFocus = OldFocus.Pin();
		const bool bRestoredFocus = PreviousFocus ? App.SetUserFocus(User, PreviousFocus) : (App.ClearUserFocus(User), true);
		Report->SetBoolField(TEXT("passed"), bPassed && Reason.IsEmpty() && CheckCount == 10);
		Report->SetStringField(TEXT("reason"), Reason);
		Report->SetNumberField(TEXT("check_count"), CheckCount);
		Report->SetBoolField(TEXT("synthesized_keys_released"), HeldKeys.IsEmpty());
		Report->SetBoolField(TEXT("previous_focus_restored"), bRestoredFocus);
		Report->SetArrayField(TEXT("observations"), Observations);
		FString Json;
		FJsonSerializer::Serialize(Report, TJsonWriterFactory<>::Create(&Json));
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(Output), true);
		const bool bSaved = FFileHelper::SaveStringToFile(Json, *Output, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
		UE_LOG(LogGuLiStrike, Display, TEXT("Slate input QA passed=%d saved=%d report=%s reason=%s"), bPassed && Reason.IsEmpty() && CheckCount == 10, bSaved, *Output, *Reason);
		ActiveRun.Reset();
	}
	void EndPIE(bool) { Finish(TEXT("PIE ended before completion")); }
	void Advance()
	{
		if (!PC.IsValid() || !HUD.IsValid() || !HUD->IsInViewport() || !PC->CanIssueCommanderOrders()) { Finish(TEXT("Runtime sources became unavailable")); return; }
		switch (Step++)
		{
		case 0: Click(TEXT("BTN_Cmd_Select")); break;
		case 1: Check(TEXT("select click"), ERadius::Small); Key(EKeys::Add, true); break;
		case 2: Check(TEXT("Add 80 to 200"), ERadius::Medium); Key(EKeys::Add, false); break;
		case 3: Key(EKeys::LeftShift, true); break;
		case 4: Key(EKeys::Equals, true); break;
		case 5: Check(TEXT("Shift+= 200 to 450"), ERadius::Large); Key(EKeys::Equals, false); break;
		case 6: Key(EKeys::LeftShift, false); break;
		case 7: Key(EKeys::Add, true); break;
		case 8: Check(TEXT("Add 450 to 80"), ERadius::Small); Key(EKeys::Add, false); break;
		case 9: Key(EKeys::Equals, true); break;
		case 10: Check(TEXT("bare Equals unchanged"), ERadius::Small); Key(EKeys::Equals, false); break;
		case 11: Click(TEXT("BTN_Cmd_Move")); break;
		case 12: Check(TEXT("move click"), ERadius::Small, EMode::Move); Key(EKeys::Add, true); break;
		case 13: Check(TEXT("move ignores Add"), ERadius::Small, EMode::Move); Key(EKeys::Add, false); break;
		case 14: Key(EKeys::LeftShift, true); break;
		case 15: Key(EKeys::Equals, true); break;
		case 16: Check(TEXT("move ignores Shift+="), ERadius::Small, EMode::Move); Key(EKeys::Equals, false); break;
		case 17: Key(EKeys::LeftShift, false); break;
		case 18: Click(TEXT("BTN_Cmd_Select")); break;
		case 19: Check(TEXT("return to select by click"), ERadius::Small); Key(EKeys::Add, true); break;
		case 20: Check(TEXT("Add after select click"), ERadius::Medium); Key(EKeys::Add, false); break;
		default: Finish(); return;
		}
		if (!bPassed) Finish(TEXT("Observed input result differed from the expected state"));
	}
};

void Start(const TArray<FString>& Args)
{
	UWorld* World = GEditor ? GEditor->PlayWorld : nullptr;
	auto* PC = World ? Cast<AGuLiCommanderPlayerController>(World->GetFirstPlayerController()) : nullptr;
	if (ActiveRun || Args.Num() != 1 || !IsInGameThread() || !FSlateApplication::IsInitialized()
		|| !World || World->GetNetMode() != NM_Standalone || World->IsPaused() || !PC || !PC->IsLocalController()
		|| !PC->HasAuthority() || !PC->CanIssueCommanderOrders() || !PC->GetCommanderNetSyncComponent()
		|| PC->GetCommanderNetSyncComponent()->GetSelectionState().Cohorts.IsEmpty())
	{ UE_LOG(LogGuLiStrike, Error, TEXT("Slate QA requires one output path, no active run, and an unpaused standalone PIE Commander with a live selection.")); return; }
	FString Output = Args[0].TrimQuotes();
	if (FPaths::IsRelative(Output)) { UE_LOG(LogGuLiStrike, Error, TEXT("Slate QA requires an absolute outputs/*.json path.")); return; }
	FPaths::NormalizeFilename(Output); FPaths::CollapseRelativeDirectories(Output);
	if (!FPaths::IsUnderDirectory(Output, FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("outputs"))) || !FPaths::GetExtension(Output).Equals(TEXT("json"), ESearchCase::IgnoreCase))
	{ UE_LOG(LogGuLiStrike, Error, TEXT("Slate QA output must be JSON beneath Project/outputs.")); return; }
	UGuLiCommanderHUDWidget* HUD = nullptr;
	for (TObjectIterator<UGuLiCommanderHUDWidget> It; It; ++It) if (It->GetWorld() == World && It->GetOwningPlayer() == PC && It->IsInViewport()) { HUD = *It; break; }
	auto& App = FSlateApplication::Get();
	const uint32 User = App.GetUserIndexForKeyboard();
	const auto SlateUser = App.GetUser(User);
	const FModifierKeysState Mods = App.GetModifierKeys();
	if (!HUD || !SlateUser || SlateUser->HasAnyCapture() || SlateUser->IsDragDropping() || App.GetActiveModalWindow()
		|| !App.GetPressedMouseButtons().IsEmpty() || Mods.IsShiftDown() || Mods.IsControlDown() || Mods.IsAltDown() || Mods.IsCommandDown()
		|| PC->IsInputKeyDown(EKeys::Add) || PC->IsInputKeyDown(EKeys::Equals) || PC->IsInputKeyDown(EKeys::LeftShift))
	{ UE_LOG(LogGuLiStrike, Error, TEXT("Slate QA needs the live HUD and no modal window, drag, pointer capture or held input.")); return; }
	ActiveRun = MakeShared<FRun>();
	FRun& Run = *ActiveRun;
	Run.World = World; Run.PC = PC; Run.HUD = HUD; Run.Output = Output; Run.User = User;
	Run.OldFocus = App.GetUserFocusedWidget(User); Run.OldRadius = PC->GetSelectionRadiusPreset(); Run.OldMode = PC->GetCommanderToolMode();
	Run.SelectionRevision = PC->GetCommanderNetSyncComponent()->GetSelectionState().SelectionRevision;
	Run.Report->SetStringField(TEXT("scope"), TEXT("Actual viewport Slate button paths, pointer down/up, focused key down/up and normal PlayerInput ticks. No OS injection; direct widget paths bypass coordinate hit testing and platform mouse preprocessors."));
	Run.Report->SetStringField(TEXT("world"), World->GetPathName()); Run.Report->SetStringField(TEXT("hud"), HUD->GetPathName());
	Run.Report->SetBoolField(TEXT("os_input_injected"), false);
	PC->SetSelectionRadiusPreset(ERadius::Small);
	if (!PC->ArmMoveTool()) { Run.Finish(TEXT("Could not establish initial move mode")); return; }
	Run.EndPIEHandle = FEditorDelegates::PrePIEEnded.AddSP(ActiveRun.ToSharedRef(), &FRun::EndPIE);
	FTimerManagerTimerParameters TimerParameters; TimerParameters.bLoop = true; TimerParameters.bMaxOncePerFrame = true;
	World->GetTimerManager().SetTimer(Run.Timer, FTimerDelegate::CreateSP(ActiveRun.ToSharedRef(), &FRun::Advance), 0.08f, TimerParameters);
}
FAutoConsoleCommand Command(TEXT("gs.Commander.QA.SlateKeys"), TEXT("Standalone PIE only: SlateKeys \"absolute Project/outputs/report.json\". Requires selection; restores tool/radius/focus."), FConsoleCommandWithArgsDelegate::CreateStatic(&Start));
}
#endif
