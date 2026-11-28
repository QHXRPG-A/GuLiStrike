#pragma once

#include "Blueprint/UserWidget.h"
#include "Gameplay/Ship/Build/GuLiShipBuildComponent.h"
#include "GuLiXuntianBellyChoice.generated.h"

class AGuLiStrikeShip;

UCLASS()
class GULISTRIKE_API UGuLiXuntianBellyChoice final : public UUserWidget
{
    GENERATED_BODY()
public:
    UGuLiXuntianBellyChoice(const FObjectInitializer& Initializer);
    UFUNCTION(BlueprintCallable, Category="Ship|Build") void ConfirmUpgrade();
    UFUNCTION(BlueprintCallable, Category="Ship|Build") void Close();
    UFUNCTION() void OnResolved(const FGuLiShipChoiceResult& Result);
protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual FReply NativeOnKeyDown(const FGeometry& Geometry, const FKeyEvent& Key) override;
    virtual void NativeDestruct() override;
private:
    FSlateBrush CardIcon;
    int32 Level() const;
    void SubmitWhenReady();
    FGuid Pending;
    int32 RequestedLevel = 0;
    int64 RequestedEpoch = 0, SubmittedRevision = 0;
    bool bRetried = false;
    FText Feedback;
    TWeakObjectPtr<UGuLiShipBuildComponent> ObservedBuild;
};
