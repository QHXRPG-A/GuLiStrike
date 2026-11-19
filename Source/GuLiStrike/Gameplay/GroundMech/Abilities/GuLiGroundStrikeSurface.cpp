#include "Gameplay/GroundMech/Abilities/GuLiGroundStrikeSurface.h"
#include "Gameplay/Navigation/Grid/GuLiGroundGridSubsystem.h"
#include "Gameplay/Units/GuLiUnitMovementLayer.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/WorldSettings.h"

namespace
{
bool TerrainHeight(const FGuLiGridSnapshot* Grid, FVector Point, double& Height)
{
	if (!Grid || Grid->Heights.IsEmpty()) return false;
	const FVector2D Offset = FVector2D(Point) - Grid->Origin;
	if (Offset.X < 0. || Offset.Y < 0. || Offset.X > Grid->Cells.X * GuLiGrid::CellSize
		|| Offset.Y > Grid->Cells.Y * GuLiGrid::CellSize) return false;
	Height = Grid->HeightAt(Point.X, Point.Y);
	return true;
}

bool SupportFace(const FHitResult& Hit)
{
	const auto* Component = Hit.GetComponent();
	if (!Component || Hit.bStartPenetrating || Component->GetCollisionResponseToChannel(ECC_Pawn) != ECR_Block
		|| Hit.ImpactNormal.Z < FMath::Max(.01f, Component->GetWalkableSlopeOverride().ModifyWalkableFloorZ(.65f))) return false;
	for (const AActor* Actor = Hit.GetActor(); Actor; Actor = Actor->GetAttachParentActor())
		if (Actor->IsA<APawn>()) return false; // Includes ship hulls marked WorldStatic.
	return !GuLiUnitMovement::LayerForActor(Hit.GetActor()).IsSet();
}

TOptional<FPlane> ReadSupport(UWorld& World, FVector XY, double Top, double Bottom)
{
	if (Top <= Bottom) return {};
	FCollisionObjectQueryParams Solid(ECC_WorldStatic);
	Solid.AddObjectTypesToQuery(ECC_WorldDynamic);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(GuLiGroundStrikeHeight), false);
	TArray<FHitResult> Hits;
	World.LineTraceMultiByObjectType(Hits, FVector(XY.X, XY.Y, Top), FVector(XY.X, XY.Y, Bottom), Solid, Params);
	for (const auto& Hit : Hits)
		if (SupportFace(Hit)) return FPlane(Hit.ImpactPoint, Hit.ImpactNormal);
	return {};
}

double HeightOnPlane(const FPlane& Plane, FVector XY)
{
	return (Plane.W - Plane.X * XY.X - Plane.Y * XY.Y) / Plane.Z;
}

bool CrossesPlane(FVector A, FVector B, const FPlane& Plane, double& Fraction)
{
	const double Start = Plane.PlaneDot(A), End = Plane.PlaneDot(B);
	if (Start < -.01 || End > 0. || Start <= End) return false;
	Fraction = FMath::Clamp(Start / (Start - End), 0., 1.);
	return true;
}
}

bool GuLiGroundStrikeSurface::HeightBelow(UWorld& World, const FVector& Ceiling, double& OutHeight)
{
	const auto* Service = World.GetSubsystem<UGuLiGroundGridSubsystem>();
	const auto Grid = Service ? Service->Snapshot() : nullptr;
	double Terrain = 0.;
	const bool HasTerrain = TerrainHeight(Grid.Get(), Ceiling, Terrain) && Terrain <= Ceiling.Z;
	const double Bottom = HasTerrain ? Terrain - 1. : FMath::Min(double(World.GetWorldSettings()->KillZ), Ceiling.Z - 1.);
	if (const auto Plane = ReadSupport(World, Ceiling, Ceiling.Z, Bottom))
	{
		OutHeight = HeightOnPlane(*Plane, Ceiling);
		return true;
	}
	if (HasTerrain) OutHeight = Terrain;
	return HasTerrain;
}

bool GuLiGroundStrikeSurface::FindContact(UWorld& World, TConstArrayView<FVector> Path,
	FGuLiGroundStrikeHeightCache& Cache, FVector& OutContact)
{
	if (Path.Num() < 2) return false;
	const auto* Service = World.GetSubsystem<UGuLiGroundGridSubsystem>();
	const auto Grid = Service ? Service->Snapshot() : nullptr;
	double Top = Path[0].Z;
	for (const FVector& Point : Path) Top = FMath::Max(Top, Point.Z);
	// A probe starts at the flight window's ceiling, so an overhead deck cannot mask the floor beneath it.
	auto Sample = [&](FVector Point)
	{
		double Terrain;
		const double Bottom = TerrainHeight(Grid.Get(), Point, Terrain) ? Terrain - 1. : World.GetWorldSettings()->KillZ;
		return ReadSupport(World, Point, Top + 1., Bottom);
	};
	const auto StartPlane = Cache.bInitialized ? Cache.Support : Sample(Path[0]);
	const auto EndPlane = Sample(Path.Last());
	Cache.Support = EndPlane;
	Cache.bInitialized = true;
	const bool Edge = StartPlane.IsSet() != EndPlane.IsSet()
		|| (StartPlane && EndPlane && (!FVector(*StartPlane).Equals(FVector(*EndPlane), .001)
			|| FMath::Abs(HeightOnPlane(*StartPlane, Path.Last()) - HeightOnPlane(*EndPlane, Path.Last())) > 2.));
	auto PreviousPlane = StartPlane;
	for (int32 Index = 1; Index < Path.Num(); ++Index)
	{
		const FVector From = Path[Index - 1], To = Path[Index];
		const auto NextPlane = Edge && Index < Path.Num() - 1 ? Sample(To) : EndPlane;
		double Earliest = 2., H0, H1;
		if (TerrainHeight(Grid.Get(), From, H0) && TerrainHeight(Grid.Get(), To, H1) && From.Z >= H0 - .1 && To.Z <= H1)
		{
			double Low = 0., High = 1.;
			for (int32 N = 0; N < 9; ++N)
			{
				const double Mid = (Low + High) * .5;
				const FVector P = FMath::Lerp(From, To, Mid);
				if (P.Z > Grid->HeightAt(P.X, P.Y)) Low = Mid; else High = Mid;
			}
			Earliest = High;
			OutContact = FMath::Lerp(From, To, High);
			OutContact.Z = Grid->HeightAt(OutContact.X, OutContact.Y);
		}
		auto CheckFace = [&](const TOptional<FPlane>& Plane)
		{
			double T;
			if (!Plane || !CrossesPlane(From, To, *Plane, T) || T >= Earliest) return;
			FVector Point = FMath::Lerp(From, To, T);
			// Confirm the inferred height at the actual XY. A lateral wall or the void beside a roof is not a floor.
			const auto Surface = ReadSupport(World, Point, Point.Z + 2., Point.Z - 2.);
			if (!Surface) return;
			Earliest = T;
			Point.Z = HeightOnPlane(*Surface, Point);
			OutContact = Point;
		};
		CheckFace(NextPlane);
		if (Edge) CheckFace(PreviousPlane);
		if (Earliest <= 1.) return true;
		PreviousPlane = NextPlane;
	}
	return false;
}
