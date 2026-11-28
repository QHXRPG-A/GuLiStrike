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
#include "Gameplay/Ship/GuLiStrikeShip.h"
#include "Network/GuLiHybridActorComponent.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "ProfilingDebugging/CsvProfiler.h"

CSV_DEFINE_CATEGORY(GuLiCommanderHealthBars, true);

FVector GuLiHealthBars::ViewOrigin(const APlayerController& Viewer)
{
	if (const auto* Subject = Viewer.GetViewTarget()) return Subject->GetActorLocation();
	return Viewer.GetFocalLocation();
}

bool GuLiHealthBars::IsNearby(const FVector& Origin, const FVector& Location)
{
	constexpr double RadiusCm = 20000.;
	return FVector::DistSquaredXY(Origin, Location) <= RadiusCm * RadiusCm;
}

float GuLiHealthBars::DamageOpacity(double Now, double LastDamage)
{
	return GetDefault<UGuLiUnitFeedbackSettings>()->HealthBarOpacity(Now, LastDamage);
}

float GuLiHealthBars::Visibility(const FVector& Origin, const FVector& Location, float RecentDamageOpacity)
{
	return IsNearby(Origin, Location) ? 1.f : RecentDamageOpacity;
}

void GuLiHealthBars::SetVitals(FGuLiSceneUIHealthBar& Bar, float Health, float MaxHealth, float Shield, float MaxShield)
{
	Bar.MaxHealth = MaxHealth;
	Bar.MaxShield = MaxShield;
	Bar.Fraction = MaxHealth > 0 ? FMath::Clamp(Health / MaxHealth, 0.f, 1.f) : 0.f;
	Bar.ShieldFraction = MaxShield > 0 ? FMath::Clamp(Shield / MaxShield, 0.f, 1.f) : 0.f;
}

FGuLiSceneUIHealthBar GuLiHealthBars::FromEvent(const UObject* Source, uint64 Element, const FGuLiHealthBarEvent& Event, double Now)
{
	FGuLiSceneUIHealthBar Bar;
	Bar.Key = {Source, Element, Event.Generation};
	Bar.Fraction = Event.HealthFraction;
	Bar.MaxHealth = Event.MaxHealth;
	Bar.ShieldFraction = Event.ShieldFraction;
	Bar.MaxShield = Event.MaxShield;
	Bar.Opacity = DamageOpacity(Now, Event.LastDamage);
	Bar.bRecentDamage = Bar.Opacity > 0;
	return Bar;
}

const FBox& FGuLiWorldHealthBars::SoldierBounds(const AGuLiCommanderPresentationActor& Presentation, uint16 Type)
{
	if (BoundsOwner.Get() != &Presentation)
	{
		BoundsOwner = &Presentation;
		ModelBounds.Reset();
	}
	auto* Local = ModelBounds.Find(Type);
	if (!Local || !Local->IsValid) Local = &ModelBounds.Add(Type, Presentation.GetUnitModelBoundsCentimeters(Type));
	return *Local;
}

bool FGuLiWorldHealthBars::ReadSoldierModel(const AGuLiCommanderPresentationActor& Presentation, FGuLiSoldierId Id,
	uint16 Type, FGuLiHealthBarModel& Model)
{
	const FBox& Local = SoldierBounds(Presentation, Type);
	FTransform Pose;
	if (!Local.IsValid || !Presentation.TryGetPresentedVisualTransform(Id, Pose)) return false;
	Pose.SetScale3D(FVector::OneVector); // The definition's centimetre bounds already include PresentationScale.
	const FVector Size = Local.GetSize();
	Model.Set(Local.TransformBy(Pose), FMath::Max(Size.X, Size.Y));
	return Model.Width > 0;
}

bool FGuLiWorldHealthBars::ReadActorModel(const AActor* Actor, FGuLiHealthBarModel& Model)
{
	if (!IsValid(Actor) || Actor->IsHidden()) return false;
	const UStaticMeshComponent* Body = nullptr;
	if (const auto* Wingman = Cast<AGuLiWingmanPawn>(Actor))
	{
		if (!Wingman->IsPresentationInteractable()) return false;
		Body = Wingman->GetBodyMesh();
	}
	else if (const auto* Ship = Cast<AGuLiStrikeShip>(Actor)) Body = Ship->GetHullMeshComponent();
	if (Body && Body->GetStaticMesh())
	{
		if (!Body->IsVisible()) return false;
		const FBox Local = Body->GetStaticMesh()->GetBoundingBox();
		const FVector Size = Local.GetSize() * Body->GetComponentScale().GetAbs();
		Model.Set(Local.TransformBy(Body->GetComponentTransform()), FMath::Max(Size.X, Size.Y));
		return Model.Width > 0.;
	}
	FBox Bounds(ForceInit);
	float Size = 0;
	double Width = 0;
	if (!UGuLiUnitFeedbackSubsystem::GetActorVisualBounds(Actor, Bounds, Size, &Width)) return false;
	Model.Set(Bounds, Width);
	return Width > 0.;
}

void FGuLiWorldHealthBars::Gather(APlayerController& Viewer, const AGuLiCommanderPresentationActor* Presentation,
	FGuLiHealthBarPoseCache& Poses, TArray<FGuLiSceneUIHealthBar>& Bars)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(GuLiWorldHealthBars_Gather);
	CSV_SCOPED_TIMING_STAT(GuLiCommanderHealthBars, Gather);
	if (!Viewer.PlayerCameraManager || !Viewer.IsLocalController()) return;
	auto* World = Viewer.GetWorld();
	const double Now = World->GetTimeSeconds();
	const FVector Origin = GuLiHealthBars::ViewOrigin(Viewer);
	const auto* Commander = Cast<AGuLiCommanderPlayerController>(&Viewer);
	const auto* Sync = Commander && Commander->IsCommanderViewActive() ? Commander->GetCommanderNetSyncComponent() : nullptr;
	const auto* Selection = Sync ? &Sync->GetSelectionState() : nullptr;

	if (auto* Roster = Presentation ? Presentation->GetSoldierRoster() : nullptr)
	{
		const auto& Events = Roster->GetActiveVitalChanges();
		if (SelectionSource.Get() != Sync || (Selection && SelectionRevision != Selection->SelectionRevision))
		{
			SelectedSoldiers.Reset();
			SelectionSource = Sync;
			SelectionRevision = Selection ? Selection->SelectionRevision : 0;
			if (Selection)
				for (const auto& Cohort : Selection->Cohorts)
					SelectedSoldiers.Append(Cohort.MemberIds);
		}
		for (const auto& State : Roster->GetItems())
		{
			const auto Id = State.SoldierId;
			FVector Position;
			uint64 Generation;
			if (!State.IsAlive() || State.bPhased || !Presentation->GetVisibleSoldierLocation(Id, Position, Generation)) continue;
			const auto* Event = Events.Find(Id);
			const float Damage = Event ? GuLiHealthBars::DamageOpacity(Now, Event->LastDamage) : 0.f;
			const float Alpha = GuLiHealthBars::Visibility(Origin, Position, Damage);
			if (Alpha <= 0) continue;
			FGuLiSceneUIHealthBar Bar;
			Bar.Key = {Roster, Id.Value, Generation};
			Bar.bRecentDamage = Damage > 0;
			Bar.Opacity = Alpha;
			GuLiHealthBars::SetVitals(Bar, State.Health, State.MaxHealth, State.Shield, State.MaxShield);
			auto Read = [&](FGuLiHealthBarModel& Model) { return ReadSoldierModel(*Presentation, Id, State.UnitTypeId, Model); };
			if (!Poses.Resolve(Bar, Read, State.DisplacementFrameFloor)) continue;
			Bar.bSelected = Selection && SelectedSoldiers.Contains(Id);
			Bars.Add(Bar);
		}
	}

	const auto* Feedback = World->GetSubsystem<UGuLiUnitFeedbackSubsystem>();
	if (!Feedback) return;
	for (const auto& Source : ActorHealthSources)
	{
		const auto* Health = Source.Get();
		if (!Health || !Health->IsAlive() || !Health->GetTargetHandle().IsValid()) continue;
		auto* Actor = Health->GetOwner();
		if (!Actor || Actor == Viewer.GetViewTarget() || Actor->IsHidden()) continue;
		const auto* Wingman = Cast<AGuLiWingmanPawn>(Actor);
		const FVector Position = Wingman ? Wingman->GetPresentationTransform().GetLocation() : Actor->GetActorLocation();
		const auto* Event = Feedback->GetActorHealthBars().Find(Actor);
		const float Damage = Event ? GuLiHealthBars::DamageOpacity(Now, Event->LastDamage) : 0.f;
		const float Alpha = GuLiHealthBars::Visibility(Origin, Position, Damage);
		if (Alpha <= 0 || (Wingman && !Wingman->IsPresentationInteractable()) || UGuLiExternalUnitControlComponent::IsActorPhased(Actor)) continue;
		const auto& State = Health->GetHealthState();
		FGuLiSceneUIHealthBar Bar;
		Bar.Key = {Actor, 0, Health->GetTargetHandle().Generation};
		Bar.Opacity = Alpha;
		Bar.bRecentDamage = Damage > 0;
		GuLiHealthBars::SetVitals(Bar, State.Health, State.MaxHealth, State.Shield, State.MaxShield);
		if (const auto* Binding = Actor->FindComponentByClass<UGuLiHybridActorComponent>()) Bar.Target = Binding->GetObjectKey();
		if (!Poses.Resolve(Bar, [Actor](FGuLiHealthBarModel& Model) { return ReadActorModel(Actor, Model); })) continue;
		if (const auto* Vehicle = Cast<IGuLiEngineeringVehicle>(Actor); Vehicle && Selection)
			Bar.bSelected = Selection->ActorIds.Contains(Vehicle->GetStableActorId());
		Bars.Add(Bar);
	}
}
