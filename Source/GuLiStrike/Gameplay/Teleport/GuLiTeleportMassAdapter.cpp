#include "Gameplay/Teleport/GuLiTeleportUnitAdapters.h"
#include "Commander/Mass/GuLiBattleAuthoritySubsystem.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"

namespace
{
	TArray<FGuLiMassExternalUnit> MakeBatch(TConstArrayView<FGuLiTeleportUnit> Units, bool bLanding)
	{
		TArray<FGuLiMassExternalUnit> Batch;
		for (const auto& Unit : Units)
		{ if (Unit.Kind == EGuLiTeleportUnitKind::Mass) { Batch.Add({Unit.SoldierId,bLanding ? Unit.Landing : Unit.Original,Unit.Radius}); } }
		return Batch;
	}
}
void GuLiTeleportMassAdapter::Collect(UWorld& World, const FGuLiTeleportCastState& State, TArray<FGuLiTeleportUnit>& Out)
{
	if (auto* Authority = World.GetSubsystem<UGuLiBattleAuthoritySubsystem>())
	{
		TArray<FGuLiMassExternalUnit> Soldiers;
		Authority->CollectExternalUnitsInDisc(State.Team,State.Source,State.Config.RadiusCentimeters,Soldiers);
		for (const auto& Soldier : Soldiers)
		{
			auto& Unit = Out.AddDefaulted_GetRef(); Unit.SoldierId = Soldier.Id;
			Unit.Original = Unit.Landing = Soldier.Transform; Unit.Radius = Unit.HalfHeight = Soldier.Radius;
		}
	}
}
bool GuLiTeleportMassAdapter::IsAlive(UWorld& World, const FGuLiTeleportUnit& Unit)
{
	const auto* Authority = World.GetSubsystem<UGuLiBattleAuthoritySubsystem>();
	FGuLiSoldierCombatDebug State;
	return Authority && Authority->TryGetSoldierCombatDebug(Unit.SoldierId,State) && State.Health > 0;
}
bool GuLiTeleportMassAdapter::CanApply(UWorld& World, TConstArrayView<FGuLiTeleportUnit> Units, FGuid Token)
{
	const auto* Authority = World.GetSubsystem<UGuLiBattleAuthoritySubsystem>();
	return Authority && Authority->CanApplyExternalUnitState(MakeBatch(Units,true),Token);
}
bool GuLiTeleportMassAdapter::Apply(UWorld& World, TConstArrayView<FGuLiTeleportUnit> Units, FGuid Token, bool bPhased, bool bLocked, bool bDisplace)
{
	auto* Authority = World.GetSubsystem<UGuLiBattleAuthoritySubsystem>();
	return Authority && Authority->ApplyExternalUnitState(MakeBatch(Units,bDisplace),Token,bPhased,bLocked,bDisplace);
}
bool GuLiTeleportMassAdapter::ResolveGround(UWorld& World, const FVector& Point, FVector& Out, double* OutSurfaceHeight)
{
	if (Point.ContainsNaN() || Point.GetAbsMax() > 10000000) { return false; }
	FHitResult Hit; FCollisionQueryParams Params(SCENE_QUERY_STAT(GuLiTeleportGround),false);
	// Some vehicle hull meshes use WorldStatic for camera queries. They are not
	// terrain and must not turn the ground under a flying participant into its roof.
	for (TActorIterator<APawn> It(&World); It; ++It) { Params.AddIgnoredActor(*It); }
	FCollisionObjectQueryParams Objects(ECC_WorldStatic);
	if (!World.LineTraceSingleByObjectType(Hit,Point+FVector(0,0,20000),Point-FVector(0,0,30000),Objects,Params)
		|| !Hit.bBlockingHit || Hit.ImpactNormal.Z < .65f) { return false; }
	if (OutSurfaceHeight) { *OutSurfaceHeight = Hit.ImpactPoint.Z; }
	// Clients have no authority subsystem. Their preview uses the ground trace;
	// every submitted point still passes server navigation and whole-batch validation.
	if (World.GetNetMode() == NM_Client) { Out = Hit.ImpactPoint; return true; }
	const auto* Authority = World.GetSubsystem<UGuLiBattleAuthoritySubsystem>();
	return Authority && Authority->ProjectExternalUnitLocation(Hit.ImpactPoint,Out)
		&& FVector::DistSquared2D(Point,Out) <= FMath::Square(100.0);
}
void GuLiTeleportMassAdapter::GetObstacles(UWorld& World, TArray<FGuLiTeleportUnit>& Out)
{
	if (const auto* Authority = World.GetSubsystem<UGuLiBattleAuthoritySubsystem>())
	{
		TArray<FVector> Locations; Authority->BuildLivingSoldierLocationSnapshot(Locations);
		for (const FVector& Location : Locations)
		{
			auto& Unit = Out.AddDefaulted_GetRef(); Unit.Radius = Unit.HalfHeight = Authority->GetExternalUnitRadius(); Unit.bGroundPivot = false;
			Unit.Landing = FTransform(Location+FVector(0,0,Unit.HalfHeight+3));
		}
	}
}
