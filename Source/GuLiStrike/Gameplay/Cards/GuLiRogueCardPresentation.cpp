#include "Gameplay/Cards/GuLiRogueCardPresentation.h"
#include "Network/GuLiNetworkSettings.h"
#include "Gameplay/Cards/GuLiRogueCardSubsystem.h"
#include "Gameplay/Cards/GuLiRogueCardSettings.h"
#include "Gameplay/Data/Generated/GuLiStrikeGameTextsTableRows.h"
#include "Internationalization/StringTable.h"
#include "Internationalization/StringTableCore.h"
#include "Commander/Framework/GuLiCommanderPlayerController.h"
#include "Commander/Framework/GuLiCommanderNetSyncComponent.h"
#include "Commander/Presentation/GuLiCommanderHUD.h"
#include "Battle/Framework/GuLiBattlePlayerState.h"
#include "Battle/Framework/GuLiBattleGameState.h"
#include "Blueprint/WidgetTree.h"
#include "Components/BackgroundBlur.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/RichTextBlock.h"
#include "Components/TextBlock.h"
#include "Components/WidgetComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Camera/CameraComponent.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "MaterialShared.h"
#include "Components/PrimitiveComponent.h"
#include "UObject/StructOnScope.h"
#include "UObject/UnrealType.h"
#include "InputCoreTypes.h"
#include "Engine/GameViewportClient.h"
#include "UnrealClient.h"
#include "Gameplay/Data/Generated/GuLiStrikeGroundHUDTableRows.h"
#include "Gameplay/Data/GuLiGameText.h"
#include "Gameplay/GroundMech/UI/GuLiGroundHUDVisuals.h"
#include "Gameplay/GroundMech/UI/GuLiGroundCardFace.h"
#include "Components/ScaleBox.h"
#include "Components/SizeBox.h"
#include "Styling/CoreStyle.h"

namespace
{
	// The existing director is Blueprint-owned. Keep reflection isolated and reject a broken contract before opening.
	FProperty* Property(UObject* Object,FName Name) { return Object ? FindFProperty<FProperty>(Object->GetClass(),Name) : nullptr; }
	bool SetObject(UObject* O,FName N,UObject* Value)
	{ if (auto* P=CastField<FObjectPropertyBase>(Property(O,N))) { P->SetObjectPropertyValue_InContainer(O,Value); return true; } return false; }
	UObject* GetObject(UObject* O,FName N)
	{ if (auto* P=CastField<FObjectPropertyBase>(Property(O,N))) return P->GetObjectPropertyValue_InContainer(O); return nullptr; }
	bool SetBool(UObject* O,FName N,bool V)
	{ if (auto* P=CastField<FBoolProperty>(Property(O,N))) { P->SetPropertyValue_InContainer(O,V); return true; } return false; }
	bool SetNumber(UObject* O,FName N,double V)
	{
		if (auto* P=CastField<FNumericProperty>(Property(O,N)))
		{ void* D=P->ContainerPtrToValuePtr<void>(O); if (P->IsFloatingPoint()) P->SetFloatingPointPropertyValue(D,V); else P->SetIntPropertyValue(D,static_cast<int64>(V)); return true; }
		return false;
	}
	int32 GetInteger(UObject* O,FName N)
	{ if (auto* P=CastField<FIntProperty>(Property(O,N))) return P->GetPropertyValue_InContainer(O); return INDEX_NONE; }
	bool Call(UObject* O,FName Name)
	{
		UFunction* F=O ? O->FindFunction(Name) : nullptr;
		if (!F || F->NumParms!=0) return false;
		O->ProcessEvent(F,nullptr); return true;
	}
	bool InitializeDirector(UObject* O,APlayerController* Controller,UUserWidget* HUD)
	{
		UFunction* F=O ? O->FindFunction(TEXT("Initialize")) : nullptr;
		if (!F || F->NumParms!=2) return false;
		auto* PCParam=FindFProperty<FObjectPropertyBase>(F,TEXT("PlayerController"));
		auto* HUDParam=FindFProperty<FObjectPropertyBase>(F,TEXT("HUD"));
		if (!PCParam || !HUDParam) return false;
		FStructOnScope Params(F);
		PCParam->SetObjectPropertyValue_InContainer(Params.GetStructMemory(),Controller);
		HUDParam->SetObjectPropertyValue_InContainer(Params.GetStructMemory(),HUD);
		O->ProcessEvent(F,Params.GetStructMemory()); return true;
	}
	bool Bind(UObject* O,FName Name,UObject* Receiver,FName Function)
	{
		if (auto* P=CastField<FMulticastInlineDelegateProperty>(Property(O,Name)))
		{ FScriptDelegate D; D.BindUFunction(Receiver,Function); P->AddDelegate(D,O,P->ContainerPtrToValuePtr<void>(O)); return true; }
		return false;
	}
	bool SetArray(UObject* O,FName Name,const TArray<UObject*>& Values)
	{
		auto* P=CastField<FArrayProperty>(Property(O,Name)); auto* Inner=P ? CastField<FObjectPropertyBase>(P->Inner) : nullptr;
		if (!Inner) return false;
		FScriptArrayHelper A(P,P->ContainerPtrToValuePtr<void>(O)); A.Resize(Values.Num());
		for (int32 I=0; I<Values.Num(); ++I) Inner->SetObjectPropertyValue(A.GetRawPtr(I),Values[I]); return true;
	}
	bool SetIds(UObject* O,const TArray<FString>& Values)
	{
		auto* P=CastField<FArrayProperty>(Property(O,TEXT("LiveCardIds"))); auto* Inner=P ? CastField<FStrProperty>(P->Inner) : nullptr;
		if (!Inner) return false;
		FScriptArrayHelper A(P,P->ContainerPtrToValuePtr<void>(O)); A.Resize(Values.Num());
		for (int32 I=0; I<Values.Num(); ++I) Inner->SetPropertyValue(A.GetRawPtr(I),Values[I]); return true;
	}
}

TSharedRef<SWidget> UGuLiRogueCardOverlay::RebuildWidget()
{
	SetIsFocusable(true);
	// UUserWidget and UOverlay default to SelfHitTestInvisible. This modal must
	// own a hit-testable path so Slate can route presses to NativeOnMouseButtonDown.
	SetVisibility(ESlateVisibility::Visible);
	if (!WidgetTree) WidgetTree=NewObject<UWidgetTree>(this,TEXT("RogueCardTree"));
	auto* Root=WidgetTree->ConstructWidget<UOverlay>(); WidgetTree->RootWidget=Root;
	Root->SetVisibility(ESlateVisibility::Visible);
	Blur=WidgetTree->ConstructWidget<UBackgroundBlur>(); Blur->SetApplyAlphaToBlur(false); Blur->SetBlurStrength(0);
	Blur->SetVisibility(ESlateVisibility::HitTestInvisible);
	Cards=WidgetTree->ConstructWidget<UImage>(); Cards->SetVisibility(ESlateVisibility::HitTestInvisible);
	auto* Labels=WidgetTree->ConstructWidget<UCanvasPanel>(); Labels->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	for (UWidget* W : TArray<UWidget*>{Blur,Cards,Labels})
	{ auto* OverlaySlot=Root->AddChildToOverlay(W); OverlaySlot->SetHorizontalAlignment(HAlign_Fill); OverlaySlot->SetVerticalAlignment(VAlign_Fill); }
    const auto* Player=GetOwningPlayer()?GetOwningPlayer()->GetPlayerState<AGuLiBattlePlayerState>():nullptr;
    const bool Ground=Player&&Player->GetBattleRole()==EGuLiCommanderRole::Ground;
    auto* Theme=Ground?LoadObject<UDataTable>(nullptr,TEXT("/Game/GuLiStrike/Data/DT_GuLiStrikeGroundHUD_Elements")):nullptr;
    bGroundTheme=Ground;
    auto Color=[&](FName Name,FLinearColor Fallback)
    {const auto* Row=Theme?Theme->FindRow<FGuLiStrikeGroundHUDElementsRow>(Name,TEXT("Card HUD theme"),false):nullptr;return Row&&!Row->ColorSRGB.IsEmpty()?FLinearColor::FromSRGBColor(FColor::FromHex(Row->ColorSRGB)):Fallback;};
    if(Ground)
    {
        // Edge anchors share the combat HUD's adaptive design extent. Card capture
        // stays centered at its existing uniform aspect and six-layer projection.
        Labels->RemoveFromParent();auto* Fit=WidgetTree->ConstructWidget<UScaleBox>();Fit->SetStretch(EStretch::ScaleToFit);
        GroundDesign=WidgetTree->ConstructWidget<USizeBox>();GroundDesign->SetWidthOverride(1920);GroundDesign->SetHeightOverride(1080);GroundDesign->SetContent(Labels);Fit->SetContent(GroundDesign);
        auto* OverlaySlot=Root->AddChildToOverlay(Fit);OverlaySlot->SetHorizontalAlignment(HAlign_Fill);OverlaySlot->SetVerticalAlignment(VAlign_Fill);
        auto* Scrim=WidgetTree->ConstructWidget<UBorder>();Scrim->SetBrushColor(Color(TEXT("Panel"),FLinearColor::Black).CopyWithNewOpacity(.6f));Scrim->SetVisibility(ESlateVisibility::HitTestInvisible);Blur->SetContent(Scrim);
        auto Add=[&](UWidget* Widget,FVector2D P,FVector2D S,int32 Z=0){auto* Slot=Labels->AddChildToCanvas(Widget);Slot->SetPosition(P);Slot->SetSize(S);Slot->SetZOrder(Z);};
        auto Label=[&](FName Key,FVector2D P,FVector2D S,int32 Size)
        {
            auto* T=WidgetTree->ConstructWidget<UTextBlock>();T->SetText(GuLiGameText::Get(*(TEXT("UI.GroundHUD.")+Key.ToString())));
            T->SetFont(GuLiGroundUI::Font(Theme,Size,Size>=24));
            T->SetColorAndOpacity(Color(TEXT("Text"),FLinearColor::White));T->SetJustification(ETextJustify::Center);T->SetVisibility(ESlateVisibility::HitTestInvisible);Add(T,P,S,2);return T;
        };
        auto* Chrome=WidgetTree->ConstructWidget<UGuLiGroundHUDChrome>();Chrome->SetTheme(Theme);Chrome->SetMode(3);Chrome->SetVisibility(ESlateVisibility::HitTestInvisible);Add(Chrome,{0,0},{1920,1080});
        TitleLabel=Label(TEXT("SelectTitle"),{700,3},{520,39},26);
        Label(TEXT("SelectOne"),{700,38},{520,28},16)->SetColorAndOpacity(GuLiGroundUI::Light(Color(TEXT("Line"),FLinearColor::White)));
        Label(TEXT("GroundIdentity"),{1510,52},{340,27},16);
        auto* Brand=Label(TEXT("Brand"),{166,55},{215,28},16);auto Font=Brand->GetFont();Font.LetterSpacing=360;Brand->SetFont(Font);
        HintLabel=Label(TEXT("SelectDirect"),{375,908},{1170,35},20);HintLabel->SetAutoWrapText(true);
        auto MakeButton=[&](FVector2D P,FVector2D S,bool Highlight,UTextBlock*& Text)
        {
            auto* Panel=WidgetTree->ConstructWidget<UGuLiGroundHUDPanel>();Panel->SetTheme(Theme,0,true);Panel->SetPadding(FMargin(8,3));Panel->SetHighlighted(Highlight);Add(Panel,P,S,3);
            auto* Button=WidgetTree->ConstructWidget<UButton>();FButtonStyle Skin;Skin.Normal.DrawAs=Skin.Hovered.DrawAs=Skin.Pressed.DrawAs=ESlateBrushDrawType::NoDrawType;Button->SetStyle(Skin);Panel->SetContent(Button);
            Text=WidgetTree->ConstructWidget<UTextBlock>();Text->SetFont(GuLiGroundUI::Font(Theme,27,true));Text->SetJustification(ETextJustify::Center);Text->SetColorAndOpacity(Color(TEXT("Text"),FLinearColor::White));Button->SetContent(Text);return Button;
        };
        UTextBlock* RerollText=nullptr;RerollButton=MakeButton({674,950},{282,76},true,RerollText);RerollLabel=RerollText;
        RerollButton->SetIsEnabled(false);RerollButton->OnClicked.AddUniqueDynamic(this,&ThisClass::HandleRerollClicked);
        UTextBlock* CloseText=nullptr;CloseButton=MakeButton({997,950},{260,76},false,CloseText);CloseText->SetText(GuLiGameText::Get(TEXT("UI.GroundHUD.Close")));CloseButton->OnClicked.AddUniqueDynamic(this,&ThisClass::HandleCloseClicked);
        for(UWidget* Widget:Labels->GetAllChildren())
        {
            auto* FrameSlot=CastChecked<UCanvasPanelSlot>(Widget->Slot);
            if(Widget==Chrome){FrameSlot->SetAnchors(FAnchors(0,0,1,1));FrameSlot->SetOffsets(FMargin());continue;}
            const FVector2D P=FrameSlot->GetPosition();const FVector2D Anchor(P.Y>900?.5:P.X>1400?1.:P.X>600?.5:0.,P.Y>900?1.:0.);
            FrameSlot->SetPosition(P-Anchor*FVector2D(1920,1080));FrameSlot->SetAnchors(FAnchors(Anchor.X,Anchor.Y));
        }
        if(auto* PC=GetOwningPlayer()){int32 W=0,H=0;PC->GetViewportSize(W,H);FitViewport(FVector2D(W,H));}
        return Super::RebuildWidget();
    }
	TitleLabel=WidgetTree->ConstructWidget<UTextBlock>(); HintLabel=WidgetTree->ConstructWidget<UTextBlock>();
	int32 I=0;
	for (UTextBlock* Label : {TitleLabel.Get(),HintLabel.Get()})
	{
		Label->SetVisibility(ESlateVisibility::HitTestInvisible);
        if(Ground)Label->SetColorAndOpacity(Color(TEXT("Text"),FLinearColor::White));
		Label->SetAutoWrapText(I!=0);
		Label->SetJustification(ETextJustify::Center); FSlateFontInfo Font=Label->GetFont(); Font.Size=I==0 ? 24 : 18; Label->SetFont(Font);
		auto* CanvasSlot=Labels->AddChildToCanvas(Label); CanvasSlot->SetAnchors(FAnchors(.05f,I==0?.07f:.92f,I==0?.95f:.76f,I==0?.07f:.92f));
		CanvasSlot->SetOffsets(FMargin(0,0,0,60)); CanvasSlot->SetAlignment(FVector2D(0,.5)); ++I;
	}
	RerollButton=WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(),TEXT("RerollButton"));
	RerollButton->SetBackgroundColor(Ground?Color(TEXT("PanelRaised"),FLinearColor::Black):FLinearColor(.12f,.24f,.34f,1.f)); RerollButton->SetIsEnabled(false);
	RerollButton->OnClicked.AddUniqueDynamic(this,&UGuLiRogueCardOverlay::HandleRerollClicked);
	RerollLabel=WidgetTree->ConstructWidget<UTextBlock>(); RerollLabel->SetVisibility(ESlateVisibility::HitTestInvisible);
	FSlateFontInfo ButtonFont=RerollLabel->GetFont(); ButtonFont.Size=18; RerollLabel->SetFont(ButtonFont);
	RerollLabel->SetJustification(ETextJustify::Center); RerollLabel->SetColorAndOpacity(FSlateColor(Ground?Color(TEXT("Accent"),FLinearColor::White):FLinearColor(1.f,.93f,.8f,1.f)));
	RerollButton->AddChild(RerollLabel);
	auto* ButtonSlot=Labels->AddChildToCanvas(RerollButton); ButtonSlot->SetAnchors(FAnchors(.94f,.92f));
	ButtonSlot->SetAlignment(FVector2D(1.f,.5f)); ButtonSlot->SetOffsets(FMargin(0,0,176,44));
	return Super::RebuildWidget();
}
void UGuLiRogueCardOverlay::Setup(UGuLiRogueCardPresentation* Owner, UMaterialInstanceDynamic* Material)
{
	Presentation=Owner; Cards->SetBrushFromMaterial(Material); Cards->SetVisibility(Material?ESlateVisibility::HitTestInvisible:ESlateVisibility::Hidden);
	if (Owner)
		if (auto* Catalog=Owner->GetWorld()->GetSubsystem<UGuLiRogueCardSubsystem>())
		{
			SetRerollState(false,Catalog->GetText(TEXT("UI.RogueCards.Reroll")));
			RerollButton->SetToolTipText(Catalog->GetText(TEXT("UI.RogueCards.RerollHint")));
		}
}
void UGuLiRogueCardOverlay::SetBackdrop(float Strength) { if (Blur && !FMath::IsNearlyEqual(Blur->GetBlurStrength(),Strength)) Blur->SetBlurStrength(Strength); }
void UGuLiRogueCardOverlay::FitViewport(FVector2D ViewportSize){GuLiGroundUI::FitDesignToViewport(GroundDesign,ViewportSize);}
void UGuLiRogueCardOverlay::ResetPresentation()
{
	Presentation.Reset(); SetBackdrop(0.f);
	if (RerollButton) { RerollButton->OnClicked.RemoveAll(this); RerollButton->SetIsEnabled(false); }
    if(CloseButton)CloseButton->OnClicked.RemoveAll(this);
	if (Cards) Cards->SetBrush(FSlateBrush());
	SetVisibility(ESlateVisibility::Collapsed); RemoveFromParent();
}
void UGuLiRogueCardOverlay::SetCopy(const FText& Title,const FText& Hint)
{ const auto Heading=bGroundTheme?GuLiGameText::Get(TEXT("UI.GroundHUD.SelectTitle")):Title;if(!TitleLabel->GetText().EqualTo(Heading))TitleLabel->SetText(Heading);if(!HintLabel->GetText().EqualTo(Hint))HintLabel->SetText(Hint); }
void UGuLiRogueCardOverlay::SetRerollState(bool bEnabled,const FText& Label)
{
	if (RerollButton && RerollButton->GetIsEnabled()!=bEnabled) RerollButton->SetIsEnabled(bEnabled);
	if (RerollLabel && !RerollLabel->GetText().EqualTo(Label)) RerollLabel->SetText(Label);
}
void UGuLiRogueCardOverlay::HandleRerollClicked()
{ if (Presentation.IsValid()) Presentation->Reroll(); SetKeyboardFocus(); }
void UGuLiRogueCardOverlay::HandleCloseClicked(){if(Presentation.IsValid())Presentation->Cancel();}
FReply UGuLiRogueCardOverlay::NativeOnMouseButtonDown(const FGeometry&,const FPointerEvent& Event)
{
	// Disabled buttons must not let their click fall through to a card behind the footer.
	if (RerollButton && RerollButton->GetCachedGeometry().IsUnderLocation(Event.GetScreenSpacePosition())) return FReply::Handled();
    if(CloseButton&&CloseButton->GetCachedGeometry().IsUnderLocation(Event.GetScreenSpacePosition()))return FReply::Handled();
	if (Presentation.IsValid() && Event.GetEffectingButton()==EKeys::LeftMouseButton) Presentation->Click();
	return FReply::Handled();
}
FReply UGuLiRogueCardOverlay::NativeOnKeyDown(const FGeometry&,const FKeyEvent& Event)
{ if (Presentation.IsValid() && Event.GetKey()==EKeys::Escape && !Event.IsRepeat()) Presentation->Cancel(); return FReply::Handled(); }

UGuLiRogueCardPresentation::UGuLiRogueCardPresentation()
{ PrimaryComponentTick.bCanEverTick=true; PrimaryComponentTick.bStartWithTickEnabled=false;PrimaryComponentTick.TickGroup=TG_PostUpdateWork; }
void UGuLiRogueCardPresentation::Open()
{
	auto* PC=Cast<AGuLiCommanderPlayerController>(GetOwner());
	if (bOpen || bCleaningUp || bTearingDown || !PC || !PC->IsLocalController()) return;
    const auto* Player=PC->GetPlayerState<AGuLiBattlePlayerState>();
    if(!Player||!Player->IsBattleReady()||(!Player->IsCommander()&&Player->GetBattleRole()!=EGuLiCommanderRole::Ground)||(Player->IsCommander()&&!PC->CanIssueCommanderOrders()))return;
    PresentationRole=uint8(Player->GetBattleRole());
    bDirectSelection=Player->GetBattleRole()==EGuLiCommanderRole::Ground;DisplayedNotice=FText::GetEmpty();
	bOpen=true; bClosing=false; bSubmitting=false; Fade=0; DisplayedPhase=INDEX_NONE; ErrorText=FText::GetEmpty();
	bCommitted=false; bFinishedNormally=false;
	bRerolling=false; RerollNotice=FText::GetEmpty();
	RequestId=FGuid::NewGuid(); SessionId.Invalidate(); MatchEpoch=0;
	PlayerGuid=PC->GetPlayerState<AGuLiBattlePlayerState>()->GetPlayerGuid();
	PresentationTeam=static_cast<uint8>(PC->GetPlayerState<AGuLiBattlePlayerState>()->GetTeam());
    SetComponentTickEnabled(true);
    if (!UGuLiNetworkSettings::CanPresent(GetWorld()))
    { PC->GetCommanderNetSyncComponent()->ServerRequestRogueCards(RequestId); return; }
	PC->SetRogueCardModal(true);
	Overlay=CreateWidget<UGuLiRogueCardOverlay>(PC,UGuLiRogueCardOverlay::StaticClass());
	if (!Overlay) { Close(true); return; }
	Overlay->AddToViewport(200);
	Overlay->Setup(this,nullptr);
	auto* Catalog=GetWorld()->GetSubsystem<UGuLiRogueCardSubsystem>();
	Overlay->SetCopy(Catalog->GetText(TEXT("UI.RogueCards.Title")),Catalog->GetText(TEXT("UI.RogueCards.Entering")));
	FInputModeUIOnly Mode; Mode.SetWidgetToFocus(Overlay->TakeWidget()); Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock); PC->SetInputMode(Mode);
	Overlay->SetKeyboardFocus();
	PC->GetCommanderNetSyncComponent()->ServerRequestRogueCards(RequestId);
}
void UGuLiRogueCardPresentation::ReceiveOffer(FGuid Request,FGuid Session,uint32 Epoch,const TArray<FString>& Cards,const FString& Error)
{
	auto* PC=Cast<AGuLiCommanderPlayerController>(GetOwner());
	if (!PC) return;
	if (!bOpen || bClosing || Request!=RequestId)
	{ if (Session.IsValid() && Session!=SessionId) PC->GetCommanderNetSyncComponent()->ServerCancelRogueCards(Session); return; }
	if (Director && !bRerolling) return;
	const bool bReplacing=bRerolling; bRerolling=false;
	auto* Catalog=GetWorld()->GetSubsystem<UGuLiRogueCardSubsystem>();
	const FText Failure=Error.StartsWith(TEXT("UI.RogueCards.")) ? Catalog->GetText(Error) : FText::FromString(Error);
	if (bReplacing && !Error.IsEmpty() && !CandidateIds.IsEmpty() && Session==SessionId && (!Epoch || Epoch==MatchEpoch))
	{
		// A small pool or rate limit leaves the original offer selectable.
		RerollNotice=Failure; DisplayedPhase=INDEX_NONE; if (Director) Director->SetActorTickEnabled(true);
		if (Overlay) Overlay->SetKeyboardFocus();
		return;
	}
	if (bReplacing) DestroyStage();
	RerollNotice=FText::GetEmpty(); DisplayedPhase=INDEX_NONE;
	SessionId=Session; MatchEpoch=Epoch;
	if (Error.IsEmpty() && Cards.IsEmpty() && Session.IsValid())
	{
		if (auto* HUD=Cast<AGuLiCommanderHUD>(PC->GetHUD()))
			HUD->ShowCommandFeedback(GetWorld()->GetSubsystem<UGuLiRogueCardSubsystem>()->GetText(TEXT("UI.RogueCards.Empty")),false);
		Close(true); return;
	}
    const bool bNeedsStage=UGuLiNetworkSettings::CanPresent(GetWorld());
    if (!bNeedsStage && Error.IsEmpty() && !Cards.IsEmpty() && Cards.Num()<=3 && Session.IsValid()) CandidateIds=Cards;
	if (!Error.IsEmpty() || Cards.IsEmpty() || Cards.Num()>3 || !Session.IsValid() || (bNeedsStage && !CreateStage(Cards)))
	{
		if (auto* HUD=Cast<AGuLiCommanderHUD>(PC->GetHUD()))
			HUD->ShowCommandFeedback(FText::Format(GetWorld()->GetSubsystem<UGuLiRogueCardSubsystem>()->GetText(TEXT("UI.RogueCards.OpenFailed")),
				Error.IsEmpty()?FText::FromString(TEXT("Presentation assets require deployment")):Failure),false);
		PC->GetCommanderNetSyncComponent()->ServerCancelRogueCards(SessionId); Close(true);
	}
}
bool UGuLiRogueCardPresentation::CreateStage(const TArray<FString>& Ids)
{
	auto* PC=CastChecked<AGuLiCommanderPlayerController>(GetOwner());
	const auto* Settings=GetDefault<UGuLiRogueCardSettings>();
	auto* Catalog=GetWorld()->GetSubsystem<UGuLiRogueCardSubsystem>();
	UClass* Class=Settings->DirectorClass.LoadSynchronous(); UMaterialInterface* Material=Settings->CaptureMaterial.LoadSynchronous();
	if (!Class || !Material) return false;
	FActorSpawnParameters Params; Params.Owner=PC; Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Director=GetWorld()->SpawnActor<AActor>(Class,FVector(0,0,-1000000),FRotator::ZeroRotator,Params);
	if (!Director) return false;
	Director->SetReplicates(false); Director->SetActorTickEnabled(false);
    AddTickPrerequisiteActor(Director);
    if(!SetBool(Director,TEXT("DirectSelection"),bDirectSelection))return false;
    bTimeAnimatedCards=true;
    if(bDirectSelection)
        if(const auto* Table=LoadObject<UDataTable>(nullptr,TEXT("/Game/GuLiStrike/Data/DT_GuLiStrikeGroundHUD_Elements")))
        {
            if(const auto* Row=Table->FindRow<FGuLiStrikeGroundHUDElementsRow>(TEXT("CardTimeAnimation"),TEXT("Card capture"),false))bTimeAnimatedCards=Row->Value!=0;
            if(!SetNumber(Director,TEXT("GroundCardHeight"),GuLiGroundUI::Number(Table,TEXT("CardHeight"),.68f))||!SetNumber(Director,TEXT("GroundCardSpacing"),GuLiGroundUI::Number(Table,TEXT("CardSpacing"),.335f)))return false;
        }
	TArray<FString> StageIds=Ids;
	while (StageIds.Num()<3) StageIds.Add(Ids[0]); // Hidden placeholders preserve the three-actor Blueprint contract.
	if (!Director->FindFunction(TEXT("CompleteConfirmation")) || !SetBool(Director,TEXT("ExternalConfirmation"),true)
		|| !SetBool(Director,TEXT("AwaitingConfirmation"),false) || !SetBool(Director,TEXT("UseLiveCardData"),true)
		|| !SetIds(Director,StageIds) || !SetNumber(Director,TEXT("ActiveCardCount"),Ids.Num())
		|| !Bind(Director,TEXT("OnConfirmationRequested"),this,GET_FUNCTION_NAME_CHECKED(UGuLiRogueCardPresentation,HandleConfirmation))
		|| !Bind(Director,TEXT("OnPresentationFinished"),this,GET_FUNCTION_NAME_CHECKED(UGuLiRogueCardPresentation,HandleFinished))) return false;
	CandidateIds=Ids;
	TArray<UObject*> Fronts, Frames;
	for (const FString& Id : Ids)
	{
		const auto* Row=Catalog->FindCard(Id); if (!Row) return false;
		Fronts.Add(Row->FrontMaterial.LoadSynchronous());
		Frames.Add(Settings->FrameMaterial.LoadSynchronous());
		if (!Fronts.Last() || !Frames.Last()) return false;
	}
	// Add cannot take a reference into the same TArray: growth can invalidate it.
	// Ground may offer only one or two eligible cards, so copy padding first.
	UObject* const PaddingFront=Fronts[0];
	UObject* const PaddingFrame=Frames[0];
	while (Fronts.Num()<3) { Fronts.Add(PaddingFront); Frames.Add(PaddingFrame); }
	if (!SetArray(Director,TEXT("CardFrontMaterials"),Fronts) || !SetArray(Director,TEXT("CardTextMaterials"),Frames)) return false;
	SetBool(Director,TEXT("SimpleCardFrame"),true); SetNumber(Director,TEXT("CardAreaMultiplier"),2);
	SetNumber(Director,TEXT("CardThicknessMultiplier"),2); SetNumber(Director,TEXT("MaximumTilt"),16);
	SetObject(Director,TEXT("Controller"),PC);
	// Keep the original stage/HUD contract; the unmounted demo HUD never covers the battlefield.
	UClass* HUDClass=LoadClass<UUserWidget>(nullptr,TEXT("/Game/GuLiStrike/CardSystem/RevealDemo/UI/WBP_CardRevealHUD.WBP_CardRevealHUD_C"));
	if (!HUDClass) return false;
	LegacyHUD=CreateWidget<UUserWidget>(PC,HUDClass); SetObject(Director,TEXT("PresentationHUD"),LegacyHUD);
	if (auto* Backdrop=Cast<USceneComponent>(GetObject(Director,TEXT("Backdrop")))) Backdrop->SetVisibility(false,true);
	if (!InitializeDirector(Director,PC,LegacyHUD)) return false;
	auto* Camera=Director->FindComponentByClass<UCameraComponent>(); if (!Camera) return false;
	Target=NewObject<UTextureRenderTarget2D>(this); Target->ClearColor=FLinearColor(0,0,0,1);
	Target->RenderTargetFormat=RTF_RGBA16f; Target->InitAutoFormat(1280,720);
	Capture=NewObject<USceneCaptureComponent2D>(Director,TEXT("RogueCardCapture"));
	Capture->SetupAttachment(Director->GetRootComponent()); Capture->SetRelativeTransform(Camera->GetRelativeTransform());
	Capture->FOVAngle=Camera->FieldOfView; Capture->TextureTarget=Target;
	Capture->CaptureSource=ESceneCaptureSource::SCS_SceneColorHDR; // RGB + inverse opacity, converted by the UI material.
	Capture->PrimitiveRenderMode=ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
    Capture->bCaptureEveryFrame=false; Capture->bCaptureOnMovement=false;Capture->bAlwaysPersistRenderingState=true;
	// OnRegister restores ShowFlags from the archetype, then reapplies these
	// settings. Direct ShowFlags edits made before registration are discarded.
	TArray<FEngineShowFlagsSetting> CaptureShowFlags;
	for (const TCHAR* Name : {TEXT("Atmosphere"),TEXT("Fog"),TEXT("VolumetricFog"),TEXT("Cloud"),TEXT("MotionBlur")})
	{
		FEngineShowFlagsSetting& Flag=CaptureShowFlags.AddDefaulted_GetRef();
		Flag.ShowFlagName=Name; Flag.Enabled=false;
	}
	Capture->SetShowFlagSettings(CaptureShowFlags);
	Capture->PostProcessSettings=Camera->PostProcessSettings; Capture->PostProcessBlendWeight=1.f;
	Capture->RegisterComponent(); Capture->AddTickPrerequisiteActor(Director);
	for (int32 I=0; I<3; ++I)
	{
		auto* Card=Cast<AActor>(GetObject(Director,*FString::Printf(TEXT("Card%d"),I))); if (!Card) return false;
		const auto* Text=Cast<UWidgetComponent>(GetObject(Card,TEXT("EditableText")));
		if (!Text || !Text->GetUserWidgetObject()) return false;
        if(bDirectSelection)GroundCardFrames.Add(Cast<UWidgetComponent>(GetObject(Card,TEXT("EditableText"))));
		Capture->ShowOnlyActorComponents(Card);
		TInlineComponentArray<UPrimitiveComponent*> Components(Card);
        for (auto* Component : Components) {Component->SetVisibleInSceneCaptureOnly(true);CaptureParts.Add(Component);CaptureTransforms.Add(FTransform::Identity);CaptureVisibility.Add(false);}
	}
	CaptureMID=UMaterialInstanceDynamic::Create(Material,this); CaptureMID->SetTextureParameterValue(TEXT("CardCapture"),Target);
    // Ground's manual widget redraw must not freeze a fallback shader into the
    // first capture. Keep its intro stopped until the actual materials are ready.
    bPreparingGroundCards=bDirectSelection;bGroundMaterialsReady=false;NextGroundMaterialCheck=0;GroundWarmupFrames=0;
	Overlay->Setup(this,bPreparingGroundCards?nullptr:CaptureMID.Get()); Overlay->SetCopy(Catalog->GetText(TEXT("UI.RogueCards.Title")),Catalog->GetText(TEXT("UI.RogueCards.Entering")));
	FInputModeUIOnly Mode; Mode.SetWidgetToFocus(Overlay->TakeWidget()); Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock); PC->SetInputMode(Mode);
    Overlay->SetKeyboardFocus(); UpdateCaptureSize(); Director->SetActorTickEnabled(!bPreparingGroundCards);WarmCaptureFrames=3;
    ViewportHandle=FViewport::ViewportResizedEvent.AddUObject(this,&ThisClass::ViewportResized);return true;
}
void UGuLiRogueCardPresentation::UpdateCaptureSize()
{
	if (!Target) return;
	int32 W=0,H=0; CastChecked<APlayerController>(GetOwner())->GetViewportSize(W,H); if (W<1 || H<1) return;
    if(Overlay)Overlay->FitViewport(FVector2D(W,H));
	const float Scale=FMath::Min(1.f,2048.f/FMath::Max(W,H));
	W=FMath::Max(1,FMath::RoundToInt(W*Scale)); H=FMath::Max(1,FMath::RoundToInt(H*Scale));
    if (Target->SizeX!=W || Target->SizeY!=H){Target->ResizeTarget(W,H);WarmCaptureFrames=3;}
}
void UGuLiRogueCardPresentation::ViewportResized(FViewport* V,uint32)
{if(GetWorld()&&GetWorld()->GetGameViewport()&&GetWorld()->GetGameViewport()->Viewport==V)UpdateCaptureSize();}
void UGuLiRogueCardPresentation::PrepareGroundCards()
{
    if(!bPreparingGroundCards||bClosing||!Capture||!Director||!Overlay)return;
    if(!bGroundMaterialsReady)
    {
        const double Now=FPlatformTime::Seconds();if(Now<NextGroundMaterialCheck)return;NextGroundMaterialCheck=Now+.2;
        TArray<UMaterialInterface*> Materials;
        for(const auto& Part:CaptureParts)if(auto* Primitive=Cast<UPrimitiveComponent>(Part.Get()))
        {TArray<UMaterialInterface*> Used;Primitive->GetUsedMaterials(Used);Materials.Append(Used);}
        Materials.Add(CaptureMID);
        TSet<const FMaterialResource*> Checked;bool Pending=false;const UMaterialInterface* Failed=nullptr;
        for(auto* Material:Materials)
        {
            if(!Material)continue;
            // Match the active renderer's platform and completeness contract.
            // DDC/on-demand maps can be complete without the full-compilation
            // finalized flag checked by HasValidGameThreadShaderMap().
            auto* Resource=Material->GetMaterialResource(GMaxRHIShaderPlatform);
            if(!Resource){Failed=Material;continue;}
            if(Checked.Contains(Resource))continue;Checked.Add(Resource);
#if WITH_EDITOR
            if(!Resource->IsCompilationFinished()){Pending=true;continue;}
            if(!Resource->IsGameThreadShaderMapComplete()&&Resource->GetCompileErrors().IsEmpty())
            {Resource->SubmitCompileJobs_GameThread(EShaderCompileJobPriority::High);Pending=true;continue;}
#endif
            if(!Resource->IsGameThreadShaderMapComplete())Failed=Material;
        }
        if(Pending)return;
        if(Failed)
        {
            UE_LOG(LogTemp,Error,TEXT("Ground card material has no usable shader map: %s"),*Failed->GetPathName());
            auto* PC=Cast<AGuLiCommanderPlayerController>(GetOwner());
            auto* Catalog=GetWorld()->GetSubsystem<UGuLiRogueCardSubsystem>();
            if(auto* HUD=PC?Cast<AGuLiCommanderHUD>(PC->GetHUD()):nullptr)
                HUD->ShowCommandFeedback(FText::Format(Catalog->GetText(TEXT("UI.RogueCards.OpenFailed")),Catalog->GetText(TEXT("UI.RogueCards.GroundUnavailable"))),false);
            Cancel();return;
        }
        bGroundMaterialsReady=true;GroundWarmupFrames=3;
    }
    if(GroundWarmupFrames>0)
    {
        // Widget components redraw on their next tick. Let those renders and the
        // deferred capture reach the target before exposing it to the overlay.
        for(const auto& Frame:GroundCardFrames)if(auto* Component=Frame.Get())Component->RequestRedraw();
        Capture->CaptureSceneDeferred();--GroundWarmupFrames;return;
    }
    bPreparingGroundCards=false;Overlay->Setup(this,CaptureMID);Director->SetActorTickEnabled(true);
    WarmCaptureFrames=3;DisplayedPhase=INDEX_NONE;
}
void UGuLiRogueCardPresentation::RefreshCapture()
{
    if(!Capture||!Overlay||!Overlay->IsVisible()||bClosing||bPreparingGroundCards)return;
    const int32 Phase=GetInteger(Director,TEXT("Phase"));
    const int32 Highlight=bSubmitting||bCommitted?GetInteger(Director,TEXT("SelectedIndex")):GetInteger(Director,TEXT("HoveredIndex"));
    if(bDirectSelection&&Highlight!=HighlightedGroundCard)
    {
        HighlightedGroundCard=Highlight;
        for(int32 Index=0;Index<GroundCardFrames.Num();++Index)if(auto* C=GroundCardFrames[Index].Get())
            if(auto* Face=Cast<UGuLiGroundCardFace>(C->GetUserWidgetObject());Face&&Face->SetHighlighted(Index==Highlight)){C->RequestRedraw();WarmCaptureFrames=3;}
    }
    bool Changed=bTimeAnimatedCards||WarmCaptureFrames>0||Phase==0||Phase==2||Phase==4||Phase==5;
    if(WarmCaptureFrames>0)--WarmCaptureFrames;
    for(int32 I=0;I<CaptureParts.Num();++I)if(const auto* C=CaptureParts[I].Get())
    {
        const auto T=C->GetComponentTransform();const bool Visible=C->IsVisible()&&!C->GetOwner()->IsHidden();
        Changed|=!T.Equals(CaptureTransforms[I],.0001)||Visible!=CaptureVisibility[I];CaptureTransforms[I]=T;CaptureVisibility[I]=Visible;
    }
    if(Changed)Capture->CaptureSceneDeferred();
}
void UGuLiRogueCardPresentation::Click()
{
	if (bOpen && !bClosing && !bSubmitting && !bRerolling && !bPreparingGroundCards && ErrorText.IsEmpty())
	{ RerollNotice=FText::GetEmpty(); DisplayedPhase=INDEX_NONE; Call(Director,TEXT("ProcessClick")); }
}
bool UGuLiRogueCardPresentation::CanReroll() const
{
	if (!bOpen || bClosing || bSubmitting || bRerolling || bCommitted || bPreparingGroundCards || !SessionId.IsValid() || !ErrorText.IsEmpty()) return false;
    if (!UGuLiNetworkSettings::CanPresent(GetWorld())) return !CandidateIds.IsEmpty();
    if (!Director) return false;
	const int32 Phase=GetInteger(Director,TEXT("Phase"));
	return Phase==1 || Phase==3; // Stable choosing/confirmation states only, never midway through an animation.
}
void UGuLiRogueCardPresentation::Reroll()
{
	if (!CanReroll()) return;
	auto* PC=Cast<AGuLiCommanderPlayerController>(GetOwner());
	auto* Channel=PC ? PC->GetCommanderNetSyncComponent() : nullptr;
	if (!Channel) return;
	bRerolling=true; RequestId=FGuid::NewGuid(); RerollNotice=FText::GetEmpty(); DisplayedPhase=INDEX_NONE;
	if (Director) Director->SetActorTickEnabled(false);
	auto* Catalog=GetWorld()->GetSubsystem<UGuLiRogueCardSubsystem>();
	if (Overlay)
	{
		Overlay->SetRerollState(false,Catalog->GetText(TEXT("UI.RogueCards.Rerolling")));
		Overlay->SetCopy(Catalog->GetText(TEXT("UI.RogueCards.Title")),Catalog->GetText(TEXT("UI.RogueCards.Rerolling")));
	}
	Channel->ServerRerollRogueCards(RequestId,SessionId);
}
void UGuLiRogueCardPresentation::HandleConfirmation(int32 SelectedIndex)
{ ConfirmSelection(SelectedIndex); }
int32 UGuLiRogueCardPresentation::GetPresentationPhase() const
{ return GetInteger(Director,TEXT("Phase")); }
bool UGuLiRogueCardPresentation::ActivateCandidate(int32 CandidateIndex)
{
    if (!bOpen || bClosing || bSubmitting || bRerolling || bCommitted || bPreparingGroundCards || !CandidateIds.IsValidIndex(CandidateIndex)) return false;
    if (!UGuLiNetworkSettings::CanPresent(GetWorld())) return ConfirmSelection(CandidateIndex);
    const int32 Phase=GetPresentationPhase();
    if (Phase!=1 && (Phase!=3 || GetInteger(Director,TEXT("SelectedIndex"))!=CandidateIndex)) return false;
    auto* Function=Director?Director->FindFunction(TEXT("ActivateHit")):nullptr;
    auto* Index=Function?FindFProperty<FIntProperty>(Function,TEXT("HitIndex")):nullptr;
    if (!Function || Function->NumParms!=1 || !Index) return false;
    FStructOnScope Params(Function);Index->SetPropertyValue_InContainer(Params.GetStructMemory(),CandidateIndex);
    Director->ProcessEvent(Function,Params.GetStructMemory());
    return bDirectSelection ? bSubmitting||bCommitted : Phase==1 ? GetPresentationPhase()==2 && GetInteger(Director,TEXT("SelectedIndex"))==CandidateIndex : bSubmitting || bCommitted;
}
bool UGuLiRogueCardPresentation::ConfirmSelection(int32 SelectedIndex)
{
	if (!bOpen || (UGuLiNetworkSettings::CanPresent(GetWorld()) && !Director) || bClosing || bSubmitting || bRerolling || bCommitted || bPreparingGroundCards || !CandidateIds.IsValidIndex(SelectedIndex)) return false;
	bSubmitting=true; DisplayedPhase=INDEX_NONE;
	CastChecked<AGuLiCommanderPlayerController>(GetOwner())->GetCommanderNetSyncComponent()->ServerConfirmRogueCard(SessionId,CandidateIds[SelectedIndex]);
    return true;
}
void UGuLiRogueCardPresentation::ReceiveResult(FGuid Session,bool bSuccess,const FString& Error)
{
	if (!bOpen || bClosing || Session!=SessionId || !bSubmitting || bCommitted) return;
	bSubmitting=false; DisplayedPhase=INDEX_NONE;
	if (bSuccess)
    {
        bCommitted=true;LastCommittedSession=Session;
        if (UGuLiNetworkSettings::CanPresent(GetWorld())) Call(Director,TEXT("CompleteConfirmation"));
        else {bFinishedNormally=true;Close(true);}
    }
	else ErrorText=FText::Format(GetWorld()->GetSubsystem<UGuLiRogueCardSubsystem>()->GetText(TEXT("UI.RogueCards.Failed")),FText::FromString(Error));
}
void UGuLiRogueCardPresentation::Cancel()
{
	if (!bOpen || bClosing || bSubmitting) return;
	// A successful commit cannot be cancelled during its flash/exit animation.
	const int32 Phase=GetInteger(Director,TEXT("Phase")); if (Phase==4 || Phase==5) return;
	if (auto* PC=Cast<AGuLiCommanderPlayerController>(GetOwner())) PC->GetCommanderNetSyncComponent()->ServerCancelRogueCards(SessionId);
	Close();
}
void UGuLiRogueCardPresentation::HandleFinished(int32)
{ if (bOpen && !bClosing && bCommitted) { bFinishedNormally=true; Close(); } }
void UGuLiRogueCardPresentation::Close(bool bImmediate)
{
	if (bCleaningUp) return;
	if (!bOpen && !Overlay && !Capture && !Director) return;
	if (bClosing) { if (bImmediate) Cleanup(); return; }
	bClosing=true;
	if (Director) { Director->SetActorTickEnabled(false); Call(Director,TEXT("CleanupCards")); }
	if (Overlay) Overlay->Setup(this,nullptr);
	if (Capture) { Capture->bCaptureEveryFrame=false; Capture->bCaptureOnMovement=false; Capture->Deactivate(); }
	if (bImmediate || !Overlay) Cleanup();
}
void UGuLiRogueCardPresentation::Cleanup()
{
	if (bCleaningUp || (!bOpen && !Overlay && !Capture && !Director)) return;
	TGuardValue<bool> Guard(bCleaningUp,true);
	const FGuid CompletedSession=SessionId;
	const bool bNotify=bCommitted && bFinishedNormally && !bTearingDown;
	bOpen=false; bClosing=false; bSubmitting=false; bRerolling=false; bCommitted=false; bFinishedNormally=false;
	SetComponentTickEnabled(false);
	if (Overlay) { auto* Old=Overlay.Get(); Overlay=nullptr; Old->ResetPresentation(); }
	DestroyStage();
	RequestId.Invalidate(); SessionId.Invalidate(); PlayerGuid.Invalidate(); MatchEpoch=0;
	Fade=0.f; DisplayedPhase=INDEX_NONE; ErrorText=FText::GetEmpty(); RerollNotice=FText::GetEmpty();
	if (auto* PC=Cast<AGuLiCommanderPlayerController>(GetOwner()); !bTearingDown && PC && PC->IsLocalController())
	{
		if (UGuLiNetworkSettings::CanPresent(GetWorld())) PC->SetRogueCardModal(false);
		if (bNotify && CompletedSession.IsValid()) PC->GetCommanderNetSyncComponent()->ServerRogueCardPresentationClosed(CompletedSession);
	}
}
void UGuLiRogueCardPresentation::DestroyStage()
{
    FViewport::ViewportResizedEvent.Remove(ViewportHandle);ViewportHandle.Reset();CaptureParts.Reset();CaptureTransforms.Reset();CaptureVisibility.Reset();WarmCaptureFrames=0;
    GroundCardFrames.Reset();HighlightedGroundCard=INDEX_NONE;
    bPreparingGroundCards=false;bGroundMaterialsReady=false;NextGroundMaterialCheck=0;GroundWarmupFrames=0;
	// Reroll releases only the old stage; the same overlay, blur and modal input remain active.
	if (Overlay) Overlay->Setup(this,nullptr);
	if (Capture) { auto* Old=Capture.Get(); Capture=nullptr; Old->bCaptureEveryFrame=false;
		Old->bCaptureOnMovement=false; Old->TextureTarget=nullptr; Old->Deactivate(); Old->DestroyComponent(); }
    if (Director) { auto* Old=Director.Get();RemoveTickPrerequisiteActor(Old); Director=nullptr; Old->SetActorTickEnabled(false);
		Call(Old,TEXT("CleanupCards")); Old->Destroy(); }
	if (LegacyHUD) LegacyHUD->RemoveFromParent();
	// Drop the RHI allocation now rather than accumulating full-resolution targets until the next GC.
	if (Target) Target->ReleaseResource();
	LegacyHUD=nullptr; Target=nullptr; CaptureMID=nullptr; CandidateIds.Reset();
}
void UGuLiRogueCardPresentation::TickComponent(float Dt,ELevelTick Type,FActorComponentTickFunction* Tick)
{
	Super::TickComponent(Dt,Type,Tick);
	const auto* PC=Cast<AGuLiCommanderPlayerController>(GetOwner());
	const auto* Player=PC ? PC->GetPlayerState<AGuLiBattlePlayerState>() : nullptr;
	const auto* State=GetWorld()->GetGameState<AGuLiBattleGameState>();
	if (!Player || uint8(Player->GetBattleRole())!=PresentationRole || Player->GetPlayerGuid()!=PlayerGuid || static_cast<uint8>(Player->GetTeam())!=PresentationTeam)
	{ if (PC && !bSubmitting) PC->GetCommanderNetSyncComponent()->ServerCancelRogueCards(SessionId); Close(true); return; }
    // A retained selection keeps its session identity across a global restore.
    // Temporary lack of a battle permit is not a player cancellation.
    if (!Player->IsBattleReady() || !State) return;
    if (MatchEpoch && State->GetMatchEpoch()!=MatchEpoch)
    {
        if (!UGuLiNetworkSettings::IsHybrid(GetWorld())) {Close(true);return;}
        MatchEpoch=State->GetMatchEpoch();
    }
    if (!UGuLiNetworkSettings::CanPresent(GetWorld())) return;
	if(bPreparingGroundCards&&!bClosing){PrepareGroundCards();if(!bOpen)return;}
	const auto* Settings=GetDefault<UGuLiRogueCardSettings>();
	Fade=FMath::Clamp(Fade+(bClosing?-Dt:Dt)/FMath::Max(.01f,Settings->FadeSeconds),0.f,1.f);
	if (Overlay)
	{
		Overlay->SetBackdrop(Settings->BlurStrength*Fade);
		const int32 Phase=bRerolling ? 8 : bSubmitting ? 7 : GetInteger(Director,TEXT("Phase"));
		auto* Catalog=GetWorld()->GetSubsystem<UGuLiRogueCardSubsystem>();
		Overlay->SetRerollState(CanReroll(),Catalog->GetText(bRerolling?TEXT("UI.RogueCards.Rerolling"):TEXT("UI.RogueCards.Reroll")));
		const FText& Notice=ErrorText.IsEmpty()?RerollNotice:ErrorText;
        if (Phase!=DisplayedPhase || !Notice.EqualTo(DisplayedNotice))
		{
			static const TCHAR* Keys[]={TEXT("Entering"),TEXT("Choose"),TEXT("Flip"),TEXT("Confirm"),TEXT("Success"),TEXT("Success"),TEXT("Done"),TEXT("Submitting"),TEXT("Rerolling")};
            const FText Hint=bDirectSelection && Phase==1?GuLiGameText::Get(TEXT("UI.GroundHUD.SelectDirect")):Catalog->GetText(FString(TEXT("UI.RogueCards."))+Keys[FMath::Clamp(Phase,0,8)]);
            Overlay->SetCopy(Catalog->GetText(TEXT("UI.RogueCards.Title")),Notice.IsEmpty()?Hint:Notice);DisplayedNotice=Notice;
			DisplayedPhase=Phase;
		}
	}
	if (bClosing && Fade<=0) { Cleanup(); return; }
    RefreshCapture();
}
void UGuLiRogueCardPresentation::EndPlay(const EEndPlayReason::Type Reason)
{ bTearingDown=true; Close(true); Super::EndPlay(Reason); }
void UGuLiRogueCardPresentationLibrary::SetLiveCardText(AActor* Card,const FString& CardId,APlayerController* Controller)
{
	if (!Card || !Controller) return;
	auto* Component=Cast<UWidgetComponent>(GetObject(Card,TEXT("EditableText"))); if (!Component) return;
	const auto* Player=Controller->GetPlayerState<AGuLiBattlePlayerState>();
    if(Player&&Player->GetBattleRole()==EGuLiCommanderRole::Ground)
    {
        auto* Widget=CreateWidget<UGuLiGroundCardFace>(Controller,UGuLiGroundCardFace::StaticClass());Widget->TakeWidget();
        auto* Catalog=Card->GetWorld()->GetSubsystem<UGuLiRogueCardSubsystem>();
        Widget->SetCopy(Catalog->GetCardText(CardId,0),Catalog->GetCardText(CardId,1,Player->GetPlayerGuid()));
        Component->SetDrawSize({1280,1556});Component->SetRelativeLocation(FVector(0,.8,0));Component->SetRelativeScale3D(FVector(48.617/1556));
        Component->SetBlendMode(EWidgetBlendMode::Transparent);
        Component->SetWidget(Widget);Component->SetVisibility(true);Component->SetTickWhenOffscreen(true);Component->SetManuallyRedraw(true);Component->RequestRedraw();
        // Uniform cover inside the transparent frame aperture; the six-layer UVs
        // and aspect remain intact while the frame masks portrait overscan.
        if(auto* Face=Cast<USceneComponent>(GetObject(Card,TEXT("CardFace")))){Face->SetRelativeLocation(FVector(0,0,-5));Face->SetRelativeScale3D(FVector(1.35));}
        if(auto* OldFrame=Cast<USceneComponent>(GetObject(Card,TEXT("CardText"))))OldFrame->SetVisibility(false);
        // Keep the rectangular thickness mesh inside the frame's bevels so its
        // square corners cannot protrude through the transparent hover outline.
        if(auto* Edge=Cast<USceneComponent>(GetObject(Card,TEXT("CardSolidEdge")))){auto Scale=Edge->GetRelativeScale3D();Scale.X=37.4/100;Scale.Z=46./100;Edge->SetRelativeScale3D(Scale);}
        return;
    }
	UClass* Class=LoadClass<UUserWidget>(nullptr,TEXT("/Game/GuLiStrike/CardSystem/WarMachineTarot/UI/WBP_CardText.WBP_CardText_C")); if (!Class) return;
	auto* Widget=CreateWidget<UUserWidget>(Controller,Class); Widget->TakeWidget();
	auto* Catalog=Card->GetWorld()->GetSubsystem<UGuLiRogueCardSubsystem>();
	if (auto* Title=Cast<UTextBlock>(Widget->GetWidgetFromName(TEXT("CardTitle")))) Title->SetText(Catalog->GetCardText(CardId,0));
	if (auto* Description=Cast<URichTextBlock>(Widget->GetWidgetFromName(TEXT("CardDescription"))))
    {const auto* PS=Controller->GetPlayerState<AGuLiBattlePlayerState>();Description->SetText(Catalog->GetCardText(CardId,1,PS?PS->GetPlayerGuid():FGuid{}));}
	Component->SetWidget(Widget); Component->SetVisibility(true); Component->SetTickWhenOffscreen(true); Component->RequestRedraw();
}

bool UGuLiRogueCardPresentationLibrary::RebuildGameTextStringTable(UStringTable* StringTable,UDataTable* Source)
{
#if WITH_EDITOR
	if (!StringTable || !Source || Source->GetRowStruct()!=FGuLiStrikeGameTextsTextsRow::StaticStruct()) return false;
	StringTable->Modify(); auto Strings=StringTable->GetMutableStringTable();
	Strings->SetNamespace(TEXT("GuLiStrike.GameTexts")); Strings->ClearSourceStrings();
	for (const FName RowName : Source->GetRowNames())
	{
		const auto* Row=Source->FindRow<FGuLiStrikeGameTextsTextsRow>(RowName,TEXT("StringTableExport"));
		Strings->SetSourceString(Row->TextId,Row->Content);
	}
	StringTable->MarkPackageDirty(); return true;
#else
	return false;
#endif
}
