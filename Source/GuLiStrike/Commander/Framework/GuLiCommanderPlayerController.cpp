// Copyright Epic Games, Inc. All Rights Reserved.

#include "Commander/Framework/GuLiCommanderPlayerController.h"
#include "Gameplay/GroundMech/GuLiGroundMechCharacter.h"
#include "Gameplay/GroundMech/GuLiGroundMechWeaponComponent.h"
#include "Gameplay/CommanderSkills/GuLiCommanderSkillComponent.h"
#include "Gameplay/Teleport/GuLiTeleportInputComponent.h"

#include "Commander/Framework/GuLiCommanderNetSyncComponent.h"
#include "Commander/Framework/GuLiCommanderResourceAdapter.h"
#include "Battle/Framework/GuLiBattlePlayerState.h"
#include "Commander/Presentation/GuLiCommanderCameraPawn.h"
#include "Commander/Presentation/GuLiCommanderHUD.h"
#include "Commander/Presentation/GuLiCommanderPresentationActor.h"
#include "Commander/Network/GuLiSoldierStateReplicator.h"
#include "Commander/UI/GuLiCommanderCursorWidget.h"
#include "Gameplay/Building/GuLiBuildingPlacementComponent.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Console.h"
#include "UnrealClient.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/HUD.h"
#include "InputCoreTypes.h"
#include "LandscapeProxy.h"
#include "Gameplay/Resources/GuLiResourceActors.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "Engine/LocalPlayer.h"

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

GuLiCommanderToolPolicy::FMoveAckRoutingDecision GuLiCommanderToolPolicy::ResolveMoveAckRouting(
	const EGuLiCommandKind CommandKind,
	const uint32 AckCommandId,
	const uint32 LatestIntentCommandId,
	const uint32 DispatchedCommandId,
	const uint32 LastVisualizedCommandId)
{
	FMoveAckRoutingDecision Decision;
	Decision.bResolvePrediction = CommandKind == EGuLiCommandKind::Move && AckCommandId != 0u;
	Decision.bUpdateCommandLine = Decision.bResolvePrediction
		&& AckCommandId == LatestIntentCommandId
		&& AckCommandId != LastVisualizedCommandId;
	Decision.bClearDispatchedCommand = Decision.bResolvePrediction
		&& AckCommandId == DispatchedCommandId;
	return Decision;
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
	TeleportInput = CreateDefaultSubobject<UGuLiTeleportInputComponent>(TEXT("CommanderTeleportInput"));
	BuildingPlacementComponent = CreateDefaultSubobject<UGuLiBuildingPlacementComponent>(
		TEXT("BuildingPlacement"));
}

void AGuLiCommanderPlayerController::BeginPlay()
{
	Super::BeginPlay();
	if (NetSyncComponent)
	{
		MoveReadyHandle = NetSyncComponent->OnMoveReadyToSend.AddUObject(this, &ThisClass::HandleMoveReadyToSend);
		CommandAckChangedHandle = NetSyncComponent->OnCommandAckChanged.AddUObject(
			this,
			&ThisClass::HandleCommandAckChanged);
	}
	UpdateCommanderInputMode();
}

void AGuLiCommanderPlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (BattleInputSubsystem.IsValid()) BattleInputSubsystem->RemoveMappingContext(BattleCommandMappings);
	BattleInputSubsystem.Reset();
	CancelSelectionDrag();
	RestoreCommanderCursor();
	if (NetSyncComponent)
	{
		NetSyncComponent->OnMoveReadyToSend.Remove(MoveReadyHandle);
		NetSyncComponent->OnCommandAckChanged.Remove(CommandAckChangedHandle);
	}
	Super::EndPlay(EndPlayReason);
}

void AGuLiCommanderPlayerController::FlushPressedKeys()
{
	if (auto* Mech = Cast<AGuLiGroundMechCharacter>(GetPawn())) Mech->GetWeapon()->SetFireHeld(false);
	CancelSelectionDrag();
	Super::FlushPressedKeys();
}

void AGuLiCommanderPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();
	auto& Enhanced=*CastChecked<UEnhancedInputComponent>(InputComponent);
	BattleCommandMappings=LoadObject<UInputMappingContext>(nullptr,TEXT("/Game/GuLiStrike/GroundMech/Input/IMC_BattleCommands"));
	struct FBinding { const TCHAR* Name; void (ThisClass::*Handler)(); };
	const FBinding Bindings[]={
		{TEXT("Primary"),&ThisClass::HandlePrimaryActionAtCursor},
		{TEXT("Secondary"),&ThisClass::HandleSecondaryActionAtCursor},
		{TEXT("Cancel"),&ThisClass::HandleCancelInput},
		{TEXT("Slot1"),&ThisClass::HandleArmMoveToolInput},
		{TEXT("Slot2"),&ThisClass::HandleSelectBuildingTwoInput},
		{TEXT("Slot3"),&ThisClass::HandleSelectBuildingThreeInput},
		{TEXT("Slot4"),&ThisClass::HandleSelectBuildingFourInput},
		{TEXT("Slot5"),&ThisClass::HandleSelectBuildingFiveInput},
		{TEXT("Slot6"),&ThisClass::HandleSelectBuildingSixInput},
		{TEXT("Build"),&ThisClass::HandleToggleBuildModeInput},
		{TEXT("Select"),&ThisClass::HandleActivateSelectionToolInput},
		{TEXT("Radius"),&ThisClass::HandleStepSelectionRadiusInput},
		{TEXT("ZoomIn"),&ThisClass::ZoomCameraIn},
		{TEXT("ZoomOut"),&ThisClass::ZoomCameraOut},
		{TEXT("UnitSkill"),&ThisClass::HandleUnitSkillInput}};
	for (const auto& Binding:Bindings)
	{
		auto* Action=LoadObject<UInputAction>(nullptr,*(FString(TEXT("/Game/GuLiStrike/GroundMech/Input/IA_Battle_"))+Binding.Name));
		Enhanced.BindAction(Action,ETriggerEvent::Started,this,Binding.Handler);
		if (FStringView(Binding.Name)==TEXT("Primary"))
		{
			Enhanced.BindAction(Action,ETriggerEvent::Completed,this,&ThisClass::HandlePrimaryReleased);
			Enhanced.BindAction(Action,ETriggerEvent::Canceled,this,&ThisClass::HandlePrimaryReleased);
		}
	}
	Enhanced.BindAction(LoadObject<UInputAction>(nullptr,TEXT("/Game/GuLiStrike/GroundMech/Input/IA_Battle_Teleport")),
		ETriggerEvent::Started,TeleportInput.Get(),&UGuLiTeleportInputComponent::ActivateAiming);
}

void AGuLiCommanderPlayerController::PlayerTick(const float DeltaTime)
{
	Super::PlayerTick(DeltaTime);

	UpdateCommanderInputMode();
	if (BuildingPlacementComponent)
	{
		BuildingPlacementComponent->UpdatePlacementPreview(IsCursorOverCommanderUI());
	}
	if (!IsCommanderViewActive())
	{
		CancelSelectionDrag();
		bHasCursorGroundLocation = false;
		return;
	}
	if (bSelectionMouseDown)
	{
		float X = 0.0f, Y = 0.0f;
		if (!GetMousePosition(X, Y)) CancelSelectionDrag();
		else
		{
			SelectionDragEnd = FVector2D(X, Y);
			const float Scale = UWidgetLayoutLibrary::GetViewportScale(this);
			bSelectionDragExceededThreshold |= FVector2D::Distance(SelectionDragStart, SelectionDragEnd) >= 6.0f * Scale;
		}
	}
	bHasCursorGroundLocation = TraceGroundUnderCursor(CachedCursorGroundLocation);
	if (GuLiCommanderToolPolicy::ResolveModeForSelectionAvailability(
		CommanderToolMode,
		HasConfirmedSelection() || (NetSyncComponent && NetSyncComponent->HasUnresolvedSelectionIntent())) != CommanderToolMode)
	{
		ActivateSelectionTool();
	}
	UpdateCameraInput(DeltaTime);
	UpdateAcceptedCommandVisual();
}

void AGuLiCommanderPlayerController::ToggleSelectionShape()
{
	CancelSelectionDrag();
	ActivateSelectionTool();
	SelectionShape = SelectionShape == EGuLiCommanderSelectionShape::Box
		? EGuLiCommanderSelectionShape::Radius : EGuLiCommanderSelectionShape::Box;
	OnSelectionShapeChanged.Broadcast(SelectionShape);
}

bool AGuLiCommanderPlayerController::GetSelectionDragRectangle(FVector2D& OutStart, FVector2D& OutEnd) const
{
	OutStart = SelectionDragStart;
	OutEnd = SelectionDragEnd;
	return bSelectionMouseDown && bSelectionDragExceededThreshold && !bSelectionAltOnPress
		&& SelectionShape == EGuLiCommanderSelectionShape::Box;
}

void AGuLiCommanderPlayerController::CancelSelectionDrag()
{
	bSelectionMouseDown = false;
	bSelectionDragExceededThreshold = false;
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
	if (TeleportInput && TeleportInput->IsAiming()) { TeleportInput->CancelTeleport(); }
	if (CommanderToolMode == EGuLiCommanderToolMode::Select)
	{
		return;
	}

	CommanderToolMode = EGuLiCommanderToolMode::Select;
	OnCommanderToolModeChanged.Broadcast(CommanderToolMode);
}

bool AGuLiCommanderPlayerController::ArmMoveTool()
{
	if (TeleportInput && TeleportInput->IsAiming()) { TeleportInput->CancelTeleport(); }
	CancelSelectionDrag();
	if (!GuLiCommanderToolPolicy::CanArmMove(
		CanIssueCommanderOrders(),
		NetSyncComponent != nullptr,
		HasConfirmedSelection() || (NetSyncComponent && NetSyncComponent->HasUnresolvedSelectionIntent())))
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
	if (SelectionShape != EGuLiCommanderSelectionShape::Radius) return;
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
		|| (CommandLineState != EGuLiCommandLineState::Pending
			&& CommandLineExpireTime <= World->GetTimeSeconds()))
	{
		OutAlpha = 0.0f;
		OutState = EGuLiCommandLineState::None;
		return false;
	}
	if (CommandLineState == EGuLiCommandLineState::Pending
		&& (!NetSyncComponent || !NetSyncComponent->IsSoldierStreamReady()
			|| !NetSyncComponent->IsMoveCommandPending(LatestMoveIntentCommandId)))
	{
		OutAlpha = 0.0f;
		OutState = EGuLiCommandLineState::None;
		return false;
	}

	OutStart = CommandLineStart;
	OutEnd = CommandLineEnd;
	OutState = CommandLineState;
	if (CommandLineState == EGuLiCommandLineState::Pending)
	{
		OutAlpha = 1.0f;
		return true;
	}
	const float FadeDuration = FMath::Max(CommandLineFadeDurationSeconds, UE_SMALL_NUMBER);
	OutAlpha = FMath::Clamp(
		static_cast<float>((CommandLineExpireTime - World->GetTimeSeconds()) / FadeDuration),
		0.0f,
		1.0f);
	return true;
}

bool AGuLiCommanderPlayerController::CanUseGroundMechFireInput() const
{
	return IsLocalController() && !IsMoveInputIgnored() && !IsCursorOverCommanderUI()
		&& (!TeleportInput || !TeleportInput->IsAiming())
		&& (!BuildingPlacementComponent || !BuildingPlacementComponent->IsBuildModeActive());
}

void AGuLiCommanderPlayerController::HandlePrimaryActionAtCursor()
{
	if (TeleportInput && TeleportInput->HandlePrimaryAction()) { CancelSelectionDrag(); return; }
	if (BuildingPlacementComponent
		&& BuildingPlacementComponent->HandlePrimaryAction(IsCursorOverCommanderUI()))
	{
		CancelSelectionDrag();
		return;
	}
	if (!IsCommanderViewActive())
	{
		if (auto* Mech = Cast<AGuLiGroundMechCharacter>(GetPawn()); Mech && CanUseGroundMechFireInput())
			Mech->GetWeapon()->SetFireHeld(true);
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

	if (!CanIssueCommanderOrders() || IsCursorOverCommanderUI()) return;
	float X = 0.0f, Y = 0.0f;
	if (!GetMousePosition(X, Y)) return;
	bSelectionMouseDown = true;
	bSelectionDragExceededThreshold = false;
	bSelectionAddOnPress = IsInputKeyDown(EKeys::LeftShift) || IsInputKeyDown(EKeys::RightShift);
	bSelectionAltOnPress = IsInputKeyDown(EKeys::LeftAlt) || IsInputKeyDown(EKeys::RightAlt);
	SelectionDragStart = SelectionDragEnd = FVector2D(X, Y);
}

void AGuLiCommanderPlayerController::HandlePrimaryReleased()
{
	if (auto* Mech = Cast<AGuLiGroundMechCharacter>(GetPawn())) Mech->GetWeapon()->SetFireHeld(false);
	if (!bSelectionMouseDown) return;
	float X = 0.0f, Y = 0.0f;
	if (!IsCommanderViewActive() || !GetMousePosition(X, Y) || IsCursorOverCommanderUI())
	{
		CancelSelectionDrag();
		return;
	}
	SelectionDragEnd = FVector2D(X, Y);
	bSelectionDragExceededThreshold |= FVector2D::Distance(SelectionDragStart, SelectionDragEnd)
		>= 6.0f * UWidgetLayoutLibrary::GetViewportScale(this);
	const bool bDrag = bSelectionDragExceededThreshold;
	CancelSelectionDrag();
	if (bSelectionAltOnPress)
	{
		if (!bDrag) TryIssuePointSelection(true, bSelectionAddOnPress);
	}
	else if (SelectionShape == EGuLiCommanderSelectionShape::Radius) TryIssueSelectionAtCursor();
	else if (bDrag) TryIssueBoxSelection(SelectionDragStart, SelectionDragEnd, bSelectionAddOnPress);
	else TryIssuePointSelection(false, bSelectionAddOnPress);
}

void AGuLiCommanderPlayerController::HandleSecondaryActionAtCursor()
{
	if (TeleportInput && TeleportInput->IsAiming()) { TeleportInput->CancelTeleport(); return; }
	if (BuildingPlacementComponent && BuildingPlacementComponent->HandleCancelAction())
	{
		CancelSelectionDrag();
		return;
	}
	if (!IsCommanderViewActive())
	{
		return;
	}

	CancelSelectionDrag();
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
	ToggleSelectionShape();
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
	if (BuildingPlacementComponent && BuildingPlacementComponent->HandleNumberKey(1))
	{
		return;
	}
	if (!IsCommanderViewActive())
	{
		return;
	}

	ArmMoveTool();
}

void AGuLiCommanderPlayerController::HandleToggleBuildModeInput()
{
	if (TeleportInput && TeleportInput->IsAiming()) { TeleportInput->CancelTeleport(); }
	CancelSelectionDrag();
	if (BuildingPlacementComponent)
	{
		BuildingPlacementComponent->ToggleBuildMode();
	}
}

void AGuLiCommanderPlayerController::HandleSelectBuildingTwoInput()
{
	if (BuildingPlacementComponent)
	{
		BuildingPlacementComponent->HandleNumberKey(2);
	}
}

void AGuLiCommanderPlayerController::HandleSelectBuildingThreeInput()
{
	if (BuildingPlacementComponent)
	{
		BuildingPlacementComponent->HandleNumberKey(3);
	}
}

void AGuLiCommanderPlayerController::HandleSelectBuildingFourInput() { BuildingPlacementComponent->HandleNumberKey(4); }
void AGuLiCommanderPlayerController::HandleSelectBuildingFiveInput() { BuildingPlacementComponent->HandleNumberKey(5); }
void AGuLiCommanderPlayerController::HandleSelectBuildingSixInput() { BuildingPlacementComponent->HandleNumberKey(6); }

void AGuLiCommanderPlayerController::HandleCancelInput()
{
	if (TeleportInput && TeleportInput->IsAiming()) { TeleportInput->CancelTeleport(); return; }
	if (BuildingPlacementComponent && BuildingPlacementComponent->HandleCancelAction())
	{
		CancelSelectionDrag();
		return;
	}
	if (!IsCommanderViewActive())
	{
		return;
	}
	if (bSelectionMouseDown)
	{
		CancelSelectionDrag();
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
	Request.Modifier = bSelectionAddOnPress
		? EGuLiSelectionModifier::Add
		: EGuLiSelectionModifier::Replace;
	SubmitSelectionIntent(Request);
	return true;
}

void AGuLiCommanderPlayerController::SubmitSelectionIntent(FGuLiSelectionRequest Request)
{
	if (!CanIssueCommanderOrders() || !NetSyncComponent) return;
	Request.ClientRequestId = AllocateSelectionRequestId();
	Request.KnownSelectionRevision = NetSyncComponent->GetSelectionState().SelectionRevision;
	NetSyncComponent->SubmitSelectionRequest(Request);
}

bool AGuLiCommanderPlayerController::BuildPointSelectionRequest(FGuLiSelectionRequest& Request) const
{
	FVector Origin, Direction;
	float MouseX = 0.0f, MouseY = 0.0f;
	if (!GetMousePosition(MouseX, MouseY) || !DeprojectMousePositionToWorld(Origin, Direction)) return false;
	Request.RayOrigin = Origin;
	Request.RayDirection = Direction.GetSafeNormal();
	const float PixelTolerance = 6.0f * UWidgetLayoutLibrary::GetViewportScale(this);
	FVector OtherOrigin, OtherDirection;
	if (DeprojectScreenPositionToWorld(MouseX + PixelTolerance, MouseY, OtherOrigin, OtherDirection))
	{
		Request.PickHalfAngleRadians = FMath::Clamp(static_cast<float>(FMath::Acos(FMath::Clamp(
			FVector::DotProduct(Direction.GetSafeNormal(), OtherDirection.GetSafeNormal()), -1.0, 1.0))), 0.0001f, 0.05f);
	}
	const AGuLiBattlePlayerState* State = GetPlayerState<AGuLiBattlePlayerState>();
	if (!State || !GetWorld()) return false;
	const UGuLiCommanderResourceAdapter* ResourceAdapter =
		GetWorld()->GetSubsystem<UGuLiCommanderResourceAdapter>();
	check(ResourceAdapter);
	Request.SeedActorId = ResourceAdapter->FindControllableActorAlongRay(
		State->GetTeam(), Origin, Direction, Request.PickHalfAngleRadians);
	if (Request.SeedActorId.IsValid()) return true;
	AGuLiCommanderPresentationActor* Presentation = nullptr;
	AGuLiSoldierStateReplicator* Roster = nullptr;
	for (TActorIterator<AGuLiCommanderPresentationActor> It(GetWorld()); It; ++It) { Presentation = *It; break; }
	for (TActorIterator<AGuLiSoldierStateReplicator> It(GetWorld()); It; ++It) { Roster = *It; break; }
	if (!Presentation || !Roster) return false;
	double BestScore = TNumericLimits<double>::Max();
	for (const FGuLiSoldierStateItem& Soldier : Roster->GetItems())
	{
		if (!Soldier.IsAlive() || Soldier.Team != State->GetTeam()) continue;
		FTransform Transform;
		if (!Presentation->TryGetPresentedSoldierTransform(Soldier.SoldierId, Transform)) continue;
		const FBox Bounds = Presentation->GetUnitModelBoundsCentimeters(Soldier.UnitTypeId).TransformBy(Transform);
		if (!Bounds.IsValid) continue;
		const FVector Foot(Bounds.GetCenter().X, Bounds.GetCenter().Y, Bounds.Min.Z);
		const double ModelRadius = FMath::Max(Bounds.GetExtent().X, Bounds.GetExtent().Y);
		FVector2D Bottom, Top, Edge;
		if (!ProjectWorldLocationToScreen(Foot, Bottom)
			|| !ProjectWorldLocationToScreen(Foot + FVector(0, 0, Bounds.GetSize().Z), Top)
			|| !ProjectWorldLocationToScreen(Foot + GetControlRotation().RotateVector(FVector(0, ModelRadius, 0)), Edge)) continue;
		const FVector2D Mouse(MouseX, MouseY);
		const FVector2D Segment = Top - Bottom;
		const double Alpha = Segment.SizeSquared() > UE_SMALL_NUMBER
			? FMath::Clamp(FVector2D::DotProduct(Mouse - Bottom, Segment) / Segment.SizeSquared(), 0.0, 1.0) : 0.0;
		const double Distance = FVector2D::Distance(Mouse, Bottom + Segment * Alpha);
		const double Radius = FMath::Max(static_cast<double>(PixelTolerance), FVector2D::Distance(Edge, Bottom));
		const double Depth = FVector::DotProduct(Foot - Origin, Direction);
		if (Depth < 0.0 || Distance > Radius) continue;
		const double Score = Distance + Depth * 0.000001;
		if (Score < BestScore)
		{
			BestScore = Score;
			Request.SeedSoldierId = Soldier.SoldierId;
		}
	}
	return Request.SeedSoldierId.IsValid();
}

bool AGuLiCommanderPlayerController::TryIssuePointSelection(const bool bSameType, const bool bAdd)
{
	if (!CanIssueCommanderOrders()) return false;
	FGuLiSelectionRequest Request;
	if (!BuildPointSelectionRequest(Request))
	{
		if (!bSameType && !bAdd) ClearSelection();
		return false;
	}
	Request.Kind = bSameType ? EGuLiSelectionKind::SameType : EGuLiSelectionKind::Point;
	Request.Modifier = bAdd ? EGuLiSelectionModifier::Add : EGuLiSelectionModifier::Replace;
	SubmitSelectionIntent(Request);
	return true;
}

bool AGuLiCommanderPlayerController::TryIssueBoxSelection(const FVector2D& Start, const FVector2D& End, const bool bAdd)
{
	if (!CanIssueCommanderOrders()) return false;
	const FVector2D Min(FMath::Min(Start.X, End.X), FMath::Min(Start.Y, End.Y));
	const FVector2D Max(FMath::Max(Start.X, End.X), FMath::Max(Start.Y, End.Y));
	if (Max.X - Min.X < 1.0 || Max.Y - Min.Y < 1.0) return false;
	FGuLiSelectionRequest Request;
	Request.Kind = EGuLiSelectionKind::Box;
	Request.Modifier = bAdd ? EGuLiSelectionModifier::Add : EGuLiSelectionModifier::Replace;
	FVector Origin, Ray;
	if (!DeprojectScreenPositionToWorld(Min.X, Min.Y, Origin, Ray)) return false;
	// All four rays must share the camera origin, not the four distinct near-plane points.
	FVector CameraLocation; FRotator CameraRotation;
	GetPlayerViewPoint(CameraLocation, CameraRotation);
	Request.RayOrigin = CameraLocation;
	Request.BoxTopLeftRay = Ray.GetSafeNormal();
	if (!DeprojectScreenPositionToWorld(Max.X, Min.Y, Origin, Ray)) return false;
	Request.BoxTopRightRay = Ray.GetSafeNormal();
	if (!DeprojectScreenPositionToWorld(Max.X, Max.Y, Origin, Ray)) return false;
	Request.BoxBottomRightRay = Ray.GetSafeNormal();
	if (!DeprojectScreenPositionToWorld(Min.X, Max.Y, Origin, Ray)) return false;
	Request.BoxBottomLeftRay = Ray.GetSafeNormal();
	SubmitSelectionIntent(Request);
	return true;
}

// 先展示 Pending，再发移动意图；成功返回只表示本地提交路径完成。
bool AGuLiCommanderPlayerController::TryIssueMoveAtCursor()
{
	if (!GuLiCommanderToolPolicy::AllowsWorldIntent(IsCursorOverCommanderUI()))
	{
		return false;
	}

	if (!CanIssueCommanderOrders() || !NetSyncComponent
		|| (!HasConfirmedSelection() && !NetSyncComponent->HasUnresolvedSelectionIntent()))
	{
		return false;
	}

	FGuLiMoveRequest Request;
	Request.SelectionRevision = NetSyncComponent->GetSelectionState().SelectionRevision;
	Request.ClientCommandId = AllocateMoveCommandId();
	FVector RayOrigin, RayDirection;
	const bool bHasRay = DeprojectMousePositionToWorld(RayOrigin,RayDirection);
	const auto* BattlePlayer = GetPlayerState<AGuLiBattlePlayerState>();
	if (bHasRay && BattlePlayer && (!NetSyncComponent->GetSelectionState().ActorIds.IsEmpty()
		|| NetSyncComponent->HasUnresolvedSelectionIntent()))
	{
		// Model picking is separate from the Landscape-only ground trace.
		FHitResult Hit;
		FCollisionQueryParams Query(SCENE_QUERY_STAT(CommanderOutpostOrder),true,GetPawn());
		if (GetWorld()->LineTraceSingleByChannel(Hit,RayOrigin,
			RayOrigin+RayDirection*GuLiCommanderCursorTrace::MaximumGroundTraceDistanceCentimeters,ECC_Visibility,Query))
			if (const auto* Outpost = Cast<AGuLiTerritoryOutpostActor>(Hit.GetActor()); Outpost && Outpost->GetTerritoryOwner() == BattlePlayer->GetTeam())
			{ Request.TargetTerritoryId = Outpost->GetTerritoryId(); Request.Target = Hit.ImpactPoint; }
	}
	if (Request.TargetTerritoryId.IsNone())
	{
		FVector Ground;
		if (!TraceGroundUnderCursor(Ground)) return false;
		Request.Target = Ground;
		const auto& ResourceAdapter = *GetWorld()->GetSubsystem<UGuLiCommanderResourceAdapter>();
		if (bHasRay)
		{
			uint16 ClusterId = 0; FVector ClusterCenter;
			if (ResourceAdapter.FindClusterAlongRay(RayOrigin,RayDirection,ClusterId,ClusterCenter))
			{
				if (NetSyncComponent->GetSelectionState().ActorIds.IsEmpty()) return false;
				Request.MiningOrderType = EGuLiMiningOrderType::MineCluster;
				Request.TargetClusterId = ClusterId; Request.Target = ClusterCenter;
			}
			else if (BattlePlayer && ResourceAdapter.IsFactoryAlongRay(BattlePlayer->GetTeam(),RayOrigin,RayDirection))
			{
				if (NetSyncComponent->GetSelectionState().ActorIds.IsEmpty()) return false;
				Request.MiningOrderType = EGuLiMiningOrderType::ReturnToFactory;
			}
		}
	}
	const FVector GroundLocation = Request.Target;
#if !UE_BUILD_SHIPPING
	for (TActorIterator<AGuLiCommanderPresentationActor> It(GetWorld()); It; ++It)
	{
		It->TraceCommanderMoveInput(Request.ClientCommandId, GroundLocation);
		break;
	}
#endif
	LatestMoveIntentCommandId = Request.ClientCommandId;
	PendingMoveTarget = GroundLocation;
	CommandLineStart = FindConfirmedSelectionCenter();
	CommandLineEnd = GroundLocation;
	CommandLineState = EGuLiCommandLineState::Pending;
	CommandLineFadeDurationSeconds = 2.5f;
	CommandLineExpireTime = GetWorld()
		? GetWorld()->GetTimeSeconds() + static_cast<double>(CommandLineFadeDurationSeconds)
		: 0.0;
	NetSyncComponent->SubmitMoveRequest(Request);
	return true;
}

void AGuLiCommanderPlayerController::HandleMoveReadyToSend(
	const FGuLiMoveRequest& Request, const FGuLiCommanderSelectionState& Selection)
{
	if (!IsCommanderViewActive() || !GetWorld()) return;
	(void)Selection;
	PendingMoveCommandId = Request.ClientCommandId;
	if (LatestMoveIntentCommandId == 0u)
	{
		LatestMoveIntentCommandId = Request.ClientCommandId;
	}
	PendingMoveTarget = Request.Target;
	CommandLineStart = FindConfirmedSelectionCenter();
	CommandLineEnd = Request.Target;
	// 自由落位会在服务器分帧规划，客户端无法提前知道单兵是否接受及其最终槽。
	// 位移预表现会先把单位推向点击点，再在权威姿态到达时撤销偏移，造成每次发令都明显拉回。
	// 因此这里只保留命令反馈，单位位置始终由权威姿态流插值驱动。
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
	if (TeleportInput && TeleportInput->IsHUDHovered()) { return true; }
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
	return !NetSyncComponent->GetSelectionState().ActorIds.IsEmpty();
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

	const FGuLiCommanderSelectionState& Selection = NetSyncComponent->GetSelectionState();
	FVector Center = FVector::ZeroVector;
	int32 FoundCount = 0;
	if (GetWorld())
	{
		const UGuLiCommanderResourceAdapter* ResourceAdapter =
			GetWorld()->GetSubsystem<UGuLiCommanderResourceAdapter>();
		check(ResourceAdapter);
		FVector ActorCenter;
		if (ResourceAdapter->GetControllableActorCenter(Selection.ActorIds, ActorCenter))
		{
			Center += ActorCenter * Selection.ActorIds.Num();
			FoundCount += Selection.ActorIds.Num();
		}
	}

	for (TActorIterator<AGuLiCommanderPresentationActor> It(GetWorld()); It; ++It)
	{
		for (const FGuLiControlCohortDescriptor& Cohort : Selection.Cohorts)
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
		break;
	}

	return FoundCount > 0 ? Center / static_cast<float>(FoundCount) : PendingMoveTarget;
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
	if (!IsLocalController() || !BattlePlayerState || !BattlePlayerState->IsCommander())
	{
		return false;
	}
	const UGuLiCommanderResourceAdapter* ResourceAdapter = GetWorld()
		? GetWorld()->GetSubsystem<UGuLiCommanderResourceAdapter>() : nullptr;
	return ResourceAdapter && ResourceAdapter->IsCommandRuntimeReady();
}

void AGuLiCommanderPlayerController::UpdateCommanderInputMode()
{
	if (!IsLocalController())
	{
		return;
	}
#if !UE_BUILD_SHIPPING
	// The GM surface owns focus and UIOnly while open. Role polling must not steal it back.
	if (IsGMPanelOpen())
	{
		return;
	}
#endif
	const bool bShouldEnable = IsCommanderViewActive();
	const auto* State=GetPlayerState<AGuLiBattlePlayerState>();
	const bool bGround=State && State->GetBattleRole()==EGuLiCommanderRole::Ground;
	if (bCommanderInputModeInitialized && bCommanderInputActive == bShouldEnable && bGroundInputActive==bGround)
	{
		return;
	}
	bCommanderInputModeInitialized = true;
	bCommanderInputActive = bShouldEnable;
	bGroundInputActive = bGround;
	if (BattleInputSubsystem.IsValid()) BattleInputSubsystem->RemoveMappingContext(BattleCommandMappings);
	BattleInputSubsystem.Reset();
	if (bShouldEnable || bGround)
	{
		BattleInputSubsystem=ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer());
		BattleInputSubsystem->AddMappingContext(BattleCommandMappings,10);
	}
	// HUD 可能晚于 PC 创建，由其 BeginPlay 补齐；角色切换释放不等待 DrawHUD。
	if (AGuLiCommanderHUD* CommanderHUD = Cast<AGuLiCommanderHUD>(GetHUD()))
	{
		CommanderHUD->RefreshCommanderRole();
	}
	bShowMouseCursor = bShouldEnable || bGround;
	bEnableClickEvents = bShouldEnable || bGround;
	bEnableMouseOverEvents = bShouldEnable || bGround;
	DefaultMouseCursor=bGround?EMouseCursor::Crosshairs:EMouseCursor::Default;
	if (bShouldEnable)
	{
		if (UGameViewportClient* Viewport = GetWorld() ? GetWorld()->GetGameViewport() : nullptr)
		{
			PreviousSoftwareCursor = Viewport->GetSoftwareCursorWidget(EMouseCursor::Default);
			CommanderCursorWidget = CreateWidget<UGuLiCommanderCursorWidget>(this, UGuLiCommanderCursorWidget::StaticClass());
			Viewport->SetSoftwareCursorWidget(EMouseCursor::Default, CommanderCursorWidget.Get());
			bCommanderCursorRegistered = true;
		}
		FInputModeGameAndUI InputMode;
		InputMode.SetHideCursorDuringCapture(false);
		InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		SetInputMode(InputMode);
	}
	else
	{
		CancelSelectionDrag();
		RestoreCommanderCursor();
		if (bGround)
		{
			FInputModeGameAndUI InputMode;
			InputMode.SetHideCursorDuringCapture(false);
			InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::LockOnCapture);
			SetInputMode(InputMode);
		}
		else SetInputMode(FInputModeGameOnly());
		CommandLineState = EGuLiCommandLineState::None;
		PendingMoveCommandId = 0u;
		LatestMoveIntentCommandId = 0u;
		ImmediatelyHandledMoveAckIds.Reset();
		ActivateSelectionTool();
	}
}

#if !UE_BUILD_SHIPPING
void AGuLiCommanderPlayerController::RestoreGameplayInputAfterGMPanel()
{
	// Rebuild from the role that is current at close time, including a role change made while the panel was open.
	RestoreCommanderCursor();
	bCommanderInputModeInitialized = false;
	UpdateCommanderInputMode();
}
#endif

void AGuLiCommanderPlayerController::RestoreCommanderCursor()
{
	if (bCommanderCursorRegistered)
	{
		if (UGameViewportClient* Viewport = GetWorld() ? GetWorld()->GetGameViewport() : nullptr)
		{
			Viewport->SetSoftwareCursorWidget(EMouseCursor::Default, PreviousSoftwareCursor);
		}
	}
	bCommanderCursorRegistered = false;
	CommanderCursorWidget = nullptr;
	PreviousSoftwareCursor.Reset();
}
void AGuLiCommanderPlayerController::UpdateCameraInput(const float DeltaTime)
{
	AGuLiCommanderCameraPawn* CameraPawn = GetPawn<AGuLiCommanderCameraPawn>();
	if (!CameraPawn || !IsLocalController())
	{
		return;
	}
	UGameViewportClient* GameViewport = GetWorld()->GetGameViewport();
	if (GameViewport && GameViewport->ViewportConsole && GameViewport->ViewportConsole->ConsoleActive())
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
	const bool bViewportFocused = GameViewport && GameViewport->Viewport
		&& GameViewport->Viewport->HasFocus();
	if (bViewportFocused
		&& ViewportX > 0 && ViewportY > 0
		&& GetMousePosition(MouseX, MouseY)
		&& MouseX >= 0.0f && MouseX < static_cast<float>(ViewportX)
		&& MouseY >= 0.0f && MouseY < static_cast<float>(ViewportY))
	{
		const float DPIScale = FMath::Max(0.01f, UWidgetLayoutLibrary::GetViewportScale(this));
		const float EdgePixels = 16.0f * DPIScale;
		const auto SmoothEdgeStrength = [EdgePixels](const float DistanceToEdge)
		{
			const float Alpha = FMath::Clamp((EdgePixels - DistanceToEdge) / EdgePixels, 0.0f, 1.0f);
			return Alpha * Alpha * (3.0f - 2.0f * Alpha);
		};
		MovementInput.Y -= SmoothEdgeStrength(MouseX);
		MovementInput.Y += SmoothEdgeStrength(static_cast<float>(ViewportX) - MouseX);
		MovementInput.X += SmoothEdgeStrength(MouseY);
		MovementInput.X -= SmoothEdgeStrength(static_cast<float>(ViewportY) - MouseY);
	}

	CameraPawn->AddPlanarMovement(MovementInput.GetClampedToMaxSize(1.0f) * DeltaTime);
	const float YawInput = (IsInputKeyDown(EKeys::C) ? 1.0f : 0.0f)
		- (IsInputKeyDown(EKeys::Z) ? 1.0f : 0.0f);
	CameraPawn->AddYawInput(YawInput * DeltaTime);
}

void AGuLiCommanderPlayerController::UpdateAcceptedCommandVisual()
{
	if (!NetSyncComponent || !GetWorld())
	{
		return;
	}

	TArray<FGuLiCommandAck> PendingAcks;
	// The delegate resolves normal ACKs synchronously before NetSync may dispatch the next move.
	// Keep this FIFO fallback for ACKs received before delegate binding, and drain every entry.
	NetSyncComponent->ConsumePendingCommandAcks(PendingAcks);
	for (const FGuLiCommandAck& Ack : PendingAcks)
	{
		if (Ack.CommandKind != EGuLiCommandKind::Move || Ack.ClientCommandId == 0u)
		{
			continue;
		}
		if (ImmediatelyHandledMoveAckIds.Remove(Ack.ClientCommandId) > 0)
		{
			continue;
		}
		ProcessMoveCommandAck(Ack);
	}
	// Any synchronously handled ACK evicted by the bounded FIFO must not leave an unbounded ID set.
	ImmediatelyHandledMoveAckIds.Reset();
}

void AGuLiCommanderPlayerController::HandleCommandAckChanged(const FGuLiCommandAck& Ack)
{
	if (ProcessMoveCommandAck(Ack))
	{
		ImmediatelyHandledMoveAckIds.Add(Ack.ClientCommandId);
	}
}

bool AGuLiCommanderPlayerController::ProcessMoveCommandAck(const FGuLiCommandAck& Ack)
{
	if (!GetWorld())
	{
		return false;
	}
	const GuLiCommanderToolPolicy::FMoveAckRoutingDecision Decision =
		GuLiCommanderToolPolicy::ResolveMoveAckRouting(
			Ack.CommandKind,
			Ack.ClientCommandId,
			LatestMoveIntentCommandId,
			PendingMoveCommandId,
			LastVisualizedMoveCommandId);
	if (!Decision.bResolvePrediction)
	{
		return false;
	}

	// bResolvePrediction is the existing move-ACK routing predicate. Positional prediction is
	// intentionally disabled, so ACK handling only updates command feedback and queue state here.
	if (Decision.bClearDispatchedCommand)
	{
		PendingMoveCommandId = 0u;
	}
	if (!Decision.bUpdateCommandLine)
	{
		return true;
	}
	if (Ack.Result == EGuLiCommandAckResult::Cancelled || Ack.Result == EGuLiCommandAckResult::TimedOut)
	{
		CommandLineState = EGuLiCommandLineState::None;
		CommandLineExpireTime = 0.0;
		LastVisualizedMoveCommandId = Ack.ClientCommandId;
		LatestMoveIntentCommandId = 0u;
		return true;
	}

	const bool bAccepted = Ack.IsAccepted() && (Ack.BatchOrderId != 0u || !Ack.EngineeringResults.IsEmpty());
	if (!Ack.EngineeringResults.IsEmpty())
	{
		TArray<FString> Messages;
		for (const auto& Result : Ack.EngineeringResults)
			Messages.AddUnique(GuLiEngineeringCommands::Describe(Result.Result));
		if (auto* HUD = Cast<AGuLiCommanderHUD>(GetHUD()))
			HUD->ShowCommandFeedback(FText::FromString(FString::Join(Messages,TEXT("；"))),bAccepted);
	}
	CommandLineState = bAccepted
		? EGuLiCommandLineState::Accepted
		: EGuLiCommandLineState::Rejected;
	CommandLineFadeDurationSeconds = bAccepted ? 2.5f : 0.85f;
	CommandLineExpireTime = GetWorld()->GetTimeSeconds()
		+ static_cast<double>(CommandLineFadeDurationSeconds);
	LastVisualizedMoveCommandId = Ack.ClientCommandId;
	LatestMoveIntentCommandId = 0u;
	return true;
}

void AGuLiCommanderPlayerController::HandleUnitSkillInput()
{
	if (!CanIssueCommanderOrders() || IsCursorOverCommanderUI() || bSelectionMouseDown
		|| (TeleportInput && TeleportInput->IsAiming())
		|| (GetBuildingPlacementComponent() && GetBuildingPlacementComponent()->IsBuildModeActive())) return;
	FVector Ground = FVector::ZeroVector;
	const bool bHasGround = TraceGroundUnderCursor(Ground);
	GetPlayerState<AGuLiBattlePlayerState>()->GetCommanderSkills()->ActivateSelectedUnits(bHasGround, Ground);
}
