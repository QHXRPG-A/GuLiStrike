#if WITH_DEV_AUTOMATION_TESTS
#include "Gameplay/Resources/GuLiResourceFactoryActor.h"
#include "Components/BoxComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiScale020FactoryDoorPresentationContract,
	"GuLiStrike.Scale020.FactoryDoorIsPresentationOnly",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiScale020FactoryDoorPresentationContract::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	auto* Factory = World->SpawnActor<AGuLiResourceFactoryActor>();
	auto* Door = NewObject<USkeletalMeshComponent>(Factory);
	Factory->DoorMesh = Door;
	Door->SetCollisionProfileName(TEXT("BlockAll"));
	Door->SetGenerateOverlapEvents(true);
	Door->SetCanEverAffectNavigation(true);
	const auto ShellCollision = Factory->CollisionBox->GetCollisionEnabled();
	for (int32 Pass = 0; Pass < 2; ++Pass)
	{
		Factory->ConfigureDoorPresentation();
		TestEqual(TEXT("Door never participates in queries or physics"), Door->GetCollisionEnabled(), ECollisionEnabled::NoCollision);
		TestFalse(TEXT("Visual door emits no overlaps"), Door->GetGenerateOverlapEvents());
		TestFalse(TEXT("Door poses never alter navigation"), Door->CanEverAffectNavigation());
		TestEqual(TEXT("Physical shell collision is preserved"), Factory->CollisionBox->GetCollisionEnabled(), ShellCollision);
		TestTrue(TEXT("Physical shell still affects navigation"), Factory->CollisionBox->CanEverAffectNavigation());
	}
	Factory->DoorMesh.Reset();
	Factory->ConfigureDoorPresentation();
	GEngine->DestroyWorldContext(World); World->DestroyWorld(false);
	return true;
}
#endif
