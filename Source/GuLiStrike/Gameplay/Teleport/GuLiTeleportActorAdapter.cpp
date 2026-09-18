#include "Gameplay/Skills/GuLiSkillTargeting.h"
#include "Gameplay/Teleport/GuLiTeleportUnitAdapters.h"
#include "Gameplay/Units/GuLiExternalUnitControlComponent.h"
#include "Battle/Combat/GuLiCombatDamageLedger.h"
#include "Battle/Framework/GuLiBattlePlayerState.h"
#include "Battle/Network/Relay/GuLiWingmanRelayComponent.h"
#include "Gameplay/Ship/GuLiStrikeShip.h"
#include "Gameplay/Ship/GuLiShipMovementComponent.h"
#include "Gameplay/WarMachine/GuLiWarMachinePlaceholderPawn.h"
#include "Gameplay/Wingman/GuLiWingmanSimulationSubsystem.h"
#include "Gameplay/Wingman/GuLiWingmanPawn.h"
#include "Components/CapsuleComponent.h"
#include "EngineUtils.h"
#include "Engine/World.h"

namespace
{
	FGuLiWingmanRelayServer* GetCore(AActor* Actor)
	{
		auto* Ship = Cast<AGuLiStrikeShip>(Actor);
		auto* Relay = Ship && Ship->GetHangarCapability() ? Ship->GetWingmanRelay() : nullptr;
		return Relay ? Relay->GetServerRelay() : nullptr;
	}
	FTransform Decode(const FGuLiWingmanCandidateSample& Sample)
	{
		return FTransform(FRotator(Sample.RotationCentiDegrees.X*.01,Sample.RotationCentiDegrees.Y*.01,Sample.RotationCentiDegrees.Z*.01),FVector(Sample.PositionCentimeters));
	}
}
bool GuLiTeleportActorAdapter::IsAlive(const FGuLiTeleportUnit& Unit)
{
	if (Unit.Kind == EGuLiTeleportUnitKind::Wingman)
	{
		const auto* Core = GetCore(Unit.Carrier.Get());
		const auto* Entry = Core ? Core->GetRoster().FindByPredicate([&](const auto& R) { return R.Wingman == Unit.Wingman; }) : nullptr;
		return Entry && !Entry->bDead;
	}
	const AActor* Actor = Unit.Actor.Get();
	const auto* Health = Actor ? Actor->FindComponentByClass<UGuLiCombatHealthComponent>() : nullptr;
	return IsValid(Actor) && !Actor->IsActorBeingDestroyed() && (!Health || Health->IsAlive());
}
void GuLiTeleportActorAdapter::Collect(UWorld& World, const FGuLiTeleportCastState& State, TArray<FGuLiTeleportUnit>& Out)
{
	for (TActorIterator<APawn> It(&World); It; ++It)
	{
		auto* Pawn = *It; auto* Control = Pawn->FindComponentByClass<UGuLiExternalUnitControlComponent>();
		if (!Control || Control->AreActionsLocked() || !GuLiTeleport::IsInsideDisc(Pawn->GetActorLocation(),State.Source,State.Config.RadiusCentimeters)) { continue; }
		const auto* PS = Pawn->GetPlayerState<AGuLiBattlePlayerState>();
		const auto* Health = Pawn->FindComponentByClass<UGuLiCombatHealthComponent>();
		const EGuLiTeam Team = Health ? Health->GetCombatTeam() : (PS ? PS->GetTeam() : EGuLiTeam::Unassigned);
		if (Team != State.Team || (Health && !Health->IsAlive())) { continue; }
		auto* Ship = Cast<AGuLiStrikeShip>(Pawn);
		const bool bPlayerVehicle = Pawn->IsPlayerControlled() && (Ship || Pawn->IsA<AGuLiWarMachinePlaceholderPawn>());
		if (Pawn->IsPlayerControlled() && !bPlayerVehicle) { continue; }
		FVector Ground; double SurfaceHeight = 0;
		if (!GuLiSkillTargeting::ResolveGround(World,Pawn->GetActorLocation(),Ground,&SurfaceHeight)) { continue; }
		// Flight clearance is measured from physical terrain, not Recast's voxelized surface.
		const double Height = Pawn->GetActorLocation().Z - SurfaceHeight;
		if (bPlayerVehicle && !GuLiTeleport::CanCollectVehicle(State.Config,Ship != nullptr,Height)) { continue; }
		auto* Core = GetCore(Pawn);
		if (Ship && Ship->GetHangarCapability())
		{
			if (!Core || Core->IsExternallyControlled() || Core->GetLeaseState().Lifecycle != EGuLiWingmanGroupLifecycle::Active || Core->IsTransferInProgress()) { continue; }
			bool bComplete = true;
			for (const auto& R : Core->GetRoster()) { FGuLiWingmanCandidateSample Sample; bComplete &= R.bDead || Core->TryGetLatestAcceptedSample(R.Wingman,Sample); }
			if (!bComplete) { continue; }
		}
		auto& Unit = Out.AddDefaulted_GetRef(); Unit.Kind = EGuLiTeleportUnitKind::Actor; Unit.Actor = Pawn;
		Unit.Original = Unit.Landing = Pawn->GetActorTransform(); Unit.bGroundPivot = false;
		if (const auto* Character = Cast<ACharacter>(Pawn)) { Character->GetCapsuleComponent()->GetScaledCapsuleSize(Unit.Radius,Unit.HalfHeight); }
		else { Unit.Radius = Unit.HalfHeight = 20; }
		Unit.Altitude = Ship ? Height : Unit.HalfHeight+3;
		Unit.bPreserveGroundClearance = Ship != nullptr;
		if (Core)
		{
			for (const auto& R : Core->GetRoster())
			{
				FGuLiWingmanCandidateSample Sample;
				if (R.bDead || !Core->TryGetLatestAcceptedSample(R.Wingman,Sample)) { continue; }
				auto& Child = Out.AddDefaulted_GetRef(); Child.Kind = EGuLiTeleportUnitKind::Wingman;
				Child.Carrier = Ship; Child.Wingman = R.Wingman; Child.Original = Child.Landing = Decode(Sample);
				Child.Radius = Child.HalfHeight = Core->GetAbilityConfig().FormationRuntime.AgentRadiusCentimeters;
				Child.bGroundPivot = false;
			}
		}
	}
}
bool GuLiTeleportActorAdapter::CanApply(TConstArrayView<FGuLiTeleportUnit> Units, FGuid Token, bool bEntering)
{
	for (const auto& Unit : Units)
	{
		if (Unit.Kind != EGuLiTeleportUnitKind::Actor || !IsAlive(Unit)) { continue; }
		const auto* Actor = Unit.Actor.Get(); const auto* Control = Actor->FindComponentByClass<UGuLiExternalUnitControlComponent>();
		if (!Control || Unit.Landing.ContainsNaN() || (Control->AreActionsLocked() && Control->GetOwnerToken() != Token)) { return false; }
		if (const auto* Core = GetCore(Unit.Actor.Get()))
		{
			if (bEntering && (Core->IsExternallyControlled() || Core->IsTransferInProgress()
				|| Core->GetLeaseState().Lifecycle != EGuLiWingmanGroupLifecycle::Active)) { return false; }
			for (const auto& R : Core->GetRoster())
			{
				if (!R.bDead && !Units.ContainsByPredicate([&](const auto& U) { return U.Kind == EGuLiTeleportUnitKind::Wingman && U.Wingman == R.Wingman; })) { return false; }
			}
			if (!bEntering && Core->IsPhased())
			{
				TMap<FGuLiWingmanHandle,FTransform> Positions;
				for (const auto& Child : Units) { if (Child.Kind == EGuLiTeleportUnitKind::Wingman && Child.Carrier == Actor) { Positions.Add(Child.Wingman,Child.Landing); } }
				const auto* Ship = CastChecked<AGuLiStrikeShip>(Actor);
				FGuLiCarrierSourceRef Source; Source.CanonicalEpoch = Ship->GetShipMovement()->GetCanonicalEpoch(); Source.MoveRevision = Ship->GetShipMovement()->GetCanonicalMoveRevision();
				TArray<FGuLiWingmanAcceptedBatch> Prepared;
				if (!Core->PrepareExternalGroupDisplacement(Positions,Source,Actor->GetWorld()->GetTimeSeconds(),Prepared)) { return false; }
			}
		}
	}
	return true;
}
bool GuLiTeleportActorAdapter::PrepareFollowers(UWorld& World, TArray<FGuLiTeleportUnit>& Units)
{
	for (const auto& Unit : Units)
	{
		if (Unit.Kind != EGuLiTeleportUnitKind::Actor) { continue; }
		const auto* Core = GetCore(Unit.Actor.Get()); if (!Core) { continue; }
		TArray<FVector> Positions;
		TArray<int32> LivingSlots;
		for (const auto& Child : Units) { if (Child.Kind == EGuLiTeleportUnitKind::Wingman && Child.Carrier == Unit.Actor) { LivingSlots.Add(Child.Wingman.GetGroupMemberIndex()); } }
		if (LivingSlots.IsEmpty()) { continue; }
		if (!UGuLiWingmanSimulationSubsystem::PlanExternalFormationPositions(&World,Unit.Landing,Core->GetAbilityConfig().FormationRuntime,Positions,LivingSlots)) { return false; }
		for (auto& Child : Units)
		{
			if (Child.Kind == EGuLiTeleportUnitKind::Wingman && Child.Carrier == Unit.Actor)
			{ Child.Landing = FTransform(Child.Original.GetRotation(),Positions[Child.Wingman.GetGroupMemberIndex()]); }
		}
	}
	return true;
}
bool GuLiTeleportActorAdapter::Apply(UWorld& World, TConstArrayView<FGuLiTeleportUnit> Units, FGuid Token,
	bool bPhased, bool bLocked, bool bDisplace, UMaterialInterface* PhaseMaterial)
{
	for (const auto& Unit : Units)
	{
		if (Unit.Kind != EGuLiTeleportUnitKind::Actor || !IsAlive(Unit)) { continue; }
		auto* Actor = Unit.Actor.Get(); auto* Control = Actor->FindComponentByClass<UGuLiExternalUnitControlComponent>();
		auto* Ship = Cast<AGuLiStrikeShip>(Actor);
		auto* Relay = Ship && Ship->GetHangarCapability() ? Ship->GetWingmanRelay() : nullptr;
		auto* Core = Relay ? Relay->GetServerRelay() : nullptr;
		if (Core && bPhased && !Core->IsPhased())
		{ if (!Core->BeginExternalControl(World.GetTimeSeconds())) { return false; } Relay->PublishServerExternalControl({}); }
		if (!Control->ApplyServerState(Token,bPhased,bLocked,bDisplace?Unit.Landing:Unit.Original,bDisplace,PhaseMaterial)) { return false; }
		if (Core && bDisplace && !bPhased)
		{
			TMap<FGuLiWingmanHandle,FTransform> Positions;
			for (const auto& Child : Units) { if (Child.Kind == EGuLiTeleportUnitKind::Wingman && Child.Carrier == Actor) { Positions.Add(Child.Wingman,Child.Landing); } }
			FGuLiCarrierSourceRef Source; Source.CanonicalEpoch = Ship->GetShipMovement()->GetCanonicalEpoch(); Source.MoveRevision = Ship->GetShipMovement()->GetCanonicalMoveRevision();
			TArray<FGuLiWingmanAcceptedBatch> Baselines;
			if (!Core->CommitExternalGroupDisplacement(Positions,Source,World.GetTimeSeconds(),Baselines)) { return false; }
			Relay->PublishServerExternalControl(Baselines);
		}
		if (Core && !bLocked)
		{ Core->ReleaseExternalControl(World.GetTimeSeconds()); Relay->PublishServerExternalControl(Relay->GetRelayState().ExternalDisplacementBaselines); }
	}
	return true;
}
void GuLiTeleportActorAdapter::ConfigureCollisionQuery(UWorld& World, TConstArrayView<FGuLiTeleportUnit> Units, FCollisionQueryParams& Out)
{
	for (const auto& Unit : Units) { if (Unit.Actor.IsValid()) { Out.AddIgnoredActor(Unit.Actor.Get()); } }
	// Relay Pawns are local presentation, not independent authority collision bodies.
	for (TActorIterator<AGuLiWingmanPawn> It(&World); It; ++It) { Out.AddIgnoredActor(*It); }
}
