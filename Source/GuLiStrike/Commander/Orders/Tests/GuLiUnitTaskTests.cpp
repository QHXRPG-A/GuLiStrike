#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Commander/Orders/GuLiUnitTaskSubsystem.h"
#include "Commander/Orders/GuLiSpecialTaskCatalog.h"
#include "Commander/Orders/GuLiSpecialTaskExecutor.h"
#include "Gameplay/Data/Generated/GuLiStrikeSpecialTasksTableRows.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/DataTable.h"

namespace GuLiOrderTests
{
	constexpr auto Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
	FGuLiUnitTaskState Working()
	{
		FGuLiUnitTaskState State;
		State.Context.Unit = FGuLiTaskUnitId::Soldier(FGuLiSoldierId(1));
		State.Grants = {{3,EGuLiTaskLifetime::InitialOnce,false},{1,EGuLiTaskLifetime::Persistent,false}};
		FGuLiTaskExecution Auto; Auto.bAutomatic = true; Auto.Command.Kind = EGuLiUnitTaskKind::Special; Auto.Command.SpecialTaskId = 3;
		State.Active.Emplace(Auto); return State;
	}
	FGuLiUnitTaskCommand Command(EGuLiTaskDisposition How = EGuLiTaskDisposition::Replace)
	{
		FGuLiUnitTaskCommand C; C.CommandId = C.SelectionRevision = 1; C.Disposition = How; C.Target = FVector(1000,2000,0); return C;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiOrderLifecycleTest, "GuLiStrike.Commander.Orders.InitialAndPersistentLifecycle", GuLiOrderTests::Flags)
bool FGuLiOrderLifecycleTest::RunTest(const FString&)
{
	using namespace GuLiOrderTests;
	auto A = Working(); auto Other = Working();
	TestTrue(TEXT("Valid ordinary command accepted"), UGuLiUnitTaskSubsystem::Admit(A, Command(),32));
	TestTrue(TEXT("Initial grant consumed at takeover"), A.Grants[0].bConsumed);
	TestFalse(TEXT("Persistent grant retained"), A.Grants[1].bConsumed);
	TestFalse(TEXT("Uninvolved unit keeps initial eligibility"), Other.Grants[0].bConsumed);
	A.Active.Reset(); A.Queue.Reset();
	TestTrue(TEXT("Manual completion cannot restore consumed grant"), A.Grants[0].bConsumed);
	TestTrue(TEXT("Stop accepted"), UGuLiUnitTaskSubsystem::Admit(A,Command(EGuLiTaskDisposition::Stop),32));
	TestTrue(TEXT("Stop persists with no queued work"), A.bStopped && A.Queue.IsEmpty());
	TestTrue(TEXT("New valid task unblocks automatic work"), UGuLiUnitTaskSubsystem::Admit(A,Command(EGuLiTaskDisposition::Append),32) && !A.bStopped);
	TestTrue(TEXT("Unblocking does not resurrect initial work"), A.Grants[0].bConsumed);
	auto NewUnit = Working(); NewUnit.Context.Unit.Id = 2;
	TestFalse(TEXT("New unit has independent initial eligibility"), NewUnit.Grants[0].bConsumed);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiOrderCapacityTest, "GuLiStrike.Commander.Orders.AppendCapacityAndSafeCancellation", GuLiOrderTests::Flags)
bool FGuLiOrderCapacityTest::RunTest(const FString&)
{
	using namespace GuLiOrderTests;
	auto S = Working(); const auto Append = Command(EGuLiTaskDisposition::Append);
	for (int I=0; I<32; ++I) TestTrue(TEXT("Automatic task does not consume manual capacity"),UGuLiUnitTaskSubsystem::Admit(S,Append,32));
	const auto Version = S.Version;
	TestFalse(TEXT("33rd manual command rejected"),UGuLiUnitTaskSubsystem::Admit(S,Append,32));
	TestEqual(TEXT("Rejected append does not mutate version"),S.Version,Version);
	TestFalse(TEXT("Append waits for safe work unit boundary"),S.Grants[0].bConsumed);
	TestTrue(TEXT("Executor receives yield intent"),S.Active->bYieldRequested);
	TestTrue(TEXT("Replace can supersede a full queue"),UGuLiUnitTaskSubsystem::Admit(S,Command(),32));
	TestTrue(TEXT("Current unsafe task survives while future intents are replaced"),S.bCancelPending && S.Active.IsSet() && S.Queue.Num()==1);
	TestEqual(TEXT("Cancelled current task does not take a queue slot"),S.ManualTaskCount(),1);
	UGuLiUnitTaskSubsystem::Admit(S,Command(EGuLiTaskDisposition::Stop),32);
	TestTrue(TEXT("Stop overrides latest pending replacement and preserves safe exit"),S.bStopped && S.bCancelPending && S.Queue.IsEmpty());
	UGuLiUnitTaskSubsystem::Admit(S,Command(),32);
	TestTrue(TEXT("New intent during safe stop replaces future work only"),!S.bStopped && S.bCancelPending && S.Queue.Num()==1);
	auto Manual = Working(); Manual.Active->bAutomatic=false; Manual.Queue.SetNum(31);
	TestFalse(TEXT("Current manual task is included in 32 limit"),UGuLiUnitTaskSubsystem::Admit(Manual,Append,32));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiOrderCommandTest, "GuLiStrike.Commander.Orders.UntrustedCommandValidation", GuLiOrderTests::Flags)
bool FGuLiOrderCommandTest::RunTest(const FString&)
{
	auto C = GuLiOrderTests::Command(); TestTrue(TEXT("Finite move valid"),C.IsWellFormed());
	C.CommandId=0; TestFalse(TEXT("No anonymous/replay-zero input"),C.IsWellFormed()); C.CommandId=1;
	C.Kind=EGuLiUnitTaskKind::Special; TestFalse(TEXT("Special task requires catalog ID"),C.IsWellFormed()); C.SpecialTaskId=1;
	TestTrue(TEXT("Wire contract carries ID, never class path"),C.IsWellFormed());
	C.Kind=EGuLiUnitTaskKind::Transit; TestFalse(TEXT("Transport requires territory"),C.IsWellFormed());
	C.Disposition=static_cast<EGuLiTaskDisposition>(255); TestFalse(TEXT("Unknown disposition rejected"),C.IsWellFormed());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiSpecialTaskCatalogTest, "GuLiStrike.Commander.Orders.ExcelNativeCatalog", GuLiOrderTests::Flags)
bool FGuLiSpecialTaskCatalogTest::RunTest(const FString&)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game,false);
	if (!TestNotNull(TEXT("Authority world"),World)) return false;
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	auto* Catalog = World->GetSubsystem<UGuLiSpecialTaskCatalog>();
	auto* Table = GetDefault<UGuLiUnitTaskSettings>()->SpecialTaskTable.LoadSynchronous();
	if (TestNotNull(TEXT("Excel-generated DataTable imported"),Table) && TestNotNull(TEXT("Native catalog"),Catalog))
	{
		TestTrue(*Catalog->GetError(),Catalog->IsValidCatalog());
		TestEqual(TEXT("Exactly three configured task definitions"),Catalog->GetDefinitions().Num(),3);
		const auto* Mining=Catalog->Find(1); const auto* Building=Catalog->Find(2); const auto* Advance=Catalog->Find(3);
		if (TestNotNull(TEXT("Mining id 1"),Mining) && TestNotNull(TEXT("Construction id 2"),Building) && TestNotNull(TEXT("Advance id 3"),Advance))
		{
			TestTrue(TEXT("Excel selects concrete native executor"),Mining->Executor->IsA<UGuLiMiningSpecialTaskExecutor>());
			TestTrue(TEXT("Miner binding"),Mining->UnitTypes == TArray<uint16>{3});
			TestTrue(TEXT("Builder binding"),Building->UnitTypes == TArray<uint16>{4});
			TestTrue(TEXT("Advance binding and lifetime"),Advance->UnitTypes == TArray<uint16>{1,2} && Advance->Lifetime==EGuLiTaskLifetime::InitialOnce);
		}
		auto Row=*Table->FindRow<FGuLiStrikeSpecialTasksTasksRow>(TEXT("Mining"),TEXT("Test"));
		auto* Invalid=NewObject<UDataTable>(World); Invalid->RowStruct=FGuLiStrikeSpecialTasksTasksRow::StaticStruct();
		auto Reject = [&](const TCHAR* Label, const FGuLiStrikeSpecialTasksTasksRow& R)
		{
			Invalid->EmptyTable(); Invalid->AddRow(TEXT("Bad"),R);
			TestFalse(Label,Catalog->Load(Invalid)); TestFalse(TEXT("Diagnostic names invalid field"),Catalog->GetError().IsEmpty());
			TestTrue(TEXT("Bad table leaves no fallback mapping"),Catalog->GetDefinitions().IsEmpty());
		};
		auto R=Row; R.ExecutorClass=FSoftObjectPath(TEXT("/Script/CoreUObject.Object")); Reject(TEXT("Wrong base rejected"),R);
		R=Row; R.ExecutorClass=UGuLiSpecialTaskExecutor::StaticClass(); Reject(TEXT("Abstract class rejected"),R);
		R=Row; R.ExecutorClass=UGuLiConstructionSpecialTaskExecutor::StaticClass(); Reject(TEXT("Wrong executor capability rejected"),R);
		R=Row; R.TaskTag=TEXT("Task.Special.Unregistered"); Reject(TEXT("Unregistered tag rejected"),R);
		R=Row; R.ApplicableUnitIds=TEXT("9999"); Reject(TEXT("Missing Soldier reference rejected"),R);
		R=Row; R.ApplicableUnitIds=TEXT("1"); Reject(TEXT("Mass soldier cannot run mining executor"),R);
		R=Row; R.LifetimePolicy=TEXT("Unknown"); Reject(TEXT("Unknown lifetime rejected"),R);
		R=Row; R.AutoActivate=false; Invalid->EmptyTable(); Invalid->AddRow(TEXT("Manual"),R);
		TestTrue(TEXT("AutoActivate independent of lifetime and class"),Catalog->Load(Invalid));
		TestTrue(TEXT("Original catalog reload succeeds in fixture"),Catalog->Load(Table));
	}
	World->DestroyWorld(false); GEngine->DestroyWorldContext(World); return true;
}
#endif
