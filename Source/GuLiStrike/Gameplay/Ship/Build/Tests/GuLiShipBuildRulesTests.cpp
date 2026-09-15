#include "Gameplay/Ship/Build/GuLiShipBuildCatalog.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"

namespace GuLiShipBuildTests
{
	FGuLiShipRuleExpression Leaf(EGuLiShipRuleOp Op, FName Id, int32 Required = 1)
	{
		FGuLiShipRuleExpression Rule; Rule.Root = 0;
		auto& Node = Rule.Nodes.AddDefaulted_GetRef(); Node.Op = Op; Node.SubjectId = Id; Node.Required = Required;
		return Rule;
	}
	void Add(UGuLiShipBuildCatalog& Catalog, FName Id, FName Route, int32 Level = 1, FName Replaces = NAME_None)
	{
		auto& Node = Catalog.Upgrades.AddDefaulted_GetRef();
		Node.NodeId = Id; Node.RouteId = Route; Node.Level = Level; Node.ReplacesNodeId = Replaces; Node.GroupConfigurationId = Id;
		auto& Group = Catalog.Groups.AddDefaulted_GetRef(); Group.ConfigurationId = Id; Group.GroupId = Route;
	}
	FGuLiShipRuleExpression Compound(EGuLiShipRuleOp Op, TArray<FName> Ids, int32 Required = 1)
	{
		FGuLiShipRuleExpression Rule; Rule.Root = 0; Rule.Nodes.AddDefaulted();
		Rule.Nodes[0].Op = Op; Rule.Nodes[0].Required = Required;
		for (FName Id : Ids)
		{
			const int32 Index = Rule.Nodes.AddDefaulted();
			Rule.Nodes[0].Children.Add(Index); Rule.Nodes[Index].Op = EGuLiShipRuleOp::ChosenNode; Rule.Nodes[Index].SubjectId = Id;
		}
		return Rule;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiShipBuildRulesTest,
	"GuLiStrike.Ship.Build.ExpressionsHistoryAndExclusions", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGuLiShipBuildRulesTest::RunTest(const FString& Parameters)
{
	using namespace GuLiShipBuildTests;
	auto* Catalog = NewObject<UGuLiShipBuildCatalog>();
	Add(*Catalog, "01", "Turret"); Add(*Catalog, "02", "Turret", 2, "01"); Add(*Catalog, "03", "Turret", 3, "02");
	Add(*Catalog, "04", "AirGuns"); Add(*Catalog, "05", "AirGuns", 2, "04");
	Add(*Catalog, "06", "Missiles"); Add(*Catalog, "07", "Missiles", 2, "06");
	Catalog->Upgrades[5].AcquisitionRequirements = Leaf(EGuLiShipRuleOp::ChosenNode, "03");
	Catalog->Exclusions.Add({"GunsVsMissiles", {"AirGuns", "Missiles"}});
	FGuLiShipCompiledBuildRules Rules;
	if (!TestTrue(TEXT("Catalogue compiles"), Catalog->Compile(Rules))) { AddError(Rules.Error); return false; }
	FGuLiShipRunBuildState State; State.MatchEpoch = 1; State.BuildRevision = 1; FString Error;
	TestFalse(TEXT("Unimplemented capabilities stay outside the reward pool"), Catalog->CanChoose(Rules, State, "01", Error));
	TestFalse(TEXT("Historical prerequisite is required"), Catalog->CanChoose(Rules, State, "06", Error, false));
	State.ChosenNodeIds = {"01", "02", "03", "04", "05"};
	FGuLiShipResolvedBuild Build;
	TestTrue(TEXT("Historical upgrades remain valid after replacement"), Catalog->Resolve(Rules, State, Build, Error));
	TestEqual(TEXT("Only current full groups remain installed"), Build.GroupConfigurationIds.Num(), 2);
	TestTrue(TEXT("High tier replaces its low-tier model group"), Build.GroupConfigurationIds.Contains("03") && !Build.GroupConfigurationIds.Contains("01"));
	TestFalse(TEXT("Choosing 05 keeps the 06 route locked"), Catalog->CanChoose(Rules, State, "06", Error, false));
	TestFalse(TEXT("Choosing 05 also prevents 07"), Catalog->CanChoose(Rules, State, "07", Error, false));
	TestFalse(TEXT("Same choice cannot be appended twice"), Catalog->CanChoose(Rules, State, "05", Error, false));
	TestTrue(TEXT("Higher route level satisfies a lower minimum without old instances"),
		UGuLiShipBuildCatalog::Evaluate(Leaf(EGuLiShipRuleOp::RouteLevel, "Turret", 2), Build.Context));
	TestTrue(TEXT("All requires every historical operand"), UGuLiShipBuildCatalog::Evaluate(Compound(EGuLiShipRuleOp::All, {"01", "03", "05"}), Build.Context));
	TestTrue(TEXT("Any supports alternative prerequisites"), UGuLiShipBuildCatalog::Evaluate(Compound(EGuLiShipRuleOp::Any, {"06", "03"}), Build.Context));
	TestTrue(TEXT("AtLeast counts fulfilled operands"), UGuLiShipBuildCatalog::Evaluate(Compound(EGuLiShipRuleOp::AtLeast, {"01", "03", "06"}, 2), Build.Context));
	TestFalse(TEXT("AtLeast rejects insufficient fulfilled operands"), UGuLiShipBuildCatalog::Evaluate(Compound(EGuLiShipRuleOp::AtLeast, {"01", "07", "06"}, 2), Build.Context));
	const auto RuntimeRule = Leaf(EGuLiShipRuleOp::InstalledCapability, "Hangar");
	Build.Context.InstalledCapabilities.Add("Hangar");
	TestTrue(TEXT("Installed capability satisfies a continuous dependency"), UGuLiShipBuildCatalog::Evaluate(RuntimeRule, Build.Context));
	Build.Context.InstalledCapabilities.Reset();
	TestFalse(TEXT("Losing the physical provider suspends its dependent"), UGuLiShipBuildCatalog::Evaluate(RuntimeRule, Build.Context));
	TestTrue(TEXT("Provider loss leaves the historical choice intact"), Build.Context.ChosenNodes.Contains("05"));
	Add(*Catalog, "NewPart", "NewRoute"); Catalog->Exclusions.Add({"MissilesVsNew", {"Missiles", "NewRoute"}});
	TestTrue(TEXT("A new node is added through data only"), Catalog->Compile(Rules));
	TestTrue(TEXT("Exclusion is not transitive through a third route"), Catalog->CanChoose(Rules, State, "NewPart", Error, false));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiShipBuildValidationTest,
	"GuLiStrike.Ship.Build.InvalidCatalogReasons", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGuLiShipBuildValidationTest::RunTest(const FString& Parameters)
{
	using namespace GuLiShipBuildTests;
	auto* Catalog = NewObject<UGuLiShipBuildCatalog>();
	Add(*Catalog, "A", "A"); Add(*Catalog, "B", "B"); Add(*Catalog, "C", "C");
	Catalog->Exclusions.Add({"AvsB", {"A", "B"}});
	Catalog->Upgrades[2].AcquisitionRequirements = Compound(EGuLiShipRuleOp::All, {"A", "B"});
	FGuLiShipCompiledBuildRules Rules;
	TestFalse(TEXT("Mutually exclusive mandatory prerequisites are rejected"), Catalog->Compile(Rules));
	TestTrue(TEXT("Failure identifies the affected upgrade"), Rules.Error.Contains("C:"));
	Catalog->Upgrades[2].AcquisitionRequirements.Nodes[0].Op = EGuLiShipRuleOp::Any;
	TestTrue(TEXT("An alternative prerequisite remains satisfiable"), Catalog->Compile(Rules));
	Catalog->Upgrades[0].AcquisitionRequirements = Leaf(EGuLiShipRuleOp::ChosenNode, "C");
	TestFalse(TEXT("Acquisition dependency cycle is rejected"), Catalog->Compile(Rules));
	Catalog->Upgrades[0].AcquisitionRequirements = Leaf(EGuLiShipRuleOp::ChosenNode, "Missing");
	TestFalse(TEXT("Dangling dependency is rejected"), Catalog->Compile(Rules));
	Catalog->Upgrades[0].AcquisitionRequirements = {};
	Catalog->Upgrades[1].NodeId = "A";
	TestFalse(TEXT("Duplicate stable node IDs are rejected"), Catalog->Compile(Rules));
	Catalog->Upgrades[1].NodeId = "B";
	Catalog->Upgrades[2].AcquisitionRequirements.Nodes[0].Children = {1, 1};
	TestFalse(TEXT("Duplicate children cannot inflate AtLeast counts"), Catalog->Compile(Rules));
	return true;
}
#endif
