// Copyright Epic Games, Inc. All Rights Reserved.

#include "GuLiStrike.h"

#if WITH_EDITOR

#include "Editor.h"
#include "HAL/IConsoleManager.h"
#include "String/LexFromString.h"
#include "PlayInEditorDataTypes.h"
#include "Settings/LevelEditorPlaySettings.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UObjectGlobals.h"

namespace GuLiCommanderSelectionPIE
{
	/** The asynchronous request must never depend on an unreferenced local UObject. */
	struct FTransientPlaySettings
	{
		TStrongObjectPtr<ULevelEditorPlaySettings> Settings;
		FDelegateHandle EndPIEHandle;
		FDelegateHandle CancelPIEHandle;
		FDelegateHandle EditorExitHandle;

		~FTransientPlaySettings()
		{
			Release();
		}

		void Release()
		{
			if (EndPIEHandle.IsValid())
			{
				FEditorDelegates::EndPIE.Remove(EndPIEHandle);
				EndPIEHandle.Reset();
			}
			if (CancelPIEHandle.IsValid())
			{
				FEditorDelegates::CancelPIE.Remove(CancelPIEHandle);
				CancelPIEHandle.Reset();
			}
			if (EditorExitHandle.IsValid())
			{
				FEditorDelegates::OnEditorPreExit.Remove(EditorExitHandle);
				EditorExitHandle.Reset();
			}
			Settings.Reset();
		}

		void EndPIE(bool bWasSimulating)
		{
			(void)bWasSimulating;
			Release();
		}

		void BindCleanup()
		{
			EndPIEHandle = FEditorDelegates::EndPIE.AddRaw(this, &FTransientPlaySettings::EndPIE);
			CancelPIEHandle = FEditorDelegates::CancelPIE.AddRaw(this, &FTransientPlaySettings::Release);
			EditorExitHandle = FEditorDelegates::OnEditorPreExit.AddRaw(this, &FTransientPlaySettings::Release);
		}
	};

	FTransientPlaySettings ActiveSettings;

	void StartPIE(const TArray<FString>& Args)
	{
		if (!IsInGameThread() || !GEditor)
		{
			UE_LOG(LogGuLiStrike, Error, TEXT("Commander QA StartPIE requires the editor game thread."));
			return;
		}
		if (GEditor->IsPlaySessionInProgress())
		{
			UE_LOG(LogGuLiStrike, Warning, TEXT("Commander QA StartPIE refused: PIE/SIE is already active or queued."));
			return;
		}

		const bool bClients = Args.Num() > 0 && Args[0].Equals(TEXT("clients"), ESearchCase::IgnoreCase);
		const bool bStandalone = Args.Num() > 0 && Args[0].Equals(TEXT("standalone"), ESearchCase::IgnoreCase);
		int32 Width = 1280;
		int32 Height = 720;
		if ((!bClients && !bStandalone) || (Args.Num() != 1 && Args.Num() != 3)
			|| (Args.Num() == 3 && (!LexTryParseString(Width, *Args[1]) || !LexTryParseString(Height, *Args[2])))
			|| Width < 320 || Height < 180 || Width > 8192 || Height > 8192)
		{
			UE_LOG(LogGuLiStrike, Error,
				TEXT("Usage: gs.Commander.QA.StartPIE standalone|clients [1280 720]. Size must be 320x180 through 8192x8192."));
			return;
		}

		ActiveSettings.Release();
		ActiveSettings.Settings.Reset(DuplicateObject<ULevelEditorPlaySettings>(
			GetDefault<ULevelEditorPlaySettings>(), GetTransientPackage()));
		if (!ActiveSettings.Settings.IsValid())
		{
			UE_LOG(LogGuLiStrike, Error, TEXT("Commander QA StartPIE could not duplicate Play settings."));
			return;
		}

		ULevelEditorPlaySettings* Settings = ActiveSettings.Settings.Get();
		Settings->SetFlags(RF_Transient);
		Settings->SetPlayNetMode(bClients ? EPlayNetMode::PIE_Client : EPlayNetMode::PIE_Standalone);
		Settings->SetPlayNumberOfClients(bClients ? 2 : 1);
		Settings->SetRunUnderOneProcess(true);
		// PIE_Client automatically creates a windowless dedicated server as instance 0.
		// Do not request an extra server when running the standalone path.
		Settings->bLaunchSeparateServer = false;
		Settings->NewWindowWidth = Width;
		Settings->NewWindowHeight = Height;
		Settings->SetClientWindowSize(FIntPoint(Width, Height));
		Settings->LastSize = FIntPoint(Width, Height);
		Settings->MultipleInstancePositions.Reset();
		Settings->CenterNewWindow = true;
		Settings->GameGetsMouseControl = true;

		FRequestPlaySessionParams Request;
		Request.EditorPlaySettings = Settings;
		Request.SessionDestination = EPlaySessionDestinationType::InProcess;
		Request.WorldType = EPlaySessionWorldType::PlayInEditor;
		Request.bAllowOnlineSubsystem = false;
		// No DestinationSlateViewport: every visible player gets its own PIE window.
		// No map override or config writes: use the current editor map and a settings copy.
		ActiveSettings.BindCleanup();
		GEditor->RequestPlaySession(Request);
		UE_LOG(LogGuLiStrike, Display,
			TEXT("Commander QA PIE queued: mode=%s clients=%d dedicated_server=%s in_process=true window=%dx%d transient_settings=%s. User Play mode/count are unchanged."),
			bClients ? TEXT("clients") : TEXT("standalone"), bClients ? 2 : 1,
			bClients ? TEXT("true") : TEXT("false"), Width, Height, *Settings->GetPathName());
		// UE's ordinary EndPlayMap still persists its remembered window positions/sizes.
		// This command never writes the settings CDO, SaveConfig, or a map package.
	}

	FAutoConsoleCommand StartPIECommand(
		TEXT("gs.Commander.QA.StartPIE"),
		TEXT("Queue standalone (1 player) or clients (2 players + dedicated server) in separate in-process PIE windows. Optional width height default to 1280 720. Uses transient Play settings; never saves a map."),
		FConsoleCommandWithArgsDelegate::CreateStatic(&StartPIE));
}

#endif // WITH_EDITOR
