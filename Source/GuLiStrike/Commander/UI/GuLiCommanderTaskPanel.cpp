#include "Commander/UI/GuLiCommanderHUDWidget.h"
#include "Commander/Framework/GuLiCommanderPlayerController.h"
#include "Commander/UI/GuLiCommanderHUDDataSubsystem.h"
#include "Engine/World.h"
#include "TimerManager.h"

// Layout owns the drawer; the existing periodic summary contract is retained.
void UGuLiCommanderHUDWidget::BuildTaskPanel() {}
void UGuLiCommanderHUDWidget::RefreshTaskPanel()
{
    auto* W=GetWorld();if (!W) return;const auto Now=W->GetRealTimeSeconds();
    W->GetTimerManager().SetTimer(TaskRefreshTimer,this,&ThisClass::RefreshTaskPanel,.2f,false);
    if (LastOrdinaryRefresh>=0 && Now-LastOrdinaryRefresh<.2-UE_SMALL_NUMBER) return;
    LastOrdinaryRefresh=Now;
    if (auto* Data=GetHUDData())
    {
        Data->PollDisplayTime();
        if (SkillFeedbackUntil>0)
        {
            if (Now>=SkillFeedbackUntil) {SkillFeedbackUntil=0;Data->Invalidate(EGuLiHUDData::Tasks);}
        }
        TGuardValue<bool> OrdinaryScope(bOrdinaryHUDRefresh,true);FlushHUDData(Data->Pending());
    }
    RefreshActionTooltipAtCursor();
}
void UGuLiCommanderHUDWidget::HandleFocusClicked() { if (auto* PC = CommanderController.Get()) PC->FocusSelectedUnits(); }
void UGuLiCommanderHUDWidget::HandleStopClicked() { if (auto* PC = CommanderController.Get()) PC->StopSelectedUnits(); }
