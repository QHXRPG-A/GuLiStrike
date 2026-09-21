#include "Commander/UI/GuLiCommanderActionButton.h"
UGuLiCommanderActionButton::UGuLiCommanderActionButton()
{
	InitIsFocusable(false);
	OnClicked.AddUniqueDynamic(this, &ThisClass::Dispatch);
	OnHovered.AddUniqueDynamic(this, &ThisClass::Enter);
	OnUnhovered.AddUniqueDynamic(this, &ThisClass::Leave);
}
void UGuLiCommanderActionButton::Dispatch() { Invoked.ExecuteIfBound(Action, Argument); }
void UGuLiCommanderActionButton::Enter() { HoverChanged.ExecuteIfBound(this); }
void UGuLiCommanderActionButton::Leave() { HoverChanged.ExecuteIfBound(nullptr); }
