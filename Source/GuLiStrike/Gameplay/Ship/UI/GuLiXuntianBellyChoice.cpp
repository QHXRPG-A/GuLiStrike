#include "Gameplay/Ship/UI/GuLiXuntianBellyChoice.h"
#include "Gameplay/Ship/UI/GuLiXuntianAtlas.h"
#include "Gameplay/Ship/GuLiStrikeShip.h"
#include "Gameplay/Ship/Aiming/GuLiXuntianGunnery.h"
#include "Gameplay/Ship/Build/GuLiShipAssemblyComponent.h"
#include "Gameplay/Data/GuLiGameText.h"
#include "Battle/Framework/GuLiBattlePlayerState.h"
#include "Commander/Framework/GuLiCommanderPlayerController.h"
#include "Engine/Texture2D.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

UGuLiXuntianBellyChoice::UGuLiXuntianBellyChoice(const FObjectInitializer& Initializer) : Super(Initializer)
{
    SetIsFocusable(true);
    static ConstructorHelpers::FObjectFinder<UTexture2D> Atlas(TEXT("/Game/GuLiStrike/Ship/UI/Xuntian/T_XuntianInstruments"));
    CardIcon.SetResourceObject(Atlas.Object);
    const auto& Tile = GuLiXuntianAtlas::CardBelly;
    const float Size = GuLiXuntianAtlas::Size;
    CardIcon.SetUVRegion(FBox2f(FVector2f(Tile.X,Tile.Y)/Size, FVector2f(Tile.X+Tile.W,Tile.Y+Tile.H)/Size));
    CardIcon.ImageSize = FVector2D(48,48);
}

int32 UGuLiXuntianBellyChoice::Level() const
{
    const auto* Ship = Cast<AGuLiStrikeShip>(GetOwningPlayerPawn());
    return Ship ? Ship->GetXuntianGunnery()->GetBellyLevel() : 0;
}

TSharedRef<SWidget> UGuLiXuntianBellyChoice::RebuildWidget()
{
    return SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
        .BorderBackgroundColor(FLinearColor(0.01f,0.016f,0.025f,0.78f)).HAlign(HAlign_Center).VAlign(VAlign_Center)
        [ SNew(SBox).WidthOverride(330)
          [ SNew(SBorder).Padding(22)
            [ SNew(SVerticalBox)
              + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)[ SNew(SImage).Image(&CardIcon) ]
              + SVerticalBox::Slot().AutoHeight().Padding(0,12).HAlign(HAlign_Center)
                [ SNew(STextBlock).Text(GuLiGameText::Get(TEXT("UI.Xuntian.Belly.Title"))) ]
              + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
                [ SNew(STextBlock).Text_Lambda([this] {
                    const int32 Current = Level();
                    return GuLiGameText::Get(Current >= 3 ? TEXT("UI.Xuntian.Belly.Max") :
                        Current == 2 ? TEXT("UI.Xuntian.Belly.Tier3") : TEXT("UI.Xuntian.Belly.Tier2")); }) ]
              + SVerticalBox::Slot().AutoHeight().Padding(0,12)
                [ SNew(SButton).Tag(TEXT("XuntianBellyUpgrade")).HAlign(HAlign_Center).Text(GuLiGameText::Get(TEXT("UI.Xuntian.Belly.Upgrade")))
                    .Visibility_Lambda([this]{ return Level() >= 3 ? EVisibility::Collapsed : EVisibility::Visible; })
                    .IsEnabled_Lambda([this]{ return RequestedLevel == 0 && Level() > 0; })
                    .OnClicked_Lambda([this]{ ConfirmUpgrade(); return FReply::Handled(); }) ]
              + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)[ SNew(STextBlock).Text_Lambda([this]{return Feedback;}) ]
              + SVerticalBox::Slot().AutoHeight().Padding(0,4)
                [ SNew(SButton).HAlign(HAlign_Center).Text(GuLiGameText::Get(TEXT("UI.Xuntian.Belly.Close")))
                    .OnClicked_Lambda([this]{ Close(); return FReply::Handled(); }) ]
            ]
          ]
        ];
}

void UGuLiXuntianBellyChoice::ConfirmUpgrade()
{
    auto* Ship = Cast<AGuLiStrikeShip>(GetOwningPlayerPawn());
    const int32 Current = Level();
    if (!Ship || RequestedLevel || Current < 1 || Current >= 3) return;
    const auto* PS = Ship->GetPlayerState<AGuLiBattlePlayerState>();
    if (!PS) return;
    auto* Build = PS->GetShipBuild();
    ObservedBuild = Build;
    Build->OnChoiceResolved.AddUniqueDynamic(this, &ThisClass::OnResolved);
    RequestedLevel = Current + 1;
    RequestedEpoch = Ship->GetShipAssembly()->GetAppliedMatchEpoch();
    bRetried = false;
    Feedback = GuLiGameText::Get(TEXT("UI.Xuntian.Belly.Pending"));
    SubmitWhenReady();
}

void UGuLiXuntianBellyChoice::SubmitWhenReady()
{
    auto* Ship = Cast<AGuLiStrikeShip>(GetOwningPlayerPawn());
    if (!Ship || !RequestedLevel || Pending.IsValid()) return;
    auto* Assembly = Ship->GetShipAssembly();
    if (RequestedEpoch != Assembly->GetAppliedMatchEpoch() || Level()+1 != RequestedLevel)
    {
        if (RequestedEpoch == Assembly->GetAppliedMatchEpoch() && Level() >= RequestedLevel) { Close(); return; }
        RequestedLevel = 0;
        Feedback = GuLiGameText::Get(TEXT("UI.Xuntian.Belly.Failed"));
        return;
    }
    if (Assembly->HasPendingChoiceForPilot()) return;
    SubmittedRevision = Assembly->GetAppliedBuildRevision();
    Pending = FGuid::NewGuid();
    Ship->SubmitBellyChoice(Pending,RequestedLevel,RequestedEpoch,SubmittedRevision);
}

void UGuLiXuntianBellyChoice::OnResolved(const FGuLiShipChoiceResult& Result)
{
    if (!RequestedLevel) return;
    if (!Pending.IsValid()) { SubmitWhenReady(); return; }
    if (Result.RequestId != Pending) return;
    Pending.Invalidate();
    if (Result.bCommitted) { Close(); return; }
    const auto* Ship = Cast<AGuLiStrikeShip>(GetOwningPlayerPawn());
    if (!bRetried && Ship && Ship->GetShipAssembly()->GetAppliedBuildRevision() > SubmittedRevision)
    {
        bRetried = true;
        SubmitWhenReady();
        return;
    }
    RequestedLevel = 0;
    Feedback = GuLiGameText::Get(TEXT("UI.Xuntian.Belly.Failed"));
}

FReply UGuLiXuntianBellyChoice::NativeOnKeyDown(const FGeometry& Geometry, const FKeyEvent& Key)
{
    if (Key.GetKey() == EKeys::Escape || Key.GetKey() == EKeys::F4) { Close(); return FReply::Handled(); }
    return Super::NativeOnKeyDown(Geometry, Key);
}

void UGuLiXuntianBellyChoice::Close() { RemoveFromParent(); }
void UGuLiXuntianBellyChoice::NativeDestruct()
{
    if (auto* Build = ObservedBuild.Get()) Build->OnChoiceResolved.RemoveDynamic(this, &ThisClass::OnResolved);
    Pending.Invalidate();
    RequestedLevel = 0;
    if (auto* PC = Cast<AGuLiCommanderPlayerController>(GetOwningPlayer())) PC->SetRogueCardModal(false);
    Super::NativeDestruct();
}
