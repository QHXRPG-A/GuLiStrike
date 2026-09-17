// Copyright Epic Games, Inc. All Rights Reserved.

#include "Commander/UI/GuLiCommanderHealthBarRenderer.h"

#include "GuLiStrike.h"
#include "Commander/Framework/GuLiCommanderNetSyncComponent.h"
#include "Commander/Framework/GuLiCommanderPlayerController.h"
#include "Commander/Network/GuLiSoldierStateReplicator.h"
#include "Commander/Presentation/GuLiCommanderPresentationActor.h"
#include "Gameplay/CombatEffects/GuLiUnitFeedbackSubsystem.h"
#include "Battle/Combat/GuLiCombatDamageLedger.h"
#include "Gameplay/Units/GuLiExternalUnitControlComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "HAL/PlatformTime.h"
#include "Materials/MaterialInterface.h"
#include "Math/RotationMatrix.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "ProfilingDebugging/CsvProfiler.h"

CSV_DEFINE_CATEGORY(GuLiCommanderHealthBars, true);

namespace GuLiCommanderHealthBars
{
	constexpr int32 HealthFractionCustomDataIndex = 0;
	constexpr int32 SelectedCustomDataIndex = 1;
	constexpr int32 VisibleCustomDataIndex = 2;
	constexpr int32 CustomDataFloatCount = 3;
	constexpr float MaximumDrawDistanceCentimeters = 30000.0f;
	constexpr float FallbackSoldierHeightCentimeters = 220.0f;
	constexpr float HeightPaddingCentimeters = 20.0f;
	constexpr float DesiredWidthPixels = 84.0f;
	constexpr float DesiredHeightPixels = 12.0f;
	constexpr float PlaneMeshSizeCentimeters = 100.0f;

	FTransform MakeHiddenTransform(const FTransform& Source = FTransform::Identity)
	{
		FTransform Hidden = Source;
		Hidden.SetScale3D(FVector::ZeroVector);
		return Hidden;
	}

	bool AreTransformsEqual(const TArray<FTransform>& Lhs, const TArray<FTransform>& Rhs)
	{
		if (Lhs.Num() != Rhs.Num())
		{
			return false;
		}
		for (int32 Index = 0; Index < Lhs.Num(); ++Index)
		{
			if (!Lhs[Index].Equals(Rhs[Index], 0.01f))
			{
				return false;
			}
		}
		return true;
	}
}

AGuLiCommanderHealthBarRenderer::AGuLiCommanderHealthBarRenderer()
{
	bReplicates = false;
	bNetLoadOnClient = false;
	SetReplicateMovement(false);
	SetActorEnableCollision(false);
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
	PrimaryActorTick.TickInterval = 0.0f;
	PrimaryActorTick.TickGroup = TG_PostUpdateWork;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	HealthBarInstances = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("HealthBarInstances"));
	HealthBarInstances->SetupAttachment(SceneRoot);
	HealthBarInstances->SetMobility(EComponentMobility::Movable);
	HealthBarInstances->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	HealthBarInstances->SetCanEverAffectNavigation(false);
	HealthBarInstances->SetIsReplicated(false);
	// Visibility is governed below by planar distance to the RTS camera focus.
	// A component end-cull distance is measured from the elevated camera eye and
	// would incorrectly cull every bar at the default 800 m spring-arm length.
	HealthBarInstances->SetCullDistances(0, 0);
	HealthBarInstances->SetCastShadow(false);
	HealthBarInstances->SetAffectDistanceFieldLighting(false);
	HealthBarInstances->SetAffectDynamicIndirectLighting(false);
	HealthBarInstances->SetVisibleInRayTracing(false);
	HealthBarInstances->SetReceivesDecals(false);
	HealthBarInstances->NumCustomDataFloats = GuLiCommanderHealthBars::CustomDataFloatCount;

	PlaneMeshAsset = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(
		TEXT("/Engine/BasicShapes/Plane.Plane")));
	HealthBarMaterialAsset = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(
		TEXT("/Game/GuLiStrike/FX/UnitFeedback/M_UnitHitHealthBarWorld.M_UnitHitHealthBarWorld")));
}

void AGuLiCommanderHealthBarRenderer::BeginPlay()
{
	Super::BeginPlay();

	if (GetNetMode() == NM_DedicatedServer)
	{
		SetActorTickEnabled(false);
		return;
	}

	ResolveSoftAssets();
	ResolveRuntimeDependencies();
}

void AGuLiCommanderHealthBarRenderer::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UnbindRuntimeDependencies();
	Super::EndPlay(EndPlayReason);
}

void AGuLiCommanderHealthBarRenderer::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	TRACE_CPUPROFILER_EVENT_SCOPE(GuLiCommanderHealthBars_UpdateInstances);
	CSV_SCOPED_TIMING_STAT(GuLiCommanderHealthBars, UpdateInstances);

	const double StartSeconds = FPlatformTime::Seconds();
	ResolveRuntimeDependencies();
	if (const AGuLiSoldierStateReplicator* Replicator = StateReplicator.Get())
	{
		EnsureStableInstancePool(*Replicator);
	}
	RefreshActorInstancePool();
	RebuildLocalInstances();
	LastUpdateMilliseconds = (FPlatformTime::Seconds() - StartSeconds) * 1000.0;
	CSV_CUSTOM_STAT(
		GuLiCommanderHealthBars,
		VisibleInstances,
		VisibleInstanceCount,
		ECsvCustomStatOp::Set);
}

AGuLiCommanderHealthBarRenderer* AGuLiCommanderHealthBarRenderer::FindOrSpawn(UWorld* World, APlayerController* Controller)
{
	if (!World || World->GetNetMode() == NM_DedicatedServer || !Controller || !Controller->IsLocalController()) return nullptr;
	for (TActorIterator<AGuLiCommanderHealthBarRenderer> It(World); It; ++It)
		if (IsValid(*It) && It->LocalController == Controller) return *It;
	FActorSpawnParameters Params;
	Params.Owner = Controller;
	Params.ObjectFlags |= RF_Transient;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	auto* Renderer = World->SpawnActor<AGuLiCommanderHealthBarRenderer>(StaticClass(), FTransform::Identity, Params);
	if (Renderer) Renderer->InitializeForController(Controller);
	return Renderer;
}

void AGuLiCommanderHealthBarRenderer::InitializeForController(
	APlayerController* InController)
{
	SetOwner(InController);
	HealthBarInstances->SetOnlyOwnerSee(true);
	if (LocalController.Get() == InController)
	{
		return;
	}

	BindNetSync(nullptr);
	LocalController = InController;
	ResolveRuntimeDependencies();
}

int32 AGuLiCommanderHealthBarRenderer::GetAllocatedInstanceCount() const
{
	return HealthBarInstances ? HealthBarInstances->GetInstanceCount() : 0;
}

void AGuLiCommanderHealthBarRenderer::ResolveSoftAssets()
{
	if (!HealthBarInstances)
	{
		return;
	}

	if (UStaticMesh* PlaneMesh = PlaneMeshAsset.LoadSynchronous())
	{
		HealthBarInstances->SetStaticMesh(PlaneMesh);
	}
	else if (!bLoggedMissingPlane)
	{
		UE_LOG(LogGuLiStrike, Error, TEXT("Commander health bars could not load the Engine Plane mesh."));
		bLoggedMissingPlane = true;
	}

	if (UMaterialInterface* Material = HealthBarMaterialAsset.LoadSynchronous())
	{
		HealthBarInstances->SetMaterial(0, Material);
	}
	else if (!bLoggedMissingMaterial)
	{
		UE_LOG(
			LogGuLiStrike,
			Warning,
			TEXT("Commander health bars could not load M_UI_Cmd_SoldierHealthBarWorld; instances remain available for the asset once it is created."));
		bLoggedMissingMaterial = true;
	}
}

void AGuLiCommanderHealthBarRenderer::ResolveRuntimeDependencies()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	APlayerController* Controller = LocalController.Get();
	if (!Controller) Controller = Cast<APlayerController>(GetOwner());
	if (!Controller)
	{
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
		{
			APlayerController* Candidate = It->Get();
			if (Candidate && Candidate->IsLocalController())
			{
				Controller = Candidate;
				LocalController = Candidate;
				break;
			}
		}
	}
	LocalController = Controller;
	const auto* Commander = Cast<AGuLiCommanderPlayerController>(Controller);
	BindNetSync(Commander && Commander->IsCommanderViewActive() ? Commander->GetCommanderNetSyncComponent() : nullptr);

	if (!StateReplicator.IsValid())
	{
		AGuLiSoldierStateReplicator* FoundReplicator = nullptr;
		for (TActorIterator<AGuLiSoldierStateReplicator> It(World); It; ++It)
		{
			FoundReplicator = *It;
			break;
		}
		BindStateReplicator(FoundReplicator);
	}

	if (!PresentationActor.IsValid())
	{
		AGuLiCommanderPresentationActor* FoundPresentation = nullptr;
		for (TActorIterator<AGuLiCommanderPresentationActor> It(World); It; ++It)
		{
			FoundPresentation = *It;
			break;
		}
		BindPresentationActor(FoundPresentation);
	}
}

void AGuLiCommanderHealthBarRenderer::BindNetSync(UGuLiCommanderNetSyncComponent* InNetSync)
{
	if (BoundNetSync.Get() == InNetSync)
	{
		return;
	}

	if (UGuLiCommanderNetSyncComponent* Previous = BoundNetSync.Get())
	{
		Previous->OnSelectionChanged.Remove(SelectionChangedHandle);
	}
	SelectionChangedHandle.Reset();
	BoundNetSync = InNetSync;
	SelectedSoldiers.Reset();

	if (InNetSync)
	{
		SelectionChangedHandle = InNetSync->OnSelectionChanged.AddUObject(
			this,
			&AGuLiCommanderHealthBarRenderer::HandleSelectionChanged);
		RebuildSelectedSoldiers(InNetSync->GetSelectionState());
	}
}

void AGuLiCommanderHealthBarRenderer::BindStateReplicator(
	AGuLiSoldierStateReplicator* InReplicator)
{
	if (StateReplicator.Get() == InReplicator)
	{
		return;
	}

	if (AGuLiSoldierStateReplicator* Previous = StateReplicator.Get())
	{
		Previous->OnSoldierStatesChanged.Remove(SoldierStatesChangedHandle);
	}
	SoldierStatesChangedHandle.Reset();
	StateReplicator = InReplicator;

	if (InReplicator)
	{
		SoldierStatesChangedHandle = InReplicator->OnSoldierStatesChanged.AddUObject(
			this,
			&AGuLiCommanderHealthBarRenderer::HandleSoldierStatesChanged);
		EnsureStableInstancePool(*InReplicator);
	}
}

void AGuLiCommanderHealthBarRenderer::BindPresentationActor(
	AGuLiCommanderPresentationActor* InPresentationActor)
{
	if (PresentationActor.Get() == InPresentationActor)
	{
		return;
	}

	if (AGuLiCommanderPresentationActor* Previous = PresentationActor.Get())
	{
		RemoveTickPrerequisiteActor(Previous);
	}
	PresentationActor = InPresentationActor;
	CachedSoldierMeshes.Reset();
	SoldierHeightOffsetsCentimeters.Reset();

	if (InPresentationActor)
	{
		AddTickPrerequisiteActor(InPresentationActor);
	}
}

void AGuLiCommanderHealthBarRenderer::UnbindRuntimeDependencies()
{
	BindNetSync(nullptr);
	BindStateReplicator(nullptr);
	BindPresentationActor(nullptr);
	LocalController.Reset();
}

void AGuLiCommanderHealthBarRenderer::HandleSelectionChanged(
	const FGuLiCommanderSelectionState& Selection)
{
	RebuildSelectedSoldiers(Selection);
}

void AGuLiCommanderHealthBarRenderer::HandleSoldierStatesChanged(const uint32 SnapshotRevision)
{
	if (const AGuLiSoldierStateReplicator* Replicator = StateReplicator.Get())
	{
		EnsureStableInstancePool(*Replicator);
	}
}

void AGuLiCommanderHealthBarRenderer::RebuildSelectedSoldiers(
	const FGuLiCommanderSelectionState& Selection)
{
	SelectedSoldiers.Reset();
	int32 MemberCount = 0;
	for (const FGuLiControlCohortDescriptor& Cohort : Selection.Cohorts)
	{
		MemberCount += Cohort.MemberIds.Num();
	}
	SelectedSoldiers.Reserve(MemberCount);
	for (const FGuLiControlCohortDescriptor& Cohort : Selection.Cohorts)
	{
		for (const FGuLiSoldierId SoldierId : Cohort.MemberIds)
		{
			if (SoldierId.IsValid())
			{
				SelectedSoldiers.Add(SoldierId);
			}
		}
	}
}

void AGuLiCommanderHealthBarRenderer::EnsureStableInstancePool(
	const AGuLiSoldierStateReplicator& Replicator)
{
	if (!HealthBarInstances)
	{
		return;
	}

	TArray<const FGuLiSoldierStateItem*> OrderedStates;
	OrderedStates.Reserve(Replicator.GetItems().Num());
	for (const FGuLiSoldierStateItem& State : Replicator.GetItems())
	{
		if (State.SoldierId.IsValid() && !SoldierInstanceIndices.Contains(State.SoldierId))
		{
			OrderedStates.Add(&State);
		}
	}
	OrderedStates.Sort([](const FGuLiSoldierStateItem& Lhs, const FGuLiSoldierStateItem& Rhs)
	{
		return Lhs.SoldierId < Rhs.SoldierId;
	});

	bool bPoolExtended = false;
	for (const FGuLiSoldierStateItem* State : OrderedStates)
	{
		if (!State)
		{
			continue;
		}
		const int32 InstanceIndex = AllocateInstanceSlot();
		if (InstanceIndex == INDEX_NONE)
		{
			UE_LOG(
				LogGuLiStrike,
				Error,
				TEXT("Commander health bars failed to allocate an ISM slot for SoldierId=%u."),
				State->SoldierId.Value);
			break;
		}

		SoldierInstanceIndices.Add(State->SoldierId, InstanceIndex);
		bPoolExtended = true;
	}

	if (bPoolExtended)
	{
		HealthBarInstances->MarkRenderStateDirty();
	}
}

int32 AGuLiCommanderHealthBarRenderer::AllocateInstanceSlot()
{
	const FTransform Hidden = GuLiCommanderHealthBars::MakeHiddenTransform();
	const int32 Index = HealthBarInstances->AddInstance(Hidden, true);
	if (Index != INDEX_NONE)
	{
		CachedTransforms.Add(Hidden);
		CachedHealthFractions.Add(-2.0f);
		CachedSelectedValues.Add(-1.0f);
		CachedVisibleValues.Add(-1.0f);
	}
	return Index;
}

void AGuLiCommanderHealthBarRenderer::RefreshActorInstancePool()
{
	const auto* Feedback = GetWorld()->GetSubsystem<UGuLiUnitFeedbackSubsystem>();
	if (!Feedback || !HealthBarInstances) return;
	const auto& Bars = Feedback->GetActorHealthBars();
	for (auto It = ActorInstanceIndices.CreateIterator(); It; ++It)
		if (!It.Key().IsValid() || !Bars.Contains(It.Key()))
		{
			FreeActorInstanceSlots.Add(It.Value());
			It.RemoveCurrent();
		}
	for (const auto& Bar : Bars)
		if (Bar.Key.IsValid() && !ActorInstanceIndices.Contains(Bar.Key))
		{
			const int32 Index = FreeActorInstanceSlots.IsEmpty() ? AllocateInstanceSlot() : FreeActorInstanceSlots.Pop(EAllowShrinking::No);
			if (Index != INDEX_NONE) ActorInstanceIndices.Add(Bar.Key, Index);
		}
}

float AGuLiCommanderHealthBarRenderer::ResolveSoldierHeightOffset(
	const uint16 UnitTypeId)
{
	const AGuLiCommanderPresentationActor* Presentation = PresentationActor.Get();
	const UInstancedStaticMeshComponent* UnitInstances = Presentation
		? Presentation->FindUnitInstances(UnitTypeId)
		: nullptr;
	if (!UnitInstances && Presentation)
	{
		UnitInstances = Presentation->GetUnitInstances();
	}
	UStaticMesh* SoldierMesh = UnitInstances ? UnitInstances->GetStaticMesh() : nullptr;
	const TWeakObjectPtr<UStaticMesh>* CachedMesh = CachedSoldierMeshes.Find(UnitTypeId);
	const float* CachedHeight = SoldierHeightOffsetsCentimeters.Find(UnitTypeId);
	if (CachedMesh && CachedMesh->Get() == SoldierMesh && CachedHeight)
	{
		return *CachedHeight;
	}

	CachedSoldierMeshes.Add(UnitTypeId, SoldierMesh);
	float HeightOffset = GuLiCommanderHealthBars::FallbackSoldierHeightCentimeters;
	if (SoldierMesh)
	{
		HeightOffset = CalculateSoldierHeightOffset(SoldierMesh->GetBounds());
	}
	SoldierHeightOffsetsCentimeters.Add(UnitTypeId, HeightOffset);
	return HeightOffset;
}

void AGuLiCommanderHealthBarRenderer::RebuildLocalInstances()
{
	APlayerController* Controller = LocalController.Get();
	const AGuLiSoldierStateReplicator* Replicator = StateReplicator.Get();
	const AGuLiCommanderPresentationActor* Presentation = PresentationActor.Get();
	if (!HealthBarInstances || !Controller || !Controller->IsLocalController()
		|| !Controller->PlayerCameraManager)
	{
		HideAllInstances();
		return;
	}
	// OnlyOwnerSee is evaluated against the scene's ViewActor, not its PlayerController.
	// Follow camera/Pawn changes while keeping one isolated billboard batch per local view.
	if (GetOwner() != Controller->GetViewTarget()) SetOwner(Controller->GetViewTarget());

	int32 ViewportWidth = 0;
	int32 ViewportHeight = 0;
	Controller->GetViewportSize(ViewportWidth, ViewportHeight);
	if (ViewportWidth <= 0 || ViewportHeight <= 0)
	{
		HideAllInstances();
		return;
	}

	const FVector CameraLocation = Controller->PlayerCameraManager->GetCameraLocation();
	const APawn* CameraPawn = Controller->GetPawn();
	const FVector CameraFocusLocation = CameraPawn
		? CameraPawn->GetActorLocation()
		: CameraLocation;
	const FRotator CameraRotation = Controller->PlayerCameraManager->GetCameraRotation();
	const float HorizontalFieldOfViewDegrees = Controller->PlayerCameraManager->GetFOVAngle();
	const auto* Commander = Cast<AGuLiCommanderPlayerController>(Controller);
	const bool bCommanderView = Commander && Commander->IsCommanderViewActive();
	const float MaximumDistance = bCommanderView ? GuLiCommanderHealthBars::MaximumDrawDistanceCentimeters
		: GetDefault<UGuLiUnitFeedbackSettings>()->CullDistance;
	const float Now = GetWorld()->GetTimeSeconds();
	const int32 InstanceCount = HealthBarInstances->GetInstanceCount();
	if (InstanceCount <= 0 || CachedTransforms.Num() != InstanceCount)
	{
		VisibleInstanceCount = 0;
		return;
	}

	TArray<FTransform> DesiredTransforms = CachedTransforms;
	for (FTransform& Transform : DesiredTransforms)
	{
		Transform.SetScale3D(FVector::ZeroVector);
	}
	TArray<float> DesiredHealthFractions = CachedHealthFractions;
	TArray<float> DesiredSelectedValues;
	DesiredSelectedValues.Init(0.0f, InstanceCount);
	TArray<float> DesiredVisibleValues;
	DesiredVisibleValues.Init(0.0f, InstanceCount);

	VisibleInstanceCount = 0;
	if (Replicator && Presentation) for (const FGuLiSoldierStateItem& State : Replicator->GetItems())
	{
		const int32* InstanceIndex = SoldierInstanceIndices.Find(State.SoldierId);
		if (!InstanceIndex || !DesiredTransforms.IsValidIndex(*InstanceIndex)
			|| !DesiredHealthFractions.IsValidIndex(*InstanceIndex))
		{
			continue;
		}

		const bool bSelected = SelectedSoldiers.Contains(State.SoldierId);
		const float HitOpacity = UGuLiUnitFeedbackSubsystem::HealthBarOpacity(Now - Presentation->GetSoldierHitStartTime(State.SoldierId));
		if (!bSelected && HitOpacity <= 0.0f) continue;
		const float HealthFraction = CalculateHealthFraction(State.Health, State.MaxHealth);
		DesiredHealthFractions[*InstanceIndex] = HealthFraction;
		DesiredSelectedValues[*InstanceIndex] = bSelected ? 1.0f : 0.0f;

		if (!ShouldDisplayHealthBar(
			(State.IsAlive() && !State.bPhased),
			State.Health,
			State.MaxHealth,
			bSelected || HitOpacity > 0.0f,
			0.0f,
			MaximumDistance))
		{
			continue;
		}

		FTransform SoldierTransform;
		if (!Presentation->TryGetPresentedSoldierTransform(State.SoldierId, SoldierTransform))
		{
			continue;
		}

		FVector BarLocation = SoldierTransform.GetLocation();
		BarLocation.Z += ResolveSoldierHeightOffset(State.UnitTypeId)
			* FMath::Abs(SoldierTransform.GetScale3D().Z);
		const float CameraDistanceCentimeters = FVector::Distance(CameraLocation, BarLocation);
		const float FocusDistanceCentimeters = bCommanderView ? FVector::Dist2D(CameraFocusLocation, BarLocation) : CameraDistanceCentimeters;
		if (!ShouldDisplayHealthBar(
			(State.IsAlive() && !State.bPhased),
			State.Health,
			State.MaxHealth,
			bSelected || HitOpacity > 0.0f,
			FocusDistanceCentimeters,
			MaximumDistance))
		{
			continue;
		}

		const FVector2D WorldSize = CalculateWorldSizeCentimeters(
			CameraDistanceCentimeters,
			HorizontalFieldOfViewDegrees,
			ViewportWidth,
			ViewportHeight);
		if (WorldSize.X <= UE_SMALL_NUMBER || WorldSize.Y <= UE_SMALL_NUMBER)
		{
			continue;
		}

		const FRotationMatrix CameraBasis(CameraRotation);
		const FVector BillboardRight = CameraBasis.GetUnitAxis(EAxis::Y);
		const FVector BillboardUp = CameraBasis.GetUnitAxis(EAxis::Z);
		const FQuat BillboardRotation = FRotationMatrix::MakeFromXY(
			BillboardRight,
			BillboardUp).ToQuat();
		DesiredTransforms[*InstanceIndex] = FTransform(
			BillboardRotation,
			BarLocation,
			FVector(
				WorldSize.X / GuLiCommanderHealthBars::PlaneMeshSizeCentimeters,
				WorldSize.Y / GuLiCommanderHealthBars::PlaneMeshSizeCentimeters,
				1.0f));
		DesiredVisibleValues[*InstanceIndex] = bSelected ? 1.0f : HitOpacity;
		++VisibleInstanceCount;
	}

	if (const auto* Feedback = GetWorld()->GetSubsystem<UGuLiUnitFeedbackSubsystem>())
	for (const auto& Bar : Feedback->GetActorHealthBars())
	{
		AActor* Actor = Bar.Key.Get();
		const int32* Index = ActorInstanceIndices.Find(Bar.Key);
		if (!Actor || !Index || Actor->IsHidden() || UGuLiExternalUnitControlComponent::IsActorPhased(Actor)) continue;
		const float Alpha = UGuLiUnitFeedbackSubsystem::HealthBarOpacity(Now - Bar.Value.StartTime);
		if (Alpha <= 0.0f) continue;
		float HealthFraction = Bar.Value.HealthFraction;
		if (const auto* Health = Actor->FindComponentByClass<UGuLiCombatHealthComponent>())
		{
			if (!Health->IsAlive()) continue;
			HealthFraction = CalculateHealthFraction(Health->GetHealthState().Health, Health->GetHealthState().MaxHealth);
		}
		if (HealthFraction == 0.0f) continue;
		FBox Bounds(ForceInit); float ModelSize = 0.0f;
		if (!UGuLiUnitFeedbackSubsystem::GetActorVisualBounds(Actor, Bounds, ModelSize)) continue;
		FVector Location = Bounds.GetCenter();
		Location.Z = Bounds.Max.Z + GuLiCommanderHealthBars::HeightPaddingCentimeters;
		const float CameraDistance = FVector::Distance(CameraLocation, Location);
		const float CullDistance = bCommanderView ? FVector::Dist2D(CameraFocusLocation, Location) : CameraDistance;
		if (MaximumDistance > 0.0f && CullDistance > MaximumDistance) continue;
		const FVector2D Size = CalculateWorldSizeCentimeters(CameraDistance, HorizontalFieldOfViewDegrees, ViewportWidth, ViewportHeight);
		if (Size.X <= UE_SMALL_NUMBER || Size.Y <= UE_SMALL_NUMBER) continue;
		const FRotationMatrix Basis(CameraRotation);
		DesiredTransforms[*Index] = FTransform(FRotationMatrix::MakeFromXY(Basis.GetUnitAxis(EAxis::Y), Basis.GetUnitAxis(EAxis::Z)).ToQuat(),
			Location, FVector(Size.X / GuLiCommanderHealthBars::PlaneMeshSizeCentimeters, Size.Y / GuLiCommanderHealthBars::PlaneMeshSizeCentimeters, 1.0f));
		DesiredHealthFractions[*Index] = HealthFraction;
		DesiredVisibleValues[*Index] = Alpha;
		++VisibleInstanceCount;
	}

	bool bRenderStateDirty = false;
	if (!GuLiCommanderHealthBars::AreTransformsEqual(CachedTransforms, DesiredTransforms)
		&& HealthBarInstances->BatchUpdateInstancesTransforms(
			0,
			DesiredTransforms,
			true,
			false,
			false))
	{
		CachedTransforms = MoveTemp(DesiredTransforms);
		bRenderStateDirty = true;
	}

	for (int32 InstanceIndex = 0; InstanceIndex < InstanceCount; ++InstanceIndex)
	{
		if (!FMath::IsNearlyEqual(
			CachedHealthFractions[InstanceIndex],
			DesiredHealthFractions[InstanceIndex]))
		{
			HealthBarInstances->SetCustomDataValue(
				InstanceIndex,
				GuLiCommanderHealthBars::HealthFractionCustomDataIndex,
				DesiredHealthFractions[InstanceIndex],
				false);
			CachedHealthFractions[InstanceIndex] = DesiredHealthFractions[InstanceIndex];
			bRenderStateDirty = true;
		}
		if (!FMath::IsNearlyEqual(
			CachedSelectedValues[InstanceIndex],
			DesiredSelectedValues[InstanceIndex]))
		{
			HealthBarInstances->SetCustomDataValue(
				InstanceIndex,
				GuLiCommanderHealthBars::SelectedCustomDataIndex,
				DesiredSelectedValues[InstanceIndex],
				false);
			CachedSelectedValues[InstanceIndex] = DesiredSelectedValues[InstanceIndex];
			bRenderStateDirty = true;
		}
		if (!FMath::IsNearlyEqual(
			CachedVisibleValues[InstanceIndex],
			DesiredVisibleValues[InstanceIndex]))
		{
			HealthBarInstances->SetCustomDataValue(
				InstanceIndex,
				GuLiCommanderHealthBars::VisibleCustomDataIndex,
				DesiredVisibleValues[InstanceIndex],
				false);
			CachedVisibleValues[InstanceIndex] = DesiredVisibleValues[InstanceIndex];
			bRenderStateDirty = true;
		}
	}

	if (bRenderStateDirty)
	{
		HealthBarInstances->MarkRenderStateDirty();
	}
}

void AGuLiCommanderHealthBarRenderer::HideAllInstances()
{
	VisibleInstanceCount = 0;
	if (!HealthBarInstances || CachedTransforms.IsEmpty())
	{
		return;
	}

	TArray<FTransform> HiddenTransforms = CachedTransforms;
	for (FTransform& Transform : HiddenTransforms)
	{
		Transform.SetScale3D(FVector::ZeroVector);
	}
	bool bRenderStateDirty = false;
	if (!GuLiCommanderHealthBars::AreTransformsEqual(CachedTransforms, HiddenTransforms)
		&& HealthBarInstances->BatchUpdateInstancesTransforms(
			0,
			HiddenTransforms,
			true,
			false,
			false))
	{
		CachedTransforms = MoveTemp(HiddenTransforms);
		bRenderStateDirty = true;
	}

	for (int32 InstanceIndex = 0; InstanceIndex < CachedVisibleValues.Num(); ++InstanceIndex)
	{
		if (!FMath::IsNearlyZero(CachedVisibleValues[InstanceIndex]))
		{
			HealthBarInstances->SetCustomDataValue(
				InstanceIndex,
				GuLiCommanderHealthBars::VisibleCustomDataIndex,
				0.0f,
				false);
			CachedVisibleValues[InstanceIndex] = 0.0f;
			bRenderStateDirty = true;
		}
	}
	if (bRenderStateDirty)
	{
		HealthBarInstances->MarkRenderStateDirty();
	}
}

bool AGuLiCommanderHealthBarRenderer::ShouldDisplayHealthBar(
	const bool bAlive,
	const float Health,
	const float MaxHealth,
	const bool bSelected,
	const float DistanceCentimeters,
	const float MaximumDistanceCentimeters)
{
	if (!bAlive || !FMath::IsFinite(Health) || !FMath::IsFinite(MaxHealth)
		|| Health <= 0.0f || MaxHealth <= 0.0f || (!bSelected && Health >= MaxHealth))
	{
		return false;
	}
	return FMath::IsFinite(DistanceCentimeters)
		&& FMath::IsFinite(MaximumDistanceCentimeters)
		&& DistanceCentimeters >= 0.0f
		&& MaximumDistanceCentimeters >= 0.0f
		&& DistanceCentimeters <= MaximumDistanceCentimeters;
}

float AGuLiCommanderHealthBarRenderer::CalculateHealthFraction(
	const float Health,
	const float MaxHealth)
{
	if (!FMath::IsFinite(Health) || !FMath::IsFinite(MaxHealth) || MaxHealth <= 0.0f)
	{
		return 0.0f;
	}
	return FMath::Clamp(Health / MaxHealth, 0.0f, 1.0f);
}

FVector2D AGuLiCommanderHealthBarRenderer::CalculateWorldSizeCentimeters(
	const float DistanceCentimeters,
	const float HorizontalFieldOfViewDegrees,
	const int32 ViewportWidth,
	const int32 ViewportHeight)
{
	if (!FMath::IsFinite(DistanceCentimeters)
		|| !FMath::IsFinite(HorizontalFieldOfViewDegrees)
		|| DistanceCentimeters <= 0.0f
		|| ViewportWidth <= 0
		|| ViewportHeight <= 0)
	{
		return FVector2D::ZeroVector;
	}

	const float AspectRatio = static_cast<float>(ViewportWidth)
		/ static_cast<float>(ViewportHeight);
	const float HalfHorizontalFovRadians = FMath::DegreesToRadians(
		FMath::Clamp(HorizontalFieldOfViewDegrees, 5.0f, 170.0f) * 0.5f);
	const float HalfVerticalTangent = FMath::Tan(HalfHorizontalFovRadians) / AspectRatio;
	const float CentimetersPerPixel =
		(2.0f * DistanceCentimeters * HalfVerticalTangent)
		/ static_cast<float>(ViewportHeight);
	return FVector2D(
		GuLiCommanderHealthBars::DesiredWidthPixels * CentimetersPerPixel,
		GuLiCommanderHealthBars::DesiredHeightPixels * CentimetersPerPixel);
}

float AGuLiCommanderHealthBarRenderer::CalculateSoldierHeightOffset(
	const FBoxSphereBounds& Bounds)
{
	const float MeshTop = static_cast<float>(Bounds.Origin.Z + Bounds.BoxExtent.Z);
	if (!FMath::IsFinite(MeshTop))
	{
		return GuLiCommanderHealthBars::FallbackSoldierHeightCentimeters;
	}
	return FMath::Max(
		GuLiCommanderHealthBars::HeightPaddingCentimeters,
		MeshTop + GuLiCommanderHealthBars::HeightPaddingCentimeters);
}

#if WITH_DEV_AUTOMATION_TESTS
bool AGuLiCommanderHealthBarRenderer::TestOnly_ShouldDisplayHealthBar(
	const bool bAlive,
	const float Health,
	const float MaxHealth,
	const bool bSelected,
	const float DistanceCentimeters,
	const float MaximumDistanceCentimeters)
{
	return ShouldDisplayHealthBar(
		bAlive,
		Health,
		MaxHealth,
		bSelected,
		DistanceCentimeters,
		MaximumDistanceCentimeters);
}

float AGuLiCommanderHealthBarRenderer::TestOnly_CalculateHealthFraction(
	const float Health,
	const float MaxHealth)
{
	return CalculateHealthFraction(Health, MaxHealth);
}

FVector2D AGuLiCommanderHealthBarRenderer::TestOnly_CalculateWorldSizeCentimeters(
	const float DistanceCentimeters,
	const float HorizontalFieldOfViewDegrees,
	const int32 ViewportWidth,
	const int32 ViewportHeight)
{
	return CalculateWorldSizeCentimeters(
		DistanceCentimeters,
		HorizontalFieldOfViewDegrees,
		ViewportWidth,
		ViewportHeight);
}

float AGuLiCommanderHealthBarRenderer::TestOnly_CalculateSoldierHeightOffset(
	const FBoxSphereBounds& Bounds)
{
	return CalculateSoldierHeightOffset(Bounds);
}
#endif
