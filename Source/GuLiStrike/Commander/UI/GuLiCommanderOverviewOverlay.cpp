#include "Commander/Presentation/GuLiCommanderHUD.h"
#include "Commander/Presentation/GuLiCommanderCameraPawn.h"
#include "Commander/Presentation/GuLiCommanderOverviewSubsystem.h"
#include "Commander/Presentation/GuLiCommanderPresentationActor.h"
#include "Commander/Framework/GuLiCommanderPlayerController.h"
#include "Commander/Framework/GuLiCommanderNetSyncComponent.h"
#include "Commander/Network/GuLiSoldierStateReplicator.h"
#include "Commander/UI/GuLiCommanderHUDWidget.h"
#include "Commander/UI/GuLiCommanderUITheme.h"
#include "Commander/UI/GuLiSceneUIWidget.h"
#include "Commander/UI/GuLiSceneUITypes.h"
#include "Commander/UI/GuLiCommanderHealthBarRenderer.h"
#include "Gameplay/Presentation/GuLiLocalTeamColors.h"
#include "Battle/Combat/GuLiCombatDamageLedger.h"
#include "Battle/Framework/GuLiBattlePlayerState.h"
#include "Gameplay/Building/GuLiBuildingLifecycleComponent.h"
#include "Gameplay/CombatEffects/GuLiUnitFeedbackSubsystem.h"
#include "Gameplay/Units/GuLiEngineeringTravelComponent.h"
#include "Gameplay/Units/GuLiExternalUnitControlComponent.h"
#include "Gameplay/Wingman/GuLiWingmanPawn.h"
#include "Gameplay/Presentation/GuLiTeamOutlineComponent.h"
#include "Battle/Framework/GuLiBattleGameState.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Engine/Canvas.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "Engine/LocalPlayer.h"
#include "Engine/GameViewportClient.h"
#include "SceneView.h"
#include "Network/GuLiHybridActorComponent.h"

void AGuLiCommanderHUD::BuildOverviewMarkers(bool GatherHealthBars)
{
	OverviewMarkers.Reset();
	const auto* PC = Cast<AGuLiCommanderPlayerController>(PlayerOwner);
	const auto* Camera = PC ? Cast<AGuLiCommanderCameraPawn>(PC->GetViewTarget()) : nullptr;
	if (!Camera || !Camera->HasCameraConfig() || !Camera->IsOverviewPresentation())
	{ InspectedOverviewActor.Reset(); InspectedOverviewSoldier = {}; return; }
	const auto* Sync = PC->GetCommanderNetSyncComponent();
	const auto* Registry = GetWorld()->GetSubsystem<UGuLiCommanderOverviewSubsystem>();
	if (!Sync || !Registry) return;
	const auto& Selection = Sync->GetSelectionState();
	TSet<uint32> Selected;
	GatherSelectedSoldierValues(Selection, Selected);
	const float DPI = UWidgetLayoutLibrary::GetViewportScale(this);
	const auto& Config = Camera->GetCameraConfig();
	const auto* Theme = RuntimeHUDWidget ? RuntimeHUDWidget->GetConsoleTheme() : nullptr;
	const auto* Feedback = GetWorld()->GetSubsystem<UGuLiUnitFeedbackSubsystem>();
	const double Now = GetWorld()->GetTimeSeconds();
	auto AddHealthBar = [this, Now](const FGuLiCommanderOverviewMarker& Marker, const UObject* Source,
		uint64 Element, const FGuLiHealthBarEvent& Change)
	{
		auto Bar = GuLiHealthBars::FromEvent(Source, Element, Change, Now);
		if (Bar.Opacity <= 0) return;
		Bar.Center = Marker.WorldLocation;
		Bar.OverviewIconPixels = Marker.Size;
		Bar.bSelected = Marker.bSelected;
		if (const auto* Actor = Marker.Actor.Get())
			if (const auto* Binding = Actor->FindComponentByClass<UGuLiHybridActorComponent>())
				Bar.Target = Binding->GetObjectKey();
		SceneUI->AddOverviewHealthBar(Bar);
	};
	const auto* Presentation = FindPresentationActor();
	if (auto* Roster = FindSoldierStateReplicator(); Roster && Presentation)
	{
		OverviewMarkers.Reserve(Roster->GetItems().Num() + 128);
		const auto* Events = GatherHealthBars ? &Roster->GetActiveVitalChanges() : nullptr;
		for (const auto& Soldier : Roster->GetItems())
		{
			if (!Soldier.IsAlive() || Soldier.bPhased) continue;
			FTransform Pose;
			if (!Presentation->TryGetPresentedSoldierTransform(Soldier.SoldierId, Pose)) continue;
			FGuLiCommanderOverviewMarker Marker;
			Marker.SoldierId = Soldier.SoldierId; Marker.Team = Soldier.Team;
			Marker.WorldLocation = Pose.GetLocation();
			if (!PC->ProjectWorldLocationToScreen(Marker.WorldLocation, Marker.ScreenPosition)) continue;
			Marker.Size = Config.OverviewUnitIconPixels * DPI;
			Marker.bSelected = Selected.Contains(Soldier.SoldierId.Value) || InspectedOverviewSoldier == Soldier.SoldierId;
			if (const auto* Change = Events ? Events->Find(Soldier.SoldierId) : nullptr)
				AddHealthBar(Marker, Roster, Soldier.SoldierId.Value, *Change);
			// Do not substitute the theme's generic square portrait for the circular unit fallback.
			if (Theme) if (const auto* Icon = Theme->Portraits.Find(Soldier.UnitTypeId)) Marker.Icon = Icon->Get();
			OverviewMarkers.Add(Marker);
		}
	}
	for (const auto& WeakActor : Registry->GetActors())
	{
		auto* Actor = WeakActor.Get();
		if (!UGuLiCommanderOverviewSubsystem::IsUnitOrBuilding(Actor) || Actor->IsHidden()
			|| UGuLiExternalUnitControlComponent::IsActorPhased(Actor)) continue;
		const auto* Health = Actor->FindComponentByClass<UGuLiCombatHealthComponent>();
		const auto* Lifecycle = Actor->FindComponentByClass<UGuLiBuildingLifecycleComponent>();
		const auto* Vehicle = Cast<IGuLiEngineeringVehicle>(Actor);
		if ((Health && !Health->IsAlive()) || (Lifecycle && Lifecycle->GetState().Phase == EGuLiBuildingPhase::Destroyed)) continue;
		const auto* Wingman = Cast<AGuLiWingmanPawn>(Actor);
		if (Wingman && !Wingman->IsPresentationInteractable()) continue;
		FGuLiCommanderOverviewMarker Marker;
		Marker.Actor = Actor; Marker.bBuilding = Lifecycle != nullptr;
		Marker.WorldLocation = Lifecycle ? Lifecycle->GetGroundLocation() : Wingman ? Wingman->GetPresentationTransform().GetLocation() : Actor->GetActorLocation();
		if (!PC->ProjectWorldLocationToScreen(Marker.WorldLocation, Marker.ScreenPosition)) continue;
		Marker.Size = (Marker.bBuilding ? Config.OverviewBuildingIconPixels : Config.OverviewUnitIconPixels) * DPI;
		Marker.Team = Lifecycle ? Lifecycle->GetTeam() : Health ? Health->GetCombatTeam() : EGuLiTeam::Unassigned;
		if (const auto* Outline = Actor->FindComponentByClass<UGuLiTeamOutlineComponent>()) Marker.Team = Outline->GetOutlineTeam();
		if (const auto* Pawn = Cast<APawn>(Actor))
			if (const auto* State = Pawn->GetPlayerState<AGuLiBattlePlayerState>()) Marker.Team = State->GetTeam();
		if (Vehicle)
		{
			Marker.Team = Vehicle->GetTeam(); Marker.ActorId = Vehicle->GetStableActorId();
			if (Theme) if (const auto* Icon = Theme->Portraits.Find(Vehicle->GetUnitTypeId())) Marker.Icon = Icon->Get();
		}
		if (Lifecycle && Theme) Marker.Icon = Theme->FindIcon(*FString::Printf(TEXT("Building.%d"), int32(Lifecycle->GetDefinition().Type)));
		Marker.bSelected = (Marker.ActorId.IsValid() && Selection.ActorIds.Contains(Marker.ActorId)) || InspectedOverviewActor == Actor;
		if (const auto* Change = GatherHealthBars && Feedback ? Feedback->GetActorHealthBars().Find(Actor) : nullptr)
			AddHealthBar(Marker, Actor, 0, *Change);
		OverviewMarkers.Add(Marker);
	}
}

bool AGuLiCommanderHUD::PickOverviewIcon(const FVector2D& Position, FGuLiCommanderOverviewMarker& Out, bool bInspect)
{
	BuildOverviewMarkers();
	const FGuLiCommanderOverviewMarker* Best = nullptr;
	double BestDistance = TNumericLimits<double>::Max();
	for (const auto& Marker : OverviewMarkers)
	{
		const FVector2D Offset = Position - Marker.ScreenPosition;
		const double Distance = Offset.SizeSquared();
		const double Radius = Marker.Size * .5 + 2 * UWidgetLayoutLibrary::GetViewportScale(this);
		const bool bHit = Marker.bBuilding ? FMath::Max(FMath::Abs(Offset.X), FMath::Abs(Offset.Y)) <= Radius : Distance <= Radius * Radius;
		if (bHit && (!Best || Distance < BestDistance || (FMath::IsNearlyEqual(Distance, BestDistance) && Marker.bSelected)))
		{ Best = &Marker; BestDistance = Distance; }
	}
	if (bInspect) { InspectedOverviewActor.Reset(); InspectedOverviewSoldier = {}; }
	if (!Best) return false;
	Out = *Best;
	const auto* State = PlayerOwner ? PlayerOwner->GetPlayerState<AGuLiBattlePlayerState>() : nullptr;
	// Controlled selections keep the network selection as their only source of highlighting (including Shift removal).
	if (bInspect && (!State || Best->Team != State->GetTeam() || (!Best->SoldierId.IsValid() && !Best->ActorId.IsValid())))
	{ InspectedOverviewActor = Best->Actor; InspectedOverviewSoldier = Best->SoldierId; }
	return true;
}

void AGuLiCommanderHUD::DrawOverviewMarkers()
{
	BuildOverviewMarkers(SceneUI.IsValid());
	if (!Canvas || OverviewMarkers.IsEmpty()) return;
	const float DPI = UWidgetLayoutLibrary::GetViewportScale(this);
	FVector2D ViewOrigin = FVector2D::ZeroVector;
	if (const auto* LP = PlayerOwner->GetLocalPlayer(); LP && LP->ViewportClient && LP->ViewportClient->Viewport)
	{
		FSceneViewProjectionData Projection;
		if (LP->GetProjectionData(LP->ViewportClient->Viewport, Projection)) ViewOrigin = FVector2D(Projection.GetConstrainedViewRect().Min);
	}
	// Feed the local player's final Slate batch; picking still uses the same marker cache.
	for (bool SelectedPass : {false, true}) for (const auto& Marker : OverviewMarkers)
	{
		if (Marker.bSelected != SelectedPass) continue;
		const FVector2D P = Marker.ScreenPosition - ViewOrigin;
		const float R = Marker.Size * .5f;
		if (P.X + R < 0 || P.Y + R < 0 || P.X - R > Canvas->SizeX || P.Y - R > Canvas->SizeY) continue;
		const FLinearColor Color = GuLiLocalTeamColors::IsAssigned(Marker.Team)
			? GuLiLocalTeamColors::GetUI(Marker.Team, GuLiLocalTeamColors::GetViewTeam(PlayerOwner))
			: FLinearColor(.65f,.65f,.65f);
		auto Shape = [&](float Radius, const FLinearColor& Tint)
		{
			if (Marker.bBuilding) SceneRect(Tint, P.X-Radius, P.Y-Radius, Radius*2, Radius*2);
			else if (SceneUI.IsValid()) SceneUI->AddScreenDisc(P, Radius, Tint);
		};
		Shape(R + (Marker.bSelected ? 2.f : 1.f)*DPI, Marker.bSelected ? FLinearColor::White : FLinearColor(.015f,.02f,.025f));
		Shape(R, Color);
		if (UTexture2D* Icon = Marker.Icon.Get(); Icon && SceneUI.IsValid())
			SceneUI->AddScreenImage(Icon, P-FVector2D(R-2*DPI), FVector2D(Marker.Size-4*DPI));
	}
}
