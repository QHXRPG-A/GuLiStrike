#pragma once
#include "CoreMinimal.h"
#include "Commander/Network/GuLiCommanderTypes.h"

class UWorld;
class AGuLiSoldierStateReplicator;

struct FGuLiCommanderPortraitRow
{
	FGuLiSoldierId Soldier;
	FGuLiControllableActorId Actor;
	FGuLiHybridObjectKey Object;
	FName Definition;
	FString DisplayName;
	uint32 Type = 0, Ordinal=0;
	float Health = 0, MaximumHealth = 0, Shield = 0, MaximumShield = 0;
	bool bTransporting = false;
	uint32 StableId() const { return Object.IsValid()?Ordinal:Soldier.IsValid() ? Soldier.Value : Actor.Value; }
};

/** Local immutable facts; page/type inspection is deliberately stored outside this snapshot. */
struct FGuLiCommanderUIPresentation
{
	TArray<FGuLiCommanderPortraitRow> Members;
	TMap<uint32, int32> Types;
	bool bSyncing = false;
	uint32 SelectionRevision = 0;
};

FGuLiCommanderUIPresentation BuildGuLiCommanderUIPresentation(UWorld* World,
	const FGuLiCommanderSelectionState& Selection, const AGuLiSoldierStateReplicator* Roster, bool bReady);
