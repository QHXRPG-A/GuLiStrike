// Copyright Epic Games, Inc. All Rights Reserved.

#include "Gameplay/Building/GuLiBuildingPlacementComponent.h"
#include "Gameplay/Building/GuLiConstructedUnitComponent.h"
#include "Commander/Presentation/GuLiCommanderCameraGeometry.h"
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
#include "Network/GuLiHybridActorComponent.h"
#include "Network/GuLiNetworkSettings.h"

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
	constexpr float PlacementGroundTraceUpCentimeters = 5000.0f;
	constexpr float PlacementGroundTraceDownCentimeters = 5000.0f;
	constexpr float GroundContactToleranceCentimeters = 20.0f;
	constexpr float CollisionGroundEpsilonCentimeters = 0.4f;


	bool AreaQueryClear(UWorld& InWorld,const FGuLiBuildingDefinition& Definition,const FVector& GroundLocation,
		float YawDegrees,const AActor* SupportingActor,const AActor* Preview)
	{
	UWorld* World = &InWorld;
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
	if (Preview)
	{
		QueryParams.AddIgnoredActor(Preview);
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
	return true;
	}
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
	SetIsReplicatedByDefault(false);
}

void UGuLiBuildingPlacementComponent::BeginPlay()
{
	Super::BeginPlay();
	ResolveCatalog();
	if (auto* Hybrid=GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>())
	{
		Hybrid->OnTransactionCompleted.AddDynamic(this,&ThisClass::HandleHybridTransaction);
		Hybrid->OnCommandCompleted.AddUObject(this,&ThisClass::HandleHybridPlacementCommand);
	}
}

void UGuLiBuildingPlacementComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ExitBuildMode(false);
	ServerRequestTimes.Reset();
	if (auto* Hybrid=GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>())
	{
		Hybrid->OnTransactionCompleted.RemoveDynamic(this,&ThisClass::HandleHybridTransaction);
		Hybrid->OnCommandCompleted.RemoveAll(this);
	}
	HybridPlacements.Reset(); HybridClientRequests.Reset(); HybridCommandRequests.Reset(); HybridRefundEvents.Reset();
	Super::EndPlay(EndPlayReason);
}

APlayerController* UGuLiBuildingPlacementComponent::GetOwningPlayerController() const
{
	if (const auto* Pawn=Cast<APawn>(GetOwner())) return Cast<APlayerController>(Pawn->GetController());
	return Cast<APlayerController>(GetOwner());
}
APawn* UGuLiBuildingPlacementComponent::GetPlacementPawn() const
{
	if (auto* Pawn=Cast<APawn>(GetOwner())) return Pawn;
	const auto* PC=GetOwningPlayerController(); return PC?PC->GetPawn():nullptr;
}
AGuLiBattlePlayerState* UGuLiBuildingPlacementComponent::GetPlacementPlayer() const
{
	if (const auto* Pawn=Cast<APawn>(GetOwner())) return Pawn->GetPlayerState<AGuLiBattlePlayerState>();
	const auto* PC=GetOwningPlayerController(); return PC?PC->GetPlayerState<AGuLiBattlePlayerState>():nullptr;
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
	const AGuLiBattleGameState* GameState = GetWorld()
		? GetWorld()->GetGameState<AGuLiBattleGameState>()
		: nullptr;
	const auto* Hybrid=GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>();
	const bool bConnectionReady=Hybrid && Hybrid->IsWorldReady() && !Hybrid->IsBattlePaused();
	if (!PlayerState->IsBattleReady() || !bConnectionReady
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
	SelectBuildingType(Decision.SelectedType);
	return true;
}

bool UGuLiBuildingPlacementComponent::SelectBuildingType(EGuLiBuildingType Type)
{
	auto* Catalog = ResolveCatalog();
	const auto* Definition=Catalog?Catalog->FindDefinition(Type):nullptr;
	return Definition && SelectBuildingDefinition(Definition->DefinitionId);
}
int32 UGuLiBuildingPlacementComponent::GetSelectedBuildingDefinitionId() const
{
	if (SelectedBuildingDefinitionId>0) return SelectedBuildingDefinitionId;
	const auto* Definition=CachedCatalog?CachedCatalog->FindDefinition(SelectedBuildingType):nullptr;
	return Definition?Definition->DefinitionId:0;
}
bool UGuLiBuildingPlacementComponent::SelectBuildingDefinition(int32 DefinitionId)
{
	auto* Catalog=ResolveCatalog();const auto* Definition=Catalog?Catalog->FindById(DefinitionId):nullptr;
	if (!Definition || !Definition->IsUsable()) return false;
	if (!bBuildModeActive && !EnterBuildMode()) return false;
	SelectedBuildingType=Definition->Type;SelectedBuildingDefinitionId=DefinitionId;DestroyPreview();bHasPreviewCandidate=false;
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
	Request.DefinitionId=GetSelectedBuildingDefinitionId();
	Request.DesiredGroundLocation = PreviewGroundLocation;
	Request.CompressedYaw = FRotator::CompressAxisToShort(PreviewYawDegrees);
	RouteHybridPlacement(Request);
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
		? CachedCatalog->FindById(GetSelectedBuildingDefinitionId())
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
	// Both ground players and commanders place at the cursor. Range is measured from
	// the ground Pawn below, not from its elevated camera along the view ray.
	if (!PlayerController->DeprojectMousePositionToWorld(RayOrigin, RayDirection))
	{
		return false;
	}
	const double TraceDistance = GuLiCommanderCameraGeometry::SelectionRayLength(RayOrigin, World);

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
	return TracePlacementGround(GuLiBuildingPlacementPolicy::SnapGroundLocation(OutHit.ImpactPoint), OutHit);
}

bool UGuLiBuildingPlacementComponent::TracePlacementGround(
	const FVector& DesiredLocation,
	FHitResult& OutHit) const
{
	UWorld* World = GetWorld();
	APlayerController* PlayerController = GetOwningPlayerController();
	if (!World || (!PlayerController && !GetPlacementPawn()) || DesiredLocation.ContainsNaN())
	{
		return false;
	}

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(GuLiBuildingServerGround), false);
	QueryParams.AddIgnoredActor(PlayerController);
	if (const APawn* Pawn = GetPlacementPawn())
	{
		QueryParams.AddIgnoredActor(Pawn);
	}
	GuLiBuildingPlacement::IgnoreBuildings(*World, QueryParams);
	const FVector Snapped = GuLiBuildingPlacementPolicy::SnapGroundLocation(DesiredLocation);
	const FVector Start = Snapped + FVector::UpVector
		* GuLiBuildingPlacement::PlacementGroundTraceUpCentimeters;
	const FVector End = Snapped - FVector::UpVector
		* GuLiBuildingPlacement::PlacementGroundTraceDownCentimeters;
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
	const APawn* Pawn = GetPlacementPawn();
	UWorld* World = GetWorld();
	if (!Pawn || !World)
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
    auto* World=GetWorld(); if (!World || !GuLiBuildingPlacement::AreaQueryClear(*World,Definition,GroundLocation,YawDegrees,SupportingActor,PreviewActor)) return false;
    const FVector Extent(Definition.CollisionExtent.X+GuLiBuildingPlacementPolicy::PlacementClearanceCentimeters,
        Definition.CollisionExtent.Y+GuLiBuildingPlacementPolicy::PlacementClearanceCentimeters,
        FMath::Max(1.f,Definition.CollisionExtent.Z-GuLiBuildingPlacement::CollisionGroundEpsilonCentimeters));
    const FVector Center=GroundLocation+FVector(0,0,Definition.CollisionExtent.Z+GuLiBuildingPlacement::CollisionGroundEpsilonCentimeters);
    return !DoesPlacementOverlapCommanderSoldier(Center,Extent,YawDegrees);
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
	const AGuLiBattlePlayerState* PlayerState = GetPlacementPlayer();
	if (PlayerState && PlayerState->GetBattleRole() == EGuLiCommanderRole::Ground)
	{
		const APawn* Pawn = GetPlacementPawn();
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

bool UGuLiBuildingPlacementComponent::ValidateAdmittedPlacement(UWorld& World,const FGuLiBuildingDefinition& Definition,
    const FVector& Ground,float Yaw,const FGuid& OriginalBuilder,FString* Rejection)
{
    if (Rejection) Rejection->Reset();
    const auto Reject=[&](const FString& Reason){if (Rejection) *Rejection=Reason;return false;};
    if (!OriginalBuilder.IsValid() || Ground.ContainsNaN() || !FMath::IsFinite(Yaw)) return Reject(TEXT("invalid-input"));
    FCollisionQueryParams Query(SCENE_QUERY_STAT(GuLiBuildingAdmittedGround),false);
    GuLiBuildingPlacement::IgnoreBuildings(World,Query); FHitResult Hit;
    if (!World.LineTraceSingleByObjectType(Hit,Ground+FVector(0,0,GuLiBuildingPlacement::PlacementGroundTraceUpCentimeters),
        Ground-FVector(0,0,GuLiBuildingPlacement::PlacementGroundTraceDownCentimeters),FCollisionObjectQueryParams(ECC_WorldStatic),Query)
        || !Hit.bBlockingHit || !Hit.GetActor() || GuLiBuildingPlacement::IsBuildingActor(Hit.GetActor())) return Reject(TEXT("ground-trace"));
    if (!GuLiBuildingPlacementPolicy::IsSlopeAllowed(Hit.ImpactNormal)) return Reject(TEXT("ground-slope"));
    if (FVector::DistSquared(Hit.ImpactPoint,Ground)>FMath::Square(GuLiBuildingPlacement::GroundContactToleranceCentimeters)) return Reject(TEXT("ground-contact"));
    if (!GuLiBuildingPlacement::AreaQueryClear(World,Definition,Ground,Yaw,Hit.GetActor(),nullptr)) return Reject(TEXT("actor-overlap"));
    TArray<FVector> Soldiers;
    if (const auto* Authority=World.GetSubsystem<UGuLiBattleAuthoritySubsystem>()) Authority->BuildLivingSoldierLocationSnapshot(Soldiers);
    const FVector Extent(Definition.CollisionExtent.X+GuLiBuildingPlacementPolicy::PlacementClearanceCentimeters,
        Definition.CollisionExtent.Y+GuLiBuildingPlacementPolicy::PlacementClearanceCentimeters,
        FMath::Max(1.f,Definition.CollisionExtent.Z-GuLiBuildingPlacement::CollisionGroundEpsilonCentimeters));
    const FVector Center=Ground+FVector(0,0,Definition.CollisionExtent.Z+GuLiBuildingPlacement::CollisionGroundEpsilonCentimeters);
    for (const auto& Position:Soldiers)
        if (GuLiBuildingPlacement::DoesDiscOverlapOrientedBox(Position,GuLiCommanderNavigationPolicy::RequiredAgentRadiusCentimeters,Center,Extent,Yaw))
            return Reject(TEXT("soldier-overlap ")+Position.ToString());
    int32 BuilderCount=0,WorldCount=0; TArray<UGuLiBuildingLifecycleComponent*> Buildings;
    World.GetSubsystem<UGuLiBuildingRegistrySubsystem>()->Query(Buildings);
    for (const auto* Building:Buildings) if (Building->CountsForManualLimit())
    {++WorldCount;if (Building->GetState().BuilderGuid==OriginalBuilder) ++BuilderCount;}
    if (GuLiBuildingPlacementPolicy::ValidateCapacity(BuilderCount,WorldCount)!=EGuLiBuildingPlacementRejectReason::None)
        return Reject(FString::Printf(TEXT("capacity builder=%d world=%d"),BuilderCount,WorldCount));
    auto* Presentation=Definition.Category==EGuLiBuildingCategory::Factory?GuLiBuildingVisuals::ResolveFactoryPresentation(&World):nullptr;
    if (Definition.Category==EGuLiBuildingCategory::Factory && !Presentation) return Reject(TEXT("presentation-unavailable"));
    FString Reason;
    // Range and line of sight were frozen and checked by the accepted input.
    // Recheck the world site without depending on that carrier still existing.
    if (!GuLiBuildings::ValidateGroundPlacement(World,Definition,FTransform(FRotator(0,Yaw,0),Ground),Presentation,nullptr,Reason))
        return Reject(TEXT("ground-placement ")+Reason);
    return true;
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

EGuLiBuildingPlacementRejectReason UGuLiBuildingPlacementComponent::ValidatePlacementRequest(
	const FGuLiBuildingPlacementRequest& Request,
	FTransform& OutSpawnTransform,
	const FGuLiBuildingDefinition*& OutDefinition, AActor*& OutSupportingActor) const
{
	OutDefinition = nullptr;
	const APlayerController* PlayerController = GetOwningPlayerController();
	const AGuLiBattlePlayerState* PlayerState = GetPlacementPlayer();
	const AGuLiBattleGameState* GameState = GetWorld()
		? GetWorld()->GetGameState<AGuLiBattleGameState>()
		: nullptr;
	if (!PlayerState || !GameState
		|| !PlayerState->IsBattleReady() || GameState->GetMatchEpoch() == 0u)
	{
		return EGuLiBuildingPlacementRejectReason::NotReady;
	}
	if (!GuLiBuildingPlacementPolicy::IsBuildingRole(PlayerState->GetBattleRole())
		|| PlayerState->GetTeam() == EGuLiTeam::Unassigned || !PlayerState->GetPlayerGuid().IsValid())
	{
		return EGuLiBuildingPlacementRejectReason::UnauthorizedRole;
	}

	const auto* Hybrid=GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>();
	bool bConnectionReady = Hybrid && Hybrid->IsWorldReady() && !Hybrid->IsBattlePaused()
		&& UGuLiHybridActorComponent::HasSimulation(GetOwner());
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
	OutDefinition = Request.DefinitionId>0?CachedCatalog->FindById(Request.DefinitionId):CachedCatalog->FindDefinition(Request.Type);
	if (!OutDefinition || !OutDefinition->IsUsable())
	{
		return EGuLiBuildingPlacementRejectReason::AssetUnavailable;
	}
	if (OutDefinition->CompletionUnitTypeId > 0 && PlayerState->GetBattleRole() != EGuLiCommanderRole::Commander)
		return EGuLiBuildingPlacementRejectReason::UnauthorizedRole;

	FHitResult GroundHit;
	if (!TracePlacementGround(FVector(Request.DesiredGroundLocation), GroundHit))
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





void UGuLiBuildingPlacementComponent::SubmitPlacementRequest(
	const FGuLiBuildingPlacementRequest& Request)
{
    RouteHybridPlacement(Request);
}

void UGuLiBuildingPlacementComponent::SendPlacementResult(
	const FGuLiBuildingPlacementResult& Result)
{
	ApplyPlacementResult(Result);
}

void UGuLiBuildingPlacementComponent::ApplyPlacementResult(
	const FGuLiBuildingPlacementResult& Result)
{
	LastPlacementReceipt=Result; ++PlacementReceiptCount;
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
		? CachedCatalog->FindById(GetSelectedBuildingDefinitionId())
		: nullptr;
	return Definition && !Definition->DisplayName.IsEmpty()
		? Definition->DisplayName
		: GetGuLiBuildingFallbackDisplayName(SelectedBuildingType);
}

#if WITH_EDITOR
bool UGuLiBuildingPlacementComponent::EditorQAFindClearPlacement(EGuLiBuildingType Type,const FVector& Preferred,float Yaw,FVector& Ground)
{
	const auto* Catalog=ResolveCatalog();const auto* D=Catalog?Catalog->FindDefinition(Type):nullptr;
	return D && EditorQAFindClearDefinition(D->DefinitionId,Preferred,Yaw,Ground);
}
bool UGuLiBuildingPlacementComponent::EditorQAFindClearDefinition(int32 DefinitionId,const FVector& Preferred,float Yaw,FVector& Ground)
{
	const auto* Pawn=GetPlacementPawn(); const auto* PC=GetOwningPlayerController();
	const auto* Catalog=ResolveCatalog(); const auto* Definition=Catalog?Catalog->FindById(DefinitionId):nullptr;
	if (!Pawn || !PC || !PC->IsLocalController() || !Definition || Preferred.ContainsNaN() || !FMath::IsFinite(Yaw)) return false;
	TArray<FVector> Candidates{Preferred};
	for (double Radius:{600.,1200.,1800.}) for (int32 Direction=0;Direction<8;++Direction)
	{
		const double Angle=UE_DOUBLE_TWO_PI*Direction/8.;
		Candidates.Add(Pawn->GetActorLocation()+FVector(FMath::Cos(Angle)*Radius,FMath::Sin(Angle)*Radius,0));
	}
	for (const auto& Candidate:Candidates)
	{
		FHitResult Hit;
		if (TracePlacementGround(Candidate,Hit) && ValidateSpatialCandidate(*Definition,Hit,GuLiBuildingPlacementPolicy::SnapYaw(Yaw))==EGuLiBuildingPlacementRejectReason::None)
		{Ground=Hit.ImpactPoint; return true;}
	}
	return false;
}
bool UGuLiBuildingPlacementComponent::EditorQASubmitPlacement(
	const EGuLiBuildingType Type,
	const FVector& GroundLocation,
	const float YawDegrees)
{
	const auto* Catalog=ResolveCatalog();const auto* D=Catalog?Catalog->FindDefinition(Type):nullptr;
	return D && EditorQASubmitDefinition(D->DefinitionId,GroundLocation,YawDegrees);
}
bool UGuLiBuildingPlacementComponent::EditorQASubmitDefinition(int32 DefinitionId,const FVector& GroundLocation,float YawDegrees)
{
	const auto* Catalog=ResolveCatalog();const auto* D=Catalog?Catalog->FindById(DefinitionId):nullptr;
	APlayerController* PlayerController = GetOwningPlayerController();
	if (!PlayerController || !PlayerController->IsLocalController()
		|| !UGuLiNetworkSettings::IsHybrid(GetWorld())
		|| !D
		|| GroundLocation.ContainsNaN() || !FMath::IsFinite(YawDegrees))
	{
		return false;
	}

	FGuLiBuildingPlacementRequest Request;
	Request.ClientRequestId = GuLiBuildingPlacement::AllocateNonZeroRequestId(NextClientRequestId);
	Request.Type = D->Type;Request.DefinitionId=DefinitionId;
	Request.DesiredGroundLocation = GroundLocation;
	Request.CompressedYaw = FRotator::CompressAxisToShort(YawDegrees);
	return RouteHybridPlacement(Request);
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



void UGuLiBuildingPlacementComponent::TestOnly_ResetServerRequestState()
{
	bHasServerRequestHistory = false;
	LastServerRequest = FGuLiBuildingPlacementRequest{};
	LastServerResult = FGuLiBuildingPlacementResult{};
	ServerRequestTimes.Reset();
}
#endif
const AGuLiBuildingPlacementPreview* UGuLiBuildingPlacementComponent::GetSceneUIPreview() const
{
	return PreviewActor.Get();
}
