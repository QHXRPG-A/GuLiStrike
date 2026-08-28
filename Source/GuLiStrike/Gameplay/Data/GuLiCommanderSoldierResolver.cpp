// Copyright Epic Games, Inc. All Rights Reserved.

#include "Gameplay/Data/GuLiCommanderSoldierResolver.h"

#include "Gameplay/Data/Generated/GuLiStrikeCommanderTableRows.h"
#include "GuLiStrike.h"
#include "Engine/DataTable.h"
#include "Engine/StaticMesh.h"

namespace GuLiCommanderSoldierResolverPrivate
{
	// FMassMoveTargetFragment stores desired speed in FMassInt16Real (1 cm precision).
	constexpr float MaximumReasonableMovementSpeed = static_cast<float>(MAX_int16);
	constexpr float MaximumReasonableCombatValue = 1000000.0f;
	constexpr TCHAR FallbackModelPath[] =
		TEXT("/Game/Commander/Units/SM_CommanderFourFRobot.SM_CommanderFourFRobot");

	void LogWarningOnce(const FName Key, const FString& Message)
	{
		static TSet<FName> LoggedWarnings;
		if (LoggedWarnings.Contains(Key))
		{
			return;
		}

		LoggedWarnings.Add(Key);
		UE_LOG(LogGuLiStrike, Warning, TEXT("Commander Soldier data: %s"), *Message);
	}

	float ResolveFiniteRange(
		const float Candidate,
		const float Minimum,
		const float Maximum,
		const float Fallback,
		const FName WarningKey,
		const TCHAR* FieldName,
		bool& bInOutEntireDefinitionFromDataTable)
	{
		if (FMath::IsFinite(Candidate) && Candidate >= Minimum && Candidate <= Maximum)
		{
			return Candidate;
		}

		LogWarningOnce(
			WarningKey,
			FString::Printf(
				TEXT("invalid %s value %.9g; using fallback %.9g."),
				FieldName,
				Candidate,
				Fallback));
		bInOutEntireDefinitionFromDataTable = false;
		return Fallback;
	}
}

FGuLiSoldierDefinition FGuLiCommanderSoldierResolver::MakeFallbackDefinition()
{
	using namespace GuLiCommanderSoldierResolverPrivate;

	FGuLiSoldierDefinition Definition;
	const TSoftObjectPtr<UStaticMesh> FallbackMesh{ FSoftObjectPath(FallbackModelPath) };
	Definition.Model = FallbackMesh.LoadSynchronous();
	if (!Definition.Model)
	{
		LogWarningOnce(
			TEXT("FallbackModelMissing"),
			FString::Printf(TEXT("fallback static mesh '%s' could not be loaded."), FallbackModelPath));
	}
	return Definition;
}

FGuLiSoldierDefinition FGuLiCommanderSoldierResolver::Resolve(
	const UDataTable* DataTable,
	const FName RowName,
	const FGuLiSoldierDefinition& Fallback)
{
	bool bIgnoredEntireDefinitionFromDataTable = false;
	return Resolve(DataTable, RowName, Fallback, bIgnoredEntireDefinitionFromDataTable);
}

FGuLiSoldierDefinition FGuLiCommanderSoldierResolver::Resolve(
	const UDataTable* DataTable,
	const FName RowName,
	const FGuLiSoldierDefinition& Fallback,
	bool& bOutEntireDefinitionFromDataTable)
{
	using namespace GuLiCommanderSoldierResolverPrivate;
	bOutEntireDefinitionFromDataTable = false;

	if (!DataTable)
	{
		LogWarningOnce(TEXT("MissingTable"), TEXT("Soldier DataTable is missing; using C++ fallback."));
		return Fallback;
	}

	if (DataTable->GetRowStruct() != FGuLiStrikeCommanderSoldiersRow::StaticStruct())
	{
		LogWarningOnce(
			TEXT("WrongRowStruct"),
			TEXT("Soldier DataTable has the wrong row struct; using C++ fallback."));
		return Fallback;
	}

	if (RowName.IsNone())
	{
		LogWarningOnce(TEXT("MissingRowName"), TEXT("default Soldier row name is None; using C++ fallback."));
		return Fallback;
	}

	const FGuLiStrikeCommanderSoldiersRow* Row =
		DataTable->FindRow<FGuLiStrikeCommanderSoldiersRow>(RowName, TEXT("CommanderSoldierResolver"), false);
	if (!Row)
	{
		LogWarningOnce(
			FName(*FString::Printf(TEXT("MissingRow_%s"), *RowName.ToString())),
			FString::Printf(TEXT("row '%s' is missing; using C++ fallback."), *RowName.ToString()));
		return Fallback;
	}

	return ResolveRow(Row, Fallback, bOutEntireDefinitionFromDataTable);
}

FGuLiSoldierDefinition FGuLiCommanderSoldierResolver::Resolve(
	const UDataTable* DataTable,
	const FName RowName)
{
	return Resolve(DataTable, RowName, MakeFallbackDefinition());
}

FGuLiSoldierDefinition FGuLiCommanderSoldierResolver::ResolveRow(
	const FGuLiStrikeCommanderSoldiersRow* Row,
	const FGuLiSoldierDefinition& Fallback)
{
	bool bIgnoredEntireDefinitionFromDataTable = false;
	return ResolveRow(Row, Fallback, bIgnoredEntireDefinitionFromDataTable);
}

FGuLiSoldierDefinition FGuLiCommanderSoldierResolver::ResolveRow(
	const FGuLiStrikeCommanderSoldiersRow* Row,
	const FGuLiSoldierDefinition& Fallback,
	bool& bOutEntireDefinitionFromDataTable)
{
	using namespace GuLiCommanderSoldierResolverPrivate;
	bOutEntireDefinitionFromDataTable = false;

	if (!Row)
	{
		LogWarningOnce(TEXT("NullRow"), TEXT("Soldier row is null; using C++ fallback."));
		return Fallback;
	}

	FGuLiSoldierDefinition Resolved = Fallback;
	bOutEntireDefinitionFromDataTable = true;
	Resolved.MovementSpeedCmPerSecond = ResolveFiniteRange(
		Row->MovementSpeedCmPerSecond,
		UE_SMALL_NUMBER,
		MaximumReasonableMovementSpeed,
		Fallback.MovementSpeedCmPerSecond,
		TEXT("InvalidMovementSpeed"),
		TEXT("MovementSpeedCmPerSecond"),
		bOutEntireDefinitionFromDataTable);

	if (Row->MaxHealth >= 1 && Row->MaxHealth <= MAX_uint8)
	{
		Resolved.MaxHealth = static_cast<uint8>(Row->MaxHealth);
	}
	else
	{
		bOutEntireDefinitionFromDataTable = false;
		LogWarningOnce(
			TEXT("InvalidMaxHealth"),
			FString::Printf(
				TEXT("invalid MaxHealth value %d; using fallback %u (wire contract is uint8)."),
				Row->MaxHealth,
				Fallback.MaxHealth));
	}

	Resolved.AttackPower = ResolveFiniteRange(
		Row->AttackPower,
		0.0f,
		MaximumReasonableCombatValue,
		Fallback.AttackPower,
		TEXT("InvalidAttackPower"),
		TEXT("AttackPower"),
		bOutEntireDefinitionFromDataTable);
	Resolved.Defense = ResolveFiniteRange(
		Row->Defense,
		0.0f,
		MaximumReasonableCombatValue,
		Fallback.Defense,
		TEXT("InvalidDefense"),
		TEXT("Defense"),
		bOutEntireDefinitionFromDataTable);
	Resolved.AttackRangeCentimeters = ResolveFiniteRange(
		Row->AttackRangeCentimeters,
		0.0f,
		MaximumReasonableCombatValue,
		Fallback.AttackRangeCentimeters,
		TEXT("InvalidAttackRange"),
		TEXT("AttackRangeCentimeters"),
		bOutEntireDefinitionFromDataTable);

	UObject* LoadedModel = Row->ModelAsset.LoadSynchronous();
	if (UStaticMesh* StaticMesh = Cast<UStaticMesh>(LoadedModel))
	{
		Resolved.Model = StaticMesh;
	}
	else
	{
		bOutEntireDefinitionFromDataTable = false;
		const FString SourcePath = Row->ModelAsset.ToSoftObjectPath().ToString();
		LogWarningOnce(
			TEXT("InvalidModelType"),
			FString::Printf(
				TEXT("ModelAsset '%s' is missing or is not a UStaticMesh; keeping the current proxy fallback."),
				*SourcePath));
	}

	return Resolved;
}
