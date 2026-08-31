// Copyright Epic Games, Inc. All Rights Reserved.

#include "Gameplay/Data/GuLiCommanderSoldierResolver.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Gameplay/Data/Generated/GuLiStrikeCommanderTableRows.h"
#include "Engine/DataTable.h"
#include "Engine/StaticMesh.h"
#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UObjectGlobals.h"

#include <limits>

namespace GuLiCommanderSoldierResolverTests
{
	FGuLiSoldierDefinition MakeTestFallback(UStaticMesh* Model)
	{
		FGuLiSoldierDefinition Fallback;
		Fallback.MovementSpeedCmPerSecond = 3600.0f;
		Fallback.MaxHealth = 100u;
		Fallback.Model = Model;
		Fallback.AttackPower = 11.0f;
		Fallback.Defense = 12.0f;
		Fallback.AttackRangeCentimeters = 1300.0f;
		return Fallback;
	}

	bool EqualDefinition(const FGuLiSoldierDefinition& A, const FGuLiSoldierDefinition& B)
	{
		return A.MovementSpeedCmPerSecond == B.MovementSpeedCmPerSecond
			&& A.MaxHealth == B.MaxHealth
			&& A.Model == B.Model
			&& A.AttackPower == B.AttackPower
			&& A.Defense == B.Defense
			&& A.AttackRangeCentimeters == B.AttackRangeCentimeters;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGuLiCommanderSoldierValidRowTest,
	"GuLiStrike.Commander.Data.SoldierResolver.ValidRow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiCommanderSoldierValidRowTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	TStrongObjectPtr<UStaticMesh> FallbackMesh(NewObject<UStaticMesh>());
	TStrongObjectPtr<UStaticMesh> RowMesh(NewObject<UStaticMesh>());
	const FGuLiSoldierDefinition Fallback =
		GuLiCommanderSoldierResolverTests::MakeTestFallback(FallbackMesh.Get());

	FGuLiStrikeCommanderSoldiersRow Row;
	Row.MovementSpeedCmPerSecond = 3600.0f;
	Row.MaxHealth = 255;
	Row.ModelAsset = TSoftObjectPtr<UObject>(RowMesh.Get());
	Row.AttackPower = 25.0f;
	Row.Defense = 8.0f;
	Row.AttackRangeCentimeters = 7500.0f;

	bool bEntireDefinitionFromDataTable = false;
	const FGuLiSoldierDefinition Resolved =
		FGuLiCommanderSoldierResolver::ResolveRow(&Row, Fallback, bEntireDefinitionFromDataTable);
	TestTrue(TEXT("source is DataTable"), bEntireDefinitionFromDataTable);
	TestEqual(TEXT("movement speed"), Resolved.MovementSpeedCmPerSecond, 3600.0f);
	TestEqual(TEXT("uint8 maximum health accepted"), Resolved.MaxHealth, static_cast<uint8>(255u));
	TestTrue(TEXT("UStaticMesh model accepted"), Resolved.Model == RowMesh.Get());
	TestEqual(TEXT("attack power"), Resolved.AttackPower, 25.0f);
	TestEqual(TEXT("defense"), Resolved.Defense, 8.0f);
	TestEqual(TEXT("attack range"), Resolved.AttackRangeCentimeters, 7500.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGuLiCommanderSoldierInvalidValuesFallbackTest,
	"GuLiStrike.Commander.Data.SoldierResolver.InvalidValuesFallback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiCommanderSoldierInvalidValuesFallbackTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	TStrongObjectPtr<UStaticMesh> FallbackMesh(NewObject<UStaticMesh>());
	TStrongObjectPtr<UDataTable> WrongModel(NewObject<UDataTable>());
	const FGuLiSoldierDefinition Fallback =
		GuLiCommanderSoldierResolverTests::MakeTestFallback(FallbackMesh.Get());

	FGuLiStrikeCommanderSoldiersRow Row;
	Row.MovementSpeedCmPerSecond = -1.0f;
	Row.MaxHealth = 256;
	Row.ModelAsset = TSoftObjectPtr<UObject>(WrongModel.Get());
	Row.AttackPower = -1.0f;
	Row.Defense = std::numeric_limits<float>::infinity();
	Row.AttackRangeCentimeters = std::numeric_limits<float>::quiet_NaN();

	bool bEntireDefinitionFromDataTable = true;
	const FGuLiSoldierDefinition Resolved =
		FGuLiCommanderSoldierResolver::ResolveRow(&Row, Fallback, bEntireDefinitionFromDataTable);
	TestFalse(TEXT("partial fallback is reported"), bEntireDefinitionFromDataTable);
	TestTrue(
		TEXT("every invalid field falls back"),
		GuLiCommanderSoldierResolverTests::EqualDefinition(Resolved, Fallback));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGuLiCommanderSoldierMissingTableAndRowFallbackTest,
	"GuLiStrike.Commander.Data.SoldierResolver.MissingTableAndRowFallback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiCommanderSoldierMissingTableAndRowFallbackTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	TStrongObjectPtr<UStaticMesh> FallbackMesh(NewObject<UStaticMesh>());
	const FGuLiSoldierDefinition Fallback =
		GuLiCommanderSoldierResolverTests::MakeTestFallback(FallbackMesh.Get());

	bool bEntireDefinitionFromDataTable = true;
	const FGuLiSoldierDefinition MissingTable = FGuLiCommanderSoldierResolver::Resolve(
		nullptr,
		TEXT("DefaultSoldier"),
		Fallback,
		bEntireDefinitionFromDataTable);
	TestFalse(TEXT("missing table source is fallback"), bEntireDefinitionFromDataTable);
	TestTrue(
		TEXT("missing table uses fallback"),
		GuLiCommanderSoldierResolverTests::EqualDefinition(MissingTable, Fallback));

	TStrongObjectPtr<UDataTable> EmptyTable(NewObject<UDataTable>());
	EmptyTable->RowStruct = FGuLiStrikeCommanderSoldiersRow::StaticStruct();
	bEntireDefinitionFromDataTable = true;
	const FGuLiSoldierDefinition MissingRow = FGuLiCommanderSoldierResolver::Resolve(
		EmptyTable.Get(),
		TEXT("DefaultSoldier"),
		Fallback,
		bEntireDefinitionFromDataTable);
	TestFalse(TEXT("missing row source is fallback"), bEntireDefinitionFromDataTable);
	TestTrue(
		TEXT("missing row uses fallback"),
		GuLiCommanderSoldierResolverTests::EqualDefinition(MissingRow, Fallback));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGuLiCommanderSoldierDefaultBaselineTest,
	"GuLiStrike.Commander.Data.SoldierResolver.DefaultBaseline",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiCommanderSoldierDefaultBaselineTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FGuLiSoldierDefinition Fallback = FGuLiCommanderSoldierResolver::MakeFallbackDefinition();
	TestEqual(TEXT("fallback speed is the 36 m/s baseline"), Fallback.MovementSpeedCmPerSecond, 3600.0f);
	TestEqual(TEXT("fallback health"), Fallback.MaxHealth, static_cast<uint8>(100u));
	TestTrue(TEXT("fallback model is a UStaticMesh"), IsValid(Fallback.Model));
	if (IsValid(Fallback.Model))
	{
		TestEqual(
			TEXT("fallback resolves to the dedicated Crowd mesh"),
			Fallback.Model->GetPathName(),
			FString(TEXT("/Game/Commander/Units/SM_CommanderFourFRobot_Crowd.SM_CommanderFourFRobot_Crowd")));
	}
	TestEqual(TEXT("fallback attack"), Fallback.AttackPower, 0.0f);
	TestEqual(TEXT("fallback defense"), Fallback.Defense, 0.0f);
	TestEqual(TEXT("fallback attack range"), Fallback.AttackRangeCentimeters, 0.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGuLiCommanderSoldierImportedBaselineTest,
	"GuLiStrike.Commander.Data.SoldierResolver.ImportedBaseline",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiCommanderSoldierImportedBaselineTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	constexpr TCHAR TablePath[] =
		TEXT("/Game/GuLiStrike/Data/DT_GuLiStrikeCommander_Soldiers.DT_GuLiStrikeCommander_Soldiers");
	constexpr TCHAR CrowdMeshPath[] =
		TEXT("/Game/Commander/Units/SM_CommanderFourFRobot_Crowd.SM_CommanderFourFRobot_Crowd");

	const UDataTable* DataTable = LoadObject<UDataTable>(nullptr, TablePath);
	if (!TestNotNull(TEXT("imported Soldier DataTable is available"), DataTable))
	{
		return false;
	}

	bool bEntireDefinitionFromDataTable = false;
	const FGuLiSoldierDefinition Resolved = FGuLiCommanderSoldierResolver::Resolve(
		DataTable,
		TEXT("DefaultSoldier"),
		FGuLiCommanderSoldierResolver::MakeFallbackDefinition(),
		bEntireDefinitionFromDataTable);

	TestTrue(TEXT("DefaultSoldier resolves entirely from the imported row"), bEntireDefinitionFromDataTable);
	TestEqual(TEXT("imported movement speed"), Resolved.MovementSpeedCmPerSecond, 3600.0f);
	TestEqual(TEXT("imported maximum health"), Resolved.MaxHealth, static_cast<uint8>(100u));
	TestNotNull(TEXT("imported model resolves to a UStaticMesh"), Resolved.Model.Get());
	if (IsValid(Resolved.Model))
	{
		TestEqual(
			TEXT("imported model resolves to the dedicated Crowd mesh"),
			Resolved.Model->GetPathName(),
			FString(CrowdMeshPath));
	}
	TestEqual(TEXT("imported attack power"), Resolved.AttackPower, 0.0f);
	TestEqual(TEXT("imported defense"), Resolved.Defense, 0.0f);
	TestEqual(TEXT("imported attack range"), Resolved.AttackRangeCentimeters, 0.0f);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
