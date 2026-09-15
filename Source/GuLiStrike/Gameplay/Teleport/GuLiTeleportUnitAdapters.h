#pragma once
#include "CoreMinimal.h"
#include "Gameplay/Teleport/GuLiTeleportTypes.h"
#include "Commander/Mass/GuLiMassExternalControl.h"
#include "Battle/Contracts/GuLiWingmanProtocolTypes.h"

class UMaterialInterface;
enum class EGuLiTeleportUnitKind : uint8 { Mass, Actor, Wingman };

/** One authority-owned list. Wingmen use stable relay identities on servers that have no Wingman Actors. */
struct FGuLiTeleportUnit
{
	EGuLiTeleportUnitKind Kind = EGuLiTeleportUnitKind::Mass;
	FGuLiSoldierId SoldierId;
	TWeakObjectPtr<AActor> Actor;
	TWeakObjectPtr<AActor> Carrier;
	FGuLiWingmanHandle Wingman;
	FTransform Original, Landing;
	float Radius = 0, HalfHeight = 0, Altitude = 0;
	bool bGroundPivot = true;
	bool bPreserveGroundClearance = false;
};

namespace GuLiTeleportMassAdapter
{
	void Collect(UWorld& World, const FGuLiTeleportCastState& State, TArray<FGuLiTeleportUnit>& Out);
	bool IsAlive(UWorld& World, const FGuLiTeleportUnit& Unit);
	bool CanApply(UWorld& World, TConstArrayView<FGuLiTeleportUnit> Units, FGuid Token);
	bool Apply(UWorld& World, TConstArrayView<FGuLiTeleportUnit> Units, FGuid Token, bool bPhased, bool bLocked, bool bDisplace);
	void GetObstacles(UWorld& World, TArray<FGuLiTeleportUnit>& Out);
}
namespace GuLiTeleportActorAdapter
{
	void Collect(UWorld& World, const FGuLiTeleportCastState& State, TArray<FGuLiTeleportUnit>& Out);
	bool IsAlive(const FGuLiTeleportUnit& Unit);
	bool CanApply(TConstArrayView<FGuLiTeleportUnit> Units, FGuid Token, bool bEntering);
	bool PrepareFollowers(UWorld& World, TArray<FGuLiTeleportUnit>& Units);
	bool Apply(UWorld& World, TConstArrayView<FGuLiTeleportUnit> Units, FGuid Token,
		bool bPhased, bool bLocked, bool bDisplace, UMaterialInterface* PhaseMaterial);
	void ConfigureCollisionQuery(UWorld& World, TConstArrayView<FGuLiTeleportUnit> Units, FCollisionQueryParams& Out);
}
