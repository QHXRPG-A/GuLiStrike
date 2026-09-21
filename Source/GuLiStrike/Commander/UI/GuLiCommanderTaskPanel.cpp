#include "Commander/UI/GuLiCommanderHUDWidget.h"
#include "Commander/Framework/GuLiCommanderPlayerController.h"

// Layout owns the drawer; the existing periodic summary contract is retained.
void UGuLiCommanderHUDWidget::BuildTaskPanel() {}
void UGuLiCommanderHUDWidget::RefreshTaskPanel() { RefreshConsoleTasks(); }
void UGuLiCommanderHUDWidget::HandleFocusClicked() { if (auto* PC = CommanderController.Get()) PC->FocusSelectedUnits(); }
void UGuLiCommanderHUDWidget::HandleStopClicked() { if (auto* PC = CommanderController.Get()) PC->StopSelectedUnits(); }
