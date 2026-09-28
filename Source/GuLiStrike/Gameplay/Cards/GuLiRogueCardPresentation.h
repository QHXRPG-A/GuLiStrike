#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Blueprint/UserWidget.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GuLiRogueCardPresentation.generated.h"
class UBackgroundBlur;
class UImage;
class UTextBlock;
class USceneCaptureComponent2D;
class UTextureRenderTarget2D;
class UMaterialInstanceDynamic;
class UStringTable;
class UDataTable;
class UGuLiRogueCardPresentation;
class AActor;
class APlayerController;

/** UMG owns input/focus and backdrop; the scene capture owns only the 3D card pixels. */
UCLASS()
class GULISTRIKE_API UGuLiRogueCardOverlay : public UUserWidget
{
	GENERATED_BODY()
	friend class UGuLiRogueCardQALibrary;
public:
	void Setup(UGuLiRogueCardPresentation* InOwner, UMaterialInstanceDynamic* Material);
	void SetBackdrop(float Strength);
	void SetCopy(const FText& Title, const FText& Hint);
	void ResetPresentation();
protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry&,const FPointerEvent&) override;
	virtual FReply NativeOnKeyDown(const FGeometry&,const FKeyEvent&) override;
private:
	UPROPERTY(Transient) TObjectPtr<UBackgroundBlur> Blur;
	UPROPERTY(Transient) TObjectPtr<UImage> Cards;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TitleLabel;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> HintLabel;
	TWeakObjectPtr<UGuLiRogueCardPresentation> Presentation;
};

/** Cosmetic local component. All numeric changes go through the existing commander RPC channel. */
UCLASS()
class GULISTRIKE_API UGuLiRogueCardPresentation : public UActorComponent
{
	GENERATED_BODY()
	friend class UGuLiRogueCardQALibrary;
public:
	UGuLiRogueCardPresentation();
	void Open();
	void Cancel();
	void Click();
	bool IsOpen() const { return bOpen; }
	void ReceiveOffer(FGuid Request, FGuid Session, uint32 Epoch, const TArray<FString>& Cards, const FString& Error);
	void ReceiveResult(FGuid Session, bool bSuccess, const FString& Error);
	virtual void TickComponent(float, ELevelTick, FActorComponentTickFunction*) override;
	virtual void EndPlay(const EEndPlayReason::Type) override;
private:
	UFUNCTION() void HandleConfirmation(int32 SelectedIndex);
	UFUNCTION() void HandleFinished(int32 SelectedIndex);
	bool CreateStage(const TArray<FString>& Cards);
	void Close(bool bImmediate=false);
	void Cleanup();
	void UpdateCaptureSize();
	UPROPERTY(Transient) TObjectPtr<AActor> Director;
	UPROPERTY(Transient) TObjectPtr<UUserWidget> LegacyHUD;
	UPROPERTY(Transient) TObjectPtr<USceneCaptureComponent2D> Capture;
	UPROPERTY(Transient) TObjectPtr<UTextureRenderTarget2D> Target;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> CaptureMID;
	UPROPERTY(Transient) TObjectPtr<UGuLiRogueCardOverlay> Overlay;
	TArray<FString> CandidateIds;
	FGuid RequestId, SessionId, PlayerGuid;
	uint32 MatchEpoch=0;
	uint8 PresentationTeam=0;
	bool bOpen=false, bClosing=false, bSubmitting=false, bTearingDown=false;
	bool bCommitted=false, bFinishedNormally=false, bCleaningUp=false;
	float Fade=0.f;
	int32 DisplayedPhase=INDEX_NONE;
	FText ErrorText;
};

/** Typed Blueprint entry points; no widget is allowed to mutate gameplay values. */
UCLASS()
class GULISTRIKE_API UGuLiRogueCardPresentationLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	UFUNCTION(BlueprintCallable, Category="Cards") static void SetLiveCardText(AActor* Card, const FString& CardId, APlayerController* Controller);
	UFUNCTION(BlueprintCallable, Category="Cards|Editor") static bool RebuildGameTextStringTable(UStringTable* StringTable, UDataTable* Source);
};
