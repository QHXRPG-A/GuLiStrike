#include "Commander/UI/GuLiCommanderHealthBarRenderer.h"

#include "Camera/PlayerCameraManager.h"
#include "Commander/Framework/GuLiCommanderNetSyncComponent.h"
#include "Commander/Framework/GuLiCommanderPlayerController.h"
#include "Commander/Network/GuLiSoldierStateReplicator.h"
#include "Commander/Presentation/GuLiCommanderCameraPawn.h"
#include "Commander/Presentation/GuLiCommanderPresentationActor.h"
#include "Commander/UI/GuLiHealthBarLayout.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Gameplay/CombatEffects/GuLiUnitFeedbackSubsystem.h"
#include "Gameplay/Units/GuLiEngineeringTravelComponent.h"
#include "Gameplay/Units/GuLiExternalUnitControlComponent.h"
#include "Gameplay/Wingman/GuLiWingmanPawn.h"
#include "Network/GuLiHybridActorComponent.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "ProfilingDebugging/CsvProfiler.h"

CSV_DEFINE_CATEGORY(GuLiCommanderHealthBars, true);

FGuLiSceneUIHealthBar GuLiHealthBars::FromEvent(const UObject* Source, uint64 Element, const FGuLiHealthBarEvent& Event, double Now)
{
	FGuLiSceneUIHealthBar Bar;
	Bar.Key = {Source, Element, Event.Generation};
	Bar.Fraction = Event.HealthFraction;
	Bar.ShieldFraction = Event.ShieldFraction;
	Bar.bHasShield = Event.bHasShield;
	Bar.Opacity = GetDefault<UGuLiUnitFeedbackSettings>()->HealthBarOpacity(Now, Event.LastChange);
	return Bar;
}

namespace
{
	float SoldierHeight(const AGuLiCommanderPresentationActor& Presentation, uint16 Type)
	{
		const auto* Instances = Presentation.FindUnitInstances(Type);
		if (!Instances) Instances = Presentation.GetUnitInstances();
		const UStaticMesh* Mesh = Instances ? Instances->GetStaticMesh() : nullptr;
		if (!Mesh) return 44.f;
		const auto& Bounds = Mesh->GetBounds();
		const double Top = (Bounds.Origin.Z + Bounds.BoxExtent.Z) * Presentation.GetUnitPresentationScale(Type);
		return FMath::IsFinite(Top) ? float(FMath::Max(4., Top + 4.)) : 44.f;
	}
}

void FGuLiWorldHealthBars::Gather(APlayerController& Viewer, const AGuLiCommanderPresentationActor* Presentation,
	FGuLiHealthBarPoseCache& Poses, TArray<FGuLiSceneUIHealthBar>& Bars)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(GuLiWorldHealthBars_Gather);
	CSV_SCOPED_TIMING_STAT(GuLiCommanderHealthBars, Gather);
	if (!Viewer.PlayerCameraManager || !Viewer.IsLocalController()) return;
	auto* World = Viewer.GetWorld();
	const double Now = World->GetTimeSeconds();
	const auto* Camera = Cast<AGuLiCommanderCameraPawn>(Viewer.GetViewTarget());
	const double Cull = Camera && Camera->HasCameraConfig() ? 0. : GetDefault<UGuLiUnitFeedbackSettings>()->CullDistance;
	const FVector Eye = Viewer.PlayerCameraManager->GetCameraLocation();
	auto InRange = [&](const FVector& Center) { return Cull <= 0 || FVector::DistSquared(Eye, Center) <= Cull * Cull; };
	const auto* Commander = Cast<AGuLiCommanderPlayerController>(&Viewer);
	const auto* Sync = Commander && Commander->IsCommanderViewActive() ? Commander->GetCommanderNetSyncComponent() : nullptr;
	const auto* Selection = Sync ? &Sync->GetSelectionState() : nullptr;

	if (auto* Roster = Presentation ? Presentation->GetSoldierRoster() : nullptr)
	{
		const auto& Events = Roster->GetActiveVitalChanges();
		if (!Events.IsEmpty() && (SelectionSource.Get() != Sync || (Selection && SelectionRevision != Selection->SelectionRevision)))
		{
			SelectedSoldiers.Reset();
			SelectionSource = Sync;
			SelectionRevision = Selection ? Selection->SelectionRevision : 0;
			if (Selection)
				for (const auto& Cohort : Selection->Cohorts)
					SelectedSoldiers.Append(Cohort.MemberIds);
		}
		TMap<uint16, float, TInlineSetAllocator<8>> Heights;
		for (const auto& [Id, Event] : Events)
		{
			auto Bar = GuLiHealthBars::FromEvent(Roster, Id.Value, Event, Now);
			const auto* State = Roster->FindSoldierState(Id);
			if (!State || Bar.Opacity <= 0) continue;
			auto Read = [&](FVector& Center)
			{
				FTransform Pose;
				if (!Presentation->TryGetPresentedVisualTransform(Id, Pose)) return false;
				float* Height = Heights.Find(State->UnitTypeId);
				if (!Height) Height = &Heights.Add(State->UnitTypeId, SoldierHeight(*Presentation, State->UnitTypeId));
				Center = Pose.TransformPosition(FVector(0, 0, *Height));
				return true;
			};
			if (!Poses.Resolve(Bar, Read, Bar.Center, State->DisplacementFrameFloor) || !InRange(Bar.Center)) continue;
			Bar.bSelected = Selection && SelectedSoldiers.Contains(Id);
			Bars.Add(Bar);
		}
	}

	const auto* Feedback = World->GetSubsystem<UGuLiUnitFeedbackSubsystem>();
	if (!Feedback) return;
	for (const auto& [Source, Event] : Feedback->GetActorHealthBars())
	{
		auto* Actor = Source.Get();
		if (!Actor || Actor->IsHidden() || UGuLiExternalUnitControlComponent::IsActorPhased(Actor)) continue;
		const auto* Wingman = Cast<AGuLiWingmanPawn>(Actor);
		if (Wingman && !Wingman->IsPresentationInteractable()) continue;
		auto Bar = GuLiHealthBars::FromEvent(Actor, 0, Event, Now);
		if (Bar.Opacity <= 0) continue;
		if (const auto* Binding = Actor->FindComponentByClass<UGuLiHybridActorComponent>()) Bar.Target = Binding->GetObjectKey();
		if (!Poses.Resolve(Bar, [Actor](FVector& Center) { return UGuLiUnitFeedbackSubsystem::GetHealthBarAnchor(Actor, Center); }, Bar.Center)
			|| !InRange(Bar.Center)) continue;
		if (const auto* Vehicle = Cast<IGuLiEngineeringVehicle>(Actor); Vehicle && Selection)
			Bar.bSelected = Selection->ActorIds.Contains(Vehicle->GetStableActorId());
		Bars.Add(Bar);
	}
}
