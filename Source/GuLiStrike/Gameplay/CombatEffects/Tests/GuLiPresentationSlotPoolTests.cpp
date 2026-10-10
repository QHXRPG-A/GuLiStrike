#include "Gameplay/CombatEffects/GuLiPresentationSlotPool.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiPresentationSlotReuseTest,"GuLiStrike.ClientPresentation.SlotReuseAndEpoch",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FGuLiPresentationSlotReuseTest::RunTest(const FString& Parameters)
{
	FGuLiPresentationSlotPool Pool;
	TArray<FGuLiPresentationSlotHandle> Handles;
	for (int32 I=0;I<513;++I) Handles.Add(Pool.Acquire());
	TestEqual(TEXT("512 slots grow by 256 without dropping feedback"),Pool.Capacity(),768);
	const auto Old=Handles[10];
	TestTrue(TEXT("Release an interior active slot"),Pool.Release(Old));
	TestTrue(TEXT("Dense-index last slot remains live after swap removal"),Pool.IsLive(Handles.Last()));
	const auto Reused=Pool.Acquire();
	TestEqual(TEXT("Stable free index is reused"),Reused.Index,Old.Index);
	TestFalse(TEXT("Old generation cannot retire the new effect"),Pool.Release(Old));
	TestTrue(TEXT("New generation remains live"),Pool.IsLive(Reused));
	const auto Generations=Pool.ActiveGenerations();
	TestEqual(TEXT("Upload marks the current generation"),Generations[Reused.Index],int32(Reused.Generation));
	Pool.Reset();
	const auto NextEpoch=Pool.Acquire();
	TestTrue(TEXT("Previous-frame channel identity cannot alias after an epoch reset"),NextEpoch.Generation!=Handles[NextEpoch.Index].Generation);
	TestFalse(TEXT("Late completion from previous match cannot release a new slot"),Pool.Release(Handles[0]));
	TestTrue(TEXT("New epoch is live"),Pool.IsLive(NextEpoch));
	TestEqual(TEXT("Reset retains allocated capacity"),Pool.Capacity(),768);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiPresentationUploadTest,"GuLiStrike.ClientPresentation.UploadRevision",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FGuLiPresentationUploadTest::RunTest(const FString& Parameters)
{
	FGuLiPresentationUploadStamp Stamp;
	TestTrue(TEXT("Initial nonempty payload uploads"),Stamp.NeedsUpload(10,1)); Stamp.Commit(10,1);
	TestFalse(TEXT("Second consumer cannot publish twice in one frame"),Stamp.NeedsUpload(10,2));
	TestTrue(TEXT("A deferred changed revision uploads next frame"),Stamp.NeedsUpload(11,2)); Stamp.Commit(11,2);
	TestFalse(TEXT("Heartbeat alone does not upload unchanged data"),Stamp.NeedsUpload(12,2));
	Stamp.Reset(); TestTrue(TEXT("Reactivated component receives fresh payload"),Stamp.NeedsUpload(12,2,true));
	return true;
}
#endif
