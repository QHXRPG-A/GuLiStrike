// Copyright Epic Games, Inc. All Rights Reserved.

#include "Gameplay/Data/GuLiCommanderSoldierResolver.h"

#include "Gameplay/Data/Generated/GuLiStrikeCommanderTableRows.h"
#include "GuLiStrike.h"
#include "Engine/DataTable.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"

namespace GuLiCommanderSoldierResolverPrivate
{
	// FMassMoveTargetFragment stores desired speed in FMassInt16Real (1 cm precision).
	constexpr float MaximumReasonableMovementSpeed = static_cast<float>(MAX_int16);
	constexpr float MaximumReasonableCombatValue = 1000000.0f;
	constexpr TCHAR FallbackModelPath[] =
		TEXT("/Game/Commander/Units/SM_CommanderFourFRobot_Crowd.SM_CommanderFourFRobot_Crowd");

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
	Resolved.DisplayName = FText::FromString(Row->DisplayName);
	bOutEntireDefinitionFromDataTable = true;
	if (Row->Id > 0 && Row->Id <= MAX_uint16)
	{
		Resolved.UnitTypeId = static_cast<uint16>(Row->Id);
	}
	else
	{
		bOutEntireDefinitionFromDataTable = false;
		LogWarningOnce(TEXT("InvalidUnitTypeId"), FString::Printf(
			TEXT("invalid Soldier Id %d; using fallback %u."), Row->Id, Fallback.UnitTypeId));
	}
	Resolved.MovementSpeedCmPerSecond = ResolveFiniteRange(
		Row->MovementSpeedCmPerSecond,
		UE_SMALL_NUMBER,
		MaximumReasonableMovementSpeed,
		Fallback.MovementSpeedCmPerSecond,
		TEXT("InvalidMovementSpeed"),
		TEXT("MovementSpeedCmPerSecond"),
		bOutEntireDefinitionFromDataTable);

	// War Machine's stable Soldiers Id is 2. Width is already in world meters.
	// Other units keep the existing radius; neither scale nor the gap column is applied.
	Resolved.MassAvoidanceRadiusCm = 0.0f;
	if (Resolved.UnitTypeId == 2u)
	{
		const float Radius = Row->ModelWidthMeters * 50.0f;
		if (FMath::IsFinite(Radius) && Radius > 0.0f)
		{
			Resolved.MassAvoidanceRadiusCm = Radius;
		}
		else
		{
			Resolved.MassAvoidanceRadiusCm = 625.0f;
			bOutEntireDefinitionFromDataTable = false;
			LogWarningOnce(TEXT("InvalidWarMachineWidth"), FString::Printf(
				TEXT("Soldier Id %d has invalid ModelWidthMeters %.9g; using War Machine radius 625cm."),
				Row->Id, Row->ModelWidthMeters));
		}
	}

	if (FMath::IsFinite(Row->MaxHealth) && Row->MaxHealth > 0.0f
		&& Row->MaxHealth <= MaximumSupportedHealth)
	{
		Resolved.MaxHealth = Row->MaxHealth;
	}
	else
	{
		bOutEntireDefinitionFromDataTable = false;
		LogWarningOnce(
			TEXT("InvalidMaxHealth"),
			FString::Printf(
				TEXT("invalid MaxHealth value %.9g; using fallback %.9g (valid range: finite, >0, <=1e9)."),
				Row->MaxHealth,
				Fallback.MaxHealth));
	}

	Resolved.Defense = ResolveFiniteRange(
		Row->Defense,
		0.0f,
		MaximumReasonableCombatValue,
		Fallback.Defense,
		TEXT("InvalidDefense"),
		TEXT("Defense"),
		bOutEntireDefinitionFromDataTable);

	Resolved.ActorClass = Row->ActorClass.LoadSynchronous();
	Resolved.PresentationClass = Row->PresentationClass.LoadSynchronous();
	if (!FMath::IsFinite(Row->PresentationScale) || Row->PresentationScale <= 0.0f)
	{
		bOutEntireDefinitionFromDataTable = false;
		return Resolved;
	}
	Resolved.PresentationScale = Row->PresentationScale;
	if (!Row->ActorClass.IsNull())
	{
		Resolved.Model = nullptr;
		bOutEntireDefinitionFromDataTable &= Resolved.ActorClass
			&& Resolved.ActorClass->IsChildOf(AActor::StaticClass())
			&& Resolved.PresentationClass && Resolved.PresentationClass->IsChildOf(AActor::StaticClass());
		return Resolved;
	}
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
