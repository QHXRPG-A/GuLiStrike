#include "Gameplay/Teleport/GuLiTeleportInputComponent.h"
#include "Gameplay/Data/GuLiGameText.h"
#include "Gameplay/Skills/GuLiSkillTargeting.h"
#include "Gameplay/Teleport/GuLiTeleportUnitAdapters.h"
#include "Gameplay/Teleport/GuLiTeleportFieldActor.h"
#include "Gameplay/Data/GuLiCommanderDataSubsystem.h"
#include "Gameplay/Building/GuLiBuildingPlacementComponent.h"
#include "Commander/Framework/GuLiCommanderPlayerController.h"
#include "Commander/Presentation/GuLiCommanderHUD.h"
#include "Commander/UI/GuLiCommanderCursorWidget.h"
#include "Battle/Framework/GuLiBattlePlayerState.h"
#include "Gameplay/CommanderSkills/GuLiCommanderSkillComponent.h"
#include "Battle/Framework/GuLiBattleGameState.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/SBoxPanel.h"
#include "Styling/CoreStyle.h"

TSharedRef<SWidget> UGuLiTeleportHUDWidget::RebuildWidget()
{
	return SNew(SBox).WidthOverride(230).HeightOverride(78)
	[
		SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(FLinearColor(.012f,.04f,.065f,.94f)).Padding(8)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SButton).IsFocusable(false).ButtonColorAndOpacity(FLinearColor(.02f,.27f,.37f))
				.OnClicked_Lambda([this]()
				{
					if (auto* PC=GetOwningPlayer()) { if (auto* C=PC->FindComponentByClass<UGuLiTeleportInputComponent>()) { C->ActivateAiming(); } }
					return FReply::Handled();
				})
				[ SNew(STextBlock).Text_Lambda([this]()
				{
					const auto* PC=GetOwningPlayer(); const auto* C=PC?PC->FindComponentByClass<UGuLiTeleportInputComponent>():nullptr;
					return FText::FromString(GuLiGameText::Format(TEXT("UI.TeleportInputComponent.163"), {FString::Printf(TEXT("%d"), C?C->GetLevel():1)}));
				}).Font(FCoreStyle::GetDefaultFontStyle("Regular",16)) ]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0,5,0,0)
			[
				SNew(STextBlock).AutoWrapText(true).Text_Lambda([this]()
				{
					const auto* PC=GetOwningPlayer(); const auto* C=PC?PC->FindComponentByClass<UGuLiTeleportInputComponent>():nullptr;
					return C?C->GetStatusText():FText::GetEmpty();
				}).Font(FCoreStyle::GetDefaultFontStyle("Regular",11))
			]
		]
	];
}
UGuLiTeleportInputComponent::UGuLiTeleportInputComponent()
{
	SetIsReplicatedByDefault(true); PrimaryComponentTick.bCanEverTick = true;
}
void UGuLiTeleportInputComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
}
AGuLiCommanderPlayerController* UGuLiTeleportInputComponent::GetCommander() const { return Cast<AGuLiCommanderPlayerController>(GetOwner()); }
FGuLiTeleportCastState UGuLiTeleportInputComponent::QueryState() const
{
	FGuLiTeleportCastState State;
	const auto* PC = GetCommander(); const auto* PS = PC ? PC->GetPlayerState<AGuLiBattlePlayerState>() : nullptr;
	if (PS && GetWorld()) { if (const auto* Field = AGuLiTeleportFieldActor::FindCast(*GetWorld(),PS->GetPlayerGuid())) { State = Field->GetCastState(); } }
	return State;
}
bool UGuLiTeleportInputComponent::SetServerLevel(int32 NewLevel)
{
	auto* PC = GetCommander(); auto* PS = PC ? PC->GetPlayerState<AGuLiBattlePlayerState>() : nullptr;
	if (!PS || !PS->HasAuthority() || NewLevel < 1 || NewLevel > 4) { return false; }
	return PS->GetCommanderSkills()->SetServerGlobalSkillLevel(TEXT("Teleport"), NewLevel);
}
void UGuLiTeleportInputComponent::ActivateAiming()
{
	auto* PC = GetCommander();
	if (!PC || !PC->IsLocalController() || !PC->CanIssueCommanderOrders()) { return; }
	if (bArmed) { CancelTeleport(); return; }
	if (QueryState().IsActive()) { return; }
	if (PC->GetBuildingPlacementComponent()) { PC->GetBuildingPlacementComponent()->HandleCancelAction(); }
	PC->CancelSelectionDrag(); PC->ActivateSelectionTool();
	CurrentCastId.Invalidate(); Feedback.Reset(); bSourcePending = bCancelPending = false; bArmed = true;
	if (PC->CommanderCursorWidget) { PC->CommanderCursorWidget->SetTeleportMode(true); }
}
void UGuLiTeleportInputComponent::ClearLocalAim()
{
	bArmed = false;
	if (Preview) { Preview->Destroy(); Preview = nullptr; }
	if (auto* PC = GetCommander(); PC && PC->CommanderCursorWidget) { PC->CommanderCursorWidget->SetTeleportMode(false); }
}
void UGuLiTeleportInputComponent::CancelTeleport()
{
	if (!bArmed && !bSourcePending) { return; }
	if (bSourcePending && !CurrentCastId.IsValid()) { bCancelPending = true; }
	else if (CurrentCastId.IsValid()) { ServerSubmit(FGuid::NewGuid(), EGuLiTeleportCommand::Cancel,CurrentCastId,FVector::ZeroVector); }
	ClearLocalAim();
}
bool UGuLiTeleportInputComponent::HandlePrimaryAction()
{
	if (!bArmed) { return false; }
	auto* PC = GetCommander();
	if (!PC || PC->IsCursorOverCommanderUI() || IsHUDHovered()) { return true; }
	FVector Point; if (!PC->TraceGroundUnderCursor(Point)) { Feedback = GuLiGameText::Text(TEXT("UI.TeleportInputComponent.168")); return true; }
	const auto State = QueryState();
	if (!CurrentCastId.IsValid() && !bSourcePending)
	{ bSourcePending = true; ServerSubmit(FGuid::NewGuid(), EGuLiTeleportCommand::Source,{},Point); }
	else if (State.CastId == CurrentCastId && State.Phase == EGuLiTeleportPhase::AwaitingDestination)
	{ ServerSubmit(FGuid::NewGuid(), EGuLiTeleportCommand::Destination,CurrentCastId,Point); }
	return true;
}
int32 UGuLiTeleportInputComponent::GetLevel() const
{
	const auto* PC = GetCommander();
	const auto* PS = PC ? PC->GetPlayerState<AGuLiBattlePlayerState>() : nullptr;
	return PS ? PS->GetCommanderSkills()->GetGlobalSkillLevel(TEXT("Teleport")) : 1;
}
void UGuLiTeleportInputComponent::ServerSubmit_Implementation(FGuid RequestId, EGuLiTeleportCommand Command, FGuid CastId, FVector Point)
{
	auto* PS = GetCommander()->GetPlayerState<AGuLiBattlePlayerState>();
	const auto* State = GetWorld()->GetGameState<AGuLiBattleGameState>();
	if (!PS || !State) return;
	FGuLiActiveSkillRequest Request;
	Request.RequestId = RequestId; Request.GlobalSkillId = TEXT("Teleport"); Request.MatchEpoch = State->GetMatchEpoch();
	Request.Command = static_cast<EGuLiActiveSkillCommand>(Command);
	Request.CastId = CastId; Request.bHasGroundPoint = Command != EGuLiTeleportCommand::Cancel; Request.GroundPoint = Point;
	const auto Reply = PS->GetCommanderSkills()->ExecuteServerRequest(Request);
	ClientResult(Command, Reply.Global.bSucceeded, Reply.Global.CastId, Reply.Error);
}
void UGuLiTeleportInputComponent::ClientResult_Implementation(EGuLiTeleportCommand Command, bool bSucceeded, FGuid CastId, const FString& Error)
{
	Feedback = Error;
	if (Command == EGuLiTeleportCommand::Source)
	{
		bSourcePending = false;
		if (bSucceeded) { CurrentCastId = CastId; }
		if (bCancelPending) { bCancelPending = false; if (bSucceeded) { ServerSubmit(FGuid::NewGuid(), EGuLiTeleportCommand::Cancel,CastId,FVector::ZeroVector); } }
	}
	if (Command == EGuLiTeleportCommand::Destination && bSucceeded) { ClearLocalAim(); }
}
bool UGuLiTeleportInputComponent::IsHUDHovered() const { return HUD && HUD->IsHovered(); }
FText UGuLiTeleportInputComponent::GetStatusText() const
{
	const auto S = QueryState();
	const double Remaining = FMath::Max(0.0,S.Deadline-AGuLiTeleportFieldActor::GetSynchronizedTime(*GetWorld()));
	if (S.Phase == EGuLiTeleportPhase::Windup) { return FText::FromString(GuLiGameText::Format(TEXT("UI.TeleportInputComponent.164"), {FString::Printf(TEXT("%.1f"), Remaining)})); }
	if (S.Phase == EGuLiTeleportPhase::AwaitingDestination) { return FText::FromString(GuLiGameText::Format(TEXT("UI.TeleportInputComponent.165"), {FString::Printf(TEXT("%.1f"), Remaining), FString::Printf(TEXT("%d"), S.ParticipantCount), FString(Feedback.IsEmpty()?TEXT(""):*FString::Printf(TEXT("\n%s"),*Feedback))})); }
	if (S.Phase == EGuLiTeleportPhase::Recovery) { return FText::FromString(GuLiGameText::Text(TEXT("UI.TeleportInputComponent.169"))); }
	if (S.Phase == EGuLiTeleportPhase::Returning) { return FText::FromString(GuLiGameText::Text(TEXT("UI.TeleportInputComponent.170"))); }
	if (!Feedback.IsEmpty()) { return FText::FromString(Feedback); }
	const auto* Data = GetWorld() ? GetWorld()->GetSubsystem<UGuLiCommanderDataSubsystem>() : nullptr;
	const auto* Config = Data ? Data->FindTeleportFieldConfig(GetLevel()) : nullptr;
	const int32 RadiusMeters = Config ? FMath::RoundToInt(Config->RadiusCentimeters / 100.f) : 0;
	return FText::FromString(bArmed ? GuLiGameText::Text(TEXT("UI.TeleportInputComponent.171")) : (GetLevel() == 4
		? GuLiGameText::Format(TEXT("UI.TeleportInputComponent.166"), {FString::Printf(TEXT("%d"), RadiusMeters)})
		: GuLiGameText::Format(TEXT("UI.TeleportInputComponent.167"), {FString::Printf(TEXT("%d"), RadiusMeters)})));
}
void UGuLiTeleportInputComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime,TickType,ThisTickFunction);
	auto* PC = GetCommander(); if (!PC) { return; }
	if (!PC->IsLocalController()) { return; }
	const bool bCommander = PC->IsCommanderViewActive();
	const auto* CommanderHUD = Cast<AGuLiCommanderHUD>(PC->GetHUD());
	const bool bIntegrated = CommanderHUD && CommanderHUD->GetRuntimeHUDWidget();
	if (bCommander && !bIntegrated && !HUD)
	{
		HUD = CreateWidget<UGuLiTeleportHUDWidget>(PC,UGuLiTeleportHUDWidget::StaticClass());
		HUD->AddToPlayerScreen(30);
		HUD->SetAlignmentInViewport(FVector2D(1,1)); HUD->SetPositionInViewport(FVector2D(-24,-170),false);
		HUD->SetDesiredSizeInViewport(FVector2D(230,78));
		// SetPositionInViewport resets anchors to the top-left in UE 5.7.
		HUD->SetAnchorsInViewport(FAnchors(1,1));
	}
	if (HUD) { HUD->SetVisibility(bCommander && !bIntegrated ? ESlateVisibility::Visible : ESlateVisibility::Collapsed); }
	if (!bCommander) { ClearLocalAim(); return; }
	if (!bArmed) { return; }
	const auto S = QueryState();
	if (CurrentCastId.IsValid() && S.CastId == CurrentCastId && (S.Phase == EGuLiTeleportPhase::Finished || S.Phase == EGuLiTeleportPhase::Recovery || S.Phase == EGuLiTeleportPhase::Returning))
	{ Feedback = S.Message; ClearLocalAim(); return; }
	const bool bShowPreview = !CurrentCastId.IsValid() || S.Phase == EGuLiTeleportPhase::AwaitingDestination;
	FVector Point;
	if (bShowPreview && !PC->IsCursorOverCommanderUI() && !IsHUDHovered() && PC->TraceGroundUnderCursor(Point))
	{
		if (!Preview) { Preview = GetWorld()->SpawnActor<AGuLiTeleportFieldActor>(); }
		const auto* Data = GetWorld()->GetSubsystem<UGuLiCommanderDataSubsystem>();
		const auto* Config = S.IsActive() ? &S.Config : (Data ? Data->FindTeleportFieldConfig(GetLevel()) : nullptr);
		FVector Ground;
		const bool bValid = GuLiSkillTargeting::ResolveGround(*GetWorld(),Point,Ground);
		if (Preview && Config) { Preview->SetActorHiddenInGame(false); Preview->SetPreview(bValid?Ground:Point,Config->RadiusCentimeters,bValid); }
	}
	else if (Preview) { Preview->SetActorHiddenInGame(true); }
}
void UGuLiTeleportInputComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	ClearLocalAim(); if (HUD) { HUD->RemoveFromParent(); HUD = nullptr; }
	Super::EndPlay(Reason);
}
