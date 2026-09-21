#pragma once
#include "Components/Button.h"
#include "GuLiCommanderActionButton.generated.h"

DECLARE_DELEGATE_TwoParams(FGuLiCommanderUIAction, FName, int32);
class UGuLiCommanderActionButton;
DECLARE_DELEGATE_OneParam(FGuLiCommanderUIHover, UGuLiCommanderActionButton*);

/** Mouse-only action widget; owner binds a weak UObject delegate and owns all meaning. */
UCLASS()
class GULISTRIKE_API UGuLiCommanderActionButton : public UButton
{
	GENERATED_BODY()
public:
	UGuLiCommanderActionButton();
	FName Action;
	int32 Argument = 0;
	FGuLiCommanderUIAction Invoked;
	FGuLiCommanderUIHover HoverChanged;
	FText Description;
	void SetDescription(const FText& Text) { Description = Text; }
	UPROPERTY(Transient) TObjectPtr<class UTextBlock> Label;
	UPROPERTY(Transient) TObjectPtr<class UImage> Icon;
private:
	UFUNCTION() void Dispatch();
	UFUNCTION() void Enter();
	UFUNCTION() void Leave();
};
