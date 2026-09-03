// Copyright Epic Games, Inc. All Rights Reserved.

#include "Development/GuLiWingmanQARoleProbes.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Development/GuLiWingmanAcceptanceCatalog.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGuLiWingmanQARoleProductionProbeTest,
	"GuLiStrike.Wingman.QA.RoleProductionProbes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiWingmanQARoleProductionProbeTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	int32 SupportedRoleCount = 0;
	for (const FGuLiWingmanAcceptanceRoleDefinition& Role
		: FGuLiWingmanAcceptanceCatalogV2::GetRoles())
	{
		if (!GuLiWingmanQARoleProbes::Supports(Role.RoleId))
		{
			continue;
		}
		++SupportedRoleCount;
		const FGuLiWingmanQARoleProbeResult Result =
			GuLiWingmanQARoleProbes::Run(Role.RoleId);
		TestTrue(*FString::Printf(TEXT("%s is recognized"), *Role.RoleId.ToString()),
			Result.bRecognizedRole);
		TestTrue(*FString::Printf(TEXT("%s has no probe error: %s"),
			*Role.RoleId.ToString(), *Result.Error), Result.Error.IsEmpty());
		for (const FName Gate : Role.RequiredGateIds)
		{
			TestTrue(*FString::Printf(TEXT("%s observes required gate %s"),
				*Role.RoleId.ToString(), *Gate.ToString()),
				Result.PassedGateIds.Contains(Gate));
		}
	}
	TestEqual(TEXT("Exactly the 13 S3/S4/S6/S7 roles use synchronous production probes"),
		SupportedRoleCount, 13);
	TestTrue(TEXT("Death-before-pose is routed to a real client execution domain"),
		GuLiWingmanQARoleProbes::RequiresClientExecutionDomain(TEXT("S6-DeathBeforePose")));
	TestTrue(TEXT("Respawn-before-old-pose is routed to a real client execution domain"),
		GuLiWingmanQARoleProbes::RequiresClientExecutionDomain(TEXT("S6-RespawnBeforeOldPose")));
	TestFalse(TEXT("Authority-only correction probe stays on the Dedicated endpoint"),
		GuLiWingmanQARoleProbes::RequiresClientExecutionDomain(TEXT("S6-CorrectionReverse")));
	TestFalse(TEXT("Lease probes stay on the Dedicated endpoint"),
		GuLiWingmanQARoleProbes::RequiresClientExecutionDomain(TEXT("S7-LeaseLoss")));
	return true;
}
#endif
