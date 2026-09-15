#include "Gameplay/CommanderSkills/GuLiUnitSkillExecution.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiCommanderUnitSkillExecutionTest,
	"GuLiStrike.Commander.Skills.MixedSelectionIndependentResults", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGuLiCommanderUnitSkillExecutionTest::RunTest(const FString& Parameters)
{
	FGuLiActiveSkillDefinition Point; Point.SkillId = "Point"; Point.TargetMode = EGuLiActiveSkillTargetMode::GroundPoint;
	Point.RangeCentimeters = 1000; Point.CooldownSeconds = 8;
	FGuLiActiveSkillDefinition Self = Point; Self.SkillId = "Self"; Self.TargetMode = EGuLiActiveSkillTargetMode::Self;
	int32 Effects = 0;
	FVector CapturedGround;
	auto Apply = [&](const FGuLiActiveSkillExecutionContext& Context, const UDataAsset*)
	{
		++Effects; CapturedGround = Context.GroundPoint;
		FGuLiActiveSkillExecutionResult Result; Result.bSucceeded = true; return Result;
	};
	FGuLiUnitSkillCaster Caster; Caster.bEligible = true; Caster.bHasGroundPoint = true;
	Caster.Context.SoldierId = FGuLiSoldierId(1); Caster.Context.GroundPoint = FVector(1000, 0, 0);
	FGuLiActiveSkillRuntime Ready, Cooling; Cooling.ReadyAt = 20;
	const auto None = GuLiUnitSkillExecution::Execute(nullptr, Caster, Ready, 10, 50, Apply);
	TestTrue(TEXT("A unit with no skill invokes no executor"), None.Code == EGuLiActiveSkillResultCode::NoSkill && Effects == 0);
	const auto First = GuLiUnitSkillExecution::Execute(&Point, Caster, Ready, 10, 50, Apply);
	TestTrue(TEXT("A ready unit succeeds at the inclusive range boundary"), First.Code == EGuLiActiveSkillResultCode::Succeeded);
	TestEqual(TEXT("Only its authoritative simulation cooldown changes"), Ready.ReadyAt, 18.);
	TestEqual(TEXT("The owner sees a server-clock cooldown deadline"), First.ReadyAtServerSeconds, 58.);
	TestEqual(TEXT("The keypress ground point reaches the executor unchanged"), CapturedGround, Caster.Context.GroundPoint);
	Caster.Context.SoldierId = FGuLiSoldierId(2);
	const auto Second = GuLiUnitSkillExecution::Execute(&Point, Caster, Cooling, 10, 50, Apply);
	TestTrue(TEXT("Another selected unit can remain on cooldown"), Second.Code == EGuLiActiveSkillResultCode::Cooldown);
	TestEqual(TEXT("Partial success invokes one effect"), Effects, 1);
	FGuLiActiveSkillRuntime Fresh;
	Caster.Context.GroundPoint.X = 1001;
	TestTrue(TEXT("Out of range skips without moving or queuing"), GuLiUnitSkillExecution::Execute(&Point, Caster, Fresh, 10, 50, Apply).Code == EGuLiActiveSkillResultCode::OutOfRange);
	Caster.bHasGroundPoint = false;
	TestTrue(TEXT("A missing legal ground hit skips the point skill"), GuLiUnitSkillExecution::Execute(&Point, Caster, Fresh, 10, 50, Apply).Code == EGuLiActiveSkillResultCode::InvalidGround);
	TestTrue(TEXT("A self skill in the same selection needs no ground hit"), GuLiUnitSkillExecution::Execute(&Self, Caster, Fresh, 10, 50, Apply).Code == EGuLiActiveSkillResultCode::Succeeded);
	FGuLiActiveSkillRuntime Failed;
	auto Reject = [](const FGuLiActiveSkillExecutionContext&, const UDataAsset*) { return FGuLiActiveSkillExecutionResult{}; };
	TestTrue(TEXT("Rejected execution reports failure"), GuLiUnitSkillExecution::Execute(&Self, Caster, Failed, 10, 50, Reject).Code == EGuLiActiveSkillResultCode::ExecutionFailed);
	TestEqual(TEXT("An unsuccessful effect consumes no cooldown"), Failed.ReadyAt, 0.);
	Caster.bEligible = false;
	TestTrue(TEXT("Dead, locked or foreign soldiers are ineligible"), GuLiUnitSkillExecution::Execute(&Self, Caster, Failed, 10, 50, Apply).Code == EGuLiActiveSkillResultCode::Ineligible);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiCommanderSkillCatalogTest,
	"GuLiStrike.Commander.Skills.GlobalTacticsAndRequestIdentity", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGuLiCommanderSkillCatalogTest::RunTest(const FString& Parameters)
{
	auto* Catalog = NewObject<UGuLiCommanderSkillCatalog>();
	FGuLiActiveSkillDefinition Teleport; Teleport.SkillId = "Teleport"; Teleport.Scope = EGuLiActiveSkillScope::Global;
	Teleport.TargetMode = EGuLiActiveSkillTargetMode::TwoPoint; Teleport.MaximumLevel = 4; Teleport.ExecutorClass = UGuLiTeleportSkillExecutor::StaticClass();
	Catalog->Skills = {Teleport}; Catalog->GlobalSkills = {"Teleport"}; FString Error;
	TestTrue(TEXT("Global teleport is valid with every unit mapping empty"), Catalog->Validate(Error));
	TestNull(TEXT("Global tactics never become a unit Q skill implicitly"), Catalog->FindUnitSkill(1));
	Catalog->UnitSkills.Add(1, "Teleport");
	TestFalse(TEXT("A two-point global skill cannot be assigned to unit Q"), Catalog->Validate(Error));
	FGuLiActiveSkillRequest Request; Request.RequestId = FGuid::NewGuid(); Request.MatchEpoch = 7;
	Request.SelectionRevision = 12; Request.bHasGroundPoint = true; Request.GroundPoint = FVector(100, 200, 0);
	auto Copy = Request;
	TestTrue(TEXT("Exact duplicate payload identifies the original receipt"), Request.HasSameContent(Copy));
	++Copy.SelectionRevision;
	TestFalse(TEXT("A changed selection is different request content"), Request.HasSameContent(Copy));
	Copy = Request; Copy.GlobalSkillId = "Teleport";
	TestFalse(TEXT("Unit and global requests cannot share a receipt"), Request.HasSameContent(Copy));
	Copy = Request; Copy.GroundPoint.X += 1;
	TestFalse(TEXT("An ID reused with a new target is rejected"), Request.HasSameContent(Copy));
	return true;
}
#endif
