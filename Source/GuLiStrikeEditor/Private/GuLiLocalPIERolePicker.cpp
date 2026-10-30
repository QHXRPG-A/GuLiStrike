#include "GuLiLocalPIERolePicker.h"

#include "Brushes/SlateColorBrush.h"
#include "Editor.h"
#include "Framework/Application/SlateApplication.h"
#include "InputCoreTypes.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "GuLiLocalPIERolePicker"

namespace GuLiLocalPIERolePicker
{
// GuLiStrike art direction, section 11: convert the palette's sRGB values once.
const FLinearColor Background = FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("021716")));
const FLinearColor Panel = FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("032E30")));
const FLinearColor Hover = FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("274E61")));
const FLinearColor Pressed = FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("0A6F73")));
const FLinearColor Accent = FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("6AA4BE")));
const FLinearColor Text = FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("FEE4D9")));
const FSlateColorBrush Solid(FLinearColor::White);

const FButtonStyle& ButtonStyle()
{
    static const FButtonStyle Style = FButtonStyle()
        .SetNormal(FSlateColorBrush(Panel))
        .SetHovered(FSlateColorBrush(Hover))
        .SetPressed(FSlateColorBrush(Pressed))
        .SetNormalPadding(FMargin(16, 12))
        .SetPressedPadding(FMargin(16, 12));
    return Style;
}

FText RoleName(EGuLiCommanderRole Role)
{
    switch (Role)
    {
    case EGuLiCommanderRole::Ground: return LOCTEXT("Ground", "地面机甲");
    case EGuLiCommanderRole::Air: return LOCTEXT("Air", "飞船");
    default: return LOCTEXT("Commander", "指挥官");
    }
}

class SRolePicker final : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SRolePicker) : _InitialRole(EGuLiCommanderRole::Commander) {}
        SLATE_ARGUMENT(TSharedPtr<SWindow>, OwnerWindow)
        SLATE_ARGUMENT(EGuLiCommanderRole, InitialRole)
    SLATE_END_ARGS()

    void Construct(const FArguments& Args)
    {
        OwnerWindow = Args._OwnerWindow;
        SelectedRole = Args._InitialRole;
        ChildSlot
        [
            SNew(SBorder).BorderImage(&Solid).BorderBackgroundColor(Background).Padding(24)
            [
                SNew(SVerticalBox)
                + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 6)
                [
                    SNew(STextBlock).Text(LOCTEXT("Heading", "选择本局位置"))
                    .Font(FCoreStyle::GetDefaultFontStyle("Bold", 22)).ColorAndOpacity(Text)
                ]
                + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 18)
                [
                    SNew(STextBlock).Text(LOCTEXT("Description", "选择你要操控的席位，再进入战场。"))
                    .AutoWrapText(true).ColorAndOpacity(Accent)
                ]
                + SVerticalBox::Slot().FillHeight(1)
                [
                    SNew(SScrollBox)
                    + SScrollBox::Slot().Padding(0, 0, 0, 8)
                    [RoleButton(EGuLiCommanderRole::Commander, LOCTEXT("CommanderDescription", "指挥部队，管理建设与战场。"))]
                    + SScrollBox::Slot().Padding(0, 0, 0, 8)
                    [RoleButton(EGuLiCommanderRole::Ground, LOCTEXT("GroundDescription", "驾驶地面机甲，参与地面作战。"))]
                    + SScrollBox::Slot()
                    [RoleButton(EGuLiCommanderRole::Air, LOCTEXT("AirDescription", "驾驶飞船，参与空中作战。"))]
                ]
                + SVerticalBox::Slot().AutoHeight().Padding(0, 16, 0, 12)
                [
                    SNew(STextBlock).ColorAndOpacity(Text).AutoWrapText(true)
                    .Text_Lambda([this]
                    {
                        return FText::Format(LOCTEXT("Selected", "当前选择：{0}"), RoleName(SelectedRole));
                    })
                ]
                + SVerticalBox::Slot().AutoHeight()
                [
                    SNew(SHorizontalBox)
                    + SHorizontalBox::Slot().FillWidth(1).Padding(0, 0, 8, 0)
                    [
                        SNew(SButton).ButtonStyle(&ButtonStyle()).HAlign(HAlign_Center)
                        .OnClicked(this, &SRolePicker::Cancel)
                        [SNew(STextBlock).Text(LOCTEXT("Cancel", "取消")).ColorAndOpacity(Text)]
                    ]
                    + SHorizontalBox::Slot().FillWidth(1)
                    [
                        SNew(SBorder).BorderImage(&Solid).BorderBackgroundColor(Accent).Padding(2)
                        [
                            SNew(SButton).ButtonStyle(&ButtonStyle()).HAlign(HAlign_Center)
                            .OnClicked(this, &SRolePicker::Confirm)
                            [SNew(STextBlock).Text(LOCTEXT("Enter", "进入战场")).ColorAndOpacity(Text)]
                        ]
                    ]
                ]
            ]
        ];
    }

    virtual bool SupportsKeyboardFocus() const override { return true; }

    virtual FReply OnPreviewKeyDown(const FGeometry& Geometry, const FKeyEvent& Event) override
    {
        if (Event.GetKey() == EKeys::Escape) return Cancel();
        return SCompoundWidget::OnPreviewKeyDown(Geometry, Event);
    }

    TSharedPtr<SWidget> InitialFocus() const { return RoleButtons.FindRef(SelectedRole); }
    TOptional<EGuLiCommanderRole> Result() const
    {
        return bConfirmed ? TOptional<EGuLiCommanderRole>(SelectedRole) : TOptional<EGuLiCommanderRole>();
    }

private:
    TWeakPtr<SWindow> OwnerWindow;
    TMap<EGuLiCommanderRole, TSharedPtr<SButton>> RoleButtons;
    EGuLiCommanderRole SelectedRole = EGuLiCommanderRole::Commander;
    bool bConfirmed = false;

    TSharedRef<SWidget> RoleButton(EGuLiCommanderRole Role, const FText& Description)
    {
        TSharedPtr<SButton> Button;
        TSharedRef<SWidget> Row = SNew(SBorder).BorderImage(&Solid).Padding(2)
            .BorderBackgroundColor_Lambda([this, Role] { return SelectedRole == Role ? Accent : Panel; })
            [
                SAssignNew(Button, SButton).ButtonStyle(&ButtonStyle()).HAlign(HAlign_Fill)
                .OnClicked(this, &SRolePicker::SelectRole, Role)
                [
                    SNew(SVerticalBox)
                    + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 4)
                    [SNew(STextBlock).Text(RoleName(Role)).Font(FCoreStyle::GetDefaultFontStyle("Bold", 16)).ColorAndOpacity(Text)]
                    + SVerticalBox::Slot().AutoHeight()
                    [SNew(STextBlock).Text(Description).AutoWrapText(true).ColorAndOpacity(Text)]
                ]
            ];
        RoleButtons.Add(Role, Button);
        return Row;
    }

    FReply SelectRole(EGuLiCommanderRole Role)
    {
        SelectedRole = Role;
        return FReply::Handled();
    }

    FReply Cancel()
    {
        bConfirmed = false;
        if (const auto Window = OwnerWindow.Pin()) Window->RequestDestroyWindow();
        return FReply::Handled();
    }

    FReply Confirm()
    {
        bConfirmed = true;
        if (const auto Window = OwnerWindow.Pin()) Window->RequestDestroyWindow();
        return FReply::Handled();
    }
};

TOptional<EGuLiCommanderRole> Show(EGuLiCommanderRole InitialRole)
{
    if (!GEditor || !FSlateApplication::IsInitialized()) return {};
    const TWeakPtr<SWidget> PreviousFocus = FSlateApplication::Get().GetKeyboardFocusedWidget();
    const TSharedRef<SWindow> Window = SNew(SWindow)
        .Title(LOCTEXT("WindowTitle", "开局选择"))
        .ClientSize(FVector2D(520, 470)).MinWidth(380).MinHeight(360)
        .SizingRule(ESizingRule::UserSized).AutoCenter(EAutoCenter::PreferredWorkArea)
        .SaneWindowPlacement(true).SupportsMinimize(false).SupportsMaximize(false);
    const TSharedRef<SRolePicker> Picker = SNew(SRolePicker).OwnerWindow(Window).InitialRole(InitialRole);
    Window->SetContent(Picker);
    Window->SetWidgetToFocusOnActivate(Picker->InitialFocus());
    // The modal owns input and returns only after closing. No world or service
    // is started while the player is deciding, and no widget survives this call.
    GEditor->EditorAddModalWindow(Window);
    if (const auto Focus = PreviousFocus.Pin())
        FSlateApplication::Get().SetKeyboardFocus(Focus, EFocusCause::SetDirectly);
    return Picker->Result();
}
}

#undef LOCTEXT_NAMESPACE
