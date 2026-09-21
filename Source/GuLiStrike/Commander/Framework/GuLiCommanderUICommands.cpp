#include "Commander/Framework/GuLiCommanderPlayerController.h"
#include "Commander/Framework/GuLiCommanderNetSyncComponent.h"
#include "Commander/Presentation/GuLiCommanderHUD.h"
#include "Commander/UI/GuLiCommanderHUDWidget.h"
#include "Gameplay/Building/GuLiBuildingPlacementComponent.h"
#include "Gameplay/Teleport/GuLiTeleportInputComponent.h"
#include "Gameplay/CommanderSkills/GuLiCommanderSkillComponent.h"
#include "Battle/Framework/GuLiBattlePlayerState.h"
#include "Engine/World.h"

bool AGuLiCommanderPlayerController::IsCommanderMenuOpen() const
{
	const auto* HUD=Cast<AGuLiCommanderHUD>(GetHUD());
	return HUD && HUD->GetRuntimeHUDWidget() && HUD->GetRuntimeHUDWidget()->IsLocalMenuOpen();
}
void AGuLiCommanderPlayerController::HandleInspectionTab()
{
	if(!IsCommanderViewActive()) return;
	if(auto* HUD=Cast<AGuLiCommanderHUD>(GetHUD())) if(auto* UI=HUD->GetRuntimeHUDWidget()) UI->CycleInspectionType();
}
void AGuLiCommanderPlayerController::HandleLocalMenu()
{
	if(!IsCommanderViewActive()) return;
	CancelSelectionDrag();
	if(auto* HUD=Cast<AGuLiCommanderHUD>(GetHUD())) if(auto* UI=HUD->GetRuntimeHUDWidget()) UI->ToggleLocalMenu();
}
bool AGuLiCommanderPlayerController::IssueMapMove(const FVector& Point,bool bAppend)
{
	if(!CanIssueCommanderOrders() || Point.ContainsNaN() || (!HasConfirmedSelection() && !NetSyncComponent->HasUnresolvedSelectionIntent())) return false;
	FGuLiUnitTaskCommand Command; Command.CommandId=AllocateMoveCommandId();
	Command.SelectionRevision=NetSyncComponent->GetSelectionState().SelectionRevision;
	Command.Target=Point; Command.bGroundMoveOnly=true;
	Command.Disposition=bAppend?EGuLiTaskDisposition::Append:EGuLiTaskDisposition::Replace;
	LatestMoveIntentCommandId=Command.CommandId; NetSyncComponent->SubmitOrderedTask(Command);
	if(!bAppend) ActivateSelectionTool(); return true;
}
void AGuLiCommanderPlayerController::InvokeHUDCommand(FName Action)
{
	if(!CanIssueCommanderOrders()) return;
	if(Action==TEXT("Stop")) { StopSelectedUnits(); return; }
	if(Action==TEXT("Focus")) { FocusSelectedUnits(); return; }
	if(Action==TEXT("Build")) { HandleToggleBuildModeInput(); return; }
	if(Action==TEXT("Teleport")) { if(TeleportInput) TeleportInput->ActivateAiming(); return; }
	if(Action==TEXT("Skill"))
	{
		if(!bSelectionMouseDown && (!TeleportInput || !TeleportInput->IsAiming()) && (!BuildingPlacementComponent || !BuildingPlacementComponent->IsBuildModeActive()))
			if(auto* State=GetPlayerState<AGuLiBattlePlayerState>())
			{
				auto* Skills=State->GetCommanderSkills(); const auto* Catalog=Skills->GetSkillCatalog();
				bool NeedsGround=false;
				for(const auto& View:Skills->GetSelectedUnitSkills())
					if(const auto* Def=Catalog?Catalog->FindSkill(View.Runtime.SkillId):nullptr)
						NeedsGround|=Def->TargetMode==EGuLiActiveSkillTargetMode::GroundPoint;
				if(NeedsGround) { if(ArmMoveTool()) PendingWorkAction=TEXT("Skill"); }
				else Skills->ActivateSelectedUnits(false,FVector::ZeroVector);
			}
		return;
	}
	if(Action==TEXT("Move") || Action==TEXT("Mine") || Action==TEXT("Return") || Action==TEXT("Construct") || Action==TEXT("Transit"))
	{
		if(BuildingPlacementComponent) BuildingPlacementComponent->HandleCancelAction();
		if(ArmMoveTool()) PendingWorkAction=Action;
	}
}
