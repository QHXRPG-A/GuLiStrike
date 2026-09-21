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
#include "Components/CanvasPanelSlot.h"
#include "Components/ProgressBar.h"
#include "Commander/UI/GuLiCommanderActionButton.h"
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
		if (bDockOnly) ImageSize = FIntPoint(1888, 276);
		else if (Args.Num() > 0 && Args[0] == TEXT("1280x720")) ImageSize = FIntPoint(1280, 720);
		else if (Args.Num() > 0 && Args[0] == TEXT("1920x1080")) ImageSize = FIntPoint(1920, 1080);
		else if (Args.Num() > 0 && Args[0] == TEXT("2560x1080")) ImageSize = FIntPoint(2560, 1080);
		else if (Args.Num() > 0 && Args[0] == TEXT("1366x768")) ImageSize = FIntPoint(1366,768);
		else if (Args.Num() > 0 && Args[0] == TEXT("2560x1440")) ImageSize = FIntPoint(2560,1440);
		else if (Args.Num() > 0 && Args[0] == TEXT("3840x2160")) ImageSize = FIntPoint(3840,2160);
		else if (Args.Num() > 0 && Args[0] == TEXT("3440x1440")) ImageSize = FIntPoint(3440,1440);
		else
		{
			UE_LOG(LogGuLiStrike, Error,
				TEXT("Usage: gs.Commander.CaptureHUD <1366x768|1920x1080|2560x1440|3840x2160|3440x1440|dock> <output.png>"));
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
				TEXT("TXT_UnitTypeName"), TEXT("TXT_SelectionCaption"), TEXT("TXT_TaskSummary"), TEXT("TXT_Page"),
				TEXT("TXT_Context")
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
        // Layout is now authored by the native console; inspect the same tree used at runtime.
        TStrongObjectPtr<UGuLiCommanderHUDWidget> Preview(NewObject<UGuLiCommanderHUDWidget>(GetTransientPackage(), WidgetClass));
        Preview->Initialize();
        Preview->SetDesignerFlags(EWidgetDesignFlags::Designing);
        TSharedRef<SWidget> SlatePreview = Preview->TakeWidget();
        UWidgetTree* Tree = Preview->WidgetTree;
        if (!TestNotNull(TEXT("HUD has a real native widget tree"), Tree)) return false;
        UCanvasPanel* UnitCard = Cast<UCanvasPanel>(Tree->FindWidget(TEXT("C_UnitTypeCard")));
        TestNotNull(TEXT("Unit inspection owns one panel"), UnitCard);
        UTextBlock* Detail = Cast<UTextBlock>(Tree->FindWidget(TEXT("TXT_UnitTypeName")));
        if (TestNotNull(TEXT("Unit details text exists"), Detail))
            TestTrue(TEXT("Details stay inside the unit inspection panel"), Detail->GetParent() == UnitCard);
        TestNotNull(TEXT("Real survivor health has a native progress bar"), Cast<UProgressBar>(Tree->FindWidget(TEXT("PB_UnitTypeHealth"))));
        TestNull(TEXT("Removed radius control has no button"), Tree->FindWidget(TEXT("BTN_Cmd_SelectSize")));
        TestNull(TEXT("Radius label is removed"), Tree->FindWidget(TEXT("TXT_CmdSelectAdjustLabel")));
        TestNull(TEXT("Static-review watermark is removed"), Tree->FindWidget(TEXT("TXT_VisualOnly")));
        TArray<UWidget*> Widgets; Tree->GetAllWidgets(Widgets);
        TArray<FString> LegacyWidgets;
        for (const UWidget* Widget : Widgets)
        {
            const FString Name = Widget->GetName();
            if (Name.StartsWith(TEXT("I_Squad")) || Name.StartsWith(TEXT("TXT_Squad")) || Name.Contains(TEXT("CmdSelectAdjust"))) LegacyWidgets.Add(Name);
        }
        TestTrue(FString::Printf(TEXT("No obsolete cohort cards or radius graphics remain: %s"), *FString::Join(LegacyWidgets,TEXT(", "))),LegacyWidgets.IsEmpty());
        for (const FName IslandName : BlockingIslandNames)
        {
            UCanvasPanel* Island = Cast<UCanvasPanel>(Tree->FindWidget(IslandName));
            if (TestNotNull(FString::Printf(TEXT("Native input blocker %s is retained"), *IslandName.ToString()),Island))
            {
                const auto* DesignSlot=Cast<UCanvasPanelSlot>(Island->GetParent()->Slot);
                TestTrue(TEXT("Input island has positive design area"),DesignSlot && DesignSlot->GetSize().X>1 && DesignSlot->GetSize().Y>1);
                TestTrue(TEXT("Input island remains visible for geometry blocking"),Island->GetVisibility()!=ESlateVisibility::Collapsed && Island->GetVisibility()!=ESlateVisibility::Hidden);
            }
        }
        auto SizeOf=[&](FName Name)
        {
            const auto* Panel=Tree->FindWidget(Name);
            const auto* DesignSlot=Panel && Panel->GetParent()?Cast<UCanvasPanelSlot>(Panel->GetParent()->Slot):nullptr;
            return DesignSlot?DesignSlot->GetSize():FVector2D::ZeroVector;
        };
        TestEqual(TEXT("Selection dock follows the approved 8x3 layout"),SizeOf(TEXT("SB_DockDesign")),FVector2D(924,224));
        TestEqual(TEXT("Control-group strip follows the approved layout"),SizeOf(TEXT("SB_ShortcutsDesign")),FVector2D(644,44));
        TestNotNull(TEXT("Combined dock capture includes the group strip"),Tree->FindWidget(TEXT("SB_CommandDockDesign")));
        TestNotNull(TEXT("The inspection panel uses a replaceable portrait"),Cast<UImage>(Tree->FindWidget(TEXT("I_UnitTypePortrait"))));
        UWidget* Tooltip=Tree->FindWidget(TEXT("C_SelectionTooltip"));
        if (TestNotNull(TEXT("Tooltip belongs to the viewport rather than a desktop popup"),Tooltip))
            TestEqual(TEXT("Tooltip starts hidden"),Tooltip->GetVisibility(),ESlateVisibility::Collapsed);
        TestNotNull(TEXT("Tooltip text is authored without Blueprint binding"),Cast<UTextBlock>(Tree->FindWidget(TEXT("TXT_SelectionTooltip"))));
        for(const FName ButtonName : {FName(TEXT("BTN_Cmd_Move")),FName(TEXT("BTN_Cmd_Stop"))})
        {
            const auto* Button=Cast<UGuLiCommanderActionButton>(Tree->FindWidget(ButtonName));
            if(TestNotNull(TEXT("Command has an imported icon"),Button))
            {
                TestEqual(TEXT("Available command icon is bright"),Button->Icon->GetRenderOpacity(),1.f);
                TestNotNull(TEXT("Command icon references a real texture"),Cast<UTexture2D>(Button->Icon->GetBrush().GetResourceObject()));
            }
        }
        for(const FName ButtonName : {FName(TEXT("BTN_Cmd_Move")),FName(TEXT("BTN_Cmd_Stop")),FName(TEXT("BTN_Cmd_Focus")),FName(TEXT("BTN_Group_0")),FName(TEXT("BTN_Portrait_00"))})
        {
            auto* Button=Cast<UButton>(Tree->FindWidget(ButtonName));
            if(TestNotNull(FString::Printf(TEXT("Console button %s exists"),*ButtonName.ToString()),Button))
                TestFalse(TEXT("Action buttons do not capture keyboard focus"),Button->GetIsFocusable());
        }
        auto* SelectionTitle=Cast<UTextBlock>(Tree->FindWidget(TEXT("TXT_SelectionCaption")));
        if(TestNotNull(TEXT("Selection heading exists"),SelectionTitle))
            TestEqual(TEXT("Selection heading is Chinese"),SelectionTitle->GetText().ToString(),FString(TEXT("点选或框选部队")));
        auto* CommandTitle=Cast<UTextBlock>(Tree->FindWidget(TEXT("TXT_CommandTitle")));
        if(TestNotNull(TEXT("Command heading exists"),CommandTitle))
            TestEqual(TEXT("Command heading explains full-selection scope"),CommandTitle->GetText().ToString(),FString(TEXT("部队指令 · 全部选择")));
        Preview->ReleaseSlateResources(true);
		return true;
	}
#endif // WITH_DEV_AUTOMATION_TESTS
}

#endif // WITH_EDITOR
