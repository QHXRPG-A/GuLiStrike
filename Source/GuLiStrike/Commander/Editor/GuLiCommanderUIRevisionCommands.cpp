// Copyright Epic Games, Inc. All Rights Reserved.

#include "GuLiStrike.h"

#if WITH_EDITOR

#include "Blueprint/SlateBlueprintLibrary.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "Blueprint/WidgetTree.h"
#include "Commander/Framework/GuLiCommanderPlayerController.h"
#include "Commander/UI/GuLiCommanderHUDWidget.h"
#include "Commander/UI/GuLiCommanderMiniMapWidget.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/Image.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Engine/Engine.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/Texture2D.h"
#include "Engine/UserInterfaceSettings.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "ImageUtils.h"
#include "Misc/App.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "RenderingThread.h"
#include "Slate/SObjectWidget.h"
#include "Slate/WidgetRenderer.h"
#include "TimerManager.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UObjectIterator.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#endif

namespace GuLiCommanderUIRevisionCommands
{
	constexpr TCHAR HUDClassPath[] =
		TEXT("/Game/Commander/UI/Widgets/WBP_CommanderHUD.WBP_CommanderHUD_C");
	const FName BlockingIslandNames[] = {
		TEXT("SB_TopStatus"), TEXT("SB_MapDesign"), TEXT("SB_DockDesign"), TEXT("SB_ShortcutsDesign")
	};

	AGuLiCommanderPlayerController* FindLocalCommander(UWorld* World)
	{
		if (!World || !World->IsGameWorld())
		{
			return nullptr;
		}
		for (TActorIterator<AGuLiCommanderPlayerController> It(World); It; ++It)
		{
			if (It->IsLocalController())
			{
				return *It;
			}
		}
		return nullptr;
	}

	AGuLiCommanderPlayerController* ResolveCaptureController(UWorld* World)
	{
		if (AGuLiCommanderPlayerController* Controller = FindLocalCommander(World))
		{
			return Controller;
		}
		if (GEngine)
		{
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				if (Context.WorldType == EWorldType::PIE)
				{
					if (AGuLiCommanderPlayerController* Controller = FindLocalCommander(Context.World()))
					{
						return Controller;
					}
				}
			}
		}
		return nullptr;
	}

	UClass* ResolveHUDClass(AGuLiCommanderPlayerController* Controller)
	{
		// Use the actual in-play class if it has been overridden; never redraw its live Slate tree.
		for (TObjectIterator<UGuLiCommanderHUDWidget> It; It; ++It)
		{
			if (It->GetWorld() == Controller->GetWorld()
				&& It->GetOwningPlayer() == Controller && It->IsInViewport())
			{
				return It->GetClass();
			}
		}
		return LoadClass<UGuLiCommanderHUDWidget>(nullptr, HUDClassPath);
	}

	/** Never enters a viewport; cleanup explicitly invokes the real NativeDestruct chain. */
	struct FScopedHUDCapture
	{
		TStrongObjectPtr<UGuLiCommanderHUDWidget> Widget;
		TSharedPtr<SObjectWidget> SlateRoot;

		FScopedHUDCapture(AGuLiCommanderPlayerController* Controller, UClass* WidgetClass)
			: Widget(CreateWidget<UGuLiCommanderHUDWidget>(Controller, WidgetClass))
		{
			if (Widget.IsValid())
			{
				Widget->InitializeForController(Controller);
				// A fresh runtime UUserWidget made through TakeWidget has the stock SObjectWidget wrapper.
				SlateRoot = StaticCastSharedRef<SObjectWidget>(Widget->TakeWidget());
				Widget->RefreshInitialState();
			}
		}

		~FScopedHUDCapture()
		{
			if (!Widget.IsValid())
			{
				return;
			}
			UGuLiCommanderMiniMapWidget* MiniMap = Widget->GetMiniMapWidget();
			if (SlateRoot.IsValid())
			{
				// This unbinds the HUD's runtime delegates and its two timers immediately,
				// even if a draw buffer temporarily retains a reference to the Slate wrapper.
				SlateRoot->ResetWidget();
				SlateRoot.Reset();
			}
			Widget->ReleaseSlateResources(true);
			if (UWorld* World = Widget->GetWorld())
			{
				World->GetTimerManager().ClearAllTimersForObject(Widget.Get());
				if (MiniMap)
				{
					World->GetTimerManager().ClearAllTimersForObject(MiniMap);
				}
			}
		}
	};

	bool ValidateBlockingGeometry(UGuLiCommanderHUDWidget* Widget, const FIntPoint ImageSize, FString& Report)
	{
		bool bValid = Widget->HasValidBlockingGeometry();
		Report += FString::Printf(TEXT("blocking_geometry_ready=%s\n"), bValid ? TEXT("true") : TEXT("false"));
		for (const FName IslandName : BlockingIslandNames)
		{
			UWidget* Island = Widget->WidgetTree ? Widget->WidgetTree->FindWidget(IslandName) : nullptr;
			if (!Island)
			{
				Report += FString::Printf(TEXT("%s=missing\n"), *IslandName.ToString());
				bValid = false;
				continue;
			}
			const FGeometry& Geometry = Island->GetCachedGeometry();
			const FVector2D Size = Geometry.GetLocalSize();
			const FVector2D Minimum = Geometry.LocalToAbsolute(FVector2D::ZeroVector);
			const FVector2D Maximum = Geometry.LocalToAbsolute(Size);
			FVector2D PixelCenter;
			FVector2D ViewportCenter;
			USlateBlueprintLibrary::LocalToViewport(Widget, Geometry, Size * 0.5, PixelCenter, ViewportCenter);
			const bool bBlocksCenter = Widget->IsScreenPositionBlocked(PixelCenter);
			const bool bInsideImage = Minimum.X >= -1.0 && Minimum.Y >= -1.0
				&& Maximum.X <= ImageSize.X + 1.0 && Maximum.Y <= ImageSize.Y + 1.0;
			bValid &= Size.X > 1.0 && Size.Y > 1.0 && bBlocksCenter && bInsideImage;
			Report += FString::Printf(
				TEXT("%s bounds=(%.1f,%.1f)-(%.1f,%.1f) blocks_center=%s inside_image=%s\n"),
				*IslandName.ToString(), Minimum.X, Minimum.Y, Maximum.X, Maximum.Y,
				bBlocksCenter ? TEXT("true") : TEXT("false"), bInsideImage ? TEXT("true") : TEXT("false"));
		}

		const FGeometry& RootGeometry = Widget->GetCachedGeometry();
		FVector2D PixelCenter;
		FVector2D ViewportCenter;
		USlateBlueprintLibrary::LocalToViewport(
			Widget, RootGeometry, RootGeometry.GetLocalSize() * 0.5, PixelCenter, ViewportCenter);
		const bool bBattlefieldCenterOpen = !Widget->IsScreenPositionBlocked(PixelCenter);
		bValid &= bBattlefieldCenterOpen;
		Report += FString::Printf(TEXT("battlefield_center_open=%s\n"),
			bBattlefieldCenterOpen ? TEXT("true") : TEXT("false"));
		return bValid;
	}

	bool RenderCapture(
		FScopedHUDCapture& Capture,
		const FIntPoint ImageSize,
		const bool bDockOnly,
		const FString& Filename,
		FString& Report)
	{
		TSharedRef<SWidget> WidgetToDraw = Capture.SlateRoot.ToSharedRef();
		if (bDockOnly)
		{
			UWidget* Dock = Capture.Widget->WidgetTree
				? Capture.Widget->WidgetTree->FindWidget(TEXT("SB_CommandDockDesign")) : nullptr;
			if (!Dock)
			{
				UE_LOG(LogGuLiStrike, Error, TEXT("HUD capture cannot find SB_CommandDockDesign."));
				return false;
			}
			WidgetToDraw = Dock->TakeWidget();
		}
		const FVector2D DrawSize(ImageSize.X, ImageSize.Y);
		const float DrawScale = bDockOnly ? 1.0f
			: GetDefault<UUserInterfaceSettings>()->GetDPIScaleBasedOnSize(ImageSize);
		Report += FString::Printf(TEXT("size=%dx%d\ndpi_scale=%.4f\ndock_only=%s\n"),
			ImageSize.X, ImageSize.Y, DrawScale, bDockOnly ? TEXT("true") : TEXT("false"));

		FWidgetRenderer Renderer(true, true);
		if (!Renderer.GetSlateRenderer())
		{
			UE_LOG(LogGuLiStrike, Error, TEXT("HUD capture requires an active Slate RHI renderer."));
			return false;
		}
		TStrongObjectPtr<UTextureRenderTarget2D> Target(
			// Slate already encodes the display gamma. UE 5.7 also creates an sRGB RHI
			// target when this flag is true, which would encode it a second time.
			FWidgetRenderer::CreateTargetFor(DrawSize, TF_Bilinear, false));
		if (!Target.IsValid())
		{
			UE_LOG(LogGuLiStrike, Error, TEXT("HUD capture could not create an offscreen render target."));
			return false;
		}
		Renderer.DrawWidget(Target.Get(), WidgetToDraw, DrawScale, DrawSize, 0.0f, false);
		// Finish GPU work before readback or releasing the temporary renderer/widget resources.
		FlushRenderingCommands();
		FImage Image;
		if (!FImageUtils::GetRenderTargetImage(Target.Get(), Image)
			|| Image.SizeX != ImageSize.X || Image.SizeY != ImageSize.Y
			|| Image.Format != ERawImageFormat::BGRA8)
		{
			UE_LOG(LogGuLiStrike, Error, TEXT("HUD capture expected an 8-bit Slate render target: %s"), *Filename);
			return false;
		}
		// The target is linear only to bypass hardware encoding; its bytes are already
		// display encoded by Slate. Correct the metadata without transforming the pixels.
		Image.GammaSpace = EGammaSpace::sRGB;
		if (!FImageUtils::SaveImageByExtension(*Filename, Image))
		{
			UE_LOG(LogGuLiStrike, Error, TEXT("HUD capture readback or PNG save failed: %s"), *Filename);
			return false;
		}
		Report += TEXT("color_encoding=slate_display_gamma_once\ntarget_srgb=false\npng_gamma=sRGB\n");

		if (!bDockOnly)
		{
			const bool bGeometryValid = ValidateBlockingGeometry(Capture.Widget.Get(), ImageSize, Report);
			Report += FString::Printf(TEXT("geometry_validation=%s\n"), bGeometryValid ? TEXT("passed") : TEXT("failed"));
			if (!bGeometryValid)
			{
				UE_LOG(LogGuLiStrike, Warning, TEXT("HUD PNG saved, but geometry checks failed; inspect its .txt report."));
			}
		}
		return true;
	}

	void CaptureHUD(const TArray<FString>& Args, UWorld* World)
	{
		FIntPoint ImageSize;
		const bool bDockOnly = Args.Num() > 0 && Args[0].Equals(TEXT("dock"), ESearchCase::IgnoreCase);
		if (bDockOnly) ImageSize = FIntPoint(1120, 260);
		else if (Args.Num() > 0 && Args[0] == TEXT("1280x720")) ImageSize = FIntPoint(1280, 720);
		else if (Args.Num() > 0 && Args[0] == TEXT("1920x1080")) ImageSize = FIntPoint(1920, 1080);
		else if (Args.Num() > 0 && Args[0] == TEXT("2560x1080")) ImageSize = FIntPoint(2560, 1080);
		else
		{
			UE_LOG(LogGuLiStrike, Error,
				TEXT("Usage: gs.Commander.CaptureHUD <1280x720|1920x1080|2560x1080|dock> <output.png>"));
			return;
		}
		const FString OutputArgument = Args.Num() == 2 ? Args[1].TrimQuotes() : FString();
		if (Args.Num() != 2 || !FPaths::GetExtension(OutputArgument).Equals(TEXT("png"), ESearchCase::IgnoreCase))
		{
			UE_LOG(LogGuLiStrike, Error, TEXT("HUD capture needs one explicit PNG path; quote paths containing spaces."));
			return;
		}
		if (!IsInGameThread() || !FSlateApplication::IsInitialized() || !FApp::CanEverRender())
		{
			UE_LOG(LogGuLiStrike, Error, TEXT("HUD capture requires the editor game thread with Slate and rendering enabled."));
			return;
		}
		AGuLiCommanderPlayerController* Controller = ResolveCaptureController(World);
		if (!Controller)
		{
			UE_LOG(LogGuLiStrike, Error, TEXT("HUD capture requires a local Commander in PIE; no placeholder data is synthesized."));
			return;
		}
		UClass* WidgetClass = ResolveHUDClass(Controller);
		if (!WidgetClass)
		{
			UE_LOG(LogGuLiStrike, Error, TEXT("HUD capture could not load %s."), HUDClassPath);
			return;
		}
		const FString Filename = FPaths::ConvertRelativePathToFull(
			FPaths::IsRelative(OutputArgument) ? FPaths::Combine(FPaths::ProjectDir(), TEXT("outputs"), OutputArgument) : OutputArgument);
		if (!IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename), true))
		{
			UE_LOG(LogGuLiStrike, Error, TEXT("HUD capture could not create output directory for %s."), *Filename);
			return;
		}

		FString Report = FString::Printf(TEXT("world=%s\ncontroller=%s\nwidget_class=%s\n"),
			*Controller->GetWorld()->GetPathName(), *Controller->GetPathName(), *WidgetClass->GetPathName());
		bool bSaved = false;
		{
			FScopedHUDCapture Capture(Controller, WidgetClass);
			if (!Capture.Widget.IsValid() || !Capture.SlateRoot.IsValid())
			{
				UE_LOG(LogGuLiStrike, Error, TEXT("HUD capture could not construct a temporary runtime HUD."));
				return;
			}
			const FName TextNames[] = {
				TEXT("TXT_UnitTypeName"), TEXT("TXT_UnitTypeCount"), TEXT("TXT_UnitTypeHealth"), TEXT("TXT_UnitTypeStatus"),
				TEXT("TXT_CmdLabel_Select")
			};
			for (const FName TextName : TextNames)
			{
				if (const UTextBlock* Text = Cast<UTextBlock>(Capture.Widget->WidgetTree->FindWidget(TextName)))
				{
					Report += FString::Printf(TEXT("%s=%s\n"), *TextName.ToString(), *Text->GetText().ToString());
				}
			}
			bSaved = RenderCapture(Capture, ImageSize, bDockOnly, Filename, Report);
		}
		if (bSaved)
		{
			Report += TEXT("temporary_widget_released=true\n");
			if (!FFileHelper::SaveStringToFile(Report, *(Filename + TEXT(".txt")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
			{
				UE_LOG(LogGuLiStrike, Warning, TEXT("HUD PNG saved but its text report could not be written."));
			}
			UE_LOG(LogGuLiStrike, Display, TEXT("Saved offscreen Commander HUD PNG and validation report: %s"), *Filename);
		}
	}

	FAutoConsoleCommandWithWorldAndArgs CaptureHUDCommand(
		TEXT("gs.Commander.CaptureHUD"),
		TEXT("Offscreen real-data HUD capture: <1280x720|1920x1080|2560x1080|dock> <output.png>. Relative paths use Project/outputs. Never captures an editor window."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&CaptureHUD));

#if WITH_DEV_AUTOMATION_TESTS
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FGuLiCommanderHUDAssetContractTest,
		"GuLiStrike.Commander.UI.HUD.AssetContract",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FGuLiCommanderHUDAssetContractTest::RunTest(const FString& Parameters)
	{
		(void)Parameters;
		UWidgetBlueprintGeneratedClass* WidgetClass = Cast<UWidgetBlueprintGeneratedClass>(
			LoadClass<UGuLiCommanderHUDWidget>(nullptr, HUDClassPath));
		if (!TestNotNull(TEXT("Runtime HUD Blueprint derives from the native adapter"), WidgetClass))
		{
			return false;
		}
		UWidgetTree* Tree = WidgetClass->GetWidgetTreeArchetype();
		if (!TestNotNull(TEXT("HUD has a real authored widget tree"), Tree))
		{
			return false;
		}
		UCanvasPanel* UnitCard = Cast<UCanvasPanel>(Tree->FindWidget(TEXT("C_UnitTypeCard")));
		TestNotNull(TEXT("A unit type owns one collapsible panel"), UnitCard);
		const FName TextNames[] = {
			TEXT("TXT_UnitTypeName"), TEXT("TXT_UnitTypeCount"), TEXT("TXT_UnitTypeHealth"), TEXT("TXT_UnitTypeStatus")
		};
		for (const FName TextName : TextNames)
		{
			UTextBlock* Text = Cast<UTextBlock>(Tree->FindWidget(TextName));
			if (TestNotNull(FString::Printf(TEXT("Runtime text %s exists"), *TextName.ToString()), Text))
			{
				bool bInsideCard = false;
				for (UPanelWidget* Parent = Text->GetParent(); Parent; Parent = Parent->GetParent())
				{
					bInsideCard |= Parent == UnitCard;
				}
				TestTrue(FString::Printf(TEXT("%s collapses with its unit panel"), *TextName.ToString()), bInsideCard);
			}
		}
		TestNotNull(TEXT("Real survivor health has a native-fill image"), Cast<UImage>(Tree->FindWidget(TEXT("I_UnitTypeHealthFill"))));
		TestNull(TEXT("Keyboard-only radius control has no button"), Tree->FindWidget(TEXT("BTN_Cmd_SelectSize")));
		TestNull(TEXT("Radius label is removed"), Tree->FindWidget(TEXT("TXT_CmdSelectAdjustLabel")));
		TestNull(TEXT("Static-review watermark is removed"), Tree->FindWidget(TEXT("TXT_VisualOnly")));

		TArray<UWidget*> Widgets;
		Tree->GetAllWidgets(Widgets);
		TArray<FString> LegacyWidgets;
		for (const UWidget* Widget : Widgets)
		{
			const FString Name = Widget->GetName();
			const bool bLegacyCard = Name.StartsWith(TEXT("I_SquadOuter_")) || Name.StartsWith(TEXT("I_SquadBG_"))
				|| Name.StartsWith(TEXT("I_SquadAccent_")) || Name.StartsWith(TEXT("I_SquadPortrait_"))
				|| Name.StartsWith(TEXT("I_SquadGlyph_")) || Name.StartsWith(TEXT("I_SquadBar")) || Name.StartsWith(TEXT("I_SquadReady_"))
				|| Name.StartsWith(TEXT("TXT_SquadNum_")) || Name.StartsWith(TEXT("TXT_SquadRole_"))
				|| Name.StartsWith(TEXT("TXT_SquadCount_")) || Name == TEXT("TXT_SquadSelected");
			if (bLegacyCard || Name.Contains(TEXT("CmdSelectAdjust")))
			{
				LegacyWidgets.Add(Name);
			}
		}
		TestTrue(FString::Printf(TEXT("No obsolete cohort cards or radius graphics remain: %s"),
			*FString::Join(LegacyWidgets, TEXT(", "))), LegacyWidgets.IsEmpty());

		for (const FName IslandName : BlockingIslandNames)
		{
			USizeBox* Island = Cast<USizeBox>(Tree->FindWidget(IslandName));
			if (TestNotNull(FString::Printf(TEXT("Native input blocker %s is retained"), *IslandName.ToString()), Island))
			{
				TestTrue(FString::Printf(TEXT("%s has positive design area"), *IslandName.ToString()),
					Island->GetWidthOverride() > 1.0f && Island->GetHeightOverride() > 1.0f);
				TestTrue(FString::Printf(TEXT("%s remains visible for native geometry blocking"), *IslandName.ToString()),
					Island->GetVisibility() != ESlateVisibility::Collapsed && Island->GetVisibility() != ESlateVisibility::Hidden);
			}
		}
		if (USizeBox* Dock = Cast<USizeBox>(Tree->FindWidget(TEXT("SB_DockDesign"))))
		{
			TestEqual(TEXT("Dock keeps its approved width"), Dock->GetWidthOverride(), 1120.0f);
			TestEqual(TEXT("Dock keeps its approved height"), Dock->GetHeightOverride(), 204.0f);
		}
		USizeBox* Shortcuts = Cast<USizeBox>(Tree->FindWidget(TEXT("SB_ShortcutsDesign")));
		if (TestNotNull(TEXT("Shortcut strip is a separate blocking island"), Shortcuts))
		{
			TestEqual(TEXT("Shortcut strip matches the bottom dock width"), Shortcuts->GetWidthOverride(), 1120.0f);
			TestEqual(TEXT("Shortcut strip adds exactly 56 design pixels"), Shortcuts->GetHeightOverride(), 56.0f);
		}
		TestNotNull(TEXT("Combined dock capture includes the shortcut strip"), Tree->FindWidget(TEXT("SB_CommandDockDesign")));
		TestNotNull(TEXT("The selection mode uses one replaceable icon"), Cast<UImage>(Tree->FindWidget(TEXT("I_CmdIcon_Select"))));
		UWidget* Tooltip = Tree->FindWidget(TEXT("C_SelectionTooltip"));
		if (TestNotNull(TEXT("Tooltip belongs to the viewport rather than a desktop popup"), Tooltip))
		{
			TestEqual(TEXT("Tooltip starts hidden"), Tooltip->GetVisibility(), ESlateVisibility::Collapsed);
		}
		TestNotNull(TEXT("Tooltip text is authored without Blueprint binding"), Cast<UTextBlock>(Tree->FindWidget(TEXT("TXT_SelectionTooltip"))));
		for (const FName IconName : {FName(TEXT("I_Shortcut_SameType")), FName(TEXT("I_Shortcut_AddSelection"))})
		{
			UImage* Icon = Cast<UImage>(Tree->FindWidget(IconName));
			if (TestNotNull(FString::Printf(TEXT("Shortcut icon %s is present"), *IconName.ToString()), Icon))
			{
				TestEqual(TEXT("Shortcut icons are always bright"), Icon->GetRenderOpacity(), 1.0f);
				TestNotNull(TEXT("Shortcut uses a real imported texture"), Cast<UTexture2D>(Icon->GetBrush().GetResourceObject()));
			}
		}
		const FName ButtonNames[] = {
			TEXT("BTN_Cmd_Move"), TEXT("BTN_Cmd_Select"), TEXT("BTN_MiniMapJump"),
			TEXT("BTN_Shortcut_SameType"), TEXT("BTN_Shortcut_AddSelection")
		};
		for (const FName ButtonName : ButtonNames)
		{
			UButton* Button = Cast<UButton>(Tree->FindWidget(ButtonName));
			if (TestNotNull(FString::Printf(TEXT("Existing command button %s is retained"), *ButtonName.ToString()), Button))
			{
				TestFalse(FString::Printf(TEXT("%s does not capture keyboard focus"), *ButtonName.ToString()), Button->GetIsFocusable());
			}
		}
		if (UTextBlock* Title = Cast<UTextBlock>(Tree->FindWidget(TEXT("TXT_SquadsTitle"))))
		{
			TestEqual(TEXT("Unit-section heading is Chinese only"), Title->GetText().ToString(), FString(TEXT("编队")));
		}
		else AddError(TEXT("TXT_SquadsTitle is missing."));
		if (UTextBlock* Title = Cast<UTextBlock>(Tree->FindWidget(TEXT("TXT_CommandTitle"))))
		{
			TestEqual(TEXT("Command-section heading is Chinese only"), Title->GetText().ToString(), FString(TEXT("指令矩阵")));
		}
		else AddError(TEXT("TXT_CommandTitle is missing."));
		return true;
	}
#endif // WITH_DEV_AUTOMATION_TESTS
}

#endif // WITH_EDITOR
