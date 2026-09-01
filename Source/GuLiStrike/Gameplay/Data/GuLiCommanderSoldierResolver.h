// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Gameplay/Data/GuLiCommanderSoldierDefinition.h"

class UDataTable;
struct FGuLiStrikeCommanderSoldiersRow;

/** Converts generated table rows into the validated runtime Soldier contract. */
struct GULISTRIKE_API FGuLiCommanderSoldierResolver final
{
	/** Float health is absolute gameplay health, with a finite authoring safety bound. */
	static constexpr float MaximumSupportedHealth = 1000000000.0f;

	/** Stable C++ fallback used when the table, row, value or model is invalid. */
	static FGuLiSoldierDefinition MakeFallbackDefinition();

	/** Resolves one named table row, falling back field-by-field without mutating the source table. */
	static FGuLiSoldierDefinition Resolve(
		const UDataTable* DataTable,
		FName RowName,
		const FGuLiSoldierDefinition& Fallback);
	static FGuLiSoldierDefinition Resolve(
		const UDataTable* DataTable,
		FName RowName,
		const FGuLiSoldierDefinition& Fallback,
		bool& bOutEntireDefinitionFromDataTable);

	/** Convenience overload using MakeFallbackDefinition(). */
	static FGuLiSoldierDefinition Resolve(const UDataTable* DataTable, FName RowName);

	/** Pure row boundary exposed for focused validation tests and non-DataTable callers. */
	static FGuLiSoldierDefinition ResolveRow(
		const FGuLiStrikeCommanderSoldiersRow* Row,
		const FGuLiSoldierDefinition& Fallback);
	static FGuLiSoldierDefinition ResolveRow(
		const FGuLiStrikeCommanderSoldiersRow* Row,
		const FGuLiSoldierDefinition& Fallback,
		bool& bOutEntireDefinitionFromDataTable);
};
