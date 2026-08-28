// Copyright Epic Games, Inc. All Rights Reserved.

#include "Commander/Framework/GuLiCommanderPlayerController.h"

#include "Commander/Framework/GuLiCommanderNetSyncComponent.h"
#include "Commander/Framework/GuLiCommanderPlayerState.h"
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

AGuLiCommanderPlayerController::AGuLiCommanderPlayerController()
{
	bShowMouseCursor = true;
	bEnableClickEvents = true;
	bEnableMouseOverEvents = true;
	DefaultMouseCursor = EMouseCursor::Default;
	HitResultTraceDistance = GuLiCommanderCursorTrace::MaximumGroundTraceDistanceCentimeters;

	NetSyncComponent = CreateDefaultSubobject<UGuLiCommanderNetSyncComponent>(TEXT("CommanderNetSync"));
}

void AGuLiCommanderPlayerController::BeginPlay()
{
	Super::BeginPlay();

	if (IsLocalController())
	{
		FInputModeGameAndUI InputMode;
		InputMode.SetHideCursorDuringCapture(false);
		InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		SetInputMode(InputMode);
	}
}

void AGuLiCommanderPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();
	if (!InputComponent)
	{
		return;
	}

	InputComponent->BindKey(EKeys::LeftMouseButton, IE_Pressed, this, &AGuLiCommanderPlayerController::SelectAtCursor);
	InputComponent->BindKey(EKeys::RightMouseButton, IE_Pressed, this, &AGuLiCommanderPlayerController::MoveAtCursor);
	InputComponent->BindKey(EKeys::Escape, IE_Pressed, this, &AGuLiCommanderPlayerController::ClearSelection);
	InputComponent->BindKey(EKeys::One, IE_Pressed, this, &AGuLiCommanderPlayerController::SelectSmallRadius);
	InputComponent->BindKey(EKeys::Two, IE_Pressed, this, &AGuLiCommanderPlayerController::SelectMediumRadius);
	InputComponent->BindKey(EKeys::Three, IE_Pressed, this, &AGuLiCommanderPlayerController::SelectLargeRadius);
	InputComponent->BindKey(EKeys::MouseScrollUp, IE_Pressed, this, &AGuLiCommanderPlayerController::ZoomCameraIn);
	InputComponent->BindKey(EKeys::MouseScrollDown, IE_Pressed, this, &AGuLiCommanderPlayerController::ZoomCameraOut);
}

void AGuLiCommanderPlayerController::PlayerTick(const float DeltaTime)
{
	Super::PlayerTick(DeltaTime);

	bHasCursorGroundLocation = TraceGroundUnderCursor(CachedCursorGroundLocation);
	UpdateBootstrapRetry();
	UpdateCameraInput(DeltaTime);
	UpdateAcceptedCommandVisual();
}

bool AGuLiCommanderPlayerController::CanIssueCommanderOrders() const
{
	const AGuLiCommanderPlayerState* CommanderPlayerState = GetPlayerState<AGuLiCommanderPlayerState>();
	return CommanderPlayerState
		&& CommanderPlayerState->IsCommander()
		&& CommanderPlayerState->IsSyncReady();
}

void AGuLiCommanderPlayerController::SetSelectionRadiusPreset(
	const EGuLiSelectionRadiusPreset NewPreset)
{
	switch (NewPreset)
	{
	case EGuLiSelectionRadiusPreset::Small:
	case EGuLiSelectionRadiusPreset::Medium:
	case EGuLiSelectionRadiusPreset::Large:
		SelectionRadiusPreset = NewPreset;
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

void AGuLiCommanderPlayerController::SelectAtCursor()
{
	float MouseX = 0.0f;
	float MouseY = 0.0f;
	if (const AGuLiCommanderHUD* CommanderHUD = Cast<AGuLiCommanderHUD>(GetHUD());
		CommanderHUD
		&& GetMousePosition(MouseX, MouseY)
		&& CommanderHUD->IsScreenPositionOverCommanderUI(FVector2D(MouseX, MouseY)))
	{
		// Canvas hit boxes receive their click later in the input frame. Do not
		// also turn the same LMB press into a world selection RPC.
		return;
	}

	if (!CanIssueCommanderOrders() || !NetSyncComponent)
	{
		return;
	}

	FVector GroundLocation;
	if (!TraceGroundUnderCursor(GroundLocation))
	{
		return;
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
}

void AGuLiCommanderPlayerController::MoveAtCursor()
{
	float MouseX = 0.0f;
	float MouseY = 0.0f;
	if (const AGuLiCommanderHUD* CommanderHUD = Cast<AGuLiCommanderHUD>(GetHUD());
		CommanderHUD
		&& GetMousePosition(MouseX, MouseY)
		&& CommanderHUD->IsScreenPositionOverCommanderUI(FVector2D(MouseX, MouseY)))
	{
		return;
	}

	if (!CanIssueCommanderOrders() || !NetSyncComponent
		|| NetSyncComponent->GetSelectionState().Cohorts.IsEmpty())
	{
		return;
	}

	FVector GroundLocation;
	if (!TraceGroundUnderCursor(GroundLocation))
	{
		return;
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
}

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

void AGuLiCommanderPlayerController::SelectSmallRadius()
{
	SetSelectionRadiusPreset(EGuLiSelectionRadiusPreset::Small);
}

void AGuLiCommanderPlayerController::SelectMediumRadius()
{
	SetSelectionRadiusPreset(EGuLiSelectionRadiusPreset::Medium);
}

void AGuLiCommanderPlayerController::SelectLargeRadius()
{
	SetSelectionRadiusPreset(EGuLiSelectionRadiusPreset::Large);
}

void AGuLiCommanderPlayerController::ZoomCameraIn()
{
	if (AGuLiCommanderCameraPawn* CameraPawn = GetPawn<AGuLiCommanderCameraPawn>())
	{
		CameraPawn->AddZoomInput(-1.0f);
	}
}

void AGuLiCommanderPlayerController::ZoomCameraOut()
{
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

	const TArray<FGuLiControlCohortDescriptor>& Cohorts =
		NetSyncComponent->GetSelectionState().Cohorts;
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

void AGuLiCommanderPlayerController::UpdateBootstrapRetry()
{
	if (!IsLocalController() || !NetSyncComponent || !GetWorld())
	{
		return;
	}

	const AGuLiCommanderPlayerState* CommanderPlayerState =
		GetPlayerState<AGuLiCommanderPlayerState>();
	if (!CommanderPlayerState || CommanderPlayerState->IsSyncReady())
	{
		return;
	}

	const double Now = GetWorld()->GetTimeSeconds();
	if (Now < NextBootstrapRetryTime)
	{
		return;
	}

	NetSyncComponent->ServerRequestBootstrap(NextBootstrapRequestId++);
	if (NextBootstrapRequestId == 0u)
	{
		NextBootstrapRequestId = 1u;
	}
	NextBootstrapRetryTime = Now + 2.0;
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
