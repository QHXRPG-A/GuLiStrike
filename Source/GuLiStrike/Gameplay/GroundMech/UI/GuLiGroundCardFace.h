#pragma once
#include "Blueprint/UserWidget.h"
#include "GuLiGroundCardFace.generated.h"
class UGuLiGroundHUDPanel;
class UTextBlock;
class URichTextBlock;
class UDataTable;

/** A transparent, world-space instrument frame around the existing six-layer artwork. */
UCLASS()
class GULISTRIKE_API UGuLiGroundCardFace final : public UUserWidget
{
    GENERATED_BODY()
public:
    void SetCopy(const FText& Title,const FText& Description);
    bool SetHighlighted(bool Highlighted);
protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
private:
    UPROPERTY(Transient) TObjectPtr<UGuLiGroundHUDPanel> Frame;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> TitleLabel;
    UPROPERTY(Transient) TObjectPtr<URichTextBlock> DescriptionLabel;
    UPROPERTY(Transient) TObjectPtr<UDataTable> Theme;
    FText CardTitle,CardDescription;
};
