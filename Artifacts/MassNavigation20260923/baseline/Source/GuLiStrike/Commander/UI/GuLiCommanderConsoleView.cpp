#include "Commander/UI/GuLiCommanderHUDWidget.h"
#include "Gameplay/Data/GuLiGameText.h"
#include "Commander/UI/GuLiCommanderUITheme.h"
#include "Commander/UI/GuLiCommanderActionButton.h"
#include "Commander/Framework/GuLiCommanderPlayerController.h"
#include "Commander/Framework/GuLiCommanderNetSyncComponent.h"
#include "Commander/Presentation/GuLiCommanderCameraPawn.h"
#include "Commander/Network/GuLiSoldierStateReplicator.h"
#include "Commander/Behavior/GuLiCommanderBehaviorSchema.h"
#include "Gameplay/CommanderSkills/GuLiCommanderSkillComponent.h"
#include "Gameplay/Data/GuLiUnitDataSubsystem.h"
#include "Gameplay/Building/GuLiBuildingPlacementComponent.h"
#include "Gameplay/Building/GuLiBuildingCatalog.h"
#include "Gameplay/Teleport/GuLiTeleportInputComponent.h"
#include "Battle/Framework/GuLiBattlePlayerState.h"
#include "Battle/Framework/GuLiBattleGameState.h"
#include "Blueprint/WidgetTree.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/Image.h"
#include "Components/ProgressBar.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Engine/World.h"
#include "Kismet/KismetSystemLibrary.h"
#include "InputCoreTypes.h"

void UGuLiCommanderHUDWidget::RefreshConsoleSelection()
{
	auto* PC=CommanderController.Get(); auto* Sync=NetSyncComponent.Get();
	if(!PC || !Sync || !SelectionCaption || !GetWorld()) return;
	const auto* Roster=SoldierStateReplicator.Get(); const auto* GS=GetWorld()->GetGameState<AGuLiBattleGameState>();
	const bool Ready=Sync->IsSoldierStreamReady() && GS && (!Roster || Roster->GetSnapshotMatchEpoch()==GS->GetMatchEpoch());
	Presentation=BuildGuLiCommanderUIPresentation(GetWorld(),CachedSelection,Roster,Ready);
	const auto* Data=GetWorld()->GetSubsystem<UGuLiUnitDataSubsystem>();
	auto Name=[&](uint16 Type){ const auto* Def=Data?Data->FindDefinition(Type):nullptr;
		return Def?Def->DisplayName.ToString():GuLiGameText::Format(TEXT("UI.ConsoleView.033"), {FString::Printf(TEXT("%d"), Type)}); };
	TArray<uint16> Types; Presentation.Types.GetKeys(Types); Types.Sort();
	if(InspectionType && !Types.Contains(uint16(InspectionType))) { InspectionType=0; PortraitPage=0; }
	if(InspectionTypes!=Types || TypeButtons.IsEmpty())
	{
		InspectionTypes=Types; TypeStrip->ClearChildren(); TypeButtons.Reset();
		TArray<uint16> Tabs={0}; Tabs.Append(Types);
		for(auto Type:Tabs)
		{
			auto* B=MakeActionButton(NAME_None,Type?Name(Type):GuLiGameText::Text(TEXT("UI.ConsoleView.045")),TEXT("Type"),Type);
			auto* Box=WidgetTree->ConstructWidget<USizeBox>(); Box->SetWidthOverride(130); Box->SetHeightOverride(30); Box->SetContent(B);
			TypeStrip->AddChildToHorizontalBox(Box)->SetPadding(FMargin(0,0,5,0)); TypeButtons.Add(B);
			B->SetDescription(FText::FromString(GuLiGameText::Text(TEXT("UI.ConsoleView.046"))));
		}
	}
	for(UGuLiCommanderActionButton* B:TypeButtons)
	{
		const int32 Count=B->Argument?Presentation.Types.FindRef(uint16(B->Argument)):Presentation.Members.Num();
		B->Label->SetText(FText::FromString(FString::Printf(TEXT("%s %d"),B->Argument?*Name(uint16(B->Argument)):GuLiGameText::Text(TEXT("UI.ConsoleView.045")),Count)));
		B->SetBackgroundColor(B->Argument==InspectionType?FLinearColor(.3f,.9f,1):FLinearColor::White);
	}
	VisibleMemberIndices.Reset();
	for(int32 I=0;I<Presentation.Members.Num();++I)
		if(!InspectionType || Presentation.Members[I].Type==InspectionType) VisibleMemberIndices.Add(I);
	VisiblePortraitGroups.Reset();
	for(const int32 MemberIndex:VisibleMemberIndices)
	{
		const uint16 MemberType=Presentation.Members[MemberIndex].Type;
		const bool bStartsGroup=VisiblePortraitGroups.IsEmpty()
			|| VisiblePortraitGroups.Last().Type!=MemberType
			|| VisiblePortraitGroups.Last().MemberIndices.Num()>=GULI_PANEL_PORTRAIT_GROUP_SIZE;
		if(bStartsGroup)
		{
			auto& Group=VisiblePortraitGroups.AddDefaulted_GetRef();
			Group.Type=MemberType;
			Group.MemberIndices.Reserve(GULI_PANEL_PORTRAIT_GROUP_SIZE);
		}
		VisiblePortraitGroups.Last().MemberIndices.Add(MemberIndex);
	}
	const int32 Pages=FMath::Max(1,FMath::DivideAndRoundUp(VisiblePortraitGroups.Num(),24));
	PortraitPage=FMath::Clamp(PortraitPage,0,Pages-1);
	PageCaption->SetText(FText::FromString(FString::Printf(TEXT("%d / %d"),PortraitPage+1,Pages)));
	for(int32 Cell=0;Cell<PortraitButtons.Num();++Cell)
	{
		auto* B=PortraitButtons[Cell].Get(); const int32 Index=PortraitPage*24+Cell;
		const bool Has=VisiblePortraitGroups.IsValidIndex(Index);
		B->SetVisibility(Has?ESlateVisibility::Visible:ESlateVisibility::Hidden);
		PortraitHealth[Cell]->SetVisibility(Has?ESlateVisibility::HitTestInvisible:ESlateVisibility::Hidden);
		if(!Has) continue;
		const auto& Group=VisiblePortraitGroups[Index];
		const auto& Row=Presentation.Members[Group.MemberIndices[0]];
		B->Label->SetText(FText::AsNumber(Group.MemberIndices.Num()));
		if(ConsoleTheme) B->Icon->SetBrushFromTexture(ConsoleTheme->FindPortrait(Row.Type),true);
		B->SetDescription(FText::GetEmpty());
		B->SetIsEnabled(!Sync->HasUnresolvedSelectionIntent());
		float GroupHealth=0,GroupMaximumHealth=0;
		for(const int32 GroupMemberIndex:Group.MemberIndices)
		{
			GroupHealth+=Presentation.Members[GroupMemberIndex].Health;
			GroupMaximumHealth+=Presentation.Members[GroupMemberIndex].MaximumHealth;
		}
		const float HealthPercent=GroupMaximumHealth>0?GroupHealth/GroupMaximumHealth:0;
		PortraitHealth[Cell]->SetPercent(HealthPercent);
		PortraitHealth[Cell]->SetFillColorAndOpacity(GroupMaximumHealth>0 && HealthPercent<.35f?FLinearColor(1,.18f,.13f):FLinearColor(.12f,.78f,.5f));
	}
	SelectionCaption->SetText(FText::FromString(Presentation.bSyncing?GuLiGameText::Text(TEXT("UI.ConsoleView.048")):
		GuLiGameText::Format(TEXT("UI.ConsoleView.035"), {FString::Printf(TEXT("%d"), Presentation.Members.Num()), FString::Printf(TEXT("%d"), VisibleMemberIndices.Num())})));
	float HP=0,Max=0,Shield=0,MaxShield=0;
	for(int32 Index:VisibleMemberIndices) { const auto& Row=Presentation.Members[Index]; HP+=Row.Health; Max+=Row.MaximumHealth; Shield+=Row.Shield; MaxShield+=Row.MaximumShield; }
	const uint16 Type=InspectionType?uint16(InspectionType):Types.Num()==1?Types[0]:0;
	const FString Detail=Presentation.Members.IsEmpty()?GuLiGameText::Text(TEXT("UI.ConsoleView.049")):Type?Name(Type):GuLiGameText::Text(TEXT("UI.ConsoleView.050"));
	auto Attribute=[this](FName Key,bool Visible,const FString& Value)
	{
		if(auto* Row=AttributeRows.FindRef(Key).Get()) { Row->SetVisibility(Visible?ESlateVisibility::Visible:ESlateVisibility::Collapsed); Row->Label->SetText(FText::FromString(Value)); }
	};
	const auto* Def=Type && Data?Data->FindDefinition(Type):nullptr;
	Attribute(TEXT("Health"),!VisibleMemberIndices.IsEmpty(),FString::Printf(TEXT("%.0f / %.0f"),HP,Max));
	Attribute(TEXT("Shield"),MaxShield>0,FString::Printf(TEXT("%.0f / %.0f"),Shield,MaxShield));
	Attribute(TEXT("Speed"),Def!=nullptr,Def?FString::Printf(TEXT("%.1f m/s"),Def->MovementSpeedCmPerSecond/100):TEXT(""));
	Attribute(TEXT("Defense"),Def!=nullptr,Def?FString::Printf(TEXT("%.0f"),Def->Defense):TEXT(""));
	Attribute(TEXT("CargoBlue"),false,TEXT("")); Attribute(TEXT("CargoRed"),false,TEXT(""));
	if(Presentation.Members.Num()==1)
	{
		const auto& Row=Presentation.Members[0];
		if(auto* PS=CommanderPlayerState.Get()) if(const auto* Private=PS->FindMiningVehiclePrivateState(Row.Actor))
		{ Attribute(TEXT("CargoBlue"),true,FString::FromInt(Private->Cargo.Blue)); Attribute(TEXT("CargoRed"),true,FString::FromInt(Private->Cargo.Red)); }
	}
	SelectionDetails->SetText(FText::FromString(Detail)); SelectionHealth->SetPercent(Max>0?HP/Max:0);
	if(ConsoleTheme) PortraitImage->SetBrushFromTexture(Type?ConsoleTheme->FindPortrait(Type):ConsoleTheme->FindIcon(TEXT("Unit")),true);
	PortraitImage->SetRenderOpacity(Presentation.Members.IsEmpty()?.3f:float(.96+.04*FMath::Sin(GetWorld()->GetRealTimeSeconds()*1.4)));
}

void UGuLiCommanderHUDWidget::CycleInspectionType()
{
	if(!CommanderController.IsValid() || bMenuOpen) return;
	const int32 Current=InspectionTypes.IndexOfByKey(uint16(InspectionType));
	InspectionType=InspectionTypes.IsValidIndex(Current+1)?InspectionTypes[Current+1]:0; PortraitPage=0;
	RefreshConsoleSelection();
}

void UGuLiCommanderHUDWidget::RefreshConsoleContext()
{
	auto* PC=CommanderController.Get(); auto* Sync=NetSyncComponent.Get(); if(!PC || !Sync || !ContextPanel) return;
	const bool Selected=!Presentation.Members.IsEmpty() || Sync->HasUnresolvedSelectionIntent();
	const bool Ready=PC->CanIssueCommanderOrders();
	auto* PS=CommanderPlayerState.Get(); auto* Skills=PS?PS->GetCommanderSkills():nullptr;
	const auto* SkillCatalog=Skills?Skills->GetSkillCatalog():nullptr;
	int32 Miners=0, Builders=0, Vehicles=0, SkillUnits=0, SkillReady=0;
	double NextReady=DBL_MAX;
	const auto* GS=GetWorld()->GetGameState(); const double Now=GS?GS->GetServerWorldTimeSeconds():0;
	for(const auto& Row:Presentation.Members)
	{
		const auto* Policy=GuLiCommanderBehavior::FindPolicy(*GetWorld(),Row.Type);
		Miners+=Policy && Policy->Behavior==EGuLiCommanderBehavior::Mining?1:0;
		Builders+=Policy && Policy->Behavior==EGuLiCommanderBehavior::Construction?1:0;
		Vehicles+=Row.Actor.IsValid()?1:0;
	}
	if(Skills && SkillCatalog) for(const auto& View:Skills->GetSelectedUnitSkills())
		if(SkillCatalog->FindSkill(View.Runtime.SkillId))
		{ ++SkillUnits; SkillReady+=View.Runtime.ReadyAt<=Now?1:0; NextReady=FMath::Min(NextReady,FMath::Max(0.,View.Runtime.ReadyAt-Now)); }
	const FName SelectionActions[]={TEXT("Move"),TEXT("Stop"),TEXT("Skill"),TEXT("Focus"),TEXT("Tasks"),TEXT("Mine"),TEXT("Return"),TEXT("Construct"),TEXT("Transit")};
	for(auto Action:SelectionActions)
	{
		auto* B=Cast<UGuLiCommanderActionButton>(FindRuntimeWidget(*(FString(TEXT("BTN_Cmd_"))+Action.ToString())));
		if(!B) continue; int32 Eligible=Presentation.Members.Num();
		if(Action==TEXT("Mine") || Action==TEXT("Return")) Eligible=Miners;
		if(Action==TEXT("Construct")) Eligible=Builders;
		if(Action==TEXT("Transit")) Eligible=Vehicles;
		if(Action==TEXT("Skill")) Eligible=SkillReady;
		const bool Applicable=Selected && Eligible>0;
		B->SetIsEnabled(Ready && Applicable);
		B->Icon->SetRenderOpacity(Ready && Applicable?1.f:.28f);
		B->SetBackgroundColor(PC->GetHUDTargetIntent()==Action?FLinearColor(.3f,.9f,1):FLinearColor::White);
		FString Tip;
		if(Action==TEXT("Move")) Tip=GuLiGameText::Text(TEXT("UI.ConsoleView.051"));
		else if(Action==TEXT("Stop")) Tip=GuLiGameText::Text(TEXT("UI.ConsoleView.052"));
		else if(Action==TEXT("Skill"))
		{
			Tip=GuLiGameText::Text(TEXT("UI.ConsoleView.053"));
			Tip+=GuLiGameText::Format(TEXT("UI.ConsoleView.036"), {FString::Printf(TEXT("%d"), SkillUnits), FString::Printf(TEXT("%d"), SkillReady)});
			if(SkillUnits>0 && SkillReady==0) Tip+=GuLiGameText::Format(TEXT("UI.ConsoleView.037"), {FString::Printf(TEXT("%.1f"), NextReady)});
		}
		else if(Action==TEXT("Focus")) Tip=GuLiGameText::Text(TEXT("UI.ConsoleView.054"));
		else if(Action==TEXT("Tasks")) Tip=GuLiGameText::Text(TEXT("UI.ConsoleView.055"));
		else if(Action==TEXT("Mine")) Tip=GuLiGameText::Text(TEXT("UI.ConsoleView.056"));
		else if(Action==TEXT("Return")) Tip=GuLiGameText::Text(TEXT("UI.ConsoleView.057"));
		else if(Action==TEXT("Construct")) Tip=GuLiGameText::Text(TEXT("UI.ConsoleView.058"));
		else Tip=GuLiGameText::Text(TEXT("UI.ConsoleView.059"));
		if(!Ready) Tip+=GuLiGameText::Text(TEXT("UI.ConsoleView.060"));
		else if(!Applicable) Tip+=GuLiGameText::Text(TEXT("UI.ConsoleView.061"));
		Tip+=GuLiGameText::Format(TEXT("UI.ConsoleView.038"), {FString::Printf(TEXT("%d"), Presentation.Members.Num()), FString::Printf(TEXT("%d"), Eligible)});
		B->SetDescription(FText::FromString(Tip));
	}
	const auto& Counts=Sync->GetControlGroupCounts();
	for(int32 I=0;I<GroupButtons.Num();++I)
	{
		const int32 Count=Counts.IsValidIndex(I)?Counts[I]:0;
		GroupCounts[I]->SetText(Count>0?FText::AsNumber(Count):FText::GetEmpty());
		GroupButtons[I]->Label->SetColorAndOpacity(Count>0?FLinearColor(.8f,.92f,.96f):FLinearColor(.32f,.4f,.45f));
		GroupButtons[I]->SetBackgroundColor((Sync->GetRelatedControlGroups() & (1<<I))?FLinearColor(.3f,.9f,1):FLinearColor::White);
	}
	auto* Building=PC->GetBuildingPlacementComponent(); auto* Teleport=PC->FindComponentByClass<UGuLiTeleportInputComponent>();
	const bool Build=Building && Building->IsBuildModeActive(); const bool Transit=Teleport && (Teleport->IsAiming() || Teleport->QueryState().IsActive());
	ContextPanel->GetParent()->SetVisibility((Build || Transit) && !bMenuOpen?ESlateVisibility::SelfHitTestInvisible:ESlateVisibility::Collapsed);
	ContextCaption->SetText(Build?FText::FromString(GuLiGameText::Text(TEXT("UI.ConsoleView.062"))+Building->GetPlacementStatusText().ToString()):
		Teleport?FText::FromString(GuLiGameText::Text(TEXT("UI.ConsoleView.063"))+Teleport->GetStatusText().ToString()):FText::GetEmpty());
	for(int32 I=1;I<=6;++I)
	{
		auto* B=Cast<UGuLiCommanderActionButton>(FindRuntimeWidget(*FString::Printf(TEXT("BTN_Building_%d"),I))); if(!B) continue;
		B->SetVisibility(Build?ESlateVisibility::Visible:ESlateVisibility::Collapsed);
		const auto* Catalog=Building?Building->GetBuildingCatalog():nullptr;
		const auto* Def=Catalog?Catalog->FindDefinition(EGuLiBuildingType(I)):nullptr;
		B->SetIsEnabled(Def && Ready);
		if(Def)
		{
			B->Label->SetText(Def->DisplayName);
			B->SetDescription(FText::FromString(GuLiGameText::Format(TEXT("UI.ConsoleView.039"), {FString(*Def->DisplayName.ToString()), FString::Printf(TEXT("%d"), Def->Cost.Blue), FString::Printf(TEXT("%d"), Def->Cost.Red), FString(*Def->Description.ToString())})));
			B->SetBackgroundColor(Building->GetSelectedBuildingType()==Def->Type?FLinearColor(.3f,.9f,1):FLinearColor::White);
		}
	}
	for(const FName Name:{FName(TEXT("Build")),FName(TEXT("Teleport"))})
		if(auto* B=Cast<UButton>(FindRuntimeWidget(*(FString(TEXT("BTN_Cmd_"))+Name.ToString())))) B->SetIsEnabled(Ready);
	if(auto* B=Cast<UGuLiCommanderActionButton>(FindRuntimeWidget(TEXT("BTN_Cmd_Teleport"))); B && Teleport)
	{ B->SetDescription(FText::FromString(GuLiGameText::Text(TEXT("UI.ConsoleView.064"))+Teleport->GetStatusText().ToString())); B->SetBackgroundColor(Transit?FLinearColor(.3f,.9f,1):FLinearColor::White); }
	if(auto* B=Cast<UGuLiCommanderActionButton>(FindRuntimeWidget(TEXT("BTN_Cmd_Build"))); B && Building)
	{ B->SetDescription(FText::FromString(GuLiGameText::Text(TEXT("UI.ConsoleView.065"))+Building->GetPlacementStatusText().ToString())); B->SetBackgroundColor(Build?FLinearColor(.3f,.9f,1):FLinearColor::White); }
}

void UGuLiCommanderHUDWidget::RefreshConsoleTasks()
{
	auto* Sync=NetSyncComponent.Get(); if(!Sync || !TaskText || !TaskRows) return;
	const auto& Summaries=Sync->GetTaskSummaries();
	auto StateName=[](EGuLiTaskStatus Status)->const TCHAR*
	{
		switch(Status) { case EGuLiTaskStatus::Waiting:return GuLiGameText::Text(TEXT("UI.ConsoleView.066")); case EGuLiTaskStatus::Running:return GuLiGameText::Text(TEXT("UI.ConsoleView.067"));
		case EGuLiTaskStatus::WaitingSafeExit:return GuLiGameText::Text(TEXT("UI.ConsoleView.068")); case EGuLiTaskStatus::Failed:return GuLiGameText::Text(TEXT("UI.ConsoleView.069"));
		case EGuLiTaskStatus::Completed:return GuLiGameText::Text(TEXT("UI.ConsoleView.070")); case EGuLiTaskStatus::WorkUnitComplete:return GuLiGameText::Text(TEXT("UI.ConsoleView.071")); default:return GuLiGameText::Text(TEXT("UI.ConsoleView.072")); }
	};
	int32 Prefix=Summaries.IsEmpty()?0:Summaries[0].Tasks.Num(), Count=0, Stopped=0, Safe=0;
	auto Same=[](const FGuLiUnitTaskView& A,const FGuLiUnitTaskView& B)
	{ return A.bAutomatic==B.bAutomatic && A.Command.Kind==B.Command.Kind && A.Command.SpecialTaskId==B.Command.SpecialTaskId
		&& A.Command.BuildingId==B.Command.BuildingId && A.Command.ClusterId==B.Command.ClusterId
		&& A.Command.TerritoryId==B.Command.TerritoryId && FVector(A.Command.Target).Equals(B.Command.Target,1)
		&& A.bHasLocation==B.bHasLocation && (!A.bHasLocation || FVector(A.Location).Equals(B.Location,1)); };
	for(const auto& S:Summaries)
	{
		Count+=S.UnitCount; Stopped+=S.bStopped?S.UnitCount:0; Safe+=S.bWaitingSafeExit?S.UnitCount:0;
		Prefix=FMath::Min(Prefix,S.Tasks.Num());
		for(int32 I=0;I<Prefix;++I) if(!Same(Summaries[0].Tasks[I],S.Tasks[I])) { Prefix=I; break; }
	}
	FString Status=GuLiGameText::Format(TEXT("UI.ConsoleView.040"), {FString::Printf(TEXT("%d"), Presentation.Members.Num()), FString::Printf(TEXT("%d"), Prefix), FString::Printf(TEXT("%d"), Summaries.Num()>1?Summaries.Num():0)});
	if(Count<Presentation.Members.Num()) Status+=GuLiGameText::Text(TEXT("UI.ConsoleView.073"));
	if(Stopped) Status+=GuLiGameText::Format(TEXT("UI.ConsoleView.041"), {FString::Printf(TEXT("%d"), Stopped)});
	if(Safe) Status+=GuLiGameText::Format(TEXT("UI.ConsoleView.042"), {FString::Printf(TEXT("%d"), Safe)});
	if(Summaries.Num()==1 && !Summaries[0].Tasks.IsEmpty()) Status+=FString::Printf(TEXT(" · [%s] %s"),Summaries[0].Tasks[0].bAutomatic?GuLiGameText::Text(TEXT("UI.ConsoleView.074")):GuLiGameText::Text(TEXT("UI.ConsoleView.075")),*Summaries[0].Tasks[0].DisplayName);
	const FString Feedback=Sync->GetLastTaskFeedback(); if(!Feedback.IsEmpty()) Status+=TEXT(" · ")+Feedback;
	if(auto* PS=CommanderPlayerState.Get())
	{
		const auto Reply=PS->GetCommanderSkills()->GetLastReply();
		if(Reply.RequestId.IsValid() && Reply.RequestId!=LastSkillReceipt)
		{
			LastSkillReceipt=Reply.RequestId; SkillFeedbackUntil=GetWorld()->GetRealTimeSeconds()+8;
			int32 Accepted=0; FString Reasons;
			for(const auto& Unit:Reply.Units)
			{
				Accepted+=Unit.Code==EGuLiActiveSkillResultCode::Succeeded?1:0;
				const TCHAR* Reason=Unit.Code==EGuLiActiveSkillResultCode::Cooldown?GuLiGameText::Text(TEXT("UI.ConsoleView.076")):
					Unit.Code==EGuLiActiveSkillResultCode::OutOfRange?GuLiGameText::Text(TEXT("UI.ConsoleView.077")):
					Unit.Code==EGuLiActiveSkillResultCode::InvalidGround?GuLiGameText::Text(TEXT("UI.ConsoleView.078")):
					Unit.Code==EGuLiActiveSkillResultCode::NoSkill?GuLiGameText::Text(TEXT("UI.ConsoleView.079")):
					Unit.Code==EGuLiActiveSkillResultCode::Ineligible?GuLiGameText::Text(TEXT("UI.ConsoleView.080")):
					Unit.Code==EGuLiActiveSkillResultCode::ExecutionFailed?GuLiGameText::Text(TEXT("UI.ConsoleView.081")):TEXT("");
				if(*Reason && !Reasons.Contains(Reason)) Reasons+=FString(Reason)+TEXT(" ");
			}
			SkillFeedback=GuLiGameText::Format(TEXT("UI.ConsoleView.043"), {FString::Printf(TEXT("%d"), Accepted), FString::Printf(TEXT("%d"), Reply.Units.Num()), FString(*Reasons), FString(*Reply.Error)});
		}
	}
	if(GetWorld()->GetRealTimeSeconds()<SkillFeedbackUntil) Status+=TEXT(" · ")+SkillFeedback;
	if(auto* PC=CommanderController.Get(); PC && !PC->GetHUDTargetIntent().IsNone())
		Status=FString(PC->GetHUDTargetIntent()==TEXT("Skill")?GuLiGameText::Text(TEXT("UI.ConsoleView.082")):
			GuLiGameText::Text(TEXT("UI.ConsoleView.083")))+TEXT("  |  ")+Status;
	TaskCaption->SetText(FText::FromString(Status)); TaskCaption->SetToolTipText(FText::FromString(Status));
	TaskCaption->SetColorAndOpacity(Safe?FLinearColor(1,.68f,.2f):Stopped?FLinearColor(1,.4f,.28f):FLinearColor(.7f,.87f,.94f));
	FString Description=Status+TEXT("\n");
	for(const auto& S:Summaries)
	{
		Description+=GuLiGameText::Format(TEXT("UI.ConsoleView.044"), {FString::Printf(TEXT("%d"), S.UnitCount), FString::Printf(TEXT("%d"), S.ManualTaskCount), FString(S.bStopped?GuLiGameText::Text(TEXT("UI.ConsoleView.084")):TEXT("")), FString(S.bWaitingSafeExit?GuLiGameText::Text(TEXT("UI.ConsoleView.085")):TEXT(""))});
		if(!S.RecoverableTasks.IsEmpty()) Description+=GuLiGameText::Text(TEXT("UI.ConsoleView.086"))+FString::Join(S.RecoverableTasks,TEXT("、"))+TEXT("\n");
		if(S.Tasks.IsEmpty()&&!S.bStopped&&!S.RecoverableTasks.IsEmpty()) Description+=GuLiGameText::Text(TEXT("UI.ConsoleView.087"));
		if(!S.Error.IsEmpty()) Description+=S.Error+TEXT("\n");
	}
	// Compatibility text remains a truthful full summary, and doubles as readable accessibility content.
	FString Signature=Description;
	for(int32 I=0;I<Prefix;++I)
	{
		const auto& T=Summaries[0].Tasks[I];
		Description+=FString::Printf(TEXT("%d. [%s] %s · %s\n"),I+1,T.bAutomatic?GuLiGameText::Text(TEXT("UI.ConsoleView.074")):GuLiGameText::Text(TEXT("UI.ConsoleView.075")),*T.DisplayName,StateName(T.Status));
		Signature+=FString::Printf(TEXT("%d/%s/%s"),T.bHasLocation,*FVector(T.Location).ToString(),StateName(T.Status));
	}
	Signature+=Description;
	if(Signature!=LastTaskDescription)
	{
		LastTaskDescription=Signature; TaskText->SetText(FText::FromString(Description));
		while(TaskRows->GetChildrenCount()>1) TaskRows->RemoveChildAt(TaskRows->GetChildrenCount()-1);
		TaskLocations.Reset();
		for(int32 I=0;I<Prefix;++I)
		{
			const auto& Task=Summaries[0].Tasks[I]; TaskLocations.Add(Task.Location);
			auto* B=MakeActionButton(NAME_None,FString::Printf(TEXT("%d · %s · %s"),I+1,*Task.DisplayName,Task.bHasLocation?GuLiGameText::Text(TEXT("UI.ConsoleLayout.015")):GuLiGameText::Text(TEXT("UI.ConsoleView.088"))),TEXT("TaskLocation"),I);
			B->SetIsEnabled(Task.bHasLocation && !FVector(Task.Location).ContainsNaN());
			auto* Box=WidgetTree->ConstructWidget<USizeBox>(); Box->SetHeightOverride(34); Box->SetContent(B);
			TaskRows->AddChildToVerticalBox(Box)->SetPadding(FMargin(0,3));
		}
	}
	RefreshConsoleContext();
	RefreshActionTooltipAtCursor();
}

void UGuLiCommanderHUDWidget::RefreshActionTooltipAtCursor()
{
	// Disabled commands still explain their reason. Stock UButton hover skips disabled buttons.
	auto* PC=CommanderController.Get(); float X=0,Y=0;
	if(!PC || !WidgetTree || !PC->GetMousePosition(X,Y) || bMenuOpen) { ShowActionTooltip(nullptr); return; }
	UGuLiCommanderActionButton* Hit=nullptr;
	WidgetTree->ForEachWidget([&](UWidget* Widget)
	{
		if(auto* Button=Cast<UGuLiCommanderActionButton>(Widget); Button && Button->GetParent() && IsWidgetGeometryHit(Button,FVector2D(X,Y))) Hit=Button;
	});
	ShowActionTooltip(Hit);
}

void UGuLiCommanderHUDWidget::ShowActionTooltip(UGuLiCommanderActionButton* Button)
{
	HoveredAction=Button;
	if(!ActionTooltip || !ActionTooltipText || !ActionTooltipTitle) return;
	if(!Button || Button->Description.IsEmpty() || bMenuOpen) { ActionTooltip->SetVisibility(ESlateVisibility::Collapsed); return; }
	FString Title,Body;
	if(!Button->Description.ToString().Split(TEXT("\n"),&Title,&Body)) Title=Button->Description.ToString();
	ActionTooltipTitle->SetText(FText::FromString(Title)); ActionTooltipText->SetText(FText::FromString(Body));
	ActionTooltip->SetVisibility(ESlateVisibility::HitTestInvisible); ActionTooltip->ForceLayoutPrepass();
	const auto& RootGeometry=GetCachedGeometry(); const auto& ButtonGeometry=Button->GetCachedGeometry();
	const FBox2D Anchor(RootGeometry.AbsoluteToLocal(ButtonGeometry.LocalToAbsolute(FVector2D::ZeroVector)),
		RootGeometry.AbsoluteToLocal(ButtonGeometry.LocalToAbsolute(ButtonGeometry.GetLocalSize())));
	const FBox2D Bounds=GuLiCommanderHUDLayout::PlaceTooltip(Anchor,FVector2D(380,ActionTooltipTitle->GetDesiredSize().Y+ActionTooltipText->GetDesiredSize().Y+34),RootGeometry.GetLocalSize());
	if(auto* CanvasSlot=Cast<UCanvasPanelSlot>(ActionTooltip->Slot)) { CanvasSlot->SetPosition(Bounds.Min); CanvasSlot->SetSize(Bounds.GetSize()); }
}

void UGuLiCommanderHUDWidget::HandleUIAction(FName Action,int32 Argument)
{
	const FModifierKeysState ClickModifiers = PendingActionModifiers;
	PendingActionModifiers = FModifierKeysState();
	auto* PC=CommanderController.Get(); if(!PC || !PC->IsCommanderViewActive()) return;
	if(Action==TEXT("Menu")) { ToggleLocalMenu(); return; }
	if(Action==TEXT("Resume")) { SetMenuOpen(false); return; }
	if(Action==TEXT("Help"))
	{
		SetMenuOpen(true); bHelpOpen=true; bQuitConfirmation=false;
		MenuCaption->SetText(FText::FromString(GuLiGameText::Text(TEXT("UI.ConsoleView.089")))); return;
	}
	if(Action==TEXT("Quit"))
	{
		if(bQuitConfirmation) UKismetSystemLibrary::QuitGame(this,PC,EQuitPreference::Quit,false);
		else { bQuitConfirmation=true; bHelpOpen=false; MenuCaption->SetText(FText::FromString(GuLiGameText::Text(TEXT("UI.ConsoleView.090")))); }
		return;
	}
	if(bMenuOpen) return;
	if(Action==TEXT("Type")) { InspectionType=Argument; PortraitPage=0; RefreshConsoleSelection(); return; }
	if(Action==TEXT("Page")) { PortraitPage+=Argument; RefreshConsoleSelection(); return; }
	if(Action==TEXT("Attribute")) return;
	if(Action==TEXT("Portrait"))
	{
		const bool Ctrl=ClickModifiers.IsControlDown();
		const bool Shift=ClickModifiers.IsShiftDown();
		RequestPortraitSelection(Ctrl?(Shift?EGuLiPanelSelectionAction::RemoveType:EGuLiPanelSelectionAction::KeepType):
			Shift?EGuLiPanelSelectionAction::RemoveGroup:EGuLiPanelSelectionAction::SingleGroup,Argument); return;
	}
	if(Action==TEXT("Group")) { PC->RecallControlGroupFromUI(Argument,ClickModifiers.IsControlDown(),ClickModifiers.IsShiftDown(),ClickModifiers.IsAltDown()); return; }
	if(Action==TEXT("Tasks")) { bTaskDrawerOpen=!bTaskDrawerOpen; TaskPanel->SetVisibility(bTaskDrawerOpen?ESlateVisibility::Visible:ESlateVisibility::Collapsed); return; }
	if(Action==TEXT("TaskLocation"))
	{
		if(TaskLocations.IsValidIndex(Argument)) if(auto* Camera=PC->GetPawn<AGuLiCommanderCameraPawn>()) Camera->JumpToWorldLocation(TaskLocations[Argument]);
		return;
	}
	if(Action==TEXT("Building")) { PC->GetBuildingPlacementComponent()->SelectBuildingType(EGuLiBuildingType(Argument)); RefreshConsoleContext(); return; }
	PC->InvokeHUDCommand(Action); RefreshConsoleContext();
}

bool UGuLiCommanderHUDWidget::RequestPortraitSelection(EGuLiPanelSelectionAction Action,int32 PageSlot)
{
	auto* PC=CommanderController.Get(); const int32 Cell=PortraitPage*24+PageSlot;
	if(!PC || !PC->CanIssueCommanderOrders() || bMenuOpen || PageSlot<0 || PageSlot>=24 || !VisiblePortraitGroups.IsValidIndex(Cell)) return false;
	const auto& Group=VisiblePortraitGroups[Cell];
	if(Group.MemberIndices.IsEmpty() || !Presentation.Members.IsValidIndex(Group.MemberIndices[0])) return false;
	const auto& Row=Presentation.Members[Group.MemberIndices[0]];
	FGuLiPanelSelectionRequest Request; Request.Action=Action; Request.SoldierId=Row.Soldier; Request.ActorId=Row.Actor;
	const bool Submitted=PC->GetCommanderNetSyncComponent()->SubmitPanelSelection(Request); RefreshConsoleSelection(); return Submitted;
}

void UGuLiCommanderHUDWidget::ToggleLocalMenu() { SetMenuOpen(!bMenuOpen); }
void UGuLiCommanderHUDWidget::SetMenuOpen(bool bOpen)
{
	auto* PC=CommanderController.Get(); if(!PC || !MenuLayer || (bOpen && !PC->IsCommanderViewActive())) return;
	bMenuOpen=bOpen; bQuitConfirmation=bHelpOpen=false;
	MenuLayer->SetVisibility(bOpen?ESlateVisibility::Visible:ESlateVisibility::Collapsed);
	MenuCaption->SetText(FText::FromString(GuLiGameText::Text(TEXT("UI.ConsoleView.091"))));
	if(bOpen) { FInputModeUIOnly Mode; Mode.SetWidgetToFocus(TakeWidget()); Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock); PC->SetInputMode(Mode); SetKeyboardFocus(); }
	else { FInputModeGameAndUI Mode; Mode.SetHideCursorDuringCapture(false); PC->SetInputMode(Mode); UWidgetBlueprintLibrary::SetFocusToGameViewport(); }
	ShowActionTooltip(nullptr);
	RefreshConsoleContext();
}
bool UGuLiCommanderHUDWidget::DismissTopLayer()
{
	if(bMenuOpen)
	{
		if(bHelpOpen||bQuitConfirmation) { bHelpOpen=bQuitConfirmation=false; MenuCaption->SetText(FText::FromString(GuLiGameText::Text(TEXT("UI.ConsoleView.091")))); }
		else SetMenuOpen(false);
		return true;
	}
	if(bTaskDrawerOpen) { bTaskDrawerOpen=false; TaskPanel->SetVisibility(ESlateVisibility::Collapsed); return true; }
	return false;
}
FReply UGuLiCommanderHUDWidget::NativeOnKeyDown(const FGeometry& Geometry,const FKeyEvent& Event)
{
	if(Event.GetKey()==EKeys::Escape && DismissTopLayer()) return FReply::Handled();
	if(Event.GetKey()==EKeys::F10 && !Event.IsRepeat()) { ToggleLocalMenu(); return FReply::Handled(); }
	return Super::NativeOnKeyDown(Geometry,Event);
}
