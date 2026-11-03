#include "Gameplay/GroundMech/UI/GuLiGroundCardFace.h"
#include "Gameplay/GroundMech/UI/GuLiGroundHUDVisuals.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/ScaleBox.h"
#include "Components/TextBlock.h"
#include "Components/RichTextBlock.h"
#include "Engine/DataTable.h"
#include "Styling/CoreStyle.h"

TSharedRef<SWidget> UGuLiGroundCardFace::RebuildWidget()
{
    // WidgetComponent may release the Slate wrapper after the first TakeWidget.
    // Reuse the UObject tree so rebuilding Slate never clears the authored copy.
    if(Frame)return Super::RebuildWidget();
    if(!WidgetTree)WidgetTree=NewObject<UWidgetTree>(this,TEXT("GroundCardFrameTree"));
    Theme=LoadObject<UDataTable>(nullptr,TEXT("/Game/GuLiStrike/Data/DT_GuLiStrikeGroundHUD_Elements"));
    Frame=WidgetTree->ConstructWidget<UGuLiGroundHUDPanel>();Frame->SetTheme(Theme);Frame->bArtworkFrame=true;Frame->ArtworkMin={.04,.145};Frame->ArtworkMax={.96,.84};Frame->SetPadding(FMargin(0));
    Frame->SetBrushColor(GuLiGroundUI::Color(Theme,TEXT("Panel")).CopyWithNewOpacity(1));WidgetTree->RootWidget=Frame;
    auto* Canvas=WidgetTree->ConstructWidget<UCanvasPanel>();Frame->SetContent(Canvas);
    auto Fit=[&](UWidget* Child,FVector2D Position,FVector2D Size)
    {
        auto* Box=WidgetTree->ConstructWidget<UScaleBox>();Box->SetStretch(EStretch::ScaleToFit);Box->SetStretchDirection(EStretchDirection::DownOnly);Box->SetContent(Child);
        auto* Slot=Canvas->AddChildToCanvas(Box);Slot->SetPosition(Position);Slot->SetSize(Size);
    };
    TitleLabel=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),TEXT("CardTitle"));
    TitleLabel->SetFont(GuLiGroundUI::Font(Theme,GuLiGroundUI::Number(Theme,TEXT("CardTitleFont"),90),true));
    TitleLabel->SetColorAndOpacity(GuLiGroundUI::Color(Theme,TEXT("Text")));TitleLabel->SetJustification(ETextJustify::Center);
    Fit(TitleLabel,{82,43},{1116,154});
    DescriptionLabel=WidgetTree->ConstructWidget<URichTextBlock>(URichTextBlock::StaticClass(),TEXT("CardDescription"));
    DescriptionLabel->SetTextStyleSet(LoadObject<UDataTable>(nullptr,TEXT("/Game/GuLiStrike/GroundMech/UI/DT_GroundCardTextStyles")));
    DescriptionLabel->SetAutoWrapText(false);DescriptionLabel->SetWrapTextAt(0);DescriptionLabel->SetJustification(ETextJustify::Center);
    // This card's frame is authored at twice the legacy text canvas density.
    // Magnify the complete rich-text run uniformly, then retain DownOnly fitting
    // for long copy. Font faces, relative sizes and keyword colours stay Excel-owned.
    auto* DescriptionDensity=WidgetTree->ConstructWidget<UScaleBox>();DescriptionDensity->SetStretch(EStretch::UserSpecified);DescriptionDensity->SetUserSpecifiedScale(2);DescriptionDensity->SetContent(DescriptionLabel);
    Fit(DescriptionDensity,{74,1376},{1132,113});
    TitleLabel->SetText(CardTitle);DescriptionLabel->SetText(CardDescription);
    SetVisibility(ESlateVisibility::HitTestInvisible);return Super::RebuildWidget();
}
void UGuLiGroundCardFace::SetCopy(const FText& Title,const FText& Description)
{CardTitle=Title;CardDescription=Description;if(TitleLabel)TitleLabel->SetText(Title);if(DescriptionLabel)DescriptionLabel->SetText(Description);}
bool UGuLiGroundCardFace::SetHighlighted(bool Value)
{if(Frame&&Frame->IsHighlighted()!=Value){Frame->SetHighlighted(Value);return true;}return false;}
