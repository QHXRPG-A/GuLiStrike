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
#include "TimerManager.h"
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
	constexpr float MaximumDrawDistanceCentimeters = 6000.0f;
	constexpr float FallbackSoldierHeightCentimeters = 44.0f;
	constexpr float HeightPaddingCentimeters = 4.0f;
	constexpr float DesiredWidthPixels = 84.0f;
	constexpr float DesiredHeightPixels = 12.0f;
	constexpr float PlaneMeshSizeCentimeters = 100.0f;

	FTransform MakeHiddenTransform(const FTransform& Source = FTransform::Identity)
	{
		FTransform Hidden = Source;
		Hidden.SetScale3D(FVector::ZeroVector);
		return Hidden;
	}


}

AGuLiCommanderHealthBarRenderer::AGuLiCommanderHealthBarRenderer()
{
	bReplicates = false;
	bNetLoadOnClient = false;
	SetReplicateMovement(false);
	SetActorEnableCollision(false);
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
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
	FTimerManagerTimerParameters TimerParameters;
	TimerParameters.bLoop = true; TimerParameters.bMaxOncePerFrame = true;
	GetWorld()->GetTimerManager().SetTimer(MaintenanceTimer, this, &ThisClass::MaintainActivity, 0.1f, TimerParameters);
	MaintainActivity();
}

void AGuLiCommanderHealthBarRenderer::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (GetWorld()) GetWorld()->GetTimerManager().ClearTimer(MaintenanceTimer);
	UnbindRuntimeDependencies();
	Super::EndPlay(EndPlayReason);
}

void AGuLiCommanderHealthBarRenderer::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	TRACE_CPUPROFILER_EVENT_SCOPE(GuLiCommanderHealthBars_UpdateInstances);
	CSV_SCOPED_TIMING_STAT(GuLiCommanderHealthBars, UpdateInstances);

	const double StartSeconds = FPlatformTime::Seconds();
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
	HandleSelectionChanged(FGuLiCommanderSelectionState());

	if (InNetSync)
	{
		SelectionChangedHandle = InNetSync->OnSelectionChanged.AddUObject(
			this,
			&AGuLiCommanderHealthBarRenderer::HandleSelectionChanged);
		HandleSelectionChanged(InNetSync->GetSelectionState());
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
		Previous->OnRosterDelta.Remove(SoldierStatesChangedHandle);
	}
	SoldierStatesChangedHandle.Reset();
	StateReplicator = InReplicator;
	if (!InReplicator)
	{
		for (const auto& Pair : SoldierInstanceIndices) ReleaseInstanceSlot(Pair.Value);
		SoldierInstanceIndices.Reset(); ActiveSoldierStates.Reset(); ActiveHitStartTimes.Reset();
		PendingActivityIds.Reset(); UpdateTickActivity();
	}

	if (InReplicator)
	{
		SoldierStatesChangedHandle = InReplicator->OnRosterDelta.AddUObject(this, &ThisClass::HandleRosterDelta);
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
		Previous->OnVisualStatesChanged.Remove(VisualStatesChangedHandle);
	}
	VisualStatesChangedHandle.Reset();
	PresentationActor = InPresentationActor;
	CachedSoldierMeshes.Reset();
	SoldierHeightOffsetsCentimeters.Reset();

	if (InPresentationActor)
	{
		AddTickPrerequisiteActor(InPresentationActor);
		VisualStatesChangedHandle = InPresentationActor->OnVisualStatesChanged.AddUObject(this, &ThisClass::HandleVisualStatesChanged);
	}
}

void AGuLiCommanderHealthBarRenderer::UnbindRuntimeDependencies()
{
	BindNetSync(nullptr);
	BindStateReplicator(nullptr);
	BindPresentationActor(nullptr);
	LocalController.Reset();
	if (auto* Feedback = BoundFeedback.Get()) Feedback->OnActorHealthBarChanged.Remove(ActorBarsChangedHandle);
	for (auto& Pair : ActorHealthProviders) if (auto* Health = Pair.Value.Get()) Health->OnHealthChanged.RemoveDynamic(this, &ThisClass::HandleActorHealthChanged);
	ActorHealthProviders.Reset(); BoundFeedback.Reset();
}

void AGuLiCommanderHealthBarRenderer::HandleSelectionChanged(
	const FGuLiCommanderSelectionState& Selection)
{
	TSet<FGuLiSoldierId> Affected = SelectedSoldiers;
	RebuildSelectedSoldiers(Selection);
	Affected.Append(SelectedSoldiers);
	for (auto Id : Affected) RefreshSoldierActivity(Id);
	UpdateTickActivity();
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

void AGuLiCommanderHealthBarRenderer::EnsureStableInstancePool(const AGuLiSoldierStateReplicator& Replicator)
{
	// Only a source transition calls this. Membership is independent of roster size.
	TArray<FGuLiSoldierId> Existing;
	SoldierInstanceIndices.GetKeys(Existing);
	for (auto Id : Existing) if (const auto* Index = SoldierInstanceIndices.Find(Id)) ReleaseInstanceSlot(*Index);
	SoldierInstanceIndices.Reset(); ActiveSoldierStates.Reset(); ActiveHitStartTimes.Reset(); PendingActivityIds.Reset();
	for (auto Id : SelectedSoldiers) RefreshSoldierActivity(Id);
	UpdateTickActivity();
}

void AGuLiCommanderHealthBarRenderer::RefreshSoldierActivity(FGuLiSoldierId Id)
{
	const auto* State = StateReplicator.IsValid() ? StateReplicator->FindSoldierState(Id) : nullptr;
	const float HitStart = PresentationActor.IsValid() ? PresentationActor->GetSoldierHitStartTime(Id) : -1000.0f;
	const bool bSelected = SelectedSoldiers.Contains(Id);
	const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;
	if (!State || !State->IsAlive() || State->bPhased
		|| (!bSelected && UGuLiUnitFeedbackSubsystem::HealthBarOpacity(Now - HitStart) <= 0.0f))
	{
		if (const int32* Index = SoldierInstanceIndices.Find(Id)) ReleaseInstanceSlot(*Index);
		SoldierInstanceIndices.Remove(Id); ActiveSoldierStates.Remove(Id); ActiveHitStartTimes.Remove(Id); PendingActivityIds.Remove(Id);
		return;
	}
	int32* Index = SoldierInstanceIndices.Find(Id);
	if (!Index)
	{
		const int32 Slot = AllocateInstanceSlot();
		if (Slot == INDEX_NONE) { PendingActivityIds.Add(Id); return; }
		Index = &SoldierInstanceIndices.Add(Id, Slot);
	}
	PendingActivityIds.Remove(Id);
	ActiveSoldierStates.Add(Id, *State);
	ActiveHitStartTimes.Add(Id, HitStart);
	ResolveSoldierHeightOffset(State->UnitTypeId);
}

void AGuLiCommanderHealthBarRenderer::HandleRosterDelta(const FGuLiSoldierRosterDelta& Delta)
{
	if (Delta.bReset && StateReplicator.IsValid()) EnsureStableInstancePool(*StateReplicator);
	for (auto Id : Delta.Removed) RefreshSoldierActivity(Id);
	for (auto Id : Delta.Added) if (SelectedSoldiers.Contains(Id)) RefreshSoldierActivity(Id);
	for (const auto& Pair : Delta.Changed) RefreshSoldierActivity(Pair.Key);
	UpdateTickActivity();
}

void AGuLiCommanderHealthBarRenderer::HandleVisualStatesChanged(const TArray<FGuLiSoldierId>& Ids)
{
	for (auto Id : Ids) RefreshSoldierActivity(Id);
	UpdateTickActivity();
}

void AGuLiCommanderHealthBarRenderer::UpdateTickActivity()
{
	const bool bActive = !SoldierInstanceIndices.IsEmpty() || !ActorInstanceIndices.IsEmpty();
	SetActorTickEnabled(bActive);
	if (!bActive) { VisibleInstanceCount = 0; LastUpdateMilliseconds = 0.0; }
}

void AGuLiCommanderHealthBarRenderer::MaintainActivity()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(GuLiCommanderHealthBars_MaintainActivity);
	ResolveRuntimeDependencies();
	if (!BoundFeedback.IsValid())
		if (auto* Feedback = GetWorld()->GetSubsystem<UGuLiUnitFeedbackSubsystem>())
		{
			BoundFeedback = Feedback;
			ActorBarsChangedHandle = Feedback->OnActorHealthBarChanged.AddUObject(this, &ThisClass::HandleActorBarChanged);
			for (const auto& Pair : Feedback->GetActorHealthBars()) HandleActorBarChanged(Pair.Key);
		}
	TArray<FGuLiSoldierId> Active;
	SoldierInstanceIndices.GetKeys(Active);
	for (auto Id : Active) RefreshSoldierActivity(Id);
	const auto Pending = PendingActivityIds.Array();
	for (auto Id : Pending) RefreshSoldierActivity(Id);
	// A selected unit with unavailable dependencies/slot allocation remains retryable.
	for (auto Id : SelectedSoldiers) if (!SoldierInstanceIndices.Contains(Id)) RefreshSoldierActivity(Id);
	RefreshActorInstancePool();
	UpdateTickActivity();
	CSV_CUSTOM_STAT(GuLiCommanderHealthBars, DataCacheBytes, float(CachedTransforms.GetAllocatedSize() + ActiveSoldierStates.GetAllocatedSize() + ActiveHitStartTimes.GetAllocatedSize() + SoldierInstanceIndices.GetAllocatedSize() + ActorInstanceIndices.GetAllocatedSize()), ECsvCustomStatOp::Set);
}

void AGuLiCommanderHealthBarRenderer::ReleaseInstanceSlot(int32 Index)
{
	if (!CachedTransforms.IsValidIndex(Index)) return;
	WriteInstance(Index, GuLiCommanderHealthBars::MakeHiddenTransform(), -1.0f, 0.0f, 0.0f);
	FreeInstanceSlots.AddUnique(Index);
}

void AGuLiCommanderHealthBarRenderer::HandleActorBarChanged(TWeakObjectPtr<AActor> Actor)
{
	const auto* Feedback = BoundFeedback.Get();
	const auto* Bar = Feedback ? Feedback->GetActorHealthBars().Find(Actor) : nullptr;
	if (!Actor.IsValid() || !Bar)
	{
		if (const auto* Index = ActorInstanceIndices.Find(Actor)) ReleaseInstanceSlot(*Index);
		if (const auto* Provider = ActorHealthProviders.Find(Actor); Provider && Provider->IsValid())
			Provider->Get()->OnHealthChanged.RemoveDynamic(this, &ThisClass::HandleActorHealthChanged);
		ActorInstanceIndices.Remove(Actor); ActorHealthProviders.Remove(Actor); ActorHealthFractions.Remove(Actor);
	}
	else
	{
		if (!ActorInstanceIndices.Contains(Actor))
		{
			const int32 Slot = AllocateInstanceSlot();
			if (Slot == INDEX_NONE) return;
			ActorInstanceIndices.Add(Actor, Slot);
			auto* Health = Actor->FindComponentByClass<UGuLiCombatHealthComponent>();
			ActorHealthProviders.Add(Actor, Health);
			if (Health) Health->OnHealthChanged.AddUniqueDynamic(this, &ThisClass::HandleActorHealthChanged);
		}
		ActorHealthFractions.Add(Actor, Bar->HealthFraction);
		HandleActorHealthChanged(0, 0);
	}
	UpdateTickActivity();
}

void AGuLiCommanderHealthBarRenderer::HandleActorHealthChanged(float Health, float MaxHealth)
{
	for (const auto& Pair : ActorHealthProviders)
		if (const auto* Provider = Pair.Value.Get())
			ActorHealthFractions.Add(Pair.Key, Provider->IsAlive()
				? CalculateHealthFraction(Provider->GetHealthState().Health, Provider->GetHealthState().MaxHealth) : 0.0f);
}

int32 AGuLiCommanderHealthBarRenderer::AllocateInstanceSlot()
{
	if (!FreeInstanceSlots.IsEmpty()) return FreeInstanceSlots.Pop(EAllowShrinking::No);
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
	if (!BoundFeedback.IsValid()) return;
	TArray<TWeakObjectPtr<AActor>> Existing;
	ActorInstanceIndices.GetKeys(Existing);
	for (auto Actor : Existing)
		if (!Actor.IsValid() || !BoundFeedback->GetActorHealthBars().Contains(Actor)) HandleActorBarChanged(Actor);
	for (const auto& Pair : BoundFeedback->GetActorHealthBars())
		if (!ActorInstanceIndices.Contains(Pair.Key)) HandleActorBarChanged(Pair.Key);
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
		const float Scale = Presentation ? Presentation->GetUnitPresentationScale(UnitTypeId) : 0.2f;
		HeightOffset = CalculateSoldierHeightOffset(SoldierMesh->GetBounds().TransformBy(
			FTransform(FQuat::Identity, FVector::ZeroVector, FVector(Scale))));
	}
	SoldierHeightOffsetsCentimeters.Add(UnitTypeId, HeightOffset);
	return HeightOffset;
}

void AGuLiCommanderHealthBarRenderer::WriteInstance(int32 Index, const FTransform& Transform, float Health, float Selected, float Visible)
{
	if (!HealthBarInstances || !CachedTransforms.IsValidIndex(Index)) return;
	if (!CachedTransforms[Index].Equals(Transform, 0.01f)
		&& HealthBarInstances->UpdateInstanceTransform(Index, Transform, true, false, false)) CachedTransforms[Index] = Transform;
	const float Values[] = {Health, Selected, Visible};
	TArray<float>* Caches[] = {&CachedHealthFractions, &CachedSelectedValues, &CachedVisibleValues};
	for (int32 Channel = 0; Channel < 3; ++Channel)
		if (!FMath::IsNearlyEqual((*Caches[Channel])[Index], Values[Channel]))
		{
			HealthBarInstances->SetCustomDataValue(Index, Channel, Values[Channel], false);
			(*Caches[Channel])[Index] = Values[Channel];
		}
}

void AGuLiCommanderHealthBarRenderer::RebuildLocalInstances()
{
	auto* Controller = LocalController.Get();
	const auto* Presentation = PresentationActor.Get();
	if (!HealthBarInstances || !Controller || !Controller->IsLocalController() || !Controller->PlayerCameraManager)
	{ HideAllInstances(); return; }
	if (GetOwner() != Controller->GetViewTarget()) SetOwner(Controller->GetViewTarget());
	int32 Width = 0, Height = 0;
	Controller->GetViewportSize(Width, Height);
	if (Width <= 0 || Height <= 0) { HideAllInstances(); return; }
	const APlayerCameraManager* Camera = Controller->PlayerCameraManager.Get();
	const FVector CameraLocation = Camera->GetCameraLocation();
	const FVector FocusLocation = Controller->GetPawn() ? Controller->GetPawn()->GetActorLocation() : CameraLocation;
	const auto* Commander = Cast<AGuLiCommanderPlayerController>(Controller);
	const bool bCommanderView = Commander && Commander->IsCommanderViewActive();
	const float MaxDistance = bCommanderView ? GuLiCommanderHealthBars::MaximumDrawDistanceCentimeters : GetDefault<UGuLiUnitFeedbackSettings>()->CullDistance;
	const float Now = GetWorld()->GetTimeSeconds();
	const FRotationMatrix Basis(Camera->GetCameraRotation());
	const FQuat Rotation = FRotationMatrix::MakeFromXY(Basis.GetUnitAxis(EAxis::Y), Basis.GetUnitAxis(EAxis::Z)).ToQuat();
	const FTransform Hidden = GuLiCommanderHealthBars::MakeHiddenTransform();
	auto MakeBarTransform = [&](const FVector& Location, FTransform& Out)
	{
		const float Distance = FVector::Distance(CameraLocation, Location);
		const float CullDistance = bCommanderView ? FVector::Dist2D(FocusLocation, Location) : Distance;
		if (MaxDistance > 0 && CullDistance > MaxDistance) return false;
		const FVector2D Size = CalculateWorldSizeCentimeters(Distance, Camera->GetFOVAngle(), Width, Height);
		if (Size.X <= UE_SMALL_NUMBER || Size.Y <= UE_SMALL_NUMBER) return false;
		Out = FTransform(Rotation, Location, FVector(Size.X / GuLiCommanderHealthBars::PlaneMeshSizeCentimeters, Size.Y / GuLiCommanderHealthBars::PlaneMeshSizeCentimeters, 1));
		return true;
	};
	VisibleInstanceCount = 0;
	for (const auto& Pair : SoldierInstanceIndices)
	{
		const auto& State = ActiveSoldierStates.FindChecked(Pair.Key);
		const bool bSelected = SelectedSoldiers.Contains(Pair.Key);
		float Opacity = bSelected ? 1.0f : UGuLiUnitFeedbackSubsystem::HealthBarOpacity(Now - ActiveHitStartTimes.FindChecked(Pair.Key));
		FTransform Transform = Hidden, Soldier;
		bool bVisible = false;
		if (Presentation && Opacity > 0 && State.IsAlive() && !State.bPhased && Presentation->TryGetPresentedSoldierTransform(Pair.Key, Soldier))
		{
			FVector Location = Soldier.GetLocation();
			Location.Z += SoldierHeightOffsetsCentimeters.FindRef(State.UnitTypeId) * FMath::Abs(Soldier.GetScale3D().Z);
			bVisible = MakeBarTransform(Location, Transform);
		}
		WriteInstance(Pair.Value, Transform, CalculateHealthFraction(State.Health, State.MaxHealth), bSelected ? 1.0f : 0.0f, bVisible ? Opacity : 0.0f);
		if (bVisible) ++VisibleInstanceCount;
	}
	const auto* Feedback = BoundFeedback.Get();
	for (const auto& Pair : ActorInstanceIndices)
	{
		auto* Actor = Pair.Key.Get();
		const auto* Bar = Feedback ? Feedback->GetActorHealthBars().Find(Pair.Key) : nullptr;
		const float Health = ActorHealthFractions.FindRef(Pair.Key);
		const float Opacity = Bar ? UGuLiUnitFeedbackSubsystem::HealthBarOpacity(Now - Bar->StartTime) : 0.0f;
		FTransform Transform = Hidden;
		bool bVisible = false;
		if (Actor && !Actor->IsHidden() && Health != 0 && Opacity > 0 && !UGuLiExternalUnitControlComponent::IsActorPhased(Actor))
		{
			FBox Bounds(ForceInit); float ModelSize = 0;
			if (UGuLiUnitFeedbackSubsystem::GetActorVisualBounds(Actor, Bounds, ModelSize))
			{
				auto Location = Bounds.GetCenter(); Location.Z = Bounds.Max.Z + GuLiCommanderHealthBars::HeightPaddingCentimeters;
				bVisible = MakeBarTransform(Location, Transform);
			}
		}
		WriteInstance(Pair.Value, Transform, Health, 0, bVisible ? Opacity : 0);
		if (bVisible) ++VisibleInstanceCount;
	}
}

void AGuLiCommanderHealthBarRenderer::HideAllInstances()
{
	for (const auto& Pair : SoldierInstanceIndices)
		WriteInstance(Pair.Value, GuLiCommanderHealthBars::MakeHiddenTransform(), CachedHealthFractions[Pair.Value], 0, 0);
	for (const auto& Pair : ActorInstanceIndices)
		WriteInstance(Pair.Value, GuLiCommanderHealthBars::MakeHiddenTransform(), CachedHealthFractions[Pair.Value], 0, 0);
	VisibleInstanceCount = 0;
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
