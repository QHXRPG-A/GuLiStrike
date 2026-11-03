#include "Gameplay/GroundMech/UI/GuLiGroundHUDWidget.h"
#include "Gameplay/Data/GuLiIconCatalog.h"
#include "Gameplay/Data/Generated/GuLiStrikeGroundHUDTableRows.h"
#include "Gameplay/Data/Generated/GuLiStrikeMechTableRows.h"
#include "Gameplay/Data/Generated/GuLiStrikeBuffsTableRows.h"
#include "Gameplay/Data/Generated/GuLiStrikeRogueCardsTableRows.h"
#include "Gameplay/Cards/GuLiRogueCardSettings.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Image.h"
#include "Engine/Texture2D.h"

void UGuLiGroundHUDWidget::PrepareIcons()
{
    if(!IconCatalog)IconCatalog=NewObject<UGuLiIconCatalog>(this);
    TSet<int32> Ids;BuffIconIds.Reset();
    if(Style)Style->ForeachRow<FGuLiStrikeGroundHUDElementsRow>(TEXT("HUD icons"),[&](FName,const auto& R){if(R.IconId>0)Ids.Add(R.IconId);});
    if(auto* Table=GetDefault<UGuLiRogueCardSettings>()->Cards.LoadSynchronous();Table&&Table->GetRowStruct()==FGuLiStrikeRogueCardsCardsRow::StaticStruct())
        Table->ForeachRow<FGuLiStrikeRogueCardsCardsRow>(TEXT("HUD card icons"),[&](FName,const auto& R){if(R.IconId>0)Ids.Add(R.IconId);});
    if(auto* Table=LoadObject<UDataTable>(nullptr,TEXT("/Game/GuLiStrike/Data/DT_GuLiStrikeMech_Skills"));Table&&Table->GetRowStruct()==FGuLiStrikeMechSkillsRow::StaticStruct())
        Table->ForeachRow<FGuLiStrikeMechSkillsRow>(TEXT("HUD skill icons"),[&](FName,const auto& R){if(R.IconId>0)Ids.Add(R.IconId);});
    if(auto* Table=LoadObject<UDataTable>(nullptr,TEXT("/Game/GuLiStrike/Data/DT_GuLiStrikeBuffs_Buffs"));Table&&Table->GetRowStruct()==FGuLiStrikeBuffsBuffsRow::StaticStruct())
        Table->ForeachRow<FGuLiStrikeBuffsBuffsRow>(TEXT("HUD buff icons"),[&](FName,const auto& R){BuffIconIds.Add(R.Id,R.IconId);if(R.IconId>0)Ids.Add(R.IconId);});
    IconCatalog->Prepare(Ids,GetWorld());
}
int32 UGuLiGroundHUDWidget::StyledIconId(FName Name) const
{
    const auto* R=Style?Style->FindRow<FGuLiStrikeGroundHUDElementsRow>(Name,TEXT("HUD icon"),false):nullptr;
    return R?R->IconId:0;
}
UImage* UGuLiGroundHUDWidget::DataIcon(int32 Id,FVector2D Size)
{
    auto* Image=WidgetTree->ConstructWidget<UImage>();Image->SetVisibility(ESlateVisibility::HitTestInvisible);
    FSlateBrush Brush;Brush.ImageSize=FVector2f(Size);Brush.DrawAs=ESlateBrushDrawType::NoDrawType;
    Image->SetBrush(Brush);SetDataIconTexture(Image,IconCatalog?IconCatalog->Find(Id):nullptr);return Image;
}
void UGuLiGroundHUDWidget::SetDataIconTexture(UImage* Image,UTexture2D* Texture)
{
    if(!Image || Image->GetBrush().GetResourceObject()==Texture)return;
    FSlateBrush Brush=Image->GetBrush();Brush.SetResourceObject(Texture);
    Brush.DrawAs=Texture?ESlateBrushDrawType::Image:ESlateBrushDrawType::NoDrawType;
    FVector2D Fit(1,1);
    if(Texture && Brush.ImageSize.X>0 && Brush.ImageSize.Y>0)
    {
        // Preserve source proportions inside the existing slot. This runs only
        // when the cached texture changes, not during HUD animation ticks.
        const double Width=FMath::Max(Texture->GetSizeX(),1);
        const double Height=FMath::Max(Texture->GetSizeY(),1);
        const double Scale=FMath::Min(Brush.ImageSize.X/Width,Brush.ImageSize.Y/Height);
        Fit={Width*Scale/Brush.ImageSize.X,Height*Scale/Brush.ImageSize.Y};
    }
    Image->SetRenderTransformPivot({.5,.5});Image->SetRenderScale(Fit);Image->SetBrush(Brush);
}
void UGuLiGroundHUDWidget::UpdateSkillIcons(const FGuLiGroundHUDSnapshot& View)
{
    for(int32 I=0;I<SkillIcons.Num();++I)
    {
        const int32 Id=I==1?StyledIconId(TEXT("IconAim")):View.SkillIconIds[I];
        auto* Texture=IconCatalog?IconCatalog->Find(Id):nullptr;
        SetDataIconTexture(SkillIcons[I],Texture);
    }
}
