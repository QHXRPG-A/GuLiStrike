#include "Gameplay/Ship/GuLiShipMovementComponent.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiShipCanonicalMoveHistoryTest,
	"GuLiStrike.Ship.Movement.CanonicalHistoryIsBoundedAndVersioned",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiShipCanonicalMoveHistoryTest::RunTest(const FString& Parameters)
{
	FGuLiShipCanonicalMoveHistory History;
	History.Reset(7u, 2);
	History.Append(1.0, FTransform(FVector(100.0, 0.0, 0.0)), FVector(10.0, 0.0, 0.0), 3u, 5u);
	History.Append(2.0, FTransform(FVector(200.0, 0.0, 0.0)), FVector(20.0, 0.0, 0.0), 3u, 5u);
	History.Append(3.0, FTransform(FVector(300.0, 0.0, 0.0)), FVector(30.0, 0.0, 0.0), 3u, 5u);

	TestEqual(TEXT("The canonical history keeps its configured bounded capacity"), History.Num(), 2);
	TestEqual(TEXT("Every accepted append advances one canonical revision"), History.GetLatestRevision(), 3u);

	FGuLiShipCanonicalMoveState State;
	TestTrue(TEXT("A retained exact revision is found"),
		History.Lookup(7u, 2u, State) == EGuLiShipCanonicalMoveLookupResult::Found);
	TestEqual(TEXT("The exact retained transform is returned"), State.Transform.GetLocation(), FVector(200.0, 0.0, 0.0));
	TestTrue(TEXT("A valid retained state carries all source revisions"), State.IsValid());
	TestTrue(TEXT("A revision newer than the replay stream remains pending"),
		History.Lookup(7u, 4u, State) == EGuLiShipCanonicalMoveLookupResult::Pending);
	TestTrue(TEXT("An evicted revision is expired rather than pending"),
		History.Lookup(7u, 1u, State) == EGuLiShipCanonicalMoveLookupResult::Expired);
	TestTrue(TEXT("A reference from another epoch cannot bind to this history"),
		History.Lookup(6u, 3u, State) == EGuLiShipCanonicalMoveLookupResult::EpochMismatch);

	History.Reset(8u, 2);
	TestEqual(TEXT("Reset creates a fresh epoch with no cross-lifecycle revision"), History.GetLatestRevision(), 0u);
	TestTrue(TEXT("An empty fresh epoch is unavailable"),
		History.Lookup(8u, 1u, State) == EGuLiShipCanonicalMoveLookupResult::Unavailable);
	return true;
}
#endif
