#include "Gameplay/Ship/GuLiStrikeShip.h"
#include "Gameplay/Ship/Build/GuLiShipAssemblyComponent.h"
#include "Gameplay/Ship/GuLiStrikeShipPartComponent.h"
#include "Gameplay/Ship/GuLiShipMovementComponent.h"

void AGuLiStrikeShip::CommitAssemblyParts(const TArray<UGuLiStrikeShipPartComponent*>& Previous,
	const TArray<UGuLiStrikeShipPartComponent*>& Next)
{
	check(!bEditingLoadout);
	bEditingLoadout = true;
	InstalledParts.RemoveAll([&Previous](const auto& Entry) { return Previous.Contains(Entry.Part.Get()); });
	for (auto* Part : Next)
	{
		InstalledParts.Add({Part->GetAttachSocketName(), Part});
		Part->SetHiddenInGame(false, true);
	}
	bLoadoutDirty = true;
}

void AGuLiStrikeShip::FinishAssemblyCommit()
{
	check(bEditingLoadout);
	bEditingLoadout = false;
	if (HasAuthority()) PublishLoadout();
	else
	{
		AppliedLoadoutRevision = 0;
		GetShipMovement()->SetAppliedLoadoutRevision(0);
		ApplyReplicatedLoadout();
	}
}
