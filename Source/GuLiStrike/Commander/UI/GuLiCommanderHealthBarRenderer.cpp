// Copyright Epic Games, Inc. All Rights Reserved.

#include "Commander/UI/GuLiCommanderHealthBarRenderer.h"

#include "GuLiStrike.h"
#include "Commander/Framework/GuLiCommanderNetSyncComponent.h"
#include "Commander/Framework/GuLiCommanderPlayerController.h"
#include "Commander/Network/GuLiSoldierStateReplicator.h"
#include "Commander/Presentation/GuLiCommanderPresentationActor.h"
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
		TEXT("/Game/Commander/UI/Materials/M_UI_Cmd_SoldierHealthBarWorld.M_UI_Cmd_SoldierHealthBarWorld")));
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
		RefreshSoldierHeightOffset();
		RebuildLocalInstances();
	}
	else
	{
		HideAllInstances();
	}
	LastUpdateMilliseconds = (FPlatformTime::Seconds() - StartSeconds) * 1000.0;
	CSV_CUSTOM_STAT(
		GuLiCommanderHealthBars,
		VisibleInstances,
		VisibleInstanceCount,
		ECsvCustomStatOp::Set);
}

void AGuLiCommanderHealthBarRenderer::InitializeForController(
	AGuLiCommanderPlayerController* InController)
{
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

	AGuLiCommanderPlayerController* Controller = LocalController.Get();
	if (!Controller)
	{
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
		{
			AGuLiCommanderPlayerController* Candidate = Cast<AGuLiCommanderPlayerController>(It->Get());
			if (Candidate && Candidate->IsLocalController())
			{
				Controller = Candidate;
				LocalController = Candidate;
				break;
			}
		}
	}
	BindNetSync(Controller ? Controller->GetCommanderNetSyncComponent() : nullptr);

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
	CachedSoldierMesh.Reset();
	SoldierHeightOffsetCentimeters = GuLiCommanderHealthBars::FallbackSoldierHeightCentimeters;

	if (InPresentationActor)
	{
		AddTickPrerequisiteActor(InPresentationActor);
		RefreshSoldierHeightOffset();
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
		const FTransform HiddenTransform = GuLiCommanderHealthBars::MakeHiddenTransform();
		const int32 InstanceIndex = HealthBarInstances->AddInstance(HiddenTransform, true);
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
		CachedTransforms.Add(HiddenTransform);
		CachedHealthFractions.Add(-1.0f);
		CachedSelectedValues.Add(-1.0f);
		CachedVisibleValues.Add(-1.0f);
		bPoolExtended = true;
	}

	if (bPoolExtended)
	{
		HealthBarInstances->MarkRenderStateDirty();
	}
}

void AGuLiCommanderHealthBarRenderer::RefreshSoldierHeightOffset()
{
	const AGuLiCommanderPresentationActor* Presentation = PresentationActor.Get();
	const UInstancedStaticMeshComponent* UnitInstances = Presentation
		? Presentation->GetUnitInstances()
		: nullptr;
	UStaticMesh* SoldierMesh = UnitInstances ? UnitInstances->GetStaticMesh() : nullptr;
	if (CachedSoldierMesh.Get() == SoldierMesh)
	{
		return;
	}

	CachedSoldierMesh = SoldierMesh;
	SoldierHeightOffsetCentimeters = GuLiCommanderHealthBars::FallbackSoldierHeightCentimeters;
	if (SoldierMesh)
	{
		const FBoxSphereBounds Bounds = SoldierMesh->GetBounds();
		SoldierHeightOffsetCentimeters = FMath::Max(
			GuLiCommanderHealthBars::HeightPaddingCentimeters,
			Bounds.Origin.Z + Bounds.BoxExtent.Z
				+ GuLiCommanderHealthBars::HeightPaddingCentimeters);
	}
}

void AGuLiCommanderHealthBarRenderer::RebuildLocalInstances()
{
	AGuLiCommanderPlayerController* Controller = LocalController.Get();
	const AGuLiSoldierStateReplicator* Replicator = StateReplicator.Get();
	const AGuLiCommanderPresentationActor* Presentation = PresentationActor.Get();
	if (!HealthBarInstances || !Controller || !Controller->IsLocalController()
		|| !Replicator || !Presentation || !Controller->PlayerCameraManager)
	{
		HideAllInstances();
		return;
	}

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
	for (const FGuLiSoldierStateItem& State : Replicator->GetItems())
	{
		const int32* InstanceIndex = SoldierInstanceIndices.Find(State.SoldierId);
		if (!InstanceIndex || !DesiredTransforms.IsValidIndex(*InstanceIndex)
			|| !DesiredHealthFractions.IsValidIndex(*InstanceIndex))
		{
			continue;
		}

		const bool bSelected = SelectedSoldiers.Contains(State.SoldierId);
		const float HealthFraction = CalculateHealthFraction(State.Health, State.MaxHealth);
		DesiredHealthFractions[*InstanceIndex] = HealthFraction;
		DesiredSelectedValues[*InstanceIndex] = bSelected ? 1.0f : 0.0f;

		if (!ShouldDisplayHealthBar(
			State.IsAlive(),
			State.Health,
			State.MaxHealth,
			bSelected,
			0.0f,
			GuLiCommanderHealthBars::MaximumDrawDistanceCentimeters))
		{
			continue;
		}

		FTransform SoldierTransform;
		if (!Presentation->TryGetPresentedSoldierTransform(State.SoldierId, SoldierTransform))
		{
			continue;
		}

		FVector BarLocation = SoldierTransform.GetLocation();
		BarLocation.Z += SoldierHeightOffsetCentimeters
			* FMath::Abs(SoldierTransform.GetScale3D().Z);
		const float CameraDistanceCentimeters = FVector::Distance(CameraLocation, BarLocation);
		const float FocusDistanceCentimeters = FVector::Dist2D(CameraFocusLocation, BarLocation);
		if (!ShouldDisplayHealthBar(
			State.IsAlive(),
			State.Health,
			State.MaxHealth,
			bSelected,
			FocusDistanceCentimeters,
			GuLiCommanderHealthBars::MaximumDrawDistanceCentimeters))
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
		DesiredVisibleValues[*InstanceIndex] = 1.0f;
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
	const uint8 Health,
	const uint8 MaxHealth,
	const bool bSelected,
	const float DistanceCentimeters,
	const float MaximumDistanceCentimeters)
{
	if (!bAlive || (!bSelected && Health >= FMath::Max<uint8>(1u, MaxHealth)))
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
	const uint8 Health,
	const uint8 MaxHealth)
{
	const float SafeMaximum = static_cast<float>(FMath::Max<uint8>(1u, MaxHealth));
	return FMath::Clamp(static_cast<float>(Health) / SafeMaximum, 0.0f, 1.0f);
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

#if WITH_DEV_AUTOMATION_TESTS
bool AGuLiCommanderHealthBarRenderer::TestOnly_ShouldDisplayHealthBar(
	const bool bAlive,
	const uint8 Health,
	const uint8 MaxHealth,
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
	const uint8 Health,
	const uint8 MaxHealth)
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
#endif
