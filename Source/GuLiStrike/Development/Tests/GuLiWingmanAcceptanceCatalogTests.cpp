// Copyright Epic Games, Inc. All Rights Reserved.

#include "Development/GuLiWingmanAcceptanceCatalog.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

namespace GuLiWingmanAcceptanceCatalogTests
{
	TArray<FGuLiWingmanAcceptanceRunEvidence> MakePassingCampaign()
	{
		TArray<FGuLiWingmanAcceptanceRunEvidence> Runs;
		const FString Hash = FGuLiWingmanAcceptanceCatalogV2::GetCatalogHashSha256();
		for (const FGuLiWingmanAcceptanceRoleDefinition& Role
			: FGuLiWingmanAcceptanceCatalogV2::GetRoles())
		{
			FGuLiWingmanAcceptanceRunEvidence& Run = Runs.AddDefaulted_GetRef();
			Run.RunId = FString::Printf(TEXT("run-%02d"), Runs.Num());
			Run.RoleId = Role.RoleId;
			Run.CatalogVersion = FGuLiWingmanAcceptanceCatalogV2::Version;
			Run.CatalogHash = Hash;
			Run.EvidencePath = FString::Printf(TEXT("WingmanQA/campaign/%s"), *Run.RunId);
			Run.bRunCompleted = true;
			Run.bRunPassed = true;
			for (const FName Gate : FGuLiWingmanAcceptanceCatalogV2::GetCommonRequiredGateIds())
			{
				Run.GateSampleCounts.Add(Gate, 1);
			}
			for (const FName Gate : Role.RequiredGateIds)
			{
				Run.GateSampleCounts.Add(Gate, 1);
			}
			for (const FName Invariant : FGuLiWingmanAcceptanceCatalogV2::GetInvariantKeys())
			{
				Run.InvariantCounts.Add(Invariant, 0);
			}
		}
		return Runs;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiWingmanAcceptanceCatalogShapeTest,
	"GuLiStrike.Wingman.QA.AcceptanceCatalogV2.ShapeAndHash",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiWingmanAcceptanceCatalogShapeTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const TArray<FGuLiWingmanAcceptanceRoleDefinition>& Roles =
		FGuLiWingmanAcceptanceCatalogV2::GetRoles();
	TestEqual(TEXT("Catalog V2 defines exactly 22 formal roles"), Roles.Num(), 22);
	TestEqual(TEXT("Portable SHA-256 matches the public abc vector"),
		FGuLiWingmanAcceptanceCatalogV2::ComputeSha256Hex(TEXT("abc")),
		FString(TEXT("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad")));
	TSet<FName> UniqueRoles;
	for (const FGuLiWingmanAcceptanceRoleDefinition& Role : Roles)
	{
		UniqueRoles.Add(Role.RoleId);
		TestFalse(TEXT("Every role has at least one role-specific gate"), Role.RequiredGateIds.IsEmpty());
	}
	TestEqual(TEXT("All formal role names are unique"), UniqueRoles.Num(), Roles.Num());
	TestEqual(TEXT("SHA-256 is a 64-character lowercase hex digest"),
		FGuLiWingmanAcceptanceCatalogV2::GetCatalogHashSha256().Len(),
		64);
	TestEqual(TEXT("Canonical catalog hashing is deterministic"),
		FGuLiWingmanAcceptanceCatalogV2::GetCatalogHashSha256(),
		FGuLiWingmanAcceptanceCatalogV2::GetCatalogHashSha256());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiWingmanAcceptanceCatalogFailClosedTest,
	"GuLiStrike.Wingman.QA.AcceptanceCatalogV2.FailClosedCampaignValidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiWingmanAcceptanceCatalogFailClosedTest::RunTest(const FString& Parameters)
{
	using namespace GuLiWingmanAcceptanceCatalogTests;
	(void)Parameters;

	TArray<FString> Errors;
	TArray<FGuLiWingmanAcceptanceRunEvidence> Runs = MakePassingCampaign();
	TestTrue(TEXT("A complete exact 22-role campaign validates"),
		FGuLiWingmanAcceptanceCatalogV2::ValidateCampaign(Runs, Errors));
	TestEqual(TEXT("Passing campaign has no diagnostics"), Errors.Num(), 0);

	TArray<FGuLiWingmanAcceptanceRunEvidence> MissingRole = Runs;
	MissingRole.Pop();
	TestFalse(TEXT("A missing role fails closed"),
		FGuLiWingmanAcceptanceCatalogV2::ValidateCampaign(MissingRole, Errors));

	TArray<FGuLiWingmanAcceptanceRunEvidence> DuplicateRole = Runs;
	DuplicateRole.Last().RoleId = DuplicateRole[0].RoleId;
	TestFalse(TEXT("A duplicate role fails closed"),
		FGuLiWingmanAcceptanceCatalogV2::ValidateCampaign(DuplicateRole, Errors));

	TArray<FGuLiWingmanAcceptanceRunEvidence> EmptyRequiredGate = Runs;
	EmptyRequiredGate[0].GateSampleCounts.FindOrAdd(FName(TEXT("MANIFEST_VALID"))) = 0;
	TestFalse(TEXT("An empty required gate fails closed"),
		FGuLiWingmanAcceptanceCatalogV2::ValidateCampaign(EmptyRequiredGate, Errors));

	TArray<FGuLiWingmanAcceptanceRunEvidence> LegacyGate = Runs;
	LegacyGate[0].GateSampleCounts.Add(TEXT("SERVER_MOTION_FALLBACK_BRIDGE"), 1);
	TestFalse(TEXT("A legacy/unknown gate fails closed"),
		FGuLiWingmanAcceptanceCatalogV2::ValidateCampaign(LegacyGate, Errors));

	TArray<FGuLiWingmanAcceptanceRunEvidence> NonZeroInvariant = Runs;
	NonZeroInvariant[0].InvariantCounts.FindOrAdd(
		FName(TEXT("SERVER_WINGMAN_STEERING_EXECUTED"))) = 1;
	TestFalse(TEXT("Any non-zero formal invariant fails closed"),
		FGuLiWingmanAcceptanceCatalogV2::ValidateCampaign(NonZeroInvariant, Errors));

	TArray<FGuLiWingmanAcceptanceRunEvidence> CatalogDrift = Runs;
	CatalogDrift[0].CatalogHash = FString::ChrN(64, TEXT('0'));
	TestFalse(TEXT("Catalog hash drift fails closed"),
		FGuLiWingmanAcceptanceCatalogV2::ValidateCampaign(CatalogDrift, Errors));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
