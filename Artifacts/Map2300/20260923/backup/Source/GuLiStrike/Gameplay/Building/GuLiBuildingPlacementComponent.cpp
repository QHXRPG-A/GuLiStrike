// Copyright Epic Games, Inc. All Rights Reserved.

#include "Gameplay/Building/GuLiBuildingPlacementComponent.h"
#include "Gameplay/Data/GuLiGameText.h"

#include "Battle/Framework/GuLiBattleGameState.h"
#include "Battle/Framework/GuLiBattlePlayerController.h"
#include "Battle/Framework/GuLiBattlePlayerState.h"
#include "Battle/Network/GuLiPlayerNetSyncComponent.h"
#include "Commander/Mass/GuLiBattleAuthoritySubsystem.h"
#include "Commander/Mass/Navigation/GuLiCommanderNavigationPolicy.h"
#include "Commander/Network/GuLiSoldierStateReplicator.h"
#include "Commander/Presentation/GuLiCommanderPresentationActor.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Gameplay/Building/GuLiBuildingCatalog.h"
#include "Gameplay/Building/GuLiBuildingRegistrySubsystem.h"
#include "Gameplay/Building/GuLiBuildingSpawner.h"
#include "Gameplay/Building/GuLiBuildingVisuals.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "Engine/LocalPlayer.h"
#include "Gameplay/Resources/GuLiResourceWorldSubsystem.h"
#include "Gameplay/Building/GuLiBuildingPlacementPreview.h"
#include "Gameplay/Building/GuLiPlacedBuilding.h"
#include "Gameplay/Economy/GuLiTeamEconomySubsystem.h"

#if WITH_EDITOR
#include "Commander/Framework/GuLiCommanderPlayerController.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "HAL/IConsoleManager.h"
#include "String/LexFromString.h"
#endif

namespace GuLiBuildingPlacement
{
	bool IsBuildingActor(const AActor* Actor)
	{
		for (const AActor* Current = Actor; Current; Current = Current->GetParentActor())
			if (Current->FindComponentByClass<UGuLiBuildingLifecycleComponent>()) return true;
		return false;
	}

	void IgnoreBuildings(UWorld& World, FCollisionQueryParams& Query)
	{
		TArray<UGuLiBuildingLifecycleComponent*> Buildings;
		World.GetSubsystem<UGuLiBuildingRegistrySubsystem>()->Query(Buildings);
		for (const auto* Building : Buildings)
		{
			Query.AddIgnoredActor(Building->GetOwner());
			TArray<AActor*> Children;
			Building->GetOwner()->GetAllChildActors(Children, true);
			Query.AddIgnoredActors(Children);
		}
	}
	constexpr float CommanderTraceDistanceCentimeters = 600000.0f;
	constexpr float ServerGroundTraceUpCentimeters = 5000.0f;
	constexpr float ServerGroundTraceDownCentimeters = 5000.0f;
	constexpr float GroundContactToleranceCentimeters = 20.0f;
	constexpr float CollisionGroundEpsilonCentimeters = 0.4f;

	uint32 AllocateNonZeroRequestId(uint32& NextId)
	{
		const uint32 Result = NextId == 0u ? 1u : NextId;
		NextId = Result + 1u;
		if (NextId == 0u)
		{
			NextId = 1u;
		}
		return Result;
	}

	bool DoesDiscOverlapOrientedBox(
		const FVector& DiscCenter,
		const float DiscRadius,
		const FVector& BoxCenter,
		const FVector& BoxExtent,
		const float BoxYawDegrees)
	{
		const FVector LocalCenter = FRotator(0.0f, BoxYawDegrees, 0.0f)
			.UnrotateVector(DiscCenter - BoxCenter);
		if (FMath::Abs(LocalCenter.Z) > BoxExtent.Z + DiscRadius)
		{
			return false;
		}
		const double DeltaX = FMath::Max(
			FMath::Abs(static_cast<double>(LocalCenter.X)) - static_cast<double>(BoxExtent.X),
			0.0);
		const double DeltaY = FMath::Max(
			FMath::Abs(static_cast<double>(LocalCenter.Y)) - static_cast<double>(BoxExtent.Y),
			0.0);
		return DeltaX * DeltaX + DeltaY * DeltaY
			<= FMath::Square(static_cast<double>(DiscRadius));
	}
}

UGuLiBuildingPlacementComponent::UGuLiBuildingPlacementComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UGuLiBuildingPlacementComponent::BeginPlay()
{
	Super::BeginPlay();
	ResolveCatalog();
}

void UGuLiBuildingPlacementComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ExitBuildMode(false);
	ServerRequestTimes.Reset();
	Super::EndPlay(EndPlayReason);
}

APlayerController* UGuLiBuildingPlacementComponent::GetOwningPlayerController() const
{
	return Cast<APlayerController>(GetOwner());
}

UGuLiBuildingCatalog* UGuLiBuildingPlacementComponent::ResolveCatalog()
{
	if (!CachedCatalog)
	{
		CachedCatalog = UGuLiBuildingCatalog::LoadDefaultCatalog();
	}
	return CachedCatalog;
}

bool UGuLiBuildingPlacementComponent::CanLocallyBuild(
	EGuLiBuildingPlacementRejectReason& OutReason) const
{
	OutReason = EGuLiBuildingPlacementRejectReason::NotReady;
	const APlayerController* PlayerController = GetOwningPlayerController();
	const AGuLiBattlePlayerController* BattleController = Cast<AGuLiBattlePlayerController>(PlayerController);
	const AGuLiBattlePlayerState* PlayerState = PlayerController
		? PlayerController->GetPlayerState<AGuLiBattlePlayerState>()
		: nullptr;
	if (!PlayerController || !PlayerController->IsLocalController() || !PlayerState)
	{
		return false;
	}
	if (!GuLiBuildingPlacementPolicy::IsBuildingRole(PlayerState->GetBattleRole())
		|| PlayerState->GetTeam() == EGuLiTeam::Unassigned)
	{
		OutReason = EGuLiBuildingPlacementRejectReason::UnauthorizedRole;
		return false;
	}
	const UGuLiPlayerNetSyncComponent* ConnectionSync = BattleController
		? BattleController->GetPlayerNetSyncComponent()
		: nullptr;
	const AGuLiBattleGameState* GameState = GetWorld()
		? GetWorld()->GetGameState<AGuLiBattleGameState>()
		: nullptr;
	if (!PlayerState->IsBattleReady() || !ConnectionSync || !ConnectionSync->IsConnectionReady()
		|| !GameState || GameState->GetMatchEpoch() == 0u)
	{
		return false;
	}
	if (!CachedCatalog || !CachedCatalog->IsUsable())
	{
		OutReason = EGuLiBuildingPlacementRejectReason::AssetUnavailable;
		return false;
	}
	OutReason = EGuLiBuildingPlacementRejectReason::None;
	return true;
}

bool UGuLiBuildingPlacementComponent::ToggleBuildMode()
{
	if (bBuildModeActive)
	{
		ExitBuildMode(true);
		return true;
	}
	EnterBuildMode();
	return true;
}

bool UGuLiBuildingPlacementComponent::EnterBuildMode()
{
	ResolveCatalog();
	EGuLiBuildingPlacementRejectReason Reason;
	if (!CanLocallyBuild(Reason))
	{
		EmitRejectedFeedback(Reason);
		return false;
	}
	bBuildModeActive = true;
	FVector ViewLocation;
	FRotator ViewRotation;
	GetOwningPlayerController()->GetPlayerViewPoint(ViewLocation, ViewRotation);
	PreviewYawDegrees = GuLiBuildingPlacementPolicy::SnapYaw(ViewRotation.Yaw);
	LastCandidateValidationTime = -1.0;
	SetRotationInputEnabled(true);
	bHasPreviewCandidate = false;
	PreviewRejectReason = EGuLiBuildingPlacementRejectReason::NoGround;
	EmitSelectionFeedback();
	return true;
}

void UGuLiBuildingPlacementComponent::ExitBuildMode(const bool bEmitMessage)
{
	const bool bWasActive = bBuildModeActive;
	bBuildModeActive = false;
	SetRotationInputEnabled(false);
	bHasPreviewCandidate = false;
	PreviewRejectReason = EGuLiBuildingPlacementRejectReason::NoGround;
	DestroyPreview();
	if (bWasActive && bEmitMessage)
	{
		EmitFeedback(
			GuLiGameText::Get(TEXT("UI.BuildingPlacementComponent.133")),
			EGuLiBuildingFeedbackTone::Info);
	}
}

bool UGuLiBuildingPlacementComponent::HandleNumberKey(const int32 Number)
{
	if (!bBuildModeActive)
	{
		return false;
	}
	const APlayerController* PlayerController = GetOwningPlayerController();
	const AGuLiBattlePlayerState* PlayerState = PlayerController
		? PlayerController->GetPlayerState<AGuLiBattlePlayerState>()
		: nullptr;
	const GuLiBuildingPlacementPolicy::FNumberKeyDecision Decision =
		GuLiBuildingPlacementPolicy::ResolveNumberKey(
			Number,
			bBuildModeActive,
			PlayerState ? PlayerState->GetBattleRole() : EGuLiCommanderRole::Unassigned);
	if (!Decision.bHandled)
	{
		return false;
	}
	SelectedBuildingType = Decision.SelectedType;
	DestroyPreview();
	bHasPreviewCandidate = false;
	EmitSelectionFeedback();
	return true;
}

bool UGuLiBuildingPlacementComponent::SelectBuildingType(EGuLiBuildingType Type)
{
	auto* Catalog = ResolveCatalog();
	if (!Catalog || !Catalog->FindDefinition(Type)) return false;
	if (!bBuildModeActive && !EnterBuildMode()) return false;
	SelectedBuildingType = Type; DestroyPreview(); bHasPreviewCandidate = false;
	EmitSelectionFeedback(); return true;
}

FText UGuLiBuildingPlacementComponent::GetPlacementStatusText() const
{
	if (!bBuildModeActive) return FText::GetEmpty();
	const FText Status = PreviewRejectReason == EGuLiBuildingPlacementRejectReason::None
		? FText::FromString(GuLiGameText::Text(TEXT("UI.BuildingPlacementComponent.138")))
		: GetGuLiBuildingPlacementReasonText(PreviewRejectReason);
	return FText::FromString(Status.ToString() + TEXT("  |  ") + GuLiGameText::Text(TEXT("UI.BuildingPlacementComponent.RotateHint")));
}

void UGuLiBuildingPlacementComponent::SetRotationInputEnabled(bool bEnabled)
{
	auto* Controller = GetOwningPlayerController();
	auto* Player = Controller ? Controller->GetLocalPlayer() : nullptr;
	auto* Subsystem = Player ? ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(Player) : nullptr;
	auto* Input = Controller ? Cast<UEnhancedInputComponent>(Controller->InputComponent) : nullptr;
	if (!Subsystem) return;
	if (!bEnabled)
	{
		if (RotationMappings) Subsystem->RemoveMappingContext(RotationMappings);
		return;
	}
	if (!Input) return;
	if (!RotationMappings)
	{
		RotateAction = NewObject<UInputAction>(this);
		RotateAction->ValueType = EInputActionValueType::Boolean;
		RotationMappings = NewObject<UInputMappingContext>(this);
		RotationMappings->MapKey(RotateAction, EKeys::R);
	}
	if (!bRotationInputBound)
	{
		Input->BindAction(RotateAction, ETriggerEvent::Started, this, &ThisClass::HandleRotateAction);
		bRotationInputBound = true;
	}
	Subsystem->AddMappingContext(RotationMappings, 100);
}

void UGuLiBuildingPlacementComponent::HandleRotateAction()
{
	if (!bBuildModeActive) return;
	PreviewYawDegrees = GuLiBuildingPlacementPolicy::SnapYaw(PreviewYawDegrees + 90.0f);
	LastCandidateValidationTime = -1.0;
	// The normal controller update supplies the current UI-blocking state.
}

bool UGuLiBuildingPlacementComponent::HandlePrimaryAction(
	const bool bWorldInputBlockedByUI)
{
	if (!bBuildModeActive)
	{
		return false;
	}
	if (bWorldInputBlockedByUI)
	{
		EmitFeedback(
			GuLiGameText::Get(TEXT("UI.BuildingPlacementComponent.134")),
			EGuLiBuildingFeedbackTone::Error);
		return true;
	}
	UpdatePlacementPreview(false, true);
	if (!bHasPreviewCandidate || PreviewRejectReason != EGuLiBuildingPlacementRejectReason::None)
	{
		EmitRejectedFeedback(PreviewRejectReason);
		return true;
	}

	FGuLiBuildingPlacementRequest Request;
	Request.ClientRequestId = GuLiBuildingPlacement::AllocateNonZeroRequestId(NextClientRequestId);
	Request.Type = SelectedBuildingType;
	Request.DesiredGroundLocation = PreviewGroundLocation;
	Request.CompressedYaw = FRotator::CompressAxisToShort(PreviewYawDegrees);
	ServerRequestPlacement(Request);
	return true;
}

bool UGuLiBuildingPlacementComponent::HandleCancelAction()
{
	if (!bBuildModeActive)
	{
		return false;
	}
	ExitBuildMode(true);
	return true;
}

void UGuLiBuildingPlacementComponent::UpdatePlacementPreview(
	const bool bWorldInputBlockedByUI, const bool bForceValidation)
{
	if (!bBuildModeActive)
	{
		return;
	}

	EGuLiBuildingPlacementRejectReason EligibilityReason;
	if (!CanLocallyBuild(EligibilityReason))
	{
		ExitBuildMode(false);
		EmitRejectedFeedback(EligibilityReason);
		return;
	}
	if (bWorldInputBlockedByUI)
	{
		bHasPreviewCandidate = false;
		PreviewRejectReason = EGuLiBuildingPlacementRejectReason::InvalidRequest;
		if (PreviewActor)
		{
			PreviewActor->SetActorHiddenInGame(true);
		}
		return;
	}

	const FGuLiBuildingDefinition* Definition = CachedCatalog
		? CachedCatalog->FindDefinition(SelectedBuildingType)
		: nullptr;
	if (!Definition || !Definition->IsUsable() || !EnsurePreviewForDefinition(*Definition))
	{
		bHasPreviewCandidate = false;
		PreviewRejectReason = EGuLiBuildingPlacementRejectReason::AssetUnavailable;
		if (PreviewActor) PreviewActor->SetActorHiddenInGame(true);
		return;
	}

	FHitResult GroundHit;
	float YawDegrees = 0.0f;
	if (!TraceLocalGround(GroundHit, YawDegrees))
	{
		bHasPreviewCandidate = false;
		PreviewRejectReason = EGuLiBuildingPlacementRejectReason::NoGround;
		PreviewActor->SetActorHiddenInGame(true);
		return;
	}

	const double Now = GetWorld()->GetTimeSeconds();
	const bool bChanged = !bHasPreviewCandidate || !PreviewGroundLocation.Equals(GroundHit.ImpactPoint, 0.01)
		|| PreviewSupportingActor.Get() != GroundHit.GetActor();
	PreviewGroundLocation = GroundHit.ImpactPoint;
	PreviewSupportingActor = GroundHit.GetActor();
	if (bForceValidation || bChanged || LastCandidateValidationTime < 0.0 || Now - LastCandidateValidationTime >= 0.1)
	{
		PreviewRejectReason = ValidateLocalCandidate(*Definition, GroundHit, PreviewYawDegrees);
		LastCandidateValidationTime = Now;
	}
	bHasPreviewCandidate = true;
	PreviewActor->SetActorHiddenInGame(false);
	PreviewActor->SetPlacementTransform(PreviewGroundLocation, PreviewYawDegrees);
	PreviewActor->SetPlacementValidity(
		PreviewRejectReason == EGuLiBuildingPlacementRejectReason::None);
}

void UGuLiBuildingPlacementComponent::DestroyPreview()
{
	if (PreviewActor)
	{
		PreviewActor->Destroy();
		PreviewActor = nullptr;
	}
}

bool UGuLiBuildingPlacementComponent::EnsurePreviewForDefinition(
	const FGuLiBuildingDefinition& Definition)
{
	if (IsValid(PreviewActor))
	{
		return true;
	}
	UWorld* World = GetWorld();
	APlayerController* PlayerController = GetOwningPlayerController();
	if (!World || !PlayerController || !CachedCatalog || CachedCatalog->PreviewVfxId <= 0)
	{
		return false;
	}
	FActorSpawnParameters Parameters;
	Parameters.Owner = PlayerController;
	Parameters.ObjectFlags |= RF_Transient;
	Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	PreviewActor = World->SpawnActor<AGuLiBuildingPlacementPreview>(
		AGuLiBuildingPlacementPreview::StaticClass(),
		FTransform::Identity,
		Parameters);
	if (!PreviewActor || !PreviewActor->Configure(Definition, CachedCatalog->PreviewVfxId))
	{
		DestroyPreview();
		return false;
	}
	return true;
}

bool UGuLiBuildingPlacementComponent::TraceLocalGround(
	FHitResult& OutHit,
	float& OutYawDegrees) const
{
	APlayerController* PlayerController = GetOwningPlayerController();
	UWorld* World = GetWorld();
	const AGuLiBattlePlayerState* PlayerState = PlayerController
		? PlayerController->GetPlayerState<AGuLiBattlePlayerState>()
		: nullptr;
	if (!PlayerController || !World || !PlayerState)
	{
		return false;
	}

	FVector RayOrigin;
	FVector RayDirection;
	FVector ViewLocation;
	FRotator ViewRotation;
	PlayerController->GetPlayerViewPoint(ViewLocation, ViewRotation);
	OutYawDegrees = PreviewYawDegrees;
	float TraceDistance = GuLiBuildingPlacement::CommanderTraceDistanceCentimeters;
	// Both ground players and commanders place at the cursor. Range is measured from
	// the ground Pawn below, not from its elevated camera along the view ray.
	if (!PlayerController->DeprojectMousePositionToWorld(RayOrigin, RayDirection))
	{
		return false;
	}

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(GuLiBuildingLocalGround), false);
	QueryParams.AddIgnoredActor(PlayerController);
	if (const APawn* Pawn = PlayerController->GetPawn())
	{
		QueryParams.AddIgnoredActor(Pawn);
	}
	if (PreviewActor)
	{
		QueryParams.AddIgnoredActor(PreviewActor);
	}
	// Existing buildings are validated by the clearance overlap below. Ignoring
	// them here lets the trace reach the terrain and return Blocked instead of the
	// misleading NoGround when a player aims at an occupied footprint.
	GuLiBuildingPlacement::IgnoreBuildings(*World, QueryParams);
	// Dynamic blockers are handled by the footprint overlap. Find the ground beneath
	// vehicles/props so their footprint is red instead of losing the preview entirely.
	if (!World->LineTraceSingleByObjectType(
			OutHit,
			RayOrigin,
			RayOrigin + RayDirection.GetSafeNormal() * TraceDistance,
			FCollisionObjectQueryParams(ECC_WorldStatic),
			QueryParams) || !IsLegalGroundHit(OutHit)) return false;
	return TraceServerGround(GuLiBuildingPlacementPolicy::SnapGroundLocation(OutHit.ImpactPoint), OutHit);
}

bool UGuLiBuildingPlacementComponent::TraceServerGround(
	const FVector& DesiredLocation,
	FHitResult& OutHit) const
{
	UWorld* World = GetWorld();
	APlayerController* PlayerController = GetOwningPlayerController();
	if (!World || !PlayerController || DesiredLocation.ContainsNaN())
	{
		return false;
	}

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(GuLiBuildingServerGround), false);
	QueryParams.AddIgnoredActor(PlayerController);
	if (const APawn* Pawn = PlayerController->GetPawn())
	{
		QueryParams.AddIgnoredActor(Pawn);
	}
	GuLiBuildingPlacement::IgnoreBuildings(*World, QueryParams);
	const FVector Snapped = GuLiBuildingPlacementPolicy::SnapGroundLocation(DesiredLocation);
	const FVector Start = Snapped + FVector::UpVector
		* GuLiBuildingPlacement::ServerGroundTraceUpCentimeters;
	const FVector End = Snapped - FVector::UpVector
		* GuLiBuildingPlacement::ServerGroundTraceDownCentimeters;
	return World->LineTraceSingleByObjectType(OutHit, Start, End,
		FCollisionObjectQueryParams(ECC_WorldStatic), QueryParams)
		&& IsLegalGroundHit(OutHit);
}

bool UGuLiBuildingPlacementComponent::IsLegalGroundHit(const FHitResult& Hit) const
{
	const UPrimitiveComponent* HitComponent = Hit.GetComponent();
	return Hit.bBlockingHit && HitComponent
		&& HitComponent->GetCollisionObjectType() == ECC_WorldStatic
		&& Hit.GetActor() && !GuLiBuildingPlacement::IsBuildingActor(Hit.GetActor());
}

bool UGuLiBuildingPlacementComponent::IsLineOfSightClear(
	const FVector& GroundLocation,
	const AActor* SupportingActor) const
{
	const APlayerController* PlayerController = GetOwningPlayerController();
	const APawn* Pawn = PlayerController ? PlayerController->GetPawn() : nullptr;
	UWorld* World = GetWorld();
	if (!PlayerController || !Pawn || !World)
	{
		return false;
	}
	const FVector Start = Pawn->GetPawnViewLocation();
	const FVector End = GroundLocation + FVector(0.0f, 0.0f, 50.0f);
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(GuLiBuildingLineOfSight), false);
	QueryParams.AddIgnoredActor(PlayerController);
	QueryParams.AddIgnoredActor(Pawn);
	if (PreviewActor)
	{
		QueryParams.AddIgnoredActor(PreviewActor);
	}
	FHitResult Hit;
	if (!World->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, QueryParams))
	{
		return true;
	}
	return Hit.GetActor() == SupportingActor
		|| FVector::DistSquared(Hit.ImpactPoint, End)
			<= FMath::Square(GuLiBuildingPlacement::GroundContactToleranceCentimeters);
}

bool UGuLiBuildingPlacementComponent::IsPlacementAreaClear(
	const FGuLiBuildingDefinition& Definition,
	const FVector& GroundLocation,
	const float YawDegrees,
	const AActor* SupportingActor) const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}
	const FVector QueryExtent(
		Definition.CollisionExtent.X + GuLiBuildingPlacementPolicy::PlacementClearanceCentimeters,
		Definition.CollisionExtent.Y + GuLiBuildingPlacementPolicy::PlacementClearanceCentimeters,
		FMath::Max(1.0f, Definition.CollisionExtent.Z
			- GuLiBuildingPlacement::CollisionGroundEpsilonCentimeters));
	const FVector QueryCenter = GroundLocation + FVector(
		0.0f,
		0.0f,
		Definition.CollisionExtent.Z + GuLiBuildingPlacement::CollisionGroundEpsilonCentimeters);
	FCollisionObjectQueryParams ObjectQuery;
	ObjectQuery.AddObjectTypesToQuery(ECC_WorldStatic);
	ObjectQuery.AddObjectTypesToQuery(ECC_WorldDynamic);
	ObjectQuery.AddObjectTypesToQuery(ECC_Pawn);
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(GuLiBuildingPlacementOverlap), false);
	if (SupportingActor)
	{
		QueryParams.AddIgnoredActor(SupportingActor);
	}
	if (PreviewActor)
	{
		QueryParams.AddIgnoredActor(PreviewActor);
	}
	TArray<FOverlapResult> Overlaps;
	if (World->OverlapMultiByObjectType(
		Overlaps,
		QueryCenter,
		FQuat(FRotator(0.0f, YawDegrees, 0.0f)),
		ObjectQuery,
		FCollisionShape::MakeBox(QueryExtent),
		QueryParams))
	{
		return false;
	}
	return !DoesPlacementOverlapCommanderSoldier(QueryCenter, QueryExtent, YawDegrees);
}

bool UGuLiBuildingPlacementComponent::DoesPlacementOverlapCommanderSoldier(
	const FVector& QueryCenter,
	const FVector& QueryExtent,
	const float YawDegrees) const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	SoldierLocationScratch.Reset();
	if (UGuLiBattleAuthoritySubsystem* Authority =
		World->GetSubsystem<UGuLiBattleAuthoritySubsystem>())
	{
		Authority->BuildLivingSoldierLocationSnapshot(SoldierLocationScratch);
	}
	else
	{
		const AGuLiSoldierStateReplicator* Replicator = nullptr;
		const AGuLiCommanderPresentationActor* Presentation = nullptr;
		for (TActorIterator<AGuLiSoldierStateReplicator> It(World); It; ++It)
		{
			Replicator = *It;
			break;
		}
		for (TActorIterator<AGuLiCommanderPresentationActor> It(World); It; ++It)
		{
			Presentation = *It;
			break;
		}
		if (Replicator && Presentation)
		{
			SoldierLocationScratch.Reserve(Replicator->GetItems().Num());
			for (const FGuLiSoldierStateItem& SoldierState : Replicator->GetItems())
			{
				FTransform PresentedTransform;
				if (SoldierState.IsAlive()
					&& Presentation->TryGetPresentedSoldierTransform(
						SoldierState.SoldierId, PresentedTransform)
					&& !PresentedTransform.ContainsNaN())
				{
					SoldierLocationScratch.Add(PresentedTransform.GetLocation());
				}
			}
		}
	}

	for (const FVector& SoldierLocation : SoldierLocationScratch)
	{
		if (GuLiBuildingPlacement::DoesDiscOverlapOrientedBox(
			SoldierLocation,
			GuLiCommanderNavigationPolicy::RequiredAgentRadiusCentimeters,
			QueryCenter,
			QueryExtent,
			YawDegrees))
		{
			return true;
		}
	}
	return false;
}

EGuLiBuildingPlacementRejectReason UGuLiBuildingPlacementComponent::ValidateSpatialCandidate(
	const FGuLiBuildingDefinition& Definition,
	const FHitResult& GroundHit,
	const float YawDegrees) const
{
	if (!GuLiBuildingPlacementPolicy::IsSlopeAllowed(GroundHit.ImpactNormal))
	{
		return EGuLiBuildingPlacementRejectReason::SlopeTooSteep;
	}
	const APlayerController* PlayerController = GetOwningPlayerController();
	const AGuLiBattlePlayerState* PlayerState = PlayerController
		? PlayerController->GetPlayerState<AGuLiBattlePlayerState>()
		: nullptr;
	if (PlayerState && PlayerState->GetBattleRole() == EGuLiCommanderRole::Ground)
	{
		const APawn* Pawn = PlayerController->GetPawn();
		if (!Pawn || !GuLiBuildingPlacementPolicy::IsGroundPlacementInRange(
			Pawn->GetActorLocation(), GroundHit.ImpactPoint))
		{
			return EGuLiBuildingPlacementRejectReason::OutOfRange;
		}
		if (!IsLineOfSightClear(GroundHit.ImpactPoint, GroundHit.GetActor()))
		{
			return EGuLiBuildingPlacementRejectReason::NoLineOfSight;
		}
	}
	if (!IsPlacementAreaClear(Definition, GroundHit.ImpactPoint, YawDegrees, GroundHit.GetActor()))
	{
		return EGuLiBuildingPlacementRejectReason::Blocked;
	}
	UClass* Presentation = Definition.Category == EGuLiBuildingCategory::Factory
		? GuLiBuildingVisuals::ResolveFactoryPresentation(this) : nullptr;
	if (Definition.Category == EGuLiBuildingCategory::Factory && !Presentation)
		return EGuLiBuildingPlacementRejectReason::AssetUnavailable;
	FString GroundReason;
	if (!GuLiBuildings::ValidateGroundPlacement(*GetWorld(), Definition,
		FTransform(FRotator(0, YawDegrees, 0), GroundHit.ImpactPoint), Presentation, PreviewActor, GroundReason))
		return EGuLiBuildingPlacementRejectReason::NoGround;
	return EGuLiBuildingPlacementRejectReason::None;
}

EGuLiBuildingPlacementRejectReason UGuLiBuildingPlacementComponent::ValidateLocalCandidate(
	const FGuLiBuildingDefinition& Definition, const FHitResult& GroundHit, float YawDegrees) const
{
	const auto Spatial = ValidateSpatialCandidate(Definition, GroundHit, YawDegrees);
	if (Spatial != EGuLiBuildingPlacementRejectReason::None) return Spatial;
	const auto* State = GetOwningPlayerController()->GetPlayerState<AGuLiBattlePlayerState>();
	if (!State) return EGuLiBuildingPlacementRejectReason::NotReady;
	int32 BuilderCount = 0, WorldCount = 0;
	CountPlacedBuildings(State->GetPlayerGuid(), BuilderCount, WorldCount);
	const auto Capacity = GuLiBuildingPlacementPolicy::ValidateCapacity(BuilderCount, WorldCount);
	if (Capacity != EGuLiBuildingPlacementRejectReason::None) return Capacity;
	const auto* Resources = GetWorld()->GetSubsystem<UGuLiResourceWorldSubsystem>();
	if (Resources && Resources->IsResourceWorldActive() && !State->GetResourceInventory().CanAfford(Definition.Cost))
		return EGuLiBuildingPlacementRejectReason::InsufficientResources;
	return EGuLiBuildingPlacementRejectReason::None;
}

EGuLiBuildingPlacementRejectReason UGuLiBuildingPlacementComponent::ValidateServerRequest(
	const FGuLiBuildingPlacementRequest& Request,
	FTransform& OutSpawnTransform,
	const FGuLiBuildingDefinition*& OutDefinition, AActor*& OutSupportingActor) const
{
	OutDefinition = nullptr;
	const APlayerController* PlayerController = GetOwningPlayerController();
	const AGuLiBattlePlayerController* BattleController = Cast<AGuLiBattlePlayerController>(PlayerController);
	const AGuLiBattlePlayerState* PlayerState = PlayerController
		? PlayerController->GetPlayerState<AGuLiBattlePlayerState>()
		: nullptr;
	const AGuLiBattleGameState* GameState = GetWorld()
		? GetWorld()->GetGameState<AGuLiBattleGameState>()
		: nullptr;
	if (!PlayerController || !PlayerController->HasAuthority() || !PlayerState || !GameState
		|| !PlayerState->IsBattleReady() || GameState->GetMatchEpoch() == 0u)
	{
		return EGuLiBuildingPlacementRejectReason::NotReady;
	}
	if (!GuLiBuildingPlacementPolicy::IsBuildingRole(PlayerState->GetBattleRole())
		|| PlayerState->GetTeam() == EGuLiTeam::Unassigned || !PlayerState->GetPlayerGuid().IsValid())
	{
		return EGuLiBuildingPlacementRejectReason::UnauthorizedRole;
	}

	bool bConnectionReady = false;
	if (BattleController)
	{
		const UGuLiPlayerNetSyncComponent* ConnectionSync = BattleController->GetPlayerNetSyncComponent();
		bConnectionReady = ConnectionSync && ConnectionSync->IsConnectionReady();
	}
#if WITH_DEV_AUTOMATION_TESTS
	bConnectionReady |= bTestBypassConnectionGate;
#endif
	if (!bConnectionReady)
	{
		return EGuLiBuildingPlacementRejectReason::NotReady;
	}
	if (!Request.IsWellFormed())
	{
		return EGuLiBuildingPlacementRejectReason::InvalidRequest;
	}
	if (!CachedCatalog || !CachedCatalog->IsUsable())
	{
		return EGuLiBuildingPlacementRejectReason::AssetUnavailable;
	}
	OutDefinition = CachedCatalog->FindDefinition(Request.Type);
	if (!OutDefinition || !OutDefinition->IsUsable())
	{
		return EGuLiBuildingPlacementRejectReason::AssetUnavailable;
	}

	FHitResult GroundHit;
	if (!TraceServerGround(FVector(Request.DesiredGroundLocation), GroundHit))
	{
		return EGuLiBuildingPlacementRejectReason::NoGround;
	}
	const float YawDegrees = GuLiBuildingPlacementPolicy::SnapYaw(Request.GetYawDegrees());
	const auto Spatial = ValidateSpatialCandidate(*OutDefinition, GroundHit, YawDegrees);
	if (Spatial != EGuLiBuildingPlacementRejectReason::None) return Spatial;

	int32 BuilderCount = 0;
	int32 WorldCount = 0;
	CountPlacedBuildings(PlayerState->GetPlayerGuid(), BuilderCount, WorldCount);
	const EGuLiBuildingPlacementRejectReason CapacityResult =
		GuLiBuildingPlacementPolicy::ValidateCapacity(BuilderCount, WorldCount);
	if (CapacityResult != EGuLiBuildingPlacementRejectReason::None)
	{
		return CapacityResult;
	}

	OutSupportingActor = GroundHit.GetActor();
	OutSpawnTransform = FTransform(
		FRotator(0.0f, YawDegrees, 0.0f),
		GroundHit.ImpactPoint + FVector(0.0f, 0.0f, OutDefinition->CollisionExtent.Z),
		FVector::OneVector);
	return EGuLiBuildingPlacementRejectReason::None;
}

void UGuLiBuildingPlacementComponent::CountPlacedBuildings(
	const FGuid& BuilderGuid,
	int32& OutBuilderCount,
	int32& OutWorldCount) const
{
	OutBuilderCount = 0;
	OutWorldCount = 0;
	if (!GetWorld())
	{
		return;
	}
	TArray<UGuLiBuildingLifecycleComponent*> Buildings;
	GetWorld()->GetSubsystem<UGuLiBuildingRegistrySubsystem>()->Query(Buildings);
	for (const auto* Building : Buildings)
	{
		if (!Building->CountsForManualLimit()) continue;
		++OutWorldCount;
		if (Building->GetState().BuilderGuid == BuilderGuid)
		{
			++OutBuilderCount;
		}
	}
}

bool UGuLiBuildingPlacementComponent::ConsumeServerRequestBudget()
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}
	const double NowSeconds = World->GetRealTimeSeconds();
	ServerRequestTimes.RemoveAll(
		[NowSeconds](const double TimeSeconds)
		{
			return NowSeconds - TimeSeconds >= 1.0;
		});
	if (ServerRequestTimes.Num() >= GuLiBuildingPlacementPolicy::MaximumRequestsPerSecond)
	{
		return false;
	}
	ServerRequestTimes.Add(NowSeconds);
	return true;
}

FGuLiBuildingPlacementResult UGuLiBuildingPlacementComponent::ProcessServerRequest(
	const FGuLiBuildingPlacementRequest& Request,
	const bool bSendResult)
{
	FGuLiBuildingPlacementResult Result;
	Result.ClientRequestId = Request.ClientRequestId;
	Result.RejectReason = EGuLiBuildingPlacementRejectReason::InvalidRequest;

	if (Request.ClientRequestId == 0u)
	{
		if (bSendResult)
		{
			SendPlacementResult(Result);
		}
		return Result;
	}
	if (bHasServerRequestHistory
		&& !GuLiBuildingPlacementPolicy::IsNewerRequestId(
			Request.ClientRequestId, LastServerRequest.ClientRequestId))
	{
		if (Request.ClientRequestId == LastServerRequest.ClientRequestId
			&& GuLiBuildingPlacementPolicy::AreSameRequest(Request, LastServerRequest))
		{
			Result = LastServerResult;
		}
		else
		{
			Result.RejectReason = Request.ClientRequestId == LastServerRequest.ClientRequestId
				? EGuLiBuildingPlacementRejectReason::InvalidRequest
				: EGuLiBuildingPlacementRejectReason::Duplicate;
		}
		if (bSendResult)
		{
			SendPlacementResult(Result);
		}
		return Result;
	}

	bHasServerRequestHistory = true;
	LastServerRequest = Request;
	if (!ConsumeServerRequestBudget())
	{
		Result.RejectReason = EGuLiBuildingPlacementRejectReason::RateLimited;
		LastServerResult = Result;
		if (bSendResult)
		{
			SendPlacementResult(Result);
		}
		return Result;
	}

	FTransform SpawnTransform;
	const FGuLiBuildingDefinition* Definition = nullptr;
	AActor* SupportingActor = nullptr;
	Result.RejectReason = ValidateServerRequest(Request, SpawnTransform, Definition, SupportingActor);
	if (Result.RejectReason == EGuLiBuildingPlacementRejectReason::None)
	{
		APlayerController* PlayerController = GetOwningPlayerController();
		AGuLiBattlePlayerState* PlayerState = PlayerController
			? PlayerController->GetPlayerState<AGuLiBattlePlayerState>()
			: nullptr;
		UWorld* World = GetWorld();
		check(World);
		UGuLiTeamEconomySubsystem* EconomySubsystem =
			World->GetSubsystem<UGuLiTeamEconomySubsystem>();
		check(EconomySubsystem);
		IGuLiTeamEconomy& Economy = *EconomySubsystem;
		FGuLiEconomyReservation EconomyReservation;
		const bool bUseEconomy = Economy.IsMatchActive();
		if (bUseEconomy && !Economy.AreTransactionsOpen())
		{
			Result.RejectReason = EGuLiBuildingPlacementRejectReason::NotReady;
		}
		else if (bUseEconomy && (!PlayerState || !Economy.Reserve(
			PlayerState->GetTeam(),
			PlayerState->GetPlayerGuid(),
			Request.ClientRequestId,
			GuLiBuildingPlacementPolicy::GetEconomyCost(Request.Type),
			EconomyReservation)))
		{
			Result.RejectReason = EGuLiBuildingPlacementRejectReason::InsufficientResources;
		}
		if (Result.RejectReason != EGuLiBuildingPlacementRejectReason::None)
		{
			LastServerResult = Result;
			if (bSendResult) SendPlacementResult(Result);
			return Result;
		}
		FTransform GroundTransform = SpawnTransform;
		GroundTransform.AddToTranslation(FVector(0,0,-Definition->CollisionExtent.Z));
		AActor* Building = GuLiBuildings::Spawn(*World, Definition->DefinitionId, PlayerState->GetTeam(), GroundTransform, *SupportingActor,
			World->GetSubsystem<UGuLiResourceWorldSubsystem>()->FindTerritoryIndex(GroundTransform.GetLocation()),
			EGuLiBuildingOrigin::Manual, false, PlayerState->GetPlayerGuid());
		if (!Building)
		{
			Result.RejectReason = EGuLiBuildingPlacementRejectReason::SpawnFailed;
			if (bUseEconomy) Economy.Refund(EconomyReservation);
		}
		else
		{
			Building->SetOwner(PlayerController);
			if (bUseEconomy) Economy.Commit(EconomyReservation);
		}
	}

	LastServerResult = Result;
#if WITH_EDITOR
	if (const UWorld* World = GetWorld(); World && World->WorldType == EWorldType::PIE)
	{
		UE_LOG(LogTemp, Display,
			TEXT("[GULI_BUILDING_PIE] result owner=%s net_mode=%d request=%u type=%d reason=%d"),
			GetOwner() ? *GetOwner()->GetName() : TEXT("None"),
			static_cast<int32>(World->GetNetMode()),
			Request.ClientRequestId,
			static_cast<int32>(Request.Type),
			static_cast<int32>(Result.RejectReason));
	}
#endif
	if (bSendResult)
	{
		SendPlacementResult(Result);
	}
	return Result;
}

void UGuLiBuildingPlacementComponent::ServerRequestPlacement_Implementation(
	const FGuLiBuildingPlacementRequest& Request)
{
	ProcessServerRequest(Request, true);
}

void UGuLiBuildingPlacementComponent::SendPlacementResult(
	const FGuLiBuildingPlacementResult& Result)
{
	ClientReceivePlacementResult(Result);
}

void UGuLiBuildingPlacementComponent::ClientReceivePlacementResult_Implementation(
	const FGuLiBuildingPlacementResult& Result)
{
	if (Result.WasAccepted())
	{
		EmitFeedback(
			FText::Format(
				GuLiGameText::Get(TEXT("UI.BuildingPlacementComponent.135")),
				GetSelectedDisplayName()),
			EGuLiBuildingFeedbackTone::Success);
	}
	else
	{
		EmitRejectedFeedback(Result.RejectReason);
	}
}

void UGuLiBuildingPlacementComponent::EmitSelectionFeedback()
{
	EmitFeedback(
		FText::Format(
			GuLiGameText::Get(TEXT("UI.BuildingPlacementComponent.136")),
			GetSelectedDisplayName()),
		EGuLiBuildingFeedbackTone::Info);
}

void UGuLiBuildingPlacementComponent::EmitRejectedFeedback(
	const EGuLiBuildingPlacementRejectReason Reason)
{
	EmitFeedback(
		FText::Format(
			GuLiGameText::Get(TEXT("UI.BuildingPlacementComponent.137")),
			GetGuLiBuildingPlacementReasonText(Reason)),
		EGuLiBuildingFeedbackTone::Error);
}

void UGuLiBuildingPlacementComponent::EmitFeedback(
	const FText& Message,
	const EGuLiBuildingFeedbackTone Tone)
{
	OnBuildingFeedback.Broadcast(Message, Tone);
}

FText UGuLiBuildingPlacementComponent::GetSelectedDisplayName() const
{
	const FGuLiBuildingDefinition* Definition = CachedCatalog
		? CachedCatalog->FindDefinition(SelectedBuildingType)
		: nullptr;
	return Definition && !Definition->DisplayName.IsEmpty()
		? Definition->DisplayName
		: GetGuLiBuildingFallbackDisplayName(SelectedBuildingType);
}

#if WITH_EDITOR
bool UGuLiBuildingPlacementComponent::EditorQASubmitPlacement(
	const EGuLiBuildingType Type,
	const FVector& GroundLocation,
	const float YawDegrees)
{
	APlayerController* PlayerController = GetOwningPlayerController();
	if (!PlayerController || !PlayerController->IsLocalController()
		|| PlayerController->GetNetMode() != NM_Client
		|| !GuLiBuildingPlacementPolicy::IsKnownBuildingType(Type)
		|| GroundLocation.ContainsNaN() || !FMath::IsFinite(YawDegrees))
	{
		return false;
	}

	FGuLiBuildingPlacementRequest Request;
	Request.ClientRequestId = GuLiBuildingPlacement::AllocateNonZeroRequestId(NextClientRequestId);
	Request.Type = Type;
	Request.DesiredGroundLocation = GroundLocation;
	Request.CompressedYaw = FRotator::CompressAxisToShort(YawDegrees);
	ServerRequestPlacement(Request);
	return true;
}

namespace GuLiBuildingPIEQA
{
	bool FindClearGround(
		UWorld& World,
		APlayerController& PlayerController,
		const FGuLiBuildingDefinition& Definition,
		FVector& OutGroundLocation)
	{
		const APawn* Pawn = PlayerController.GetPawn();
		const FVector Anchor = Pawn ? Pawn->GetActorLocation() : FVector::ZeroVector;
		TArray<FVector2D> Offsets;
		for (float Radius = 600.0f; Radius <= 3600.0f; Radius += 600.0f)
		{
			for (int32 DirectionIndex = 0; DirectionIndex < 8; ++DirectionIndex)
			{
				const float Radians = UE_TWO_PI * static_cast<float>(DirectionIndex) / 8.0f;
				Offsets.Emplace(FMath::Cos(Radians) * Radius, FMath::Sin(Radians) * Radius);
			}
		}

		for (const FVector2D& Offset : Offsets)
		{
			const FVector TraceXY(Anchor.X + Offset.X, Anchor.Y + Offset.Y, Anchor.Z);
			FCollisionQueryParams TraceParams(SCENE_QUERY_STAT(GuLiBuildingPIEQAGround), false);
			TraceParams.AddIgnoredActor(&PlayerController);
			if (Pawn)
			{
				TraceParams.AddIgnoredActor(Pawn);
			}
			FHitResult GroundHit;
			if (!World.LineTraceSingleByChannel(
					GroundHit,
					TraceXY + FVector::UpVector * 50000.0f,
					TraceXY - FVector::UpVector * 50000.0f,
					ECC_Visibility,
					TraceParams)
				|| !GroundHit.GetComponent()
				|| GroundHit.GetComponent()->GetCollisionObjectType() != ECC_WorldStatic
				|| !GroundHit.GetActor()
				|| GroundHit.GetActor()->IsA<AGuLiPlacedBuilding>()
				|| !GuLiBuildingPlacementPolicy::IsSlopeAllowed(GroundHit.ImpactNormal))
			{
				continue;
			}

			const FVector QueryExtent(
				Definition.CollisionExtent.X + GuLiBuildingPlacementPolicy::PlacementClearanceCentimeters,
				Definition.CollisionExtent.Y + GuLiBuildingPlacementPolicy::PlacementClearanceCentimeters,
				FMath::Max(1.0f, Definition.CollisionExtent.Z - 2.0f));
			const FVector QueryCenter = GroundHit.ImpactPoint
				+ FVector(0.0f, 0.0f, Definition.CollisionExtent.Z + 2.0f);
			FCollisionObjectQueryParams ObjectQuery;
			ObjectQuery.AddObjectTypesToQuery(ECC_WorldStatic);
			ObjectQuery.AddObjectTypesToQuery(ECC_WorldDynamic);
			ObjectQuery.AddObjectTypesToQuery(ECC_Pawn);
			FCollisionQueryParams OverlapParams(SCENE_QUERY_STAT(GuLiBuildingPIEQAOverlap), false);
			OverlapParams.AddIgnoredActor(GroundHit.GetActor());
			TArray<FOverlapResult> Overlaps;
			if (!World.OverlapMultiByObjectType(
					Overlaps,
					QueryCenter,
					FQuat(FRotator(0.0f, PlayerController.GetControlRotation().Yaw, 0.0f)),
					ObjectQuery,
					FCollisionShape::MakeBox(QueryExtent),
					OverlapParams))
			{
				OutGroundLocation = GroundHit.ImpactPoint;
				return true;
			}
		}
		return false;
	}

	void SubmitTwoClientPlacement(const TArray<FString>& Args)
	{
		int32 TypeValue = static_cast<int32>(EGuLiBuildingType::Outpost);
		if (Args.Num() > 1 || (Args.Num() == 1 && !LexTryParseString(TypeValue, *Args[0]))
			|| TypeValue < static_cast<int32>(EGuLiBuildingType::MissileTurret)
			|| TypeValue > static_cast<int32>(EGuLiBuildingType::Outpost))
		{
			UE_LOG(LogTemp, Error, TEXT("Usage: gs.Building.QA.Place [1|2|3]"));
			return;
		}

		if (!GEngine)
		{
			UE_LOG(LogTemp, Error, TEXT("Building two-client QA requires an engine."));
			return;
		}
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			UWorld* World = Context.World();
			if (!World || World->WorldType != EWorldType::PIE || World->GetNetMode() != NM_Client)
			{
				continue;
			}
			for (TActorIterator<AGuLiCommanderPlayerController> It(World); It; ++It)
			{
				AGuLiCommanderPlayerController* Controller = *It;
				const AGuLiBattlePlayerState* PlayerState = Controller
					? Controller->GetPlayerState<AGuLiBattlePlayerState>() : nullptr;
				UGuLiBuildingPlacementComponent* Placement = Controller
					? Controller->GetBuildingPlacementComponent() : nullptr;
				UGuLiBuildingCatalog* Catalog = Placement ? Placement->GetBuildingCatalog() : nullptr;
				const EGuLiBuildingType Type = static_cast<EGuLiBuildingType>(TypeValue);
				const FGuLiBuildingDefinition* Definition = Catalog ? Catalog->FindDefinition(Type) : nullptr;
				if (!Controller || !Controller->IsLocalController() || !PlayerState
					|| !PlayerState->IsBattleReady()
					|| !GuLiBuildingPlacementPolicy::IsBuildingRole(PlayerState->GetBattleRole())
					|| !Placement || !Definition || !Definition->IsUsable())
				{
					continue;
				}

				FVector GroundLocation;
				if (!FindClearGround(*World, *Controller, *Definition, GroundLocation))
				{
					continue;
				}
				const TWeakObjectPtr<UGuLiBuildingPlacementComponent> WeakPlacement(Placement);
				const float YawDegrees = Controller->GetControlRotation().Yaw;
				const FString ControllerName = Controller->GetName();
				const int32 RoleValue = static_cast<int32>(PlayerState->GetBattleRole());
				FTSTicker::GetCoreTicker().AddTicker(
					FTickerDelegate::CreateLambda(
						[WeakPlacement, Type, GroundLocation, YawDegrees,
							ControllerName, RoleValue, TypeValue](float)
						{
							UGuLiBuildingPlacementComponent* DeferredPlacement = WeakPlacement.Get();
							const bool bSubmitted = DeferredPlacement
								&& DeferredPlacement->EditorQASubmitPlacement(
									Type, GroundLocation, YawDegrees);
							if (bSubmitted)
							{
								UE_LOG(LogTemp, Display,
									TEXT("[GULI_BUILDING_PIE] deferred_submit=true client=%s role=%d type=%d ground=%s"),
									*ControllerName, RoleValue, TypeValue, *GroundLocation.ToString());
							}
							else
							{
								UE_LOG(LogTemp, Error,
									TEXT("[GULI_BUILDING_PIE] deferred_submit=false client=%s role=%d type=%d ground=%s"),
									*ControllerName, RoleValue, TypeValue, *GroundLocation.ToString());
							}
							return false;
						}),
					0.0f);
				UE_LOG(LogTemp, Display,
					TEXT("[GULI_BUILDING_PIE] queued client=%s role=%d type=%d ground=%s"),
					*ControllerName, RoleValue, TypeValue, *GroundLocation.ToString());
				return;
			}
		}
		UE_LOG(LogTemp, Error,
			TEXT("[GULI_BUILDING_PIE] no ready build-capable client with a clear ground candidate."));
	}

	FAutoConsoleCommand SubmitCommand(
		TEXT("gs.Building.QA.Place"),
		TEXT("Two-client PIE only: submit one ordinary building RPC from a ready client; optional type 1|2|3."),
		FConsoleCommandWithArgsDelegate::CreateStatic(&SubmitTwoClientPlacement));
}
#endif // WITH_EDITOR

#if WITH_DEV_AUTOMATION_TESTS
void UGuLiBuildingPlacementComponent::TestOnly_SetCatalog(UGuLiBuildingCatalog* Catalog)
{
	CachedCatalog = Catalog;
}

FGuLiBuildingPlacementResult UGuLiBuildingPlacementComponent::TestOnly_ProcessServerRequest(
	const FGuLiBuildingPlacementRequest& Request)
{
	return ProcessServerRequest(Request, false);
}

void UGuLiBuildingPlacementComponent::TestOnly_ResetServerRequestState()
{
	bHasServerRequestHistory = false;
	LastServerRequest = FGuLiBuildingPlacementRequest{};
	LastServerResult = FGuLiBuildingPlacementResult{};
	ServerRequestTimes.Reset();
}
#endif
