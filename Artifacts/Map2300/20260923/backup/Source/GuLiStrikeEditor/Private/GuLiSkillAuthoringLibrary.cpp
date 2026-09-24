#include "GuLiSkillAuthoringLibrary.h"
#include "Gameplay/Ship/Build/GuLiShipBuildCatalog.h"
#include "Gameplay/Ship/GuLiStrikeShip.h"
#include "Gameplay/Ship/GuLiStrikeShipPartComponent.h"
#include "Gameplay/CommanderSkills/GuLiCommanderSkillDefinition.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Blueprint.h"
#include "Kismet2/KismetEditorUtilities.h"

bool UGuLiSkillAuthoringLibrary::CompileSkillBlueprint(UBlueprint* Blueprint)
{
	if (!Blueprint) return false;
	FKismetEditorUtilities::CompileBlueprint(Blueprint);
	return Blueprint->Status != BS_Error;
}

TArray<FString> UGuLiSkillAuthoringLibrary::ValidateShipCatalog(UGuLiShipBuildCatalog* Catalog, TSubclassOf<AGuLiStrikeShip> ShipClass)
{
	TArray<FString> Issues;
	if (!Catalog || !ShipClass) { Issues.Add(TEXT("A catalogue and Ship class are required.")); return Issues; }
	FGuLiShipCompiledBuildRules Rules;
	if (!Catalog->Compile(Rules)) { Issues.Add(Rules.Error); return Issues; }
	const auto* Ship = ShipClass->GetDefaultObject<AGuLiStrikeShip>();
	for (const auto& Group : Catalog->Groups)
	{
		if (!Group.bExecutable) continue;
		for (const auto& Mount : Group.Mounts)
		{
			UClass* PartClass = Mount.PartClass.LoadSynchronous();
			const auto* Part = PartClass ? PartClass->GetDefaultObject<UGuLiStrikeShipPartComponent>() : nullptr;
			if (!Ship->GetHullMeshComponent()->DoesSocketExist(Mount.SocketName))
				Issues.Add(Group.ConfigurationId.ToString() + TEXT(": Ship hull has no socket ") + Mount.SocketName.ToString());
			if (!Part || !Part->CanAttachToSocket(Mount.SocketName))
				Issues.Add(Group.ConfigurationId.ToString() + TEXT(": Part has no compatible mount ") + Mount.SocketName.ToString());
		}
	}
	return Issues;
}
TArray<FString> UGuLiSkillAuthoringLibrary::ValidateCommanderCatalog(UGuLiCommanderSkillCatalog* Catalog)
{
	TArray<FString> Issues;
	FString Error;
	if (!Catalog) Issues.Add(TEXT("A commander skill catalogue is required."));
	else if (!Catalog->Validate(Error)) Issues.Add(Error);
	return Issues;
}
