#include "Gameplay/Teleport/GuLiTeleportUnitAdapters.h"
#include "Commander/Mass/GuLiBattleAuthoritySubsystem.h"
#include "Engine/World.h"

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
