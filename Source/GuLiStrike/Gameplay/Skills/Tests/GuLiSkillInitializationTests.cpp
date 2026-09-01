#include "Gameplay/Skills/GuLiArmySkillSubsystem.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Battle/Framework/GuLiBattleGameState.h"
#include "Gameplay/Data/GuLiCommanderDataSubsystem.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiSkillEarlyCommitTest,
	"GuLiStrike.Skills.Initialization.EarlyCommitPreservesDefaults", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiSkillEarlyCommitTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("An isolated authority game World exists"), World)) return false;
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	auto* Bridge = World->GetSubsystem<UGuLiArmySkillSubsystem>();
	const auto* Data = World->GetSubsystem<UGuLiCommanderDataSubsystem>();
	if (!TestNotNull(TEXT("The actual bridge initialized with pending defaults"), Bridge)
		|| !TestNotNull(TEXT("The actual table catalog exists"), Data)) return false;
	if (!TestTrue(TEXT("The imported catalog is valid"), Data->IsSkillCatalogValid())) return false;
	if (!TestNull(TEXT("This early simulation step has no GameState yet"), World->GetGameState())) return false;
	Bridge->CommitPendingChanges();
	Bridge->CommitPendingChanges();
	TestTrue(TEXT("No configuration is exposed before match initialization"), Bridge->GetResolvedSkills().IsEmpty());
	auto* State = World->SpawnActor<AGuLiBattleGameState>();
	if (!TestNotNull(TEXT("A real BattleGameState can be installed later"), State)) return false;
	World->SetGameState(State);
	TestEqual(TEXT("Fresh GameState has not initialized its epoch"), State->GetMatchEpoch(), static_cast<uint32>(0));
	Bridge->CommitPendingChanges();
	TestTrue(TEXT("An epoch-zero commit still exposes no configuration"), Bridge->GetResolvedSkills().IsEmpty());
	State->InitializeServerMatchState();
	if (!TestTrue(TEXT("The public server API creates a nonzero epoch"), State->GetMatchEpoch() != 0)) return false;
	Bridge->CommitPendingChanges();
	int32 ExpectedProfiles = 0;
	for (const auto& Config : Data->GetUnitSkillConfigs())
	{
		if (!Config.bDefault) continue;
		for (EGuLiTeam Team : {EGuLiTeam::Red, EGuLiTeam::Blue})
		{
			++ExpectedProfiles;
			const auto* Profile = Bridge->FindResolvedSkill(Team, Config.UnitTypeId, Config.SlotId);
			if (!TestNotNull(TEXT("Every team's initial unit/slot survived all early commits"), Profile)) return false;
			TestEqual(TEXT("Initial skill remains the authored default"), Profile->SkillId, Config.SkillId);
			TestEqual(TEXT("Initial damage remains the authored base"), Profile->Damage, Config.Damage);
			TestTrue(TEXT("The first valid commit assigned a real revision"), Profile->Revision > 0);
		}
	}
	TestTrue(TEXT("The regression exercised at least one real default slot"), ExpectedProfiles > 0);
	TestEqual(TEXT("Both teams' complete initial configuration was published"), Bridge->GetResolvedSkills().Num(), ExpectedProfiles);
	return true;
}
#endif
