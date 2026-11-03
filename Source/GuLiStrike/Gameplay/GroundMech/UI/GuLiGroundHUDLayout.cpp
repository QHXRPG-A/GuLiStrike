#include "Gameplay/GroundMech/UI/GuLiGroundHUDWidget.h"
#include "Gameplay/GroundMech/UI/GuLiGroundHUDVisuals.h"
#include "Gameplay/GroundMech/UI/GuLiGroundMapWidget.h"
#include "Gameplay/Data/Generated/GuLiStrikeGroundHUDTableRows.h"
#include "Gameplay/Data/GuLiGameText.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Border.h"
#include "Components/TextBlock.h"
#include "Components/Image.h"
#include "Components/ProgressBar.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/ScaleBox.h"
#include "Components/ScaleBoxSlot.h"
#include "Components/Button.h"
#include "Components/ScrollBox.h"
#include "Components/UniformGridPanel.h"
#include "Styling/CoreStyle.h"

void UGuLiGroundHUDWidget::BuildLayout()
{
    if(!WidgetTree)WidgetTree=NewObject<UWidgetTree>(this,TEXT("GroundHUDTree"));
    Style=LoadObject<UDataTable>(nullptr,TEXT("/Game/GuLiStrike/Data/DT_GuLiStrikeGroundHUD_Elements.DT_GuLiStrikeGroundHUD_Elements"));
    if(Style&&Style->GetRowStruct()!=FGuLiStrikeGroundHUDElementsRow::StaticStruct())Style=nullptr;
    if(!Style)UE_LOG(LogTemp,Error,TEXT("Ground HUD presentation table is missing or has the wrong native schema."));
    PrepareIcons();LoadMotionStyle();
    auto Add=[&](UCanvasPanel* Canvas,UWidget* Widget,FVector2D Position,FVector2D Size,int32 Z=0)
    {auto* Slot=Canvas->AddChildToCanvas(Widget);Slot->SetPosition(Position);Slot->SetSize(Size);Slot->SetZOrder(Z);return Slot;};
    auto Label=[&](UCanvasPanel* Canvas,const TCHAR* Key,FVector2D Position,FVector2D Size,int32 Font=22,ETextJustify::Type Align=ETextJustify::Left)
    {auto* T=Text(Key,Font);T->SetJustification(Align);Add(Canvas,T,Position,Size);return T;};
    auto Instrument=[&](UCanvasPanel* Canvas,FName Name,FVector2D Position,FVector2D Size,float Header=0,bool Compact=false)
    {
        auto* P=WidgetTree->ConstructWidget<UGuLiGroundHUDPanel>(UGuLiGroundHUDPanel::StaticClass(),Name);P->SetTheme(Style,Header,Compact);
        if(Name==TEXT("HealthInstrument"))P->Shape=EGuLiGroundInstrumentShape::Health;
        else if(Name==TEXT("JetInstrument"))P->Shape=EGuLiGroundInstrumentShape::Jet;
        else if(Name==TEXT("LocalTerrainInstrument"))P->Shape=EGuLiGroundInstrumentShape::LocalMap;
        else if(Name==TEXT("RogueInstrument"))P->Shape=EGuLiGroundInstrumentShape::Rogue;
        P->SetPadding(FMargin(0));Add(Canvas,P,Position,Size);return P;
    };
    auto Contents=[&](UBorder* P){auto* C=WidgetTree->ConstructWidget<UCanvasPanel>();P->SetContent(C);C->SetVisibility(ESlateVisibility::SelfHitTestInvisible);return C;};
    auto Button=[&](UCanvasPanel* Canvas,const TCHAR* Key,FVector2D Position,FVector2D Size,FName Callback,bool Highlight=false)
    {
        auto* P=Instrument(Canvas,NAME_None,Position,Size,0,true);P->SetHighlighted(Highlight);P->SetPadding(FMargin(8,4));
        auto* B=WidgetTree->ConstructWidget<UButton>();FButtonStyle Skin;Skin.Normal.DrawAs=Skin.Hovered.DrawAs=Skin.Pressed.DrawAs=ESlateBrushDrawType::NoDrawType;B->SetStyle(Skin);
        auto* T=Text(Key,20);T->SetJustification(ETextJustify::Center);
        auto* TextFit=WidgetTree->ConstructWidget<UScaleBox>();TextFit->SetStretch(EStretch::ScaleToFit);TextFit->SetStretchDirection(EStretchDirection::DownOnly);TextFit->SetContent(T);B->SetContent(TextFit);P->SetContent(B);
        FScriptDelegate Click;Click.BindUFunction(this,Callback);B->OnClicked.Add(Click);return B;
    };
    // Preserve instrument proportions, expanding the design extent for other aspects.
    // Edge-anchored panels and chrome cover the viewport; reticles use the full viewport.
    Root=WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(),TEXT("GroundHUDRoot"));WidgetTree->RootWidget=Root;
    auto* Fit=WidgetTree->ConstructWidget<UScaleBox>();Fit->SetStretch(EStretch::ScaleToFit);Fit->SetStretchDirection(EStretchDirection::Both);
    auto* FitSlot=Root->AddChildToCanvas(Fit);FitSlot->SetAnchors(FAnchors(0,0,1,1));FitSlot->SetOffsets(FMargin());
    DesignSize=WidgetTree->ConstructWidget<USizeBox>();DesignSize->SetWidthOverride(1920);DesignSize->SetHeightOverride(1080);Fit->SetContent(DesignSize);
    auto* Design=WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(),TEXT("GroundHUDDesign1920"));DesignSize->SetContent(Design);
    Battle=WidgetTree->ConstructWidget<UCanvasPanel>();Add(Design,Battle,{0,0},{1920,1080});Battle->SetVisibility(ESlateVisibility::SelfHitTestInvisible);

    auto* HealthPanel=Instrument(Battle,TEXT("HealthInstrument"),{18,810},{486,224});auto* HP=Contents(HealthPanel);
    BuildAmbientLoop(HP,0,{410,2});
    auto* HealthPortrait=Instrument(HP,NAME_None,{23,20},{98,142},0,true);HealthPortrait->bKeycap=true;HealthPortrait->SetPadding(FMargin(7));HealthPortrait->SetContent(Glyph(TEXT("Health")));
    auto* HealthNumbers=WidgetTree->ConstructWidget<UHorizontalBox>();
    HealthText=Text(TEXT(""),46);HealthNumbers->AddChildToHorizontalBox(HealthText)->SetVerticalAlignment(VAlign_Bottom);
    HealthMaximum=Text(TEXT(""),22);HealthMaximum->SetColorAndOpacity(Color(TEXT("Secondary")));
    auto* MaximumSlot=HealthNumbers->AddChildToHorizontalBox(HealthMaximum);MaximumSlot->SetVerticalAlignment(VAlign_Bottom);MaximumSlot->SetPadding(FMargin(10,0,0,6));
    auto* NumberFit=WidgetTree->ConstructWidget<UScaleBox>();NumberFit->SetStretch(EStretch::ScaleToFit);NumberFit->SetStretchDirection(EStretchDirection::DownOnly);NumberFit->SetContent(HealthNumbers);CastChecked<UScaleBoxSlot>(HealthNumbers->Slot)->SetHorizontalAlignment(HAlign_Left);Add(HP,NumberFit,{143,48},{311,60});
    HealthBar=WidgetTree->ConstructWidget<UProgressBar>();
    FProgressBarStyle HealthStyle;HealthStyle.BackgroundImage=*FCoreStyle::Get().GetBrush("WhiteBrush");HealthStyle.FillImage=HealthStyle.BackgroundImage;
    HealthStyle.BackgroundImage.TintColor=Color(TEXT("PanelRaised"));HealthStyle.FillImage.TintColor=FLinearColor::White;HealthBar->SetWidgetStyle(HealthStyle);
    HealthBar->SetFillColorAndOpacity(GuLiGroundUI::Light(Color(TEXT("Line"))));HealthBar->SetBorderPadding({3,3});
    Add(HP,HealthBar,{141,104},{313,25});
    ShieldText=Label(HP,TEXT(""),{230,135},{226,22},15,ETextJustify::Right);
    auto* EnergyRail=Icon(TEXT("EnergyRail"),{360,74});auto RailBrush=EnergyRail->GetBrush();RailBrush.SetUVRegion(FBox2f({.31f,.145f},{.69f,.47f}));EnergyRail->SetBrush(RailBrush);EnergyRail->SetRenderOpacity(.4f);Add(HP,EnergyRail,{121,84},{351,70},-1);
    // Compact attributes belong to the same lower-left housing as health.
    auto* AttributeRule=WidgetTree->ConstructWidget<UBorder>();AttributeRule->SetBrushColor(Color(TEXT("Line")).CopyWithNewOpacity(.55f));Add(HP,AttributeRule,{25,165},{436,1});
    const TCHAR* StatKeys[]={TEXT("WeaponLevel"),TEXT("FireRate"),TEXT("MoveSpeed")};
    const TCHAR* StatIcons[]={TEXT("WeaponLevel"),TEXT("FireRate"),TEXT("Speed")};
    const double StatLabelX[]={25,171,317},StatValueX[]={63,209,355};
    for(int32 I=0;I<3;++I)
    {
        auto* Symbol=Glyph(StatIcons[I]);Symbol->SetToolTipText(GuLiGameText::Get(*(FString(TEXT("UI.GroundHUD."))+StatKeys[I])));
        Add(HP,Symbol,{StatLabelX[I],175},{28,28});
        StatValues.Add(Label(HP,TEXT(""),{StatValueX[I],175},{60,29},18));
    }

    auto* Jet=Contents(Instrument(Battle,TEXT("JetInstrument"),{1704,777},{194,244}));
    Add(Jet,DataIcon(StyledIconId(TEXT("IconJetCapacity")),{94,100}),{24,35},{94,100});
    FuelText=Text(TEXT(""),38);auto* FuelFit=WidgetTree->ConstructWidget<UScaleBox>();FuelFit->SetStretch(EStretch::ScaleToFit);FuelFit->SetStretchDirection(EStretchDirection::DownOnly);FuelFit->SetContent(FuelText);CastChecked<UScaleBoxSlot>(FuelText->Slot)->SetHorizontalAlignment(HAlign_Left);Add(Jet,FuelFit,{22,164},{110,55});
    for(int32 I=0;I<16;++I)
    {auto* S=WidgetTree->ConstructWidget<UBorder>();S->SetPadding(FMargin(0));Add(Jet,S,{146,32+I*12.},{23,9});FuelSegments.Add(S);}
    auto* Rail=Icon(TEXT("JetRail"),{13,193});Rail->SetColorAndOpacity(Color(TEXT("Secondary")));Add(Jet,Rail,{133,32},{13,193},-1);

    auto* SkillRack=Instrument(Battle,TEXT("SkillRackInstrument"),{657,872},{606,160},0,false);SkillRack->SetBrushColor(Color(TEXT("PanelRaised")).CopyWithNewOpacity(.77f));
    const TCHAR* Names[]={TEXT("Fire"),TEXT("Aim"),TEXT("Jump"),TEXT("Blink")};
    for(int32 I=0;I<4;++I)
    {
        const auto Name=Names[I];auto* P=Instrument(Battle,*FString::Printf(TEXT("Skill_%s"),Name),{665+I*149.,880},{141,140},0,true);P->SetHighlighted(I==0);SkillPanels.Add(P);
        auto* C=Contents(P);auto* SkillIcon=DataIcon(I==1?StyledIconId(TEXT("IconAim")):0,{97,94});SkillIcons.Add(SkillIcon);Add(C,SkillIcon,{22,11},{97,94});
        auto* Mask=WidgetTree->ConstructWidget<UBorder>();Mask->SetBrushColor(Color(TEXT("Panel")).CopyWithNewOpacity(.82f));Mask->SetVisibility(ESlateVisibility::HitTestInvisible);Mask->SetRenderTransformPivot({0,0});Add(C,Mask,{12,9},{117,102});SkillMasks.Add(Mask);
        BuildSkillMotion(C,I);
        auto* State=Label(C,TEXT(""),{8,40},{125,42},30,ETextJustify::Center);SkillStates.Add(State);
        auto* KeyPanel=Instrument(C,NAME_None,{12,109},{117,26},0,true);KeyPanel->bKeycap=true;KeyPanel->SetPadding(FMargin(0));
        auto* Key=Text(TEXT(""),17);Key->SetJustification(ETextJustify::Center);KeyPanel->SetContent(Key);KeyLabels.Add(Key);
    }
    // Auto-size and centered alignment keep 1..6 statuses tightly grouped over the skill strip.
    BuffList=WidgetTree->ConstructWidget<UHorizontalBox>();auto* BuffSlot=Add(Battle,BuffList,{960,790},{0,76});BuffSlot->SetAutoSize(true);BuffSlot->SetAlignment({.5,0});

    RoguePanel=Instrument(Battle,TEXT("RogueInstrument"),{18,105},{272,128});auto* Rogue=Contents(RoguePanel);
    RogueScroll=WidgetTree->ConstructWidget<UScrollBox>();RogueScroll->SetAllowOverscroll(false);RogueScroll->SetScrollBarVisibility(ESlateVisibility::Collapsed);
    Add(Rogue,RogueScroll,{16,16},{240,0});
    RogueList=WidgetTree->ConstructWidget<UUniformGridPanel>();RogueList->SetSlotPadding(FMargin(4));RogueScroll->AddChild(RogueList);
    auto* Offer=Instrument(Rogue,TEXT("OfferInstrument"),{12,24},{248,88},0,true);RogueOffer=Offer;Offer->SetHighlighted(true);auto* OfferContent=Contents(Offer);
    Label(OfferContent,TEXT("RogueAvailable"),{14,8},{220,28},21,ETextJustify::Center)->SetColorAndOpacity(Color(TEXT("Accent")));
    Button(OfferContent,TEXT("RogueHint"),{13,42},{222,34},GET_FUNCTION_NAME_CHECKED(ThisClass,RogueClicked));

    auto* Mini=Contents(Instrument(Battle,TEXT("LocalTerrainInstrument"),{1521,72},{381,337},48));
    BuildAmbientLoop(Mini,1,{5,3});
    Button(Mini,TEXT("MapHintShort"),{259,10},{109,31},GET_FUNCTION_NAME_CHECKED(ThisClass,MapClicked));
    MiniMap=WidgetTree->ConstructWidget<UGuLiGroundMapWidget>();MiniMap->Configure(false,Number(TEXT("MapRange"),45000),Number(TEXT("ContourStep"),500),Number(TEXT("MapHeightScale"),2));
    Add(Mini,MiniMap,{15,51},{351,269});MiniMap->SetVisibility(ESlateVisibility::HitTestInvisible);
    MiniMapStatus=Label(Mini,TEXT("MapUnavailable"),{22,166},{337,32},20,ETextJustify::Center);

    Modal=WidgetTree->ConstructWidget<UCanvasPanel>();Add(Design,Modal,{0,0},{1920,1080},10);
    auto* Back=WidgetTree->ConstructWidget<UBorder>();Back->SetBrushColor(Color(TEXT("Panel")).CopyWithNewOpacity(.93f));Back->SetVisibility(ESlateVisibility::HitTestInvisible);Add(Modal,Back,{0,92},{1920,930},-1);
    MapContent=WidgetTree->ConstructWidget<UCanvasPanel>();Add(Modal,MapContent,{0,0},{1920,1080});
    auto* MapContainer=Contents(Instrument(MapContent,TEXT("TacticalMapInstrument"),{17,96},{1507,909}));
    FullMap=WidgetTree->ConstructWidget<UGuLiGroundMapWidget>();FullMap->Configure(true,45000,Number(TEXT("ContourStep"),500),0);
    Add(MapContainer,FullMap,{36,36},{1440,836});FullMap->OnSelected.AddUObject(this,&ThisClass::SelectMarker);
    for(int32 I=0;I<6;++I){auto* T=Label(MapContainer,TEXT(""),{125+I*235.,9},{32,22},15,ETextJustify::Center);T->SetText(FText::FromString(FString::Chr('A'+I)));T->SetColorAndOpacity(Color(TEXT("Secondary")));}
    for(int32 I=0;I<4;++I){auto* T=Label(MapContainer,TEXT(""),{10,120+I*210.},{22,25},16,ETextJustify::Center);T->SetText(FText::AsNumber(I+1));T->SetColorAndOpacity(Color(TEXT("Secondary")));}

    MapSide=Instrument(MapContent,TEXT("LocationInstrument"),{1541,96},{361,589},66);auto* Info=Contents(MapSide);
    Label(Info,TEXT("LocationInfo"),{30,22},{300,38},25);
    auto* Selection=Instrument(Info,TEXT("SelectedLocationInstrument"),{17,87},{327,305},64);Selection->SetHighlighted(true);auto* Selected=Contents(Selection);
    MapInfo=Label(Selected,TEXT("MapPickLocation"),{24,18},{279,40},28,ETextJustify::Center);
    MapPreview=WidgetTree->ConstructWidget<UGuLiGroundMapWidget>();MapPreview->Configure(false,25000,Number(TEXT("ContourStep"),500),.3);
    MapPreview->SetVisibility(ESlateVisibility::HitTestInvisible);Add(Selected,MapPreview,{13,75},{301,215});
    const TCHAR* LocationKeys[]={TEXT("Distance"),TEXT("Coordinates"),TEXT("Affiliation")};
    for(int32 I=0;I<3;++I)
    {
        Label(Info,LocationKeys[I],{33,408+I*47.},{108,32},22);
        auto* Value=Label(Info,TEXT("NoSelection"),{139,402+I*47.},{189,38},24,ETextJustify::Right);
        if(I==0)MapDistance=Value;else if(I==1)MapCoordinates=Value;else MapAffiliation=Value;
        auto* Rule=WidgetTree->ConstructWidget<UBorder>();Rule->SetBrushColor(Color(TEXT("Line")));Add(Info,Rule,{28,450+I*47.},{303,1});
    }
    auto* Legend=Contents(Instrument(MapContent,TEXT("LegendInstrument"),{1541,701},{361,306},56));
    Label(Legend,TEXT("LegendTitle"),{33,19},{290,36},25);
    const TCHAR* LegendKeys[]={TEXT("SelfLegend"),TEXT("FriendlyLegend"),TEXT("EnemyLegend"),TEXT("Territory")};
    const TCHAR* Symbols[]={TEXT("MapSelf"),TEXT("MapFriendly"),TEXT("MapEnemy"),TEXT("MapPoint")};
    for(int32 I=0;I<4;++I)
    {
        Add(Legend,Glyph(Symbols[I]),{42,72+I*48.},{52,44});
        Label(Legend,LegendKeys[I],{129,68+I*48.},{170,33},23);
    }
    MapHelp=Label(MapContent,TEXT("MapWheelHint"),{593,1000},{194,37},18,ETextJustify::Center);
    Button(MapContent,TEXT("LocateSelf"),{807,995},{188,48},GET_FUNCTION_NAME_CHECKED(ThisClass,CenterClicked));
    Button(MapContent,TEXT("MapHintShort"),{1007,995},{153,48},GET_FUNCTION_NAME_CHECKED(ThisClass,CloseClicked));
    Button(MapContent,TEXT("Close"),{1171,995},{162,48},GET_FUNCTION_NAME_CHECKED(ThisClass,CloseClicked));
    for(auto* Map:{MiniMap.Get(),FullMap.Get(),MapPreview.Get()})Map->SetColors(Color(TEXT("Panel")),Color(TEXT("PanelRaised")),Color(TEXT("Line")),Color(TEXT("Secondary")),Color(TEXT("Accent")));

    Modal->SetVisibility(ESlateVisibility::Collapsed);

    Chrome=WidgetTree->ConstructWidget<UGuLiGroundHUDChrome>();Chrome->SetTheme(Style);Chrome->SetVisibility(ESlateVisibility::HitTestInvisible);Add(Design,Chrome,{0,0},{1920,1080},-1);
    auto* Header=WidgetTree->ConstructWidget<UCanvasPanel>();Header->SetVisibility(ESlateVisibility::HitTestInvisible);Add(Design,Header,{0,0},{1920,1080},30);
    MapTitle=Label(Header,TEXT(""),{720,3},{480,39},26,ETextJustify::Center);MapTitle->SetVisibility(ESlateVisibility::Collapsed);
    auto Anchor=[](UWidget* Widget,FVector2D Point)
    {
        auto* Slot=CastChecked<UCanvasPanelSlot>(Widget->Slot);
        Slot->SetPosition(Slot->GetPosition()-Point*FVector2D(1920,1080));
        Slot->SetAnchors(FAnchors(Point.X,Point.Y));
    };
    auto Fill=[](UWidget* Widget,FMargin Offsets=FMargin())
    {auto* Slot=CastChecked<UCanvasPanelSlot>(Widget->Slot);Slot->SetAnchors(FAnchors(0,0,1,1));Slot->SetOffsets(Offsets);};
    for(UWidget* Widget:TArray<UWidget*>{Battle.Get(),Modal.Get(),MapContent.Get(),Header,Chrome.Get()})Fill(Widget);
    Fill(Back,FMargin(0,92,0,58));
    for(UWidget* Widget:Battle->GetAllChildren())
    {
        const auto Position=CastChecked<UCanvasPanelSlot>(Widget->Slot)->GetPosition();
        if(Widget==BuffList||Widget==SkillRack||SkillPanels.Contains(Cast<UGuLiGroundHUDPanel>(Widget)))Anchor(Widget,{.5,1});
        else if(Position.X>1500)Anchor(Widget,{1,Position.Y>700?1.:0.});
        else if(Position.Y>800)Anchor(Widget,{0,1});
    }
    Fill(MapContainer->GetParent(),FMargin(17,96,396,75));
    Fill(FullMap,FMargin(36,36,31,37));
    for(UWidget* Widget:MapContainer->GetAllChildren())if(Cast<UTextBlock>(Widget))
    {
        auto* CoordinateSlot=CastChecked<UCanvasPanelSlot>(Widget->Slot);const auto P=CoordinateSlot->GetPosition();
        if(P.Y<30){CoordinateSlot->SetAnchors(FAnchors(P.X/1507.,0));CoordinateSlot->SetPosition({0,P.Y});}
        else if(P.X<30){CoordinateSlot->SetAnchors(FAnchors(0,P.Y/909.));CoordinateSlot->SetPosition({P.X,0});}
    }
    Anchor(MapSide,{1,0});Anchor(Legend->GetParent(),{1,1});
    for(UWidget* Widget:MapContent->GetAllChildren())
        if(CastChecked<UCanvasPanelSlot>(Widget->Slot)->GetPosition().Y>=995)Anchor(Widget,{.5,1});
    for(UWidget* Widget:Header->GetAllChildren())
    {
        const auto Position=CastChecked<UCanvasPanelSlot>(Widget->Slot)->GetPosition();
        if(Position.Y>1000)Anchor(Widget,{.5,1});
        else if(Position.X>=700)Anchor(Widget,{.5,0});
    }
    BuildMotionLayout(Design,Header);
    SetIsFocusable(true);
}
