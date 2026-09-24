// Copyright Epic Games, Inc. All Rights Reserved.
#include "GuLiStrike.h"
#if WITH_EDITOR
#include "Commander/Framework/GuLiCommanderNetSyncComponent.h"
#include "Commander/Framework/GuLiCommanderPlayerController.h"
#include "Commander/Network/GuLiSoldierStateReplicator.h"
#include "Commander/Presentation/GuLiCommanderPresentationActor.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Editor.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Application/SlateUser.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Layout/WidgetPath.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "TimerManager.h"
#include "Widgets/SViewport.h"
#include "Widgets/SWindow.h"

namespace GuLiCommanderSelectionPointerQA
{
UWorld* ResolveWorld(UWorld* World)
{
	return World && World->IsGameWorld() ? World : GEditor ? GEditor->PlayWorld.Get() : nullptr;
}
AGuLiCommanderPlayerController* FindLocalPC(UWorld* World)
{
	if (!World) return nullptr;
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		auto* PC = Cast<AGuLiCommanderPlayerController>(It->Get());
		if (PC && PC->IsLocalController()) return PC;
	}
	return nullptr;
}
template<typename T> T* FindActor(UWorld* World)
{
	if (World) for (TActorIterator<T> It(World); It; ++It) return *It;
	return nullptr;
}
TSharedRef<FJsonObject> VectorJson(const FVector& V)
{
	auto Object = MakeShared<FJsonObject>();
	Object->SetNumberField(TEXT("x"), V.X); Object->SetNumberField(TEXT("y"), V.Y); Object->SetNumberField(TEXT("z"), V.Z);
	return Object;
}

struct FPointerRun;
TSharedPtr<FPointerRun> ActiveRun;
struct FPointerRun : TSharedFromThis<FPointerRun>
{
	TWeakObjectPtr<UWorld> World;
	TWeakObjectPtr<AGuLiCommanderPlayerController> PC;
	TWeakPtr<SWidget> OldFocus;
	TWeakPtr<SViewport> Viewport;
	FTimerHandle Timer;
	FDelegateHandle EndPIEHandle;
	FVector StartWorld = FVector::ZeroVector, EndWorld = FVector::ZeroVector;
	FVector2D OldCursor = FVector2D::ZeroVector, LastPosition = FVector2D::ZeroVector;
	uint32 User = 0;
	int32 Step = 0;
	bool bShift = false, bAlt = false, bDrag = false, bRight = false, bButtonHeld = false;
	bool bShiftHeld = false, bAltHeld = false, bFinished = false;
	bool bKeyboardOnly = false, bKeyboardHeld = false;
	FKey KeyboardKey;

	FModifierKeysState Modifiers() const
	{
		return FModifierKeysState(bShiftHeld, false, false, false, bAltHeld, false, false, false, false);
	}
	void Key(const FKey& Key, bool bDown)
	{
		if (Key == EKeys::LeftShift) bShiftHeld = bDown;
		if (Key == EKeys::LeftAlt) bAltHeld = bDown;
		const FKeyEvent Event(Key, Modifiers(), User, false, 0, 0);
		if (bDown) FSlateApplication::Get().ProcessKeyDownEvent(Event);
		else FSlateApplication::Get().ProcessKeyUpEvent(Event);
	}
	bool Pointer(const FVector& PositionWorld, int32 Action)
	{
		const auto Widget = Viewport.Pin();
		if (!Widget || !PC.IsValid()) return false;
		FVector2D Screen;
		int32 Width = 0, Height = 0; PC->GetViewportSize(Width, Height);
		if (!PC->ProjectWorldLocationToScreen(PositionWorld, Screen, false) || Width < 1 || Height < 1
			|| Screen.X < 1 || Screen.Y < 1 || Screen.X >= Width - 1 || Screen.Y >= Height - 1) return false;
		auto& App = FSlateApplication::Get();
		const auto Window = App.FindWidgetWindow(Widget.ToSharedRef());
		if (!Window || !Window->IsVisible() || !Window->GetNativeWindow()) return false;
		FWidgetPath LayoutPath;
		if (!App.GeneratePathToWidgetUnchecked(Widget.ToSharedRef(), LayoutPath) || !LayoutPath.IsValid()) return false;
		const FGeometry& Geometry = LayoutPath.Widgets.Last().Geometry;
		const FVector2D Position = Geometry.LocalToAbsolute(FVector2D(Screen.X / Width, Screen.Y / Height) * Geometry.GetLocalSize());
		App.SetCursorPos(Position);
		TSet<FKey> Buttons;
		const FKey Button = bRight ? EKeys::RightMouseButton : EKeys::LeftMouseButton;
		if (bButtonHeld) Buttons.Add(Button);
		FPointerEvent Move(User, FSlateApplication::CursorPointerIndex, Position, LastPosition, Buttons, FKey(), 0.0f, Modifiers());
		// The platform processing path maintains hover/leave and cursor state across
		// frames. Directly routing a viewport path bypasses that state and can leave
		// FSceneViewport::CachedCursorPos invalid when the gameplay press is consumed.
		App.ProcessMouseMoveEvent(Move, false);
		LastPosition = Position;
		if (Action == 1)
		{
			Buttons.Add(Button); bButtonHeld = true;
			App.ProcessMouseButtonDownEvent(Window->GetNativeWindow(), FPointerEvent(User, FSlateApplication::CursorPointerIndex, Position, Position, Buttons, Button, 0.0f, Modifiers()));
		}
		else if (Action == 2)
		{
			Buttons.Remove(Button); bButtonHeld = false;
			App.ProcessMouseButtonUpEvent(FPointerEvent(User, FSlateApplication::CursorPointerIndex, Position, Position, Buttons, Button, 0.0f, Modifiers()));
		}
		return true;
	}
	void Finish(const FString& Reason = FString())
	{
		if (bFinished) return;
		const auto KeepAlive = AsShared(); bFinished = true;
		if (World.IsValid()) World->GetTimerManager().ClearTimer(Timer);
		FEditorDelegates::PrePIEEnded.Remove(EndPIEHandle);
		if (FSlateApplication::IsInitialized())
		{
			if (bKeyboardHeld) { Key(KeyboardKey, false); bKeyboardHeld = false; }
			if (bButtonHeld) Pointer(bDrag ? EndWorld : StartWorld, 2);
			if (bShiftHeld) Key(EKeys::LeftShift, false);
			if (bAltHeld) Key(EKeys::LeftAlt, false);
			auto& App = FSlateApplication::Get();
			if (const auto SlateUser = App.GetUser(User)) SlateUser->ReleaseCapture(FSlateApplication::CursorPointerIndex);
			App.SetCursorPos(OldCursor);
			if (const auto Focus = OldFocus.Pin()) App.SetUserFocus(User, Focus);
			else App.ClearUserFocus(User);
		}
		// Abort-only safety: a disappearing/offscreen viewport cannot route a release.
		// Normal runs always use Slate; no gameplay entry point is called here.
		if (!Reason.IsEmpty() && PC.IsValid()) PC->FlushPressedKeys();
		UE_LOG(LogGuLiStrike, Display, TEXT("Commander PointerQA finished world=%s completed=%d reason=%s. Routed Slate viewport events; no direct gameplay calls. Cursor/focus restored."),
			World.IsValid() ? *World->GetPathName() : TEXT("unavailable"), Reason.IsEmpty(), *Reason);
		ActiveRun.Reset();
	}
	void EndPIE(bool) { Finish(TEXT("PIE ended")); }
	void Advance()
	{
		if (!World.IsValid() || !PC.IsValid() || !PC->IsCommanderViewActive() || World->IsPaused())
		{ Finish(TEXT("Runtime context unavailable or paused")); return; }
		if (bKeyboardOnly)
		{
			switch (Step++)
			{
			case 0: bKeyboardHeld = true; Key(KeyboardKey, true); break;
			case 1: bKeyboardHeld = false; Key(KeyboardKey, false); break;
			default: Finish(); break;
			}
			return;
		}
		bool bOK = true;
		switch (Step++)
		{
		case 0: if (bShift) Key(EKeys::LeftShift, true); if (bAlt) Key(EKeys::LeftAlt, true); bOK = Pointer(StartWorld, 0); break;
		case 1: bOK = Pointer(StartWorld, 1); break;
		case 2: if (bDrag) bOK = Pointer(FMath::Lerp(StartWorld, EndWorld, 0.5f), 0); break;
		case 3: if (bDrag) bOK = Pointer(EndWorld, 0); break;
		case 4: bOK = Pointer(bDrag ? EndWorld : StartWorld, 2); break;
		case 5: if (bShiftHeld) Key(EKeys::LeftShift, false); if (bAltHeld) Key(EKeys::LeftAlt, false); break;
		default: Finish(); return;
		}
		if (!bOK) Finish(TEXT("World position offscreen or viewport pointer path unavailable"));
	}
};

void StartWorld(const TArray<FString>& Args, UWorld* InWorld)
{
	UWorld* World = ResolveWorld(InWorld);
	auto* PC = FindLocalPC(World);
	if (Args.Num() != 4 && Args.Num() != 7)
	{ UE_LOG(LogGuLiStrike, Error, TEXT("Usage: gs.Commander.QA.MouseWorld click|right|alt|shift|altshift|drag|shiftdrag X Y Z [EndX EndY EndZ]")); return; }
	const FString Action = Args[0].ToLower();
	const bool bDrag = Action == TEXT("drag") || Action == TEXT("shiftdrag");
	if ((bDrag && Args.Num() != 7) || (!bDrag && Args.Num() != 4)
		|| (!bDrag && Action != TEXT("click") && Action != TEXT("right") && Action != TEXT("alt") && Action != TEXT("shift") && Action != TEXT("altshift"))) return;
	FVector Start, End;
	if (!LexTryParseString(Start.X, *Args[1]) || !LexTryParseString(Start.Y, *Args[2]) || !LexTryParseString(Start.Z, *Args[3])) return;
	End = Start;
	if (bDrag && (!LexTryParseString(End.X, *Args[4]) || !LexTryParseString(End.Y, *Args[5]) || !LexTryParseString(End.Z, *Args[6]))) return;
	if (Start.ContainsNaN() || End.ContainsNaN() || ActiveRun || !PC || !World || World->IsPaused()
		|| !PC->IsCommanderViewActive() || !FSlateApplication::IsInitialized() || !World->GetGameViewport())
	{ UE_LOG(LogGuLiStrike, Error, TEXT("PointerQA needs an idle local commander game viewport, no pause, and finite coordinates.")); return; }
	auto Viewport = World->GetGameViewport()->GetGameViewportWidget();
	auto& App = FSlateApplication::Get();
	const uint32 User = App.GetUserIndexForKeyboard();
	const auto SlateUser = App.GetUser(User);
	const auto Mods = App.GetModifierKeys();
	if (!Viewport || !SlateUser || SlateUser->HasAnyCapture() || SlateUser->IsDragDropping()
		|| App.GetActiveModalWindow() || !App.GetPressedMouseButtons().IsEmpty()
		|| Mods.IsShiftDown() || Mods.IsAltDown() || Mods.IsControlDown() || Mods.IsCommandDown())
	{ UE_LOG(LogGuLiStrike, Error, TEXT("PointerQA rejected held input, capture, drag or modal window.")); return; }
	const auto Window = App.FindWidgetWindow(Viewport.ToSharedRef());
	if (!Window || !Window->IsVisible() || !Window->GetNativeWindow())
	{ UE_LOG(LogGuLiStrike, Error, TEXT("PointerQA needs its target PIE window visible; refusing input into a hidden window.")); return; }
	Window->BringToFront(true);
	ActiveRun = MakeShared<FPointerRun>();
	auto& Run = *ActiveRun;
	Run.World = World; Run.PC = PC; Run.Viewport = Viewport; Run.User = User;
	Run.OldFocus = App.GetUserFocusedWidget(User); Run.OldCursor = App.GetCursorPos(); Run.LastPosition = Run.OldCursor;
	Run.StartWorld = Start; Run.EndWorld = End; Run.bDrag = bDrag;
	Run.bShift = Action.Contains(TEXT("shift")); Run.bAlt = Action.Contains(TEXT("alt")); Run.bRight = Action == TEXT("right");
	App.SetUserFocus(User, Viewport);
	Run.EndPIEHandle = FEditorDelegates::PrePIEEnded.AddSP(ActiveRun.ToSharedRef(), &FPointerRun::EndPIE);
	FTimerManagerTimerParameters Params; Params.bLoop = true; Params.bMaxOncePerFrame = true;
	World->GetTimerManager().SetTimer(Run.Timer, FTimerDelegate::CreateSP(ActiveRun.ToSharedRef(), &FPointerRun::Advance), 0.035f, Params);
	UE_LOG(LogGuLiStrike, Display, TEXT("Commander PointerQA started action=%s world=%s start=%s end=%s"), *Action, *World->GetPathName(), *Start.ToCompactString(), *End.ToCompactString());
}

void StartSoldier(const TArray<FString>& Args, UWorld* InWorld)
{
	UWorld* World = ResolveWorld(InWorld);
	uint32 Id = 0;
	auto* Presentation = FindActor<AGuLiCommanderPresentationActor>(World);
	FGuLiSoldierId SoldierId;
	FTransform Transform;
	if (Args.Num() != 2 || !LexTryParseString(Id, *Args[1]) || !Presentation) return;
	SoldierId.Value = Id;
	if (!Presentation->TryGetPresentedSoldierTransform(SoldierId, Transform)) return;
	const FVector V = Transform.GetLocation();
	StartWorld({Args[0], FString::SanitizeFloat(V.X), FString::SanitizeFloat(V.Y), FString::SanitizeFloat(V.Z)}, World);
}

void StartKey(const TArray<FString>& Args, UWorld* InWorld)
{
	UWorld* World = ResolveWorld(InWorld);
	auto* PC = FindLocalPC(World);
	if (Args.Num() != 1 || (Args[0] != TEXT("Seven") && Args[0] != TEXT("Add")
		&& Args[0] != TEXT("One") && Args[0] != TEXT("Escape")))
	{ UE_LOG(LogGuLiStrike, Error, TEXT("Usage: gs.Commander.QA.Key Seven|Add|One|Escape")); return; }
	if (ActiveRun || !PC || !World || World->IsPaused() || !PC->IsCommanderViewActive()
		|| !FSlateApplication::IsInitialized() || !World->GetGameViewport()) return;
	auto Viewport = World->GetGameViewport()->GetGameViewportWidget();
	auto& App = FSlateApplication::Get();
	const uint32 User = App.GetUserIndexForKeyboard();
	const auto SlateUser = App.GetUser(User);
	const auto Mods = App.GetModifierKeys();
	const FKey InputKey(*Args[0]);
	if (!Viewport || !SlateUser || SlateUser->HasAnyCapture() || SlateUser->IsDragDropping()
		|| App.GetActiveModalWindow() || !App.GetPressedMouseButtons().IsEmpty()
		|| Mods.IsShiftDown() || Mods.IsAltDown() || Mods.IsControlDown() || Mods.IsCommandDown()
		|| PC->IsInputKeyDown(InputKey))
	{ UE_LOG(LogGuLiStrike, Error, TEXT("KeyQA rejected held input, capture, drag or modal window.")); return; }
	const auto Window = App.FindWidgetWindow(Viewport.ToSharedRef());
	if (!Window || !Window->IsVisible())
	{ UE_LOG(LogGuLiStrike, Error, TEXT("KeyQA needs its target PIE window visible.")); return; }
	Window->BringToFront(true);
	ActiveRun = MakeShared<FPointerRun>();
	auto& Run = *ActiveRun;
	Run.World = World; Run.PC = PC; Run.Viewport = Viewport; Run.User = User;
	Run.OldFocus = App.GetUserFocusedWidget(User); Run.OldCursor = App.GetCursorPos();
	Run.bKeyboardOnly = true; Run.KeyboardKey = InputKey;
	App.SetUserFocus(User, Viewport);
	Run.EndPIEHandle = FEditorDelegates::PrePIEEnded.AddSP(ActiveRun.ToSharedRef(), &FPointerRun::EndPIE);
	FTimerManagerTimerParameters Params; Params.bLoop = true; Params.bMaxOncePerFrame = true;
	World->GetTimerManager().SetTimer(Run.Timer, FTimerDelegate::CreateSP(ActiveRun.ToSharedRef(), &FPointerRun::Advance), 0.035f, Params);
	UE_LOG(LogGuLiStrike, Display, TEXT("Commander KeyQA started key=%s world=%s"), *Args[0], *World->GetPathName());
}

void Snapshot(const TArray<FString>& Args, UWorld* InWorld)
{
	UWorld* World = ResolveWorld(InWorld);
	auto* PC = FindLocalPC(World);
	if (Args.Num() != 1 || !PC || !PC->GetCommanderNetSyncComponent()) return;
	FString Output = Args[0].TrimQuotes();
	if (FPaths::IsRelative(Output)) return;
	FPaths::NormalizeFilename(Output); FPaths::CollapseRelativeDirectories(Output);
	if (!FPaths::IsUnderDirectory(Output, FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("outputs")))
		|| !FPaths::GetExtension(Output).Equals(TEXT("json"), ESearchCase::IgnoreCase)) return;
	auto Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("world"), World->GetPathName());
	Result->SetNumberField(TEXT("net_mode"), static_cast<int32>(World->GetNetMode()));
	Result->SetNumberField(TEXT("frame"), static_cast<double>(GFrameCounter));
	Result->SetBoolField(TEXT("pointer_qa_active"), ActiveRun.IsValid());
	Result->SetStringField(TEXT("selection_shape"), PC->GetSelectionShape() == EGuLiCommanderSelectionShape::Box ? TEXT("box") : TEXT("radius"));
	Result->SetNumberField(TEXT("selection_revision"), PC->GetCommanderNetSyncComponent()->GetSelectionState().SelectionRevision);
	Result->SetNumberField(TEXT("last_ack_command_id"), PC->GetCommanderNetSyncComponent()->GetLastCommandAck().ClientCommandId);
	FVector Camera; FRotator Rotation; PC->GetPlayerViewPoint(Camera, Rotation);
	Result->SetObjectField(TEXT("camera"), VectorJson(Camera)); Result->SetObjectField(TEXT("camera_rotation"), VectorJson(FVector(Rotation.Pitch, Rotation.Yaw, Rotation.Roll)));
	int32 Width = 0, Height = 0; PC->GetViewportSize(Width, Height);
	Result->SetNumberField(TEXT("viewport_width"), Width); Result->SetNumberField(TEXT("viewport_height"), Height);
	TSet<uint32> Selected;
	for (const auto& Cohort : PC->GetCommanderNetSyncComponent()->GetSelectionState().Cohorts)
		for (const auto Id : Cohort.MemberIds) Selected.Add(Id.Value);
	TArray<uint32> Sorted = Selected.Array(); Sorted.Sort();
	TArray<TSharedPtr<FJsonValue>> Selection;
	for (const uint32 Id : Sorted) Selection.Add(MakeShared<FJsonValueNumber>(Id));
	Result->SetArrayField(TEXT("selected_ids"), Selection);
	TArray<TSharedPtr<FJsonValue>> Soldiers;
	auto* Roster = FindActor<AGuLiSoldierStateReplicator>(World);
	auto* Presentation = FindActor<AGuLiCommanderPresentationActor>(World);
	if (Roster && Presentation) for (const auto& Soldier : Roster->GetItems())
	{
		FTransform Transform;
		if (!Presentation->TryGetPresentedSoldierTransform(Soldier.SoldierId, Transform)) continue;
		auto Row = MakeShared<FJsonObject>(); const FVector V = Transform.GetLocation(); FVector2D Screen = FVector2D::ZeroVector;
		const bool bProjected = PC->ProjectWorldLocationToScreen(V, Screen, false);
		Row->SetNumberField(TEXT("id"), Soldier.SoldierId.Value); Row->SetNumberField(TEXT("team"), static_cast<int32>(Soldier.Team));
		Row->SetBoolField(TEXT("alive"), Soldier.IsAlive()); Row->SetBoolField(TEXT("selected"), Selected.Contains(Soldier.SoldierId.Value));
		Row->SetObjectField(TEXT("world_position"), VectorJson(V)); Row->SetNumberField(TEXT("screen_x"), Screen.X); Row->SetNumberField(TEXT("screen_y"), Screen.Y);
		Row->SetBoolField(TEXT("on_screen"), bProjected && Screen.X >= 0 && Screen.Y >= 0 && Screen.X < Width && Screen.Y < Height);
		Soldiers.Add(MakeShared<FJsonValueObject>(Row));
	}
	Result->SetArrayField(TEXT("soldiers"), Soldiers);
	FString Json; FJsonSerializer::Serialize(Result, TJsonWriterFactory<>::Create(&Json));
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(Output), true);
	const bool bSaved = FFileHelper::SaveStringToFile(Json, *Output, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	UE_LOG(LogGuLiStrike, Display, TEXT("Commander InputSnapshot saved=%d soldiers=%d selected=%d output=%s"), bSaved, Soldiers.Num(), Selected.Num(), *Output);
}

FAutoConsoleCommandWithWorldAndArgs MouseWorldCommand(TEXT("gs.Commander.QA.MouseWorld"), TEXT("click|right|alt|shift|altshift|drag|shiftdrag X Y Z [EndX EndY EndZ]. Routed viewport Slate input over separate frames; cursor/focus restored."), FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&StartWorld));
FAutoConsoleCommandWithWorldAndArgs MouseSoldierCommand(TEXT("gs.Commander.QA.MouseSoldier"), TEXT("click|right|alt|shift|altshift SoldierId. Uses the current presented soldier position and real viewport input routing."), FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&StartSoldier));
FAutoConsoleCommandWithWorldAndArgs KeyCommand(TEXT("gs.Commander.QA.Key"), TEXT("Seven|Add|One|Escape. Normal Slate key down/up on separate frames; restores viewport focus and releases synthesized input."), FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&StartKey));
FAutoConsoleCommandWithWorldAndArgs SnapshotCommand(TEXT("gs.Commander.QA.InputSnapshot"), TEXT("Absolute Project/outputs/*.json path. Captures local world, camera, viewport, selected IDs and presented soldier world/screen positions."), FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Snapshot));
}
#endif
