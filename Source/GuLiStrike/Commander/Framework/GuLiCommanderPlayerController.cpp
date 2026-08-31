// Copyright Epic Games, Inc. All Rights Reserved.

#include "Commander/Framework/GuLiCommanderPlayerController.h"

#include "Commander/Framework/GuLiCommanderNetSyncComponent.h"
#include "Battle/Framework/GuLiBattlePlayerState.h"
#include "Commander/Presentation/GuLiCommanderCameraPawn.h"
#include "Commander/Presentation/GuLiCommanderHUD.h"
#include "Commander/Presentation/GuLiCommanderPresentationActor.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/HUD.h"
#include "InputCoreTypes.h"
#include "LandscapeProxy.h"

namespace GuLiCommanderCursorTrace
{
	constexpr float MaximumGroundTraceDistanceCentimeters = 600000.0f;
	constexpr int32 MaximumIgnoredNonLandscapeBlockers = 16;
}

bool GuLiCommanderToolPolicy::CanArmMove(
	const bool bCanIssueOrders,
	const bool bHasNetSync,
	const bool bHasConfirmedSelection)
{
	return bCanIssueOrders && bHasNetSync && bHasConfirmedSelection;
}

EGuLiSelectionRadiusPreset GuLiCommanderToolPolicy::ResolveRadiusStep(
	const EGuLiCommanderToolMode ToolMode,
	const EGuLiSelectionRadiusPreset CurrentPreset)
{
	if (ToolMode != EGuLiCommanderToolMode::Select)
	{
		return CurrentPreset;
	}

	switch (CurrentPreset)
	{
	case EGuLiSelectionRadiusPreset::Small:
		return EGuLiSelectionRadiusPreset::Medium;
	case EGuLiSelectionRadiusPreset::Medium:
		return EGuLiSelectionRadiusPreset::Large;
	case EGuLiSelectionRadiusPreset::Large:
	default:
		return EGuLiSelectionRadiusPreset::Small;
	}
}

GuLiCommanderToolPolicy::ECancelAction GuLiCommanderToolPolicy::ResolveCancelAction(
	const EGuLiCommanderToolMode ToolMode)
{
	return ToolMode == EGuLiCommanderToolMode::Move
		? ECancelAction::CancelMove
		: ECancelAction::ClearSelection;
}

EGuLiCommanderToolMode GuLiCommanderToolPolicy::ResolveModeAfterMoveAttempt(
	const EGuLiCommanderToolMode CurrentMode,
	const bool bMoveSubmitted)
{
	return bMoveSubmitted ? EGuLiCommanderToolMode::Select : CurrentMode;
}

EGuLiCommanderToolMode GuLiCommanderToolPolicy::ResolveModeForSelectionAvailability(
	const EGuLiCommanderToolMode CurrentMode,
	const bool bHasConfirmedSelection)
{
	return CurrentMode == EGuLiCommanderToolMode::Move && !bHasConfirmedSelection
		? EGuLiCommanderToolMode::Select
		: CurrentMode;
}

bool GuLiCommanderToolPolicy::AllowsWorldIntent(const bool bCursorOverCommanderUI)
{
	return !bCursorOverCommanderUI;
}

AGuLiCommanderPlayerController::AGuLiCommanderPlayerController(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UGuLiCommanderNetSyncComponent>(PlayerNetSyncComponentName))
{
	bShowMouseCursor = false;
	bEnableClickEvents = false;
	bEnableMouseOverEvents = false;
	DefaultMouseCursor = EMouseCursor::Default;
	HitResultTraceDistance = GuLiCommanderCursorTrace::MaximumGroundTraceDistanceCentimeters;

	// 替换公共默认子对象的具体类型；旧属性继续指向同一个对象，不再额外创建组件。
	NetSyncComponent = CastChecked<UGuLiCommanderNetSyncComponent>(GetPlayerNetSyncComponent());
}

void AGuLiCommanderPlayerController::BeginPlay()
{
	Super::BeginPlay();

	UpdateCommanderInputMode();
}

void AGuLiCommanderPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();
	if (!InputComponent)
	{
		return;
	}

	InputComponent->BindKey(EKeys::LeftMouseButton, IE_Pressed, this, &AGuLiCommanderPlayerController::HandlePrimaryActionAtCursor).bConsumeInput = false;
	InputComponent->BindKey(EKeys::RightMouseButton, IE_Pressed, this, &AGuLiCommanderPlayerController::HandleSecondaryActionAtCursor).bConsumeInput = false;
	InputComponent->BindKey(EKeys::Escape, IE_Pressed, this, &AGuLiCommanderPlayerController::HandleCancelInput).bConsumeInput = false;
	InputComponent->BindKey(EKeys::One, IE_Pressed, this, &AGuLiCommanderPlayerController::HandleArmMoveToolInput).bConsumeInput = false;
	InputComponent->BindKey(EKeys::Seven, IE_Pressed, this, &AGuLiCommanderPlayerController::HandleActivateSelectionToolInput).bConsumeInput = false;
	InputComponent->BindKey(EKeys::Add, IE_Pressed, this, &AGuLiCommanderPlayerController::HandleStepSelectionRadiusInput).bConsumeInput = false;
	InputComponent->BindKey(FInputChord(EKeys::Equals, true, false, false, false), IE_Pressed, this, &AGuLiCommanderPlayerController::HandleStepSelectionRadiusInput).bConsumeInput = false;
	InputComponent->BindKey(EKeys::MouseScrollUp, IE_Pressed, this, &AGuLiCommanderPlayerController::ZoomCameraIn).bConsumeInput = false;
	InputComponent->BindKey(EKeys::MouseScrollDown, IE_Pressed, this, &AGuLiCommanderPlayerController::ZoomCameraOut).bConsumeInput = false;
}

void AGuLiCommanderPlayerController::PlayerTick(const float DeltaTime)
{
	Super::PlayerTick(DeltaTime);

	UpdateCommanderInputMode();
	if (!IsCommanderViewActive())
	{
		bHasCursorGroundLocation = false;
		return;
	}
	bHasCursorGroundLocation = TraceGroundUnderCursor(CachedCursorGroundLocation);
	if (GuLiCommanderToolPolicy::ResolveModeForSelectionAvailability(
		CommanderToolMode,
		HasConfirmedSelection()) != CommanderToolMode)
	{
		ActivateSelectionTool();
	}
	UpdateCameraInput(DeltaTime);
	UpdateAcceptedCommandVisual();
}

bool AGuLiCommanderPlayerController::CanIssueCommanderOrders() const
{
	const AGuLiBattlePlayerState* CommanderPlayerState = GetPlayerState<AGuLiBattlePlayerState>();
	return CommanderPlayerState
		&& CommanderPlayerState->IsCommander()
		&& NetSyncComponent && NetSyncComponent->IsConnectionReady()
		&& NetSyncComponent->IsSoldierStreamReady();
}

void AGuLiCommanderPlayerController::ActivateSelectionTool()
{
	if (CommanderToolMode == EGuLiCommanderToolMode::Select)
	{
		return;
	}

	CommanderToolMode = EGuLiCommanderToolMode::Select;
	OnCommanderToolModeChanged.Broadcast(CommanderToolMode);
}

bool AGuLiCommanderPlayerController::ArmMoveTool()
{
	if (!GuLiCommanderToolPolicy::CanArmMove(
		CanIssueCommanderOrders(),
		NetSyncComponent != nullptr,
		HasConfirmedSelection()))
	{
		return false;
	}

	if (CommanderToolMode != EGuLiCommanderToolMode::Move)
	{
		CommanderToolMode = EGuLiCommanderToolMode::Move;
		OnCommanderToolModeChanged.Broadcast(CommanderToolMode);
	}
	return true;
}

void AGuLiCommanderPlayerController::StepSelectionRadiusUp()
{
	SetSelectionRadiusPreset(GuLiCommanderToolPolicy::ResolveRadiusStep(
		CommanderToolMode,
		SelectionRadiusPreset));
}

void AGuLiCommanderPlayerController::SetSelectionRadiusPreset(
	const EGuLiSelectionRadiusPreset NewPreset)
{
	switch (NewPreset)
	{
	case EGuLiSelectionRadiusPreset::Small:
	case EGuLiSelectionRadiusPreset::Medium:
	case EGuLiSelectionRadiusPreset::Large:
		if (SelectionRadiusPreset == NewPreset)
		{
			return;
		}
		SelectionRadiusPreset = NewPreset;
		OnSelectionRadiusPresetChanged.Broadcast(SelectionRadiusPreset);
		break;
	default:
		break;
	}
}

bool AGuLiCommanderPlayerController::GetCursorGroundLocation(FVector& OutLocation) const
{
	OutLocation = CachedCursorGroundLocation;
	return bHasCursorGroundLocation;
}

bool AGuLiCommanderPlayerController::GetActiveCommandLine(
	FVector& OutStart,
	FVector& OutEnd,
	float& OutAlpha,
	EGuLiCommandLineState& OutState) const
{
	const UWorld* World = GetWorld();
	if (!World || CommandLineState == EGuLiCommandLineState::None
		|| CommandLineExpireTime <= World->GetTimeSeconds())
	{
		OutAlpha = 0.0f;
		OutState = EGuLiCommandLineState::None;
		return false;
	}

	OutStart = CommandLineStart;
	OutEnd = CommandLineEnd;
	OutState = CommandLineState;
	const float FadeDuration = FMath::Max(CommandLineFadeDurationSeconds, UE_SMALL_NUMBER);
	OutAlpha = FMath::Clamp(
		static_cast<float>((CommandLineExpireTime - World->GetTimeSeconds()) / FadeDuration),
		0.0f,
		1.0f);
	return true;
}

void AGuLiCommanderPlayerController::HandlePrimaryActionAtCursor()
{
	if (!IsCommanderViewActive())
	{
		return;
	}

	if (CommanderToolMode == EGuLiCommanderToolMode::Move)
	{
		const bool bMoveSubmitted = TryIssueMoveAtCursor();
		if (GuLiCommanderToolPolicy::ResolveModeAfterMoveAttempt(
			CommanderToolMode,
			bMoveSubmitted) == EGuLiCommanderToolMode::Select)
		{
			ActivateSelectionTool();
		}
		return;
	}

	TryIssueSelectionAtCursor();
}

void AGuLiCommanderPlayerController::HandleSecondaryActionAtCursor()
{
	if (!IsCommanderViewActive())
	{
		return;
	}

	const bool bMoveSubmitted = TryIssueMoveAtCursor();
	if (GuLiCommanderToolPolicy::ResolveModeAfterMoveAttempt(
		CommanderToolMode,
		bMoveSubmitted) == EGuLiCommanderToolMode::Select)
	{
		ActivateSelectionTool();
	}
}

// 只在本地指挥角色中解释这些快捷键；共享 helper 本身不绑定 World/PlayerState。
void AGuLiCommanderPlayerController::HandleActivateSelectionToolInput()
{
	if (!IsCommanderViewActive())
	{
		return;
	}
	ActivateSelectionTool();
}

void AGuLiCommanderPlayerController::HandleStepSelectionRadiusInput()
{
	if (!IsCommanderViewActive())
	{
		return;
	}
	StepSelectionRadiusUp();
}

void AGuLiCommanderPlayerController::HandleArmMoveToolInput()
{
	if (!IsCommanderViewActive())
	{
		return;
	}

	ArmMoveTool();
}

void AGuLiCommanderPlayerController::HandleCancelInput()
{
	if (!IsCommanderViewActive())
	{
		return;
	}

	if (GuLiCommanderToolPolicy::ResolveCancelAction(CommanderToolMode)
		== GuLiCommanderToolPolicy::ECancelAction::CancelMove)
	{
		ActivateSelectionTool();
		return;
	}

	ClearSelection();
}

// 生成区域意图和非零请求号；不把本地推测的士兵/控制组列表传给服务器。
bool AGuLiCommanderPlayerController::TryIssueSelectionAtCursor()
{
	if (!GuLiCommanderToolPolicy::AllowsWorldIntent(IsCursorOverCommanderUI()))
	{
		return false;
	}

	if (!CanIssueCommanderOrders() || !NetSyncComponent)
	{
		return false;
	}

	FVector GroundLocation;
	if (!TraceGroundUnderCursor(GroundLocation))
	{
		return false;
	}

	FGuLiSelectionRequest Request;
	Request.Center = GroundLocation;
	Request.RadiusPreset = SelectionRadiusPreset;
	Request.Modifier = IsInputKeyDown(EKeys::LeftShift) || IsInputKeyDown(EKeys::RightShift)
		? EGuLiSelectionModifier::Toggle
		: EGuLiSelectionModifier::Replace;
	Request.ClientRequestId = AllocateSelectionRequestId();
	Request.KnownSelectionRevision = NetSyncComponent->GetSelectionState().SelectionRevision;
	NetSyncComponent->SubmitSelectionRequest(Request);
	return true;
}

// 先展示 Pending 与短时预测，再发移动意图；成功返回只表示本地提交路径完成。
bool AGuLiCommanderPlayerController::TryIssueMoveAtCursor()
{
	if (!GuLiCommanderToolPolicy::AllowsWorldIntent(IsCursorOverCommanderUI()))
	{
		return false;
	}

	if (!CanIssueCommanderOrders() || !NetSyncComponent || !HasConfirmedSelection())
	{
		return false;
	}

	FVector GroundLocation;
	if (!TraceGroundUnderCursor(GroundLocation))
	{
		return false;
	}

	FGuLiMoveRequest Request;
	Request.Target = GroundLocation;
	Request.SelectionRevision = NetSyncComponent->GetSelectionState().SelectionRevision;
	Request.ClientCommandId = AllocateMoveCommandId();
	PendingMoveCommandId = Request.ClientCommandId;
	PendingMoveTarget = GroundLocation;
	CommandLineStart = FindConfirmedSelectionCenter();
	CommandLineEnd = GroundLocation;
	CommandLineState = EGuLiCommandLineState::Pending;
	CommandLineFadeDurationSeconds = 2.5f;
	CommandLineExpireTime = GetWorld()
		? GetWorld()->GetTimeSeconds() + static_cast<double>(CommandLineFadeDurationSeconds)
		: 0.0;
	for (TActorIterator<AGuLiCommanderPresentationActor> It(GetWorld()); It; ++It)
	{
		It->BeginPredictedMove(
			NetSyncComponent->GetSelectionState(),
			GroundLocation,
			Request.ClientCommandId);
		break;
	}
	NetSyncComponent->SubmitMoveRequest(Request);
	return true;
}

// 清空同样是一条选兵请求，必须经过服务器版本/权限判定，不能只清客户端副本。
void AGuLiCommanderPlayerController::ClearSelection()
{
	if (!CanIssueCommanderOrders() || !NetSyncComponent)
	{
		return;
	}

	FGuLiSelectionRequest Request;
	Request.Center = CachedCursorGroundLocation;
	Request.RadiusPreset = SelectionRadiusPreset;
	Request.Modifier = EGuLiSelectionModifier::Clear;
	Request.ClientRequestId = AllocateSelectionRequestId();
	Request.KnownSelectionRevision = NetSyncComponent->GetSelectionState().SelectionRevision;
	NetSyncComponent->SubmitSelectionRequest(Request);
}

bool AGuLiCommanderPlayerController::IsCursorOverCommanderUI() const
{
	float MouseX = 0.0f;
	float MouseY = 0.0f;
	const AGuLiCommanderHUD* CommanderHUD = Cast<AGuLiCommanderHUD>(GetHUD());
	return CommanderHUD
		&& GetMousePosition(MouseX, MouseY)
		&& CommanderHUD->IsScreenPositionOverCommanderUI(FVector2D(MouseX, MouseY));
}

bool AGuLiCommanderPlayerController::HasConfirmedSelection() const
{
	if (!NetSyncComponent)
	{
		return false;
	}

	for (const FGuLiControlCohortDescriptor& Cohort : NetSyncComponent->GetSelectionState().Cohorts)
	{
		if (!Cohort.MemberIds.IsEmpty())
		{
			return true;
		}
	}
	return false;
}

void AGuLiCommanderPlayerController::ZoomCameraIn()
{
	if (!IsCommanderViewActive())
	{
		return;
	}

	if (AGuLiCommanderCameraPawn* CameraPawn = GetPawn<AGuLiCommanderCameraPawn>())
	{
		CameraPawn->AddZoomInput(-1.0f);
	}
}

void AGuLiCommanderPlayerController::ZoomCameraOut()
{
	if (!IsCommanderViewActive())
	{
		return;
	}

	if (AGuLiCommanderCameraPawn* CameraPawn = GetPawn<AGuLiCommanderCameraPawn>())
	{
		CameraPawn->AddZoomInput(1.0f);
	}
}

bool AGuLiCommanderPlayerController::TraceGroundUnderCursor(FVector& OutLocation) const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	FVector RayOrigin;
	FVector RayDirection;
	if (!DeprojectMousePositionToWorld(RayOrigin, RayDirection)
		|| RayOrigin.ContainsNaN()
		|| RayDirection.ContainsNaN())
	{
		return false;
	}

	RayDirection = RayDirection.GetSafeNormal();
	if (RayDirection.IsNearlyZero())
	{
		return false;
	}

	const FVector TraceEnd = RayOrigin
		+ RayDirection * GuLiCommanderCursorTrace::MaximumGroundTraceDistanceCentimeters;
	FCollisionQueryParams QueryParams(
		SCENE_QUERY_STAT(GuLiCommanderCursorGround),
		true);
	if (const APawn* ControlledPawn = GetPawn())
	{
		QueryParams.AddIgnoredActor(ControlledPawn);
	}

	int32 IgnoredNonLandscapeBlockers = 0;
	for (;;)
	{
		FHitResult Hit;
		if (!World->LineTraceSingleByChannel(
			Hit,
			RayOrigin,
			TraceEnd,
			ECC_Visibility,
			QueryParams)
			|| !Hit.bBlockingHit)
		{
			return false;
		}

		if (Hit.GetActor() && Hit.GetActor()->IsA<ALandscapeProxy>())
		{
			OutLocation = Hit.ImpactPoint;
			return !OutLocation.ContainsNaN();
		}

		if (IgnoredNonLandscapeBlockers
			>= GuLiCommanderCursorTrace::MaximumIgnoredNonLandscapeBlockers)
		{
			return false;
		}

		if (AActor* HitActor = Hit.GetActor())
		{
			QueryParams.AddIgnoredActor(HitActor);
		}
		else if (UPrimitiveComponent* HitComponent = Hit.GetComponent())
		{
			QueryParams.AddIgnoredComponent(HitComponent);
		}
		else
		{
			return false;
		}
		++IgnoredNonLandscapeBlockers;
	}
}

FVector AGuLiCommanderPlayerController::FindConfirmedSelectionCenter() const
{
	if (!NetSyncComponent)
	{
		return PendingMoveTarget;
	}

	const TArray<FGuLiControlCohortDescriptor>& Cohorts = NetSyncComponent->GetSelectionState().Cohorts;
	if (Cohorts.IsEmpty())
	{
		return PendingMoveTarget;
	}

	for (TActorIterator<AGuLiCommanderPresentationActor> It(GetWorld()); It; ++It)
	{
		FVector Center = FVector::ZeroVector;
		int32 FoundCount = 0;
		for (const FGuLiControlCohortDescriptor& Cohort : Cohorts)
		{
			for (const FGuLiSoldierId SoldierId : Cohort.MemberIds)
			{
				FTransform PresentedTransform;
				if (It->TryGetPresentedSoldierTransform(SoldierId, PresentedTransform))
				{
					Center += PresentedTransform.GetLocation();
					++FoundCount;
				}
			}
		}
		return FoundCount > 0 ? Center / static_cast<float>(FoundCount) : PendingMoveTarget;
	}

	return PendingMoveTarget;
}

// 选兵序号与移动序号分别增长并跳过 0；因此相同数字必须结合 CommandKind 区分。
uint32 AGuLiCommanderPlayerController::AllocateSelectionRequestId()
{
	const uint32 Result = NextSelectionRequestId++;
	if (NextSelectionRequestId == 0u)
	{
		NextSelectionRequestId = 1u;
	}
	return Result;
}

uint32 AGuLiCommanderPlayerController::AllocateMoveCommandId()
{
	const uint32 Result = NextMoveCommandId++;
	if (NextMoveCommandId == 0u)
	{
		NextMoveCommandId = 1u;
	}
	return Result;
}

bool AGuLiCommanderPlayerController::IsCommanderViewActive() const
{
	const AGuLiBattlePlayerState* BattlePlayerState = GetPlayerState<AGuLiBattlePlayerState>();
	return IsLocalController() && BattlePlayerState && BattlePlayerState->IsCommander();
}

void AGuLiCommanderPlayerController::UpdateCommanderInputMode()
{
	if (!IsLocalController())
	{
		return;
	}
	const bool bShouldEnable = IsCommanderViewActive();
	if (bCommanderInputModeInitialized && bCommanderInputActive == bShouldEnable)
	{
		return;
	}
	bCommanderInputModeInitialized = true;
	bCommanderInputActive = bShouldEnable;
	// HUD 可能晚于 PC 创建，由其 BeginPlay 补齐；角色切换释放不等待 DrawHUD。
	if (AGuLiCommanderHUD* CommanderHUD = Cast<AGuLiCommanderHUD>(GetHUD()))
	{
		CommanderHUD->RefreshCommanderRole();
	}
	bShowMouseCursor = bShouldEnable;
	bEnableClickEvents = bShouldEnable;
	bEnableMouseOverEvents = bShouldEnable;
	if (bShouldEnable)
	{
		FInputModeGameAndUI InputMode;
		InputMode.SetHideCursorDuringCapture(false);
		InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		SetInputMode(InputMode);
	}
	else
	{
		SetInputMode(FInputModeGameOnly());
		CommandLineState = EGuLiCommandLineState::None;
		PendingMoveCommandId = 0u;
		ActivateSelectionTool();
	}
}
void AGuLiCommanderPlayerController::UpdateCameraInput(const float DeltaTime)
{
	AGuLiCommanderCameraPawn* CameraPawn = GetPawn<AGuLiCommanderCameraPawn>();
	if (!CameraPawn || !IsLocalController())
	{
		return;
	}

	FVector2D MovementInput = FVector2D::ZeroVector;
	MovementInput.X = (IsInputKeyDown(EKeys::W) ? 1.0f : 0.0f)
		- (IsInputKeyDown(EKeys::S) ? 1.0f : 0.0f);
	MovementInput.Y = (IsInputKeyDown(EKeys::D) ? 1.0f : 0.0f)
		- (IsInputKeyDown(EKeys::A) ? 1.0f : 0.0f);

	int32 ViewportX = 0;
	int32 ViewportY = 0;
	GetViewportSize(ViewportX, ViewportY);
	float MouseX = 0.0f;
	float MouseY = 0.0f;
	if (ViewportX > 0 && ViewportY > 0 && GetMousePosition(MouseX, MouseY))
	{
		constexpr float EdgePixels = 8.0f;
		MovementInput.Y += MouseX <= EdgePixels ? -1.0f : (MouseX >= ViewportX - EdgePixels ? 1.0f : 0.0f);
		MovementInput.X += MouseY <= EdgePixels ? 1.0f : (MouseY >= ViewportY - EdgePixels ? -1.0f : 0.0f);
	}

	CameraPawn->AddPlanarMovement(MovementInput.GetClampedToMaxSize(1.0f) * DeltaTime);
	const float YawInput = (IsInputKeyDown(EKeys::E) ? 1.0f : 0.0f)
		- (IsInputKeyDown(EKeys::Q) ? 1.0f : 0.0f);
	CameraPawn->AddYawInput(YawInput * DeltaTime);
}

void AGuLiCommanderPlayerController::UpdateAcceptedCommandVisual()
{
	if (!NetSyncComponent || !GetWorld())
	{
		return;
	}

	TArray<FGuLiCommandAck> PendingAcks;
	// 逐条消费回执，避免同帧只读最近值丢失反馈；只呈现当前 PendingMoveCommandId 的结果。
	NetSyncComponent->ConsumePendingCommandAcks(PendingAcks);
	for (const FGuLiCommandAck& Ack : PendingAcks)
	{
		if (Ack.CommandKind != EGuLiCommandKind::Move
			|| Ack.ClientCommandId == 0u
			|| Ack.ClientCommandId != PendingMoveCommandId
			|| Ack.ClientCommandId == LastVisualizedMoveCommandId)
		{
			continue;
		}

		for (TActorIterator<AGuLiCommanderPresentationActor> It(GetWorld()); It; ++It)
		{
			It->ResolvePredictedMove(Ack);
			break;
		}
		const bool bAccepted = Ack.IsAccepted() && Ack.BatchOrderId != 0u;
		CommandLineState = bAccepted
			? EGuLiCommandLineState::Accepted
			: EGuLiCommandLineState::Rejected;
		CommandLineFadeDurationSeconds = bAccepted ? 2.5f : 0.85f;
		CommandLineExpireTime = GetWorld()->GetTimeSeconds()
			+ static_cast<double>(CommandLineFadeDurationSeconds);
		LastVisualizedMoveCommandId = Ack.ClientCommandId;
		PendingMoveCommandId = 0u;
	}
}
