#include "Gameplay/GroundMech/UI/GuLiGroundHUDWidget.h"
#include "Gameplay/GroundMech/UI/GuLiGroundMapWidget.h"
#include "Gameplay/GroundMech/UI/GuLiGroundHUDVisuals.h"
#include "Gameplay/GroundMech/GuLiGroundMechCharacter.h"
#include "Gameplay/GroundMech/Weapons/GuLiGroundMechWeaponComponent.h"
#include "Gameplay/Data/Generated/GuLiStrikeGroundHUDTableRows.h"
#include "Gameplay/Data/GuLiGameText.h"
#include "Gameplay/Data/GuLiIconCatalog.h"
#include "Commander/Framework/GuLiCommanderPlayerController.h"
#include "Blueprint/WidgetTree.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Border.h"
#include "Components/TextBlock.h"
#include "Components/Image.h"
#include "Components/NativeWidgetHost.h"
#include "Components/ProgressBar.h"
#include "Components/VerticalBox.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/Button.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/ScrollBox.h"
#include "Components/UniformGridPanel.h"
#include "Components/UniformGridSlot.h"
#include "Components/ScaleBox.h"
#include "Serialization/JsonSerializer.h"
#include "Components/VerticalBoxSlot.h"
#include "EnhancedInputSubsystems.h"
#include "InputAction.h"
#include "Engine/LocalPlayer.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "Camera/PlayerCameraManager.h"
#include "TimerManager.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SInvalidationPanel.h"
#include "Widgets/SLeafWidget.h"
#include "Styling/CoreStyle.h"
#include "Rendering/DrawElements.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

namespace
{
FText Copy(const TCHAR* Key){return GuLiGameText::Get(*(FString(TEXT("UI.GroundHUD."))+Key));}
FString Format(const TCHAR* Key,const TArray<FString>& Values){return GuLiGameText::Format(*(FString(TEXT("UI.GroundHUD."))+Key),Values);}
void Set(UTextBlock* T,const FText& Value){if(T&&!T->GetText().EqualTo(Value))T->SetText(Value);}
void Set(UTextBlock* T,const FString& Value){Set(T,FText::FromString(Value));}
void Place(UCanvasPanelSlot* S,FAnchors A,FVector2D P,FVector2D Size){S->SetAnchors(A);S->SetPosition(P);S->SetSize(Size);}
}
class SGuLiGroundReticle final:public SLeafWidget
{
public:
    SLATE_BEGIN_ARGS(SGuLiGroundReticle){}SLATE_END_ARGS()
    void Construct(const FArguments&,UGuLiGroundHUDWidget* In){Owner=In;SetVisibility(EVisibility::HitTestInvisible);}
    virtual FVector2D ComputeDesiredSize(float) const override{return FVector2D::ZeroVector;}
    virtual int32 OnPaint(const FPaintArgs&,const FGeometry& G,const FSlateRect&,FSlateWindowElementList& E,int32 L,const FWidgetStyle&,bool) const override
    {
        if(!Owner.IsValid()||Owner->ModalMode||Owner->bRogueHidden)return L;
        const auto* PC=Owner->Controller();if(!PC||!PC->CanUseGroundMechFireInput()||PC->bShowMouseCursor)return L;
        const auto Size=G.GetLocalSize(),C=Size*.5;const auto Color=Owner->Color(TEXT("Text"));
        const double Scale=FMath::Min(Size.X/1920.,Size.Y/1080.);
        auto Line=[&](FVector2D A,FVector2D B,FLinearColor Tint,float W=1.5f)
        {
            TArray<FVector2D> P{A,B};
            FSlateDrawElement::MakeLines(E,L,G.ToPaintGeometry(),P,ESlateDrawEffect::None,Owner->Color(TEXT("Panel")).CopyWithNewOpacity(Tint.A*.8f),true,(W+2.f)*Scale);
            FSlateDrawElement::MakeLines(E,L+1,G.ToPaintGeometry(),P,ESlateDrawEffect::None,Tint,true,W*Scale);
        };
        const double Now=FPlatformTime::Seconds();
        const float Kick=Owner->ShotExpansion(Now);
        const float ShotAge=Now-Owner->ShotStarted;
        const float Jitter=Owner->ShotStarted>=0&&ShotAge<Owner->MotionStyle.Shot?
            Owner->MotionStyle.Jitter*FMath::Sin(ShotAge*85+Owner->ShotPulses*.73f)*(1-ShotAge/Owner->MotionStyle.Shot):0;
        for(const FVector2D D:{FVector2D(-1,0),FVector2D(1,0),FVector2D(0,-1),FVector2D(0,1)})
        {
            const FVector2D Tangent(-D.Y,D.X);
            Line(C+(D*(8+Kick)+Tangent*Jitter)*Scale,C+(D*(23+Kick)+Tangent*Jitter)*Scale,Color,2.6f);
        }
        if(Owner->bMuzzle)
        {
            const auto P=Owner->MuzzlePoint;
            const auto MuzzleColor=GuLiGroundUI::Light(Owner->Color(TEXT("Secondary")));
            // Preserve the actual impact point. When both rays align, a compact
            // diamond fits inside the aim cross instead of disappearing.
            const double R=(FVector2D::Distance(P,C)<18*Scale?5.:12.)*Scale;
            Line(P+FVector2D(-R,0),P+FVector2D(0,-R),MuzzleColor,2.2f);
            Line(P+FVector2D(0,-R),P+FVector2D(R,0),MuzzleColor,2.2f);
            Line(P+FVector2D(R,0),P+FVector2D(0,R),MuzzleColor,2.2f);
            Line(P+FVector2D(0,R),P+FVector2D(-R,0),MuzzleColor,2.2f);
        }
        const double Remaining=Owner->HitUntil-Now;
        if(Remaining>0)
        {
            const float T=FMath::Clamp(float((Now-Owner->HitStarted)/(Owner->HitUntil-Owner->HitStarted)),0.f,1.f);
            auto Tint=Owner->Color(Owner->bKill?TEXT("Accent"):TEXT("Text"));Tint.A=FMath::Pow(1-T,.65f);
            const double RadiusScale=(1+.30f*FMath::Square(1-T))*Scale;
            for(int32 X:{-1,1})for(int32 Y:{-1,1})Line(C+FVector2D(19*X,19*Y)*RadiusScale,C+FVector2D(29*X,29*Y)*RadiusScale,Tint,Owner->bKill?3.2f:2.5f);
        }
        return L+1;
    }
private:TWeakObjectPtr<UGuLiGroundHUDWidget> Owner;
};
AGuLiCommanderPlayerController* UGuLiGroundHUDWidget::Controller() const{return Cast<AGuLiCommanderPlayerController>(GetOwningPlayer());}
FLinearColor UGuLiGroundHUDWidget::Color(FName N) const
{
    const auto* R=Style?Style->FindRow<FGuLiStrikeGroundHUDElementsRow>(N,TEXT("Ground HUD"),false):nullptr;
    return R&&!R->ColorSRGB.IsEmpty()?FLinearColor::FromSRGBColor(FColor::FromHex(R->ColorSRGB)):FLinearColor::White;
}
float UGuLiGroundHUDWidget::Number(FName N,float Default) const
{const auto* R=Style?Style->FindRow<FGuLiStrikeGroundHUDElementsRow>(N,TEXT("Ground HUD"),false):nullptr;return R?R->Value:Default;}
UTextBlock* UGuLiGroundHUDWidget::Text(const FString& Key,int32 Size)
{
    auto* T=WidgetTree->ConstructWidget<UTextBlock>();const int32 Base=Size>=28?28:Size>=20?22:18;
    const float Config=Number(Size>=28?TEXT("HeadingFont"):Size>=20?TEXT("BodyFont"):TEXT("SmallFont"),Base);
    auto Font=GuLiGroundUI::Font(Style,FMath::Max(10,FMath::RoundToInt(Size*Config/Base)),Size>=24);
    T->SetFont(Font);
    T->SetColorAndOpacity(Color(TEXT("Text")));T->SetVisibility(ESlateVisibility::HitTestInvisible);
    if(!Key.IsEmpty())
    {const auto* Row=Style?Style->FindRow<FGuLiStrikeGroundHUDElementsRow>(*Key,TEXT("HUD label"),false):nullptr;T->SetText(Row&&!Row->TextId.IsEmpty()?GuLiGameText::Get(*Row->TextId):Copy(*Key));}return T;
}
UImage* UGuLiGroundHUDWidget::Icon(FName N,FVector2D Size)
{
    auto* Image=WidgetTree->ConstructWidget<UImage>();Image->SetVisibility(ESlateVisibility::HitTestInvisible);
    if(const auto* R=Style?Style->FindRow<FGuLiStrikeGroundHUDElementsRow>(N,TEXT("Ground HUD"),false):nullptr)
    {
        auto* Texture=Cast<UTexture2D>(R->Texture.LoadSynchronous());if(Texture)Art.AddUnique(Texture);
        FSlateBrush Brush;Brush.SetResourceObject(Texture);Brush.ImageSize=FVector2f(Size);Brush.DrawAs=ESlateBrushDrawType::Image;
        if(R->Sprite>=0){const FVector2f Min((R->Sprite%4)/4.f,(R->Sprite/4)/2.f);Brush.SetUVRegion(FBox2f(Min,Min+FVector2f(.25f,.5f)));}
        Image->SetBrush(Brush);
    }
    return Image;
}
UWidget* UGuLiGroundHUDWidget::Glyph(FName N)
{
    auto* Host=WidgetTree->ConstructWidget<UNativeWidgetHost>();Host->SetVisibility(ESlateVisibility::HitTestInvisible);
    Host->SetContent(GuLiGroundUI::Glyph(N,Color(TEXT("Line")),Color(TEXT("Accent")),Color(TEXT("Text"))));return Host;
}
TSharedRef<SWidget> UGuLiGroundHUDWidget::RebuildWidget()
{
    if(!Root)BuildLayout();const auto Content=Super::RebuildWidget();Reticle=SNew(SGuLiGroundReticle,this);
    return SNew(SOverlay).Visibility(EVisibility::SelfHitTestInvisible)+SOverlay::Slot()[SNew(SInvalidationPanel).Visibility(EVisibility::SelfHitTestInvisible)[Content]]+SOverlay::Slot()[SNew(SInvalidationPanel).Visibility(EVisibility::HitTestInvisible)[Reticle.ToSharedRef()]];
}
void UGuLiGroundHUDWidget::NativeConstruct()
{
    Super::NativeConstruct();SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    PrepareIcons();
    if(auto* PC=Controller();PC&&PC->GetLocalPlayer())
    {
        Data=PC->GetLocalPlayer()->GetSubsystem<UGuLiGroundHUDData>();Data->Bind(PC);Data->SetActive(true);BoundPawn=PC->GetPawn();
        UrgentHandle=Data->OnUrgent.AddUObject(this,&ThisClass::Apply);HitHandle=Data->OnHit.AddUObject(this,&ThisClass::Hit);
        MotionHandle=Data->OnMotion.AddUObject(this,&ThisClass::Motion);
    }
    if(auto* PC=Controller();PC&&PC->GetLocalPlayer())PC->GetLocalPlayer()->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>()->ControlMappingsRebuiltDelegate.AddDynamic(this,&ThisClass::RefreshKeyLabels);
    RefreshKeyLabels();Refresh();FTimerManagerTimerParameters P;P.bLoop=true;P.bMaxOncePerFrame=true;
    GetWorld()->GetTimerManager().SetTimer(RefreshTimer,this,&ThisClass::RefreshPeriodic,FMath::Max(.2f,Number(TEXT("RefreshSeconds"),.2f)),P);
    StartEntry();
}
void UGuLiGroundHUDWidget::NativeDestruct()
{
    if(auto* PC=Controller();PC&&PC->GetLocalPlayer())PC->GetLocalPlayer()->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>()->ControlMappingsRebuiltDelegate.RemoveDynamic(this,&ThisClass::RefreshKeyLabels);
    UpdateAmbient(true);CloseModal(true);ClearMotion();if(GetWorld())GetWorld()->GetTimerManager().ClearTimer(RefreshTimer);
    if(Data){Data->OnUrgent.Remove(UrgentHandle);Data->OnHit.Remove(HitHandle);Data->OnMotion.Remove(MotionHandle);Data->SetActive(false);Data->Unbind();}Data=nullptr;
    if(IconCatalog)IconCatalog->Release();
    Super::NativeDestruct();
}
void UGuLiGroundHUDWidget::ReleaseSlateResources(bool R){Super::ReleaseSlateResources(R);Reticle.Reset();}
void UGuLiGroundHUDWidget::RefreshPeriodic(){++RegularRefreshes;Refresh();}
void UGuLiGroundHUDWidget::Refresh()
{
    RefreshPawnBinding();
    if(bRogueHidden||!Data)return;
    Apply(EGuLiGroundHUDGroup::All);
}
void UGuLiGroundHUDWidget::RefreshPawnBinding()
{
    auto* PC=Controller();APawn* Next=PC?PC->GetPawn():nullptr;
    const bool Changed=BoundPawn.Get()!=Next;
    if(Changed){BoundPawn=Next;bBarsInitialized=false;CloseModal(true);ClearMotion();bObservedHealth=false;bMuzzle=false;}
    if(Data)Data->Bind(PC);
    if(Changed){ObservedGeneration=Data?Data->Read(EGuLiGroundHUDGroup::None).MotionGeneration:0;StartEntry();}
}
void UGuLiGroundHUDWidget::Apply(EGuLiGroundHUDGroup Groups)
{
    if(!Data||bRogueHidden)return;TRACE_CPUPROFILER_EVENT_SCOPE(GuLiGroundHUD_Apply);
    if(ModalMode&&EnumHasAnyFlags(Groups,EGuLiGroundHUDGroup::Vitals)&&Data->Read(EGuLiGroundHUDGroup::Vitals).Health.bDead)
    {CloseModal(true);ClearMotion();}
    const auto VisibleGroups=ModalMode?EGuLiGroundHUDGroup::Map:EGuLiGroundHUDGroup::All;
    Groups&=VisibleGroups;if(Groups==EGuLiGroundHUDGroup::None)return;
    const auto& V=Data->Read(Groups);
    if(ObservedGeneration!=V.MotionGeneration)
    {ObservedGeneration=V.MotionGeneration;ClearMotion();bObservedHealth=false;}
    if(!ModalMode&&EnumHasAnyFlags(Groups,EGuLiGroundHUDGroup::Vitals))
    {
        if(bObservedHealth&&bWasDead&&!V.Health.bDead)StartEntry();
        if(V.Health.bDead&&!bWasDead)ClearMotion();
        bWasDead=V.Health.bDead;bObservedHealth=true;
        Set(HealthText,V.Health.bDead?Copy(TEXT("Dead")):FText::FromString(FString::Printf(TEXT("%.0f"),V.Health.Health)));
        Set(HealthMaximum,FString::Printf(TEXT("/ %.0f"),V.Health.MaxHealth));
        const float HP=V.Health.MaxHealth>0?FMath::Clamp(V.Health.Health/V.Health.MaxHealth,0.f,1.f):0;
        TargetHealth=HP;if(!bBarsInitialized)HealthBar->SetPercent(HP);
        Set(ShieldText,V.Health.MaxShield>0?Format(TEXT("Shield"),{FString::Printf(TEXT("%.0f"),V.Health.Shield),FString::Printf(TEXT("%.0f"),V.Health.MaxShield)}):FString());
    }
    if(!ModalMode&&EnumHasAnyFlags(Groups,EGuLiGroundHUDGroup::Skills))
    {
        UpdateSkillIcons(V);
        Set(FuelText,FString::Printf(TEXT("%.0f%%"),V.Fuel*100));
        const auto FuelColor=Color(V.Fuel<.2?TEXT("Accent"):TEXT("Line"));
        for(int32 I=0;I<FuelSegments.Num();++I){const auto C=(FuelSegments.Num()-I)<=FMath::CeilToInt(V.Fuel*FuelSegments.Num())?GuLiGroundUI::Light(FuelColor):Color(TEXT("PanelRaised"));if(FuelSegments[I]->GetBrushColor()!=C)FuelSegments[I]->SetBrushColor(C);}
        Set(StatValues[0],FString::Printf(TEXT("%02d"),V.WeaponLevel));Set(StatValues[1],FString::Printf(TEXT("%.1f"),V.FireRate));Set(StatValues[2],FString::Printf(TEXT("%.1f"),V.Speed));
        Set(SkillStates[0],V.bWeaponReady?FText::GetEmpty():Copy(TEXT("Disabled")));Set(SkillStates[1],V.bAimReady?FText::GetEmpty():Copy(TEXT("Disabled")));
        Set(SkillStates[2],V.bThrusting||V.bJetReady?FText::GetEmpty():Copy(V.Fuel<=0?TEXT("Empty"):TEXT("Disabled")));
        Set(SkillStates[3],V.BlinkRemaining>0?FText::FromString(FString::Printf(TEXT("%.1f"),V.BlinkRemaining)):V.bBlinkReady?FText::GetEmpty():Copy(TEXT("Disabled")));
        SkillPanels[0]->SetHighlighted(V.bWeaponReady);SkillPanels[2]->SetHighlighted(V.bThrusting);
        bHeldAim=V.bAiming;bHeldJet=V.bThrusting;bCooling=V.BlinkRemaining>0;
        BlinkEndsAt=V.BlinkEndsAt;BlinkDuration=V.BlinkDuration;
        const float Masks[]={V.bWeaponReady?0.f:1.f,V.bAimReady?0.f:1.f,V.bJetReady||V.bThrusting?0.f:1.f,V.bBlinkReady||bCooling?0.f:1.f};
        for(int32 I=0;I<4;++I)
        {
            TargetMasks[I]=Masks[I];if(!bBarsInitialized)SkillMasks[I]->SetRenderScale(FVector2D(1,Masks[I]));
        }
    }
    if(!ModalMode&&EnumHasAnyFlags(Groups,EGuLiGroundHUDGroup::Rogue))
    {
        UpdateRogueCards(V.Cards);
    }
    if(!ModalMode&&EnumHasAnyFlags(Groups,EGuLiGroundHUDGroup::Buffs))
    {
        FString Key;for(const auto& B:V.Buffs)Key+=FString::Printf(TEXT("%d;"),B.Id);
        if(Key!=LastBuffText)
        {
            LastBuffText=Key;BuffList->ClearChildren();BuffTimes.Reset();
            for(int32 I=0;I<FMath::Min(6,V.Buffs.Num());++I)
            {
                const auto& B=V.Buffs[I];auto* Box=WidgetTree->ConstructWidget<USizeBox>();Box->SetWidthOverride(78);Box->SetHeightOverride(76);
                auto* Frame=WidgetTree->ConstructWidget<UGuLiGroundHUDPanel>();Frame->SetTheme(Style,0,true);Frame->SetPadding(FMargin(0));Box->SetContent(Frame);auto* Canvas=WidgetTree->ConstructWidget<UCanvasPanel>();Frame->SetContent(Canvas);
                Place(Canvas->AddChildToCanvas(DataIcon(BuffIconIds.FindRef(B.Id),{54,54})),FAnchors(0,0),{12,5},{54,54});
                auto* T=Text(TEXT(""),17);T->SetJustification(ETextJustify::Right);Place(Canvas->AddChildToCanvas(T),FAnchors(0,0),{5,53},{62,23});BuffTimes.Add(T);Box->SetToolTipText(B.Name);BuffList->AddChildToHorizontalBox(Box)->SetPadding(FMargin(5,0));
            }
            if(V.Buffs.Num()>6){auto* T=Text(TEXT(""),18);Set(T,Format(TEXT("BuffMore"),{FString::FromInt(V.Buffs.Num()-6)}));BuffList->AddChildToHorizontalBox(T);}
        }
        for(int32 I=0;I<BuffTimes.Num();++I)
        {const auto& B=V.Buffs[I];Set(BuffTimes[I],B.bTimed?FText::FromString(FString::Printf(TEXT("%.0fs"),FMath::CeilToDouble(B.Remaining))):FText::GetEmpty());}
    }
    if(EnumHasAnyFlags(Groups,EGuLiGroundHUDGroup::Map))
    {if(!ModalMode){MiniMap->SetSnapshot(&V);const auto StatusVisibility=MiniMap->HasTerrain()?ESlateVisibility::Collapsed:ESlateVisibility::HitTestInvisible;if(MiniMapStatus->GetVisibility()!=StatusVisibility)MiniMapStatus->SetVisibility(StatusVisibility);}if(ModalMode==1){FullMap->SetSnapshot(&V);MapPreview->SetSnapshot(&V);Set(MapTitle,Copy(FullMap->HasTerrain()?TEXT("MapTitle"):TEXT("MapUnavailable")));}}
    if(!ModalMode)bBarsInitialized=true;
}
void UGuLiGroundHUDWidget::UpdateRogueCards(const TArray<FGuLiGroundHUDCard>& Cards)
{
    TArray<const FGuLiGroundHUDCard*> Sorted;
    for(const auto& Card:Cards)Sorted.Add(&Card);
    Sorted.Sort([](const FGuLiGroundHUDCard& A,const FGuLiGroundHUDCard& B){return A.Id<B.Id;});
    TArray<FString> Ids;for(const auto* Card:Sorted)Ids.Add(Card->Id);
    if(!bRogueGridInitialized||Ids!=RogueIds)
    {
        bRogueGridInitialized=true;RogueIds=MoveTemp(Ids);RogueList->ClearChildren();RogueLevels.Reset();RogueImages.Reset();
        for(int32 I=0;I<Sorted.Num();++I)
        {
            const auto& Card=*Sorted[I];
            auto* Box=WidgetTree->ConstructWidget<USizeBox>();Box->SetWidthOverride(72);Box->SetHeightOverride(72);Box->SetToolTipText(Card.Title);
            auto* Frame=WidgetTree->ConstructWidget<UGuLiGroundHUDPanel>();Frame->SetTheme(Style,0,true);Frame->SetPadding(FMargin(0));Box->SetContent(Frame);
            auto* Canvas=WidgetTree->ConstructWidget<UCanvasPanel>();Frame->SetContent(Canvas);
            auto* Image=DataIcon(Card.IconId,{58,58});RogueImages.Add(Card.Id,Image);
            Place(Canvas->AddChildToCanvas(Image),FAnchors(0,0),{7,5},{58,58});
            auto* Badge=WidgetTree->ConstructWidget<UBorder>();Badge->SetBrushColor(Color(TEXT("Panel")));Badge->SetPadding(FMargin(2,0));
            Place(Canvas->AddChildToCanvas(Badge),FAnchors(0,0),{37,49},{31,21});
            auto* Fit=WidgetTree->ConstructWidget<UScaleBox>();Fit->SetStretch(EStretch::ScaleToFit);Fit->SetStretchDirection(EStretchDirection::DownOnly);Badge->SetContent(Fit);
            auto* Level=Text(TEXT(""),16);Level->SetJustification(ETextJustify::Right);Fit->SetContent(Level);RogueLevels.Add(Card.Id,Level);
            RogueList->AddChildToUniformGrid(Box,I/3,I%3);
        }
        const float Height=FMath::Min((Sorted.Num()+2)/3,5)*80.f;
        CastChecked<UCanvasPanelSlot>(RogueScroll->Slot)->SetSize({240,Height});
        CastChecked<UCanvasPanelSlot>(RoguePanel->Slot)->SetSize({272,Height+128});
        CastChecked<UCanvasPanelSlot>(RogueOffer->Slot)->SetPosition({12,Height+24});
    }
    // Acquisitions arrive only after the authority confirms them. Updating a
    // level never recreates icons, resets scrolling, or predicts an acquisition.
    for(const auto* Card:Sorted)
    {
        if(auto* Level=RogueLevels.Find(Card->Id))Set(Level->Get(),FString::FromInt(Card->Count));
        if(auto* Image=RogueImages.Find(Card->Id))
        {
            auto* Texture=IconCatalog?IconCatalog->Find(Card->IconId):nullptr;
            SetDataIconTexture(Image->Get(),Texture);
        }
    }
}
#if WITH_EDITOR
FString UGuLiGroundHUDWidget::ValidateIconLayouts()
{
    auto* Test=NewObject<UGuLiGroundHUDWidget>();Test->BuildLayout();auto Root=MakeShared<FJsonObject>();
    bool Passed=true;TArray<TSharedPtr<FJsonValue>> Cases;
    for(const int32 Count:{0,1,3,15,16})
    {
        TArray<FGuLiGroundHUDCard> Cards;
        for(int32 I=0;I<Count;++I){auto& C=Cards.AddDefaulted_GetRef();C.Id=FString::Printf(TEXT("%02d.01"),I+1);C.Count=I?12:1;}
        Test->UpdateRogueCards(Cards);const auto Before=Test->RogueLevels;
        if(Count)Cards[0].Count=123;Test->UpdateRogueCards(Cards);
        bool Valid=Test->RogueList->GetChildrenCount()==Count;
        for(int32 I=0;I<Count;++I)
        {
            const auto* Slot=CastChecked<UUniformGridSlot>(Test->RogueList->GetChildAt(I)->Slot);
            const auto Text=Test->RogueLevels[Cards[I].Id];
            Valid&=Slot->GetRow()==I/3&&Slot->GetColumn()==I%3&&Before[Cards[I].Id]==Text&&Text->GetText().ToString()==FString::FromInt(Cards[I].Count);
        }
        const float Height=CastChecked<UCanvasPanelSlot>(Test->RogueScroll->Slot)->GetSize().Y;
        Valid&=Height==FMath::Min((Count+2)/3,5)*80;
        for(const FVector2D Resolution:{FVector2D(1920,1080),FVector2D(1920,1200),FVector2D(2520,1080)})
        {
            GuLiGroundUI::FitDesignToViewport(Test->DesignSize,Resolution);
            const FVector2D Size(Test->DesignSize->GetWidthOverride(),Test->DesignSize->GetHeightOverride());
            for(const auto* Widget:Test->Battle->GetAllChildren())
            {
                const auto* Slot=CastChecked<UCanvasPanelSlot>(Widget->Slot);const FVector2D P=Slot->GetPosition()+Slot->GetAnchors().Minimum*Size;
                const FVector2D End=P+Slot->GetSize();Valid&=P.X>=0&&P.Y>=0&&End.X<=Size.X&&End.Y<=Size.Y;
            }
        }
        Passed&=Valid;auto Row=MakeShared<FJsonObject>();Row->SetNumberField(TEXT("icons"),Count);Row->SetNumberField(TEXT("visible_height"),Height);Row->SetBoolField(TEXT("passed"),Valid);Cases.Add(MakeShared<FJsonValueObject>(Row));
    }
    Root->SetBoolField(TEXT("success"),Passed);Root->SetArrayField(TEXT("cases"),Cases);Root->SetBoolField(TEXT("visual_test"),false);
    FString Json;FJsonSerializer::Serialize(Root,TJsonWriterFactory<>::Create(&Json));return Json;
}
#endif
void UGuLiGroundHUDWidget::Hit(bool Kill)
{
    if(ModalMode||bRogueHidden)return;const double Now=FPlatformTime::Seconds();
    if(!Kill&&bKill&&HitUntil>Now)return;
    bKill=Kill;HitStarted=Now;HitUntil=Now+(Kill?MotionStyle.Kill:MotionStyle.Hit);
    if(Kill)++KillPlays;else ++HitPlays;if(Reticle)Reticle->Invalidate(EInvalidateWidgetReason::Paint);
}
void UGuLiGroundHUDWidget::NativeTick(const FGeometry& G,float D)
{
    Super::NativeTick(G,D);if(bRogueHidden)return;
    GuLiGroundUI::FitDesignToViewport(DesignSize,G.GetLocalSize());
    TickMotion(FPlatformTime::Seconds(),D);
    UpdateAmbient();
    if(!ModalMode)
    {
        // Presentation interpolation never advances a gameplay timer or samples gameplay data.
        const float HP=HealthBar->GetPercent();if(!FMath::IsNearlyEqual(HP,TargetHealth,.0001f))HealthBar->SetPercent(FMath::FInterpTo(HP,TargetHealth,D,18.f));
        for(int32 I=0;I<SkillMasks.Num();++I)
        {const float Current=SkillMasks[I]->GetRenderTransform().Scale.Y;if(!FMath::IsNearlyEqual(Current,TargetMasks[I],.0001f))SkillMasks[I]->SetRenderScale(FVector2D(1,FMath::FInterpTo(Current,TargetMasks[I],D,18.f)));}
        UpdateReticle();if(auto* PC=Controller();PC&&PC->PlayerCameraManager){const float Yaw=PC->PlayerCameraManager->GetCameraRotation().Yaw;MiniMap->Present(D,Yaw);Chrome->SetHeading(Yaw);}
    }
    const double Now=FPlatformTime::Seconds();
    const bool ReticleMoving=HitUntil>Now||(ShotStarted>=0&&Now-ShotStarted<MotionStyle.Shot);
    if(Reticle&&(ReticleMoving||bReticleMotionActive))Reticle->Invalidate(EInvalidateWidgetReason::Paint);
    bReticleMotionActive=ReticleMoving;
    if(HitUntil<=Now)HitUntil=0;
}
void UGuLiGroundHUDWidget::UpdateReticle()
{
    const auto* PC=Controller();const auto* P=PC?Cast<AGuLiGroundMechCharacter>(PC->GetPawn()):nullptr;
    const bool Visible=P&&PC->CanUseGroundMechFireInput()&&!PC->bShowMouseCursor;
    if(Visible!=bReticleVisible){bReticleVisible=Visible;if(Reticle)Reticle->Invalidate(EInvalidateWidgetReason::Paint);}
    if(!Visible){if(bMuzzle&&Reticle)Reticle->Invalidate(EInvalidateWidgetReason::Paint);bMuzzle=false;return;}
    const auto Size=GetCachedGeometry().GetLocalSize();FVector Origin,Direction;bool Valid=false;FVector2D Point=MuzzlePoint;
    if(P->GetWeapon()->GetActualShotRay(Origin,Direction))
    {
        FHitResult H;FCollisionQueryParams Q(SCENE_QUERY_STAT(GroundHUDActualRay),false,P);const FVector End=Origin+Direction*600000;
        const bool Hit=GetWorld()->LineTraceSingleByChannel(H,Origin,End,ECC_Visibility,Q);
        Valid=PC->ProjectWorldLocationToScreen(Hit?H.ImpactPoint:End,Point,true);
        Point/=FMath::Max(.01f,UWidgetLayoutLibrary::GetViewportScale(this));
        if(!Valid){const auto Local=PC->PlayerCameraManager->GetCameraRotation().UnrotateVector(Direction);auto Edge=FVector2D(Local.Y,-Local.Z).GetSafeNormal();if(Edge.IsNearlyZero())Edge=FVector2D(0,1);Point=Size*.5+Edge*FMath::Max(Size.X,Size.Y);Valid=true;}
        const double Margin=14*FMath::Min(Size.X/1920.,Size.Y/1080.);
        Point.X=FMath::Clamp(Point.X,Margin,FMath::Max(Margin,Size.X-Margin));Point.Y=FMath::Clamp(Point.Y,Margin,FMath::Max(Margin,Size.Y-Margin));
    }
    if(Valid!=bMuzzle||!Point.Equals(MuzzlePoint,.2)){bMuzzle=Valid;MuzzlePoint=Point;if(Reticle)Reticle->Invalidate(EInvalidateWidgetReason::Paint);}
}
void UGuLiGroundHUDWidget::RefreshKeyLabels()
{
    auto* PC=Controller();auto* Input=PC&&PC->GetLocalPlayer()?PC->GetLocalPlayer()->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>():nullptr;
    const TCHAR* Names[]={TEXT("IA_Battle_Primary"),TEXT("IA_Battle_Secondary"),TEXT("IA_Ground_RocketJump"),TEXT("IA_Ground_Sprint")};
    for(int32 I=0;I<4;++I)
    {
        const auto* A=LoadObject<UInputAction>(nullptr,*(FString(TEXT("/Game/GuLiStrike/GroundMech/Input/"))+Names[I]));
        const auto Keys=Input&&A?Input->QueryKeysMappedToAction(A):TArray<FKey>();FString Label;
        for(const auto& K:Keys){Label=K==EKeys::LeftMouseButton?TEXT("LMB"):K==EKeys::RightMouseButton?TEXT("RMB"):K==EKeys::SpaceBar?TEXT("Space"):K==EKeys::LeftShift||K==EKeys::RightShift?TEXT("Shift"):K.GetDisplayName().ToString();break;}
        Set(KeyLabels[I],Label);
    }
}
void UGuLiGroundHUDWidget::ToggleMap(){if(ModalMode==1)CloseModal();else OpenModal(1);}
void UGuLiGroundHUDWidget::OpenModal(uint8 Mode)
{
    auto* PC=Controller();if(!PC||bRogueHidden)return;
    UpdateAmbient(true);
    const float From=ModalMode?ModalAlpha:0;ClearMotion();ModalMode=Mode;PC->SetGroundHUDModal(true);
    bModalClosing=false;ModalFrom=From;ModalAlpha=From;ModalStarted=FPlatformTime::Seconds();
    Modal->SetIsEnabled(true);Modal->SetRenderOpacity(ModalAlpha);
    Battle->SetVisibility(ESlateVisibility::Collapsed);Modal->SetVisibility(ESlateVisibility::Visible);
    Chrome->SetMode(Mode);MapContent->SetVisibility(Mode==1?ESlateVisibility::SelfHitTestInvisible:ESlateVisibility::Collapsed);
    FullMap->SetVisibility(Mode==1?ESlateVisibility::Visible:ESlateVisibility::Collapsed);MapSide->SetVisibility(Mode==1?ESlateVisibility::SelfHitTestInvisible:ESlateVisibility::Collapsed);
    Set(MapTitle,Copy(TEXT("MapTitle")));MapTitle->SetVisibility(ESlateVisibility::HitTestInvisible);MapHelp->SetVisibility(ESlateVisibility::HitTestInvisible);
    if(Reticle)Reticle->Invalidate(EInvalidateWidgetReason::Paint);Refresh();FInputModeUIOnly Input;Input.SetWidgetToFocus(TakeWidget());Input.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);PC->SetInputMode(Input);SetKeyboardFocus();
}
void UGuLiGroundHUDWidget::CloseModal(bool Immediate)
{
    if(!ModalMode)return;
    if(Immediate){FinishCloseModal();return;}
    if(bModalClosing)return;
    bModalClosing=true;ModalFrom=ModalAlpha;ModalStarted=FPlatformTime::Seconds();Modal->SetIsEnabled(false);
}
void UGuLiGroundHUDWidget::FinishCloseModal()
{
    if(!ModalMode)return;ModalMode=0;bModalClosing=false;ModalStarted=-1;ModalAlpha=0;
    ShowClip(ModalClip,0,0);
    if(Modal){Modal->SetVisibility(ESlateVisibility::Collapsed);Modal->SetRenderOpacity(1);Modal->SetRenderScale(FVector2D(1));Modal->SetRenderTranslation(FVector2D::ZeroVector);Modal->SetIsEnabled(true);}
    if(Battle)Battle->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    if(Chrome)Chrome->SetMode(0);if(MapTitle)MapTitle->SetVisibility(ESlateVisibility::Collapsed);
    if(auto* PC=Controller())PC->SetGroundHUDModal(false);if(Data&&!bRogueHidden)Apply(EGuLiGroundHUDGroup::All);if(Reticle)Reticle->Invalidate(EInvalidateWidgetReason::Paint);
}
void UGuLiGroundHUDWidget::CloseClicked(){CloseModal();}
void UGuLiGroundHUDWidget::CenterClicked(){if(FullMap)FullMap->CenterPlayer();}
void UGuLiGroundHUDWidget::MapClicked(){ToggleMap();}
void UGuLiGroundHUDWidget::RogueClicked(){if(auto* PC=Controller())PC->OpenRogueCards();}
void UGuLiGroundHUDWidget::SetRogueHidden(bool Hidden)
{
    if(bRogueHidden==Hidden)return;if(Hidden){UpdateAmbient(true);CloseModal(true);ClearMotion();}bRogueHidden=Hidden;
    SetVisibility(Hidden?ESlateVisibility::Collapsed:ESlateVisibility::SelfHitTestInvisible);if(Data)Data->SetActive(!Hidden);
    if(GetWorld())
    {
        if(Hidden)GetWorld()->GetTimerManager().PauseTimer(RefreshTimer);
        else{GetWorld()->GetTimerManager().UnPauseTimer(RefreshTimer);RefreshKeyLabels();Refresh();}
    }
}
FReply UGuLiGroundHUDWidget::NativeOnKeyDown(const FGeometry& G,const FKeyEvent& E)
{
    if(!ModalMode)return Super::NativeOnKeyDown(G,E);if(E.IsRepeat())return FReply::Handled();const FKey K=E.GetKey();
    if(K==EKeys::Escape||K==EKeys::M){CloseModal();return FReply::Handled();}
    if(K==EKeys::SpaceBar&&ModalMode==1){CenterClicked();return FReply::Handled();}
    if(K==EKeys::F4){CloseModal(true);if(auto* PC=Controller())PC->OpenRogueCards();return FReply::Handled();}
    return FReply::Handled();
}
void UGuLiGroundHUDWidget::SelectMarker(const FGuLiGroundHUDMarker* M)
{
    if(!M){Set(MapInfo,Copy(TEXT("MapPickLocation")));for(auto* T:{MapDistance.Get(),MapCoordinates.Get(),MapAffiliation.Get()})Set(T,Copy(TEXT("NoSelection")));return;}
    const bool Enemy=Data&&M->Team!=Data->Read(EGuLiGroundHUDGroup::None).Team;
    const auto Name=M->DisplayName.IsEmpty()?Copy(M->Key.Kind==EGuLiHybridObjectKind::Territory?TEXT("Territory"):Enemy?TEXT("Enemy"):TEXT("Friendly")):M->DisplayName;
    Set(MapInfo,Name);const auto& View=Data->Read(EGuLiGroundHUDGroup::None);
    Set(MapDistance,FString::Printf(TEXT("%.0f m"),FVector::Dist2D(View.Position,M->Location)/100));
    Set(MapCoordinates,FString::Printf(TEXT("%.0f, %.0f"),M->Location.X/100,M->Location.Y/100));
    Set(MapAffiliation,Copy(Enemy?TEXT("EnemyLegend"):TEXT("FriendlyLegend")));MapPreview->SetPreviewLocation(M->Location);
}
