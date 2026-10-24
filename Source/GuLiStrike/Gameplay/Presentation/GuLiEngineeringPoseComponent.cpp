#include "Gameplay/Presentation/GuLiEngineeringPoseComponent.h"
#include "Gameplay/Units/GuLiExternalUnitControlComponent.h"
#include "Gameplay/Vfx/GuLiClientPresentationPolicy.h"
#include "Commander/Presentation/GuLiCommanderLODSubsystem.h"
#include "Commander/UI/GuLiSceneUISourceRegistry.h"
#include "Network/GuLiHybridActorComponent.h"
#include "Components/ChildActorComponent.h"
#include "Engine/World.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

UGuLiEngineeringPoseComponent::UGuLiEngineeringPoseComponent()
{
	SetIsReplicatedByDefault(false);
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	// Work beams and health bars consume the resulting mesh pose in PostUpdateWork.
	PrimaryComponentTick.TickGroup = TG_PostPhysics;
}

void UGuLiEngineeringPoseComponent::BeginPlay()
{
	Super::BeginPlay();
	bCanPresent = GuLiClientPresentation::CanPresent(GetWorld());
	if (!bCanPresent) return;
	CollisionRoot = GetOwner()->GetRootComponent();
	if (CollisionRoot.IsValid()) RootHandle = CollisionRoot->TransformUpdated.AddUObject(this, &ThisClass::RootChanged);
	ExternalControl = GetOwner()->FindComponentByClass<UGuLiExternalUnitControlComponent>();
	if (ExternalControl.IsValid())
	{
		ExternalHandle = ExternalControl->OnStateApplied.AddUObject(this, &ThisClass::ExternalStateApplied);
		DisplacementRevision = ExternalControl->GetState().DisplacementRevision;
		bPhased = ExternalControl->IsPhased();
	}
	if (auto* Component = Visual.Get())
	{
		Component->SetAbsolute(true, true, false);
		if (auto* Child = Component->GetChildActor()) Child->AddTickPrerequisiteComponent(this);
	}
	ResetToAuthority();
}

void UGuLiEngineeringPoseComponent::InitializeVisual(UChildActorComponent& Component, const FBox& Bounds)
{
	Visual = &Component;
	BaseRelative = Component.GetRelativeTransform();
	LocalBounds = Bounds;
	bHasVisualPose = false;
	if (bCanPresent)
	{
		Component.SetAbsolute(true, true, false);
		if (auto* Child = Component.GetChildActor()) Child->AddTickPrerequisiteComponent(this);
	}
	ResetToAuthority();
}

void UGuLiEngineeringPoseComponent::ReleaseVisual()
{
	SetComponentTickEnabled(false);
	if (auto* Component = Visual.Get())
	{
		if (auto* Child = Component->GetChildActor()) Child->RemoveTickPrerequisiteComponent(this);
		Component->SetAbsolute(false, false, false);
		Component->SetRelativeTransform(BaseRelative);
	}
	Visual.Reset(); bHasVisualPose = false; bPending = bBlending = false;
}

void UGuLiEngineeringPoseComponent::ResetToAuthority()
{
	if (!GetOwner()) return;
	BlendFrom = BlendTarget = GetOwner()->GetActorTransform();
	DisplayedRoot = BlendTarget;
	BlendDuration = 0; LastReceipt = -1;
	LastTickTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0;
	bPending = bBlending = bOffscreen = false;
	SetComponentTickEnabled(false); SetComponentTickInterval(0);
	bHasVisualPose = false;
	if (bCanPresent) WriteVisual(DisplayedRoot);
}

void UGuLiEngineeringPoseComponent::SetPresentationVisible(bool Visible)
{
	if (bVisible == Visible) return;
	bVisible = Visible;
	ResetToAuthority();
}

void UGuLiEngineeringPoseComponent::RootChanged(USceneComponent*, EUpdateTransformFlags, ETeleportType)
{
	// Remote ordinary samples also use TeleportPhysics. Genuine displacement is
	// identified by the external-control revision, not that low-level flag.
	if (!bCanPresent || !bVisible || bPhased || !Visual.IsValid()) return;
	bPending = true;
	if (!IsComponentTickEnabled()) LastTickTime = GetWorld()->GetTimeSeconds() - GetWorld()->GetDeltaSeconds();
	SetComponentTickEnabled(true);
}

void UGuLiEngineeringPoseComponent::ExternalStateApplied()
{
	const auto* Control = ExternalControl.Get();
	if (!Control) return;
	if (DisplacementRevision != Control->GetState().DisplacementRevision || bPhased != Control->IsPhased())
	{
		DisplacementRevision = Control->GetState().DisplacementRevision;
		bPhased = Control->IsPhased();
		ResetToAuthority();
	}
}

void UGuLiEngineeringPoseComponent::WriteVisual(const FTransform& Pose)
{
	auto* Component = Visual.Get();
	if (!Component || (bHasVisualPose && DisplayedRoot.Equals(Pose, .001f))) return;
	DisplayedRoot = Pose; bHasVisualPose = true;
	const FTransform World = BaseRelative * Pose;
	Component->SetWorldLocationAndRotation(World.GetLocation(), World.GetRotation(), false, nullptr, ETeleportType::None);
	OnDisplayedPoseChanged.Broadcast();
	UGuLiSceneUISourceRegistry::Notify(GetOwner(), EGuLiSceneUIChange::Pose);
}

void UGuLiEngineeringPoseComponent::TickComponent(float Dt, ELevelTick TickType, FActorComponentTickFunction* Function)
{
	Super::TickComponent(Dt, TickType, Function);
	TRACE_CPUPROFILER_EVENT_SCOPE(GuLiEngineeringPose);
	if (!bCanPresent || !bVisible || bPhased || !Visual.IsValid()) { SetComponentTickEnabled(false); return; }
	const double Now = GetWorld()->GetTimeSeconds();
	const double FrameDt = FMath::Max(0.0, Now - LastTickTime);
	LastTickTime = Now;
	const FTransform Target = GetOwner()->GetActorTransform();
	auto* LOD = GetWorld()->GetSubsystem<UGuLiCommanderLODSubsystem>();
	FGuLiCommanderLODQuery Query;
	Query.Bounds = LocalBounds.TransformBy(Target) + LocalBounds.TransformBy(DisplayedRoot);
	const bool Offscreen = GuLiClientPresentation::OffscreenUnitsEnabled() && LOD && !LOD->Evaluate(Query).bVisible;
	if (Offscreen || bOffscreen)
	{
		// Keep the latest committed pose offscreen; returning never replays a backlog.
		WriteVisual(Target); bPending = bBlending = false; LastReceipt = -1;
		bOffscreen = Offscreen;
		SetComponentTickInterval(Offscreen ? float(GuLiClientPresentation::OffscreenUnitInterval) : 0.f);
		SetComponentTickEnabled(false);
		return;
	}
	if (bPending && !Target.Equals(BlendTarget, .001f))
	{
		const double NominalInterval = UGuLiHybridActorComponent::HasSimulation(GetOwner()) ? 1.0 / 60.0 : .1;
		// Local Mass commits can be batched as well. Learn the actual receipt
		// cadence even if the previous blend already finished; simulation ownership
		// does not imply a 60 Hz Actor transform stream. Forget long idle gaps.
		const double SinceReceipt = Now - LastReceipt;
		const double Interval = LastReceipt >= 0 && SinceReceipt <= .2 ? SinceReceipt : NominalInterval;
		BlendFrom = DisplayedRoot; BlendTarget = Target; BlendStarted = LastReceipt = Now;
		// Cover the receiving render frame too, so a 60 Hz/10 Hz producer cannot
		// finish its interpolation a frame early and leave alternating held poses.
		BlendDuration = FMath::Clamp(Interval + FrameDt, FMath::Min(2.0 * FrameDt, .2), .2);
		bBlending = true;
	}
	bPending = false;
	const double Alpha = BlendDuration > UE_DOUBLE_SMALL_NUMBER ? FMath::Clamp((Now - BlendStarted + FrameDt) / BlendDuration, 0.0, 1.0) : 1.0;
	FTransform Pose; Pose.Blend(BlendFrom, BlendTarget, float(Alpha));
	WriteVisual(Pose);
	if (Alpha >= 1.0) { bBlending = false; SetComponentTickEnabled(false); }
}

void UGuLiEngineeringPoseComponent::EndPlay(EEndPlayReason::Type Reason)
{
	if (CollisionRoot.IsValid()) CollisionRoot->TransformUpdated.Remove(RootHandle);
	if (ExternalControl.IsValid()) ExternalControl->OnStateApplied.Remove(ExternalHandle);
	ReleaseVisual();
	Super::EndPlay(Reason);
}

FString UGuLiEngineeringPoseComponent::GetPoseDiagnosticsJson() const
{
	const FVector Raw = GetOwner()->GetActorLocation(), Shown = DisplayedRoot.GetLocation();
	return FString::Printf(TEXT("{\"raw\":[%.6f,%.6f,%.6f],\"shown\":[%.6f,%.6f,%.6f],\"raw_yaw\":%.6f,\"shown_yaw\":%.6f,\"active\":%s,\"offscreen\":%s,\"duration\":%.6f,\"local\":%s}"),
		Raw.X,Raw.Y,Raw.Z,Shown.X,Shown.Y,Shown.Z,GetOwner()->GetActorRotation().Yaw,DisplayedRoot.Rotator().Yaw,
		IsComponentTickEnabled()?TEXT("true"):TEXT("false"),bOffscreen?TEXT("true"):TEXT("false"),BlendDuration,
		UGuLiHybridActorComponent::HasSimulation(GetOwner())?TEXT("true"):TEXT("false"));
}
