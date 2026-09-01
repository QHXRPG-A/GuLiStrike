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
		Fallback.MaxHealth = 100.0f;
		Fallback.Model = Model;
		Fallback.Defense = 12.0f;
		return Fallback;
	}

	bool EqualDefinition(const FGuLiSoldierDefinition& A, const FGuLiSoldierDefinition& B)
	{
		return A.UnitTypeId == B.UnitTypeId
			&& A.MovementSpeedCmPerSecond == B.MovementSpeedCmPerSecond
			&& A.MaxHealth == B.MaxHealth
			&& A.Model == B.Model
			&& A.Defense == B.Defense;
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
	Row.Id = 2;
	Row.MovementSpeedCmPerSecond = 3600.0f;
	Row.MaxHealth = 300.5f;
	Row.ModelAsset = TSoftObjectPtr<UObject>(RowMesh.Get());
	Row.Defense = 8.0f;

	bool bEntireDefinitionFromDataTable = false;
	const FGuLiSoldierDefinition Resolved =
		FGuLiCommanderSoldierResolver::ResolveRow(&Row, Fallback, bEntireDefinitionFromDataTable);
	TestTrue(TEXT("source is DataTable"), bEntireDefinitionFromDataTable);
	TestEqual(TEXT("movement speed"), Resolved.MovementSpeedCmPerSecond, 3600.0f);
	TestEqual(TEXT("Fractional maximum health above the old uint8 ceiling is preserved"), Resolved.MaxHealth, 300.5f);
	TestEqual(TEXT("Stable unit identity comes from the row id"), Resolved.UnitTypeId, static_cast<uint16>(2u));
	TestTrue(TEXT("UStaticMesh model accepted"), Resolved.Model == RowMesh.Get());
	TestEqual(TEXT("defense"), Resolved.Defense, 8.0f);
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
	Row.MaxHealth = std::numeric_limits<float>::quiet_NaN();
	Row.ModelAsset = TSoftObjectPtr<UObject>(WrongModel.Get());
	Row.Defense = std::numeric_limits<float>::infinity();

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
	TestEqual(TEXT("fallback health"), Fallback.MaxHealth, 100.0f);
	TestTrue(TEXT("fallback model is a UStaticMesh"), IsValid(Fallback.Model));
	if (IsValid(Fallback.Model))
	{
		TestEqual(
			TEXT("fallback resolves to the dedicated Crowd mesh"),
			Fallback.Model->GetPathName(),
			FString(TEXT("/Game/Commander/Units/SM_CommanderFourFRobot_Crowd.SM_CommanderFourFRobot_Crowd")));
	}
	TestEqual(TEXT("fallback defense"), Fallback.Defense, 0.0f);
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
	TestEqual(TEXT("imported maximum health"), Resolved.MaxHealth, 100.0f);
	TestNotNull(TEXT("imported model resolves to a UStaticMesh"), Resolved.Model.Get());
	if (IsValid(Resolved.Model))
	{
		TestEqual(
			TEXT("imported model resolves to the dedicated Crowd mesh"),
			Resolved.Model->GetPathName(),
			FString(CrowdMeshPath));
	}
	TestEqual(TEXT("imported defense"), Resolved.Defense, 0.0f);
	const FGuLiSoldierDefinition SecondType = FGuLiCommanderSoldierResolver::Resolve(
		DataTable, TEXT("TestSoldierB"), Resolved, bEntireDefinitionFromDataTable);
	TestTrue(TEXT("Second type resolves without fallback"), bEntireDefinitionFromDataTable);
	TestEqual(TEXT("Second type has its own stable identity"), SecondType.UnitTypeId, static_cast<uint16>(2u));
	TestEqual(TEXT("Imported fractional health survives above 255"), SecondType.MaxHealth, 300.5f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGuLiCommanderSoldierHealthBoundaryTest,
	"GuLiStrike.Commander.Data.SoldierResolver.FloatHealthBoundaries",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiCommanderSoldierHealthBoundaryTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	TStrongObjectPtr<UStaticMesh> Model(NewObject<UStaticMesh>());
	const FGuLiSoldierDefinition Fallback = GuLiCommanderSoldierResolverTests::MakeTestFallback(Model.Get());
	FGuLiStrikeCommanderSoldiersRow Row;
	Row.Id = 2;
	Row.MovementSpeedCmPerSecond = 3600.0f;
	Row.ModelAsset = TSoftObjectPtr<UObject>(Model.Get());
	for (const float Candidate : {0.5f, 300.5f, 1000000000.0f})
	{
		Row.MaxHealth = Candidate;
		bool bFromTable = false;
		const FGuLiSoldierDefinition Result = FGuLiCommanderSoldierResolver::ResolveRow(&Row, Fallback, bFromTable);
		TestTrue(TEXT("Finite positive health within the safety bound is accepted"), bFromTable);
		TestEqual(TEXT("Valid float health is never rounded"), Result.MaxHealth, Candidate);
	}
	for (const float Candidate : {0.0f, -1.0f, 1000000064.0f,
		std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()})
	{
		Row.MaxHealth = Candidate;
		bool bFromTable = true;
		const FGuLiSoldierDefinition Result = FGuLiCommanderSoldierResolver::ResolveRow(&Row, Fallback, bFromTable);
		TestFalse(TEXT("Invalid health marks partial fallback"), bFromTable);
		TestEqual(TEXT("Only invalid health falls back"), Result.MaxHealth, Fallback.MaxHealth);
		TestEqual(TEXT("A valid second unit id survives health fallback"), Result.UnitTypeId, static_cast<uint16>(2u));
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
