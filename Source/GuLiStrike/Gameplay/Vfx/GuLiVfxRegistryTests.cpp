#if WITH_DEV_AUTOMATION_TESTS
#include "Gameplay/Vfx/GuLiVfxRegistrySubsystem.h"
#include "Misc/AutomationTest.h"
#include "Materials/Material.h"
#include "NiagaraSystem.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiVfxRegistryRules, "GuLi.Vfx.RegistryRules",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGuLiVfxRegistryRules::RunTest(const FString&)
{
	using Registry = UGuLiVfxRegistrySubsystem;
	FGuLiStrikeVfxEffectsRow A;
	A.Id = 1; A.ResourcePath = FSoftObjectPath(TEXT("/Game/Test/Effect.Effect")); A.Scale = FVector::OneVector;
	auto B = A; B.Id = 2; B.Note = TEXT("Another use does not define a different effect");
	TestTrue(TEXT("Same path and scale share identity, irrespective of use"), Registry::SameDefinition(A, B));
	FString Error;
	TestFalse(TEXT("Duplicate pair must be merged before export"), Registry::ValidateDefinitions({A, B}, Error));
	B.Scale = FVector(2);
	TestFalse(TEXT("Different base scale has separate identity"), Registry::SameDefinition(A, B));
	TestTrue(TEXT("Distinct scales can coexist"), Registry::ValidateDefinitions({A, B}, Error));
	B.Id = A.Id;
	TestFalse(TEXT("Duplicate IDs rejected"), Registry::ValidateDefinitions({A, B}, Error));
	B.Id = 0;
	TestFalse(TEXT("Nonpositive IDs rejected"), Registry::ValidateDefinitions({B}, Error));
	B.Id = 2; B.Scale.X = 0;
	TestFalse(TEXT("Zero base scale rejected"), Registry::ValidateDefinitions({B}, Error));
	FGuLiStrikeVfxEffectsRow Missing;
	TestFalse(TEXT("Optional zero does not resolve"), Registry::GetDefinitionWithoutWorld(0, Missing));
	TestFalse(TEXT("Negative ID does not resolve"), Registry::GetDefinitionWithoutWorld(-1, Missing));
	auto* Material = NewObject<UMaterial>();
	TestTrue(TEXT("Material type accepted"), Registry::MatchesType(Material, UMaterialInterface::StaticClass()));
	TestFalse(TEXT("Material cannot be loaded as Niagara"), Registry::MatchesType(Material, UNiagaraSystem::StaticClass()));
	TestFalse(TEXT("Missing resource rejected"), Registry::MatchesType(nullptr, UNiagaraSystem::StaticClass()));
	TestTrue(TEXT("Component base and dynamic scale compose once"), Registry::ComposeScale(FVector(2,3,4), FVector(.5,2,3)).Equals(FVector(1,6,12)));
	TestTrue(TEXT("Invalid dynamic scale skips visual"), Registry::ComposeScale(FVector::OneVector, FVector(-1)).IsZero());
	return true;
}
#endif
