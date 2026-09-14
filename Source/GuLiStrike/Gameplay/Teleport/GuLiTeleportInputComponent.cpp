#include "Gameplay/Teleport/GuLiTeleportInputComponent.h"
#include "Gameplay/Teleport/GuLiTeleportUnitAdapters.h"
#include "Gameplay/Teleport/GuLiTeleportFieldActor.h"
#include "Gameplay/Data/GuLiCommanderDataSubsystem.h"
#include "Gameplay/Building/GuLiBuildingPlacementComponent.h"
#include "Commander/Framework/GuLiCommanderPlayerController.h"
#include "Commander/UI/GuLiCommanderCursorWidget.h"
#include "Battle/Framework/GuLiBattlePlayerState.h"
#include "AbilitySystemComponent.h"
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
					return FText::FromString(FString::Printf(TEXT("传送  [T]   Lv.%d"),C?C->GetLevel():1));
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
	Super::GetLifetimeReplicatedProps(OutLifetimeProps); DOREPLIFETIME(UGuLiTeleportInputComponent,Level);
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
	Level = NewLevel; PC->ForceNetUpdate(); return true;
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
	else if (CurrentCastId.IsValid()) { ServerSubmit(EGuLiTeleportCommand::Cancel,CurrentCastId,FVector::ZeroVector); }
	ClearLocalAim();
}
bool UGuLiTeleportInputComponent::HandlePrimaryAction()
{
	if (!bArmed) { return false; }
	auto* PC = GetCommander();
	if (!PC || PC->IsCursorOverCommanderUI() || IsHUDHovered()) { return true; }
	FVector Point; if (!PC->TraceGroundUnderCursor(Point)) { Feedback = TEXT("请点击合法地面"); return true; }
	const auto State = QueryState();
	if (!CurrentCastId.IsValid() && !bSourcePending)
	{ bSourcePending = true; ServerSubmit(EGuLiTeleportCommand::Source,{},Point); }
	else if (State.CastId == CurrentCastId && State.Phase == EGuLiTeleportPhase::AwaitingDestination)
	{ ServerSubmit(EGuLiTeleportCommand::Destination,CurrentCastId,Point); }
	return true;
}
void UGuLiTeleportInputComponent::ServerSubmit_Implementation(EGuLiTeleportCommand Command, FGuid CastId, FVector Point)
{
	auto* PC = GetCommander(); auto* PS = PC ? PC->GetPlayerState<AGuLiBattlePlayerState>() : nullptr;
	if (!PS || !PS->HasAuthority() || Point.ContainsNaN()) { return; }
	if (Command != EGuLiTeleportCommand::Cancel)
	{
		if (GetWorld()->GetTimeSeconds() < NextServerPointTime)
		{ ClientResult(Command,false,CastId,TEXT("请稍候再选点")); return; }
		NextServerPointTime = GetWorld()->GetTimeSeconds() + .1;
	}
	auto* ASC = PS->GetAbilitySystemComponent(); if (!ASC) { return; }
	auto* Payload = NewObject<UGuLiTeleportCommandPayload>(PS);
	Payload->Command = Command; Payload->CastId = CastId; Payload->Point = Point; Payload->Level = Level;
	FGameplayEventData Event; Event.EventTag = TAG_GuLi_CommanderTeleport; Event.Instigator = PS; Event.Target = PS; Event.OptionalObject = Payload;
	ASC->HandleGameplayEvent(Event.EventTag,&Event);
	ClientResult(Command,Payload->bSucceeded,Payload->CastId,Payload->Error);
}
void UGuLiTeleportInputComponent::ClientResult_Implementation(EGuLiTeleportCommand Command, bool bSucceeded, FGuid CastId, const FString& Error)
{
	Feedback = Error;
	if (Command == EGuLiTeleportCommand::Source)
	{
		bSourcePending = false;
		if (bSucceeded) { CurrentCastId = CastId; }
		if (bCancelPending) { bCancelPending = false; if (bSucceeded) { ServerSubmit(EGuLiTeleportCommand::Cancel,CastId,FVector::ZeroVector); } }
	}
	if (Command == EGuLiTeleportCommand::Destination && bSucceeded) { ClearLocalAim(); }
}
bool UGuLiTeleportInputComponent::IsHUDHovered() const { return HUD && HUD->IsHovered(); }
FText UGuLiTeleportInputComponent::GetStatusText() const
{
	const auto S = QueryState();
	const double Remaining = FMath::Max(0.0,S.Deadline-AGuLiTeleportFieldActor::GetSynchronizedTime(*GetWorld()));
	if (S.Phase == EGuLiTeleportPhase::Windup) { return FText::FromString(FString::Printf(TEXT("源点充能  %.1f 秒 · 右键取消"),Remaining)); }
	if (S.Phase == EGuLiTeleportPhase::AwaitingDestination) { return FText::FromString(FString::Printf(TEXT("选择落点 %.1f 秒 · %d 个单位%s"),Remaining,S.ParticipantCount,Feedback.IsEmpty()?TEXT(""):*FString::Printf(TEXT("\n%s"),*Feedback))); }
	if (S.Phase == EGuLiTeleportPhase::Recovery) { return FText::FromString(TEXT("已落地，正在恢复行动")); }
	if (S.Phase == EGuLiTeleportPhase::Returning) { return FText::FromString(TEXT("正在送回源点附近")); }
	if (!Feedback.IsEmpty()) { return FText::FromString(Feedback); }
	const auto* Data = GetWorld() ? GetWorld()->GetSubsystem<UGuLiCommanderDataSubsystem>() : nullptr;
	const auto* Config = Data ? Data->FindTeleportFieldConfig(Level) : nullptr;
	const int32 RadiusMeters = Config ? FMath::RoundToInt(Config->RadiusCentimeters / 100.f) : 0;
	return FText::FromString(bArmed ? TEXT("点击源点 · 右键 / Esc 取消") : (Level == 4
		? FString::Printf(TEXT("%d 米 · 可传 WM / Ship 及僚机"), RadiusMeters)
		: FString::Printf(TEXT("%d 米 · 己方普通部队"), RadiusMeters)));
}
void UGuLiTeleportInputComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime,TickType,ThisTickFunction);
	auto* PC = GetCommander(); if (!PC) { return; }
	if (!PC->IsLocalController()) { return; }
	const bool bCommander = PC->IsCommanderViewActive();
	if (bCommander && !HUD)
	{
		HUD = CreateWidget<UGuLiTeleportHUDWidget>(PC,UGuLiTeleportHUDWidget::StaticClass());
		HUD->AddToPlayerScreen(30);
		HUD->SetAlignmentInViewport(FVector2D(1,1)); HUD->SetPositionInViewport(FVector2D(-24,-170),false);
		HUD->SetDesiredSizeInViewport(FVector2D(230,78));
		// SetPositionInViewport resets anchors to the top-left in UE 5.7.
		HUD->SetAnchorsInViewport(FAnchors(1,1));
	}
	if (HUD) { HUD->SetVisibility(bCommander ? ESlateVisibility::Visible : ESlateVisibility::Collapsed); }
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
		const auto* Config = S.IsActive() ? &S.Config : (Data ? Data->FindTeleportFieldConfig(Level) : nullptr);
		FVector Ground;
		const bool bValid = GuLiTeleportMassAdapter::ResolveGround(*GetWorld(),Point,Ground);
		if (Preview && Config) { Preview->SetActorHiddenInGame(false); Preview->SetPreview(bValid?Ground:Point,Config->RadiusCentimeters,bValid); }
	}
	else if (Preview) { Preview->SetActorHiddenInGame(true); }
}
void UGuLiTeleportInputComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	ClearLocalAim(); if (HUD) { HUD->RemoveFromParent(); HUD = nullptr; }
	Super::EndPlay(Reason);
}
