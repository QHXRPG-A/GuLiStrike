// Copyright Epic Games, Inc. All Rights Reserved.
#include "Commander/Framework/GuLiCommanderNetSyncComponent.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace GuLiSelectionMoveGateTests
{
	FGuLiCommanderSelectionState State(uint32 Revision, uint32 Soldier)
	{
		FGuLiCommanderSelectionState Result;
		Result.SelectionRevision = Revision;
		auto& Cohort = Result.Cohorts.AddDefaulted_GetRef();
		Cohort.CohortId = FGuLiControlCohortId(Revision);
		Cohort.MemberIds.Add(FGuLiSoldierId(Soldier));
		Cohort.AliveCount = 1;
		return Result;
	}
	FGuLiCommandAck Ack(EGuLiCommandAckResult Result = EGuLiCommandAckResult::Accepted)
	{
		FGuLiCommandAck Value;
		Value.CommandKind = EGuLiCommandKind::Selection;
		Value.ClientCommandId = 7;
		Value.ServerSelectionRevision = 4;
		Value.Result = Result;
		return Value;
	}
	UGuLiCommanderNetSyncComponent* Setup()
	{
		auto* Sync = NewObject<UGuLiCommanderNetSyncComponent>();
		Sync->TestOnly_ApplySelectionSnapshot(State(3, 11));
		FGuLiSelectionRequest Selection;
		Selection.ClientRequestId = 7;
		Selection.KnownSelectionRevision = 3;
		FGuLiMoveRequest Move;
		Move.ClientCommandId = 9;
		Move.SelectionRevision = 3;
		Move.Target = FVector(10000, 20000, 0);
		Sync->TestOnly_ConfigureDeferredSelectionMove(Selection, Move);
		return Sync;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiSelectionAckBeforeSnapshot,
	"GuLiStrike.Commander.Network.SelectionMove.AckBeforeSnapshot", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGuLiSelectionAckBeforeSnapshot::RunTest(const FString& Parameters)
{
	using namespace GuLiSelectionMoveGateTests;
	auto* Sync = Setup();
	Sync->TestOnly_ReceiveCommandAck(Ack());
	TestTrue(TEXT("ACK alone keeps move waiting"), Sync->TestOnly_HasQueuedMove());
	TestEqual(TEXT("Old units never trigger dispatch"), Sync->TestOnly_GetDispatchedMove().ClientCommandId, 0u);
	Sync->TestOnly_ApplySelectionSnapshot(State(3, 11));
	TestTrue(TEXT("Stale snapshot keeps move waiting"), Sync->TestOnly_HasQueuedMove());
	Sync->TestOnly_ApplySelectionSnapshot(State(4, 22));
	TestFalse(TEXT("Matching snapshot releases move"), Sync->TestOnly_HasQueuedMove());
	TestEqual(TEXT("Released request uses new revision"), Sync->TestOnly_GetDispatchedMove().SelectionRevision, 4u);
	TestEqual(TEXT("Released request retains move identity"), Sync->TestOnly_GetDispatchedMove().ClientCommandId, 9u);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiSelectionSnapshotBeforeAck,
	"GuLiStrike.Commander.Network.SelectionMove.SnapshotBeforeAck", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGuLiSelectionSnapshotBeforeAck::RunTest(const FString& Parameters)
{
	using namespace GuLiSelectionMoveGateTests;
	auto* Sync = Setup();
	Sync->TestOnly_ApplySelectionSnapshot(State(4, 22));
	TestTrue(TEXT("Snapshot alone does not confirm request"), Sync->TestOnly_HasQueuedMove());
	auto OtherAck = Ack(); OtherAck.ClientCommandId = 6;
	Sync->TestOnly_ReceiveCommandAck(OtherAck);
	TestEqual(TEXT("Unrelated ACK cannot dispatch move"), Sync->TestOnly_GetDispatchedMove().ClientCommandId, 0u);
	Sync->TestOnly_ReceiveCommandAck(Ack());
	TestEqual(TEXT("Matching ACK releases correct revision"), Sync->TestOnly_GetDispatchedMove().SelectionRevision, 4u);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiRejectedSelectionCancelsMove,
	"GuLiStrike.Commander.Network.SelectionMove.RejectionCancels", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGuLiRejectedSelectionCancelsMove::RunTest(const FString& Parameters)
{
	using namespace GuLiSelectionMoveGateTests;
	auto* Sync = Setup();
	Sync->TestOnly_ReceiveCommandAck(Ack(EGuLiCommandAckResult::Unauthorized));
	TestFalse(TEXT("Rejected selection cancels deferred move"), Sync->TestOnly_HasQueuedMove());
	TestEqual(TEXT("Never dispatches against old selection"), Sync->TestOnly_GetDispatchedMove().ClientCommandId, 0u);
	TestEqual(TEXT("Move rejection reaches normal feedback path"), Sync->GetLastCommandAck().CommandKind, EGuLiCommandKind::Move);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiSelectionMovesKeepIndependentDependencies,
	"GuLiStrike.Commander.Network.SelectionMove.IndependentDependencies", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGuLiSelectionMovesKeepIndependentDependencies::RunTest(const FString& Parameters)
{
	using namespace GuLiSelectionMoveGateTests;
	auto* Sync = Setup(); // selection A=7 has move A=9 waiting.
	FGuLiMoveRequest MoveB;
	MoveB.ClientCommandId = 10;
	MoveB.Target = FVector(5000, 2000, 0);
	Sync->TestOnly_QueueMoveForSelection(8, MoveB);
	MoveB.ClientCommandId = 11;
	MoveB.Target = FVector(6000, 3000, 0);
	Sync->TestOnly_QueueMoveForSelection(8, MoveB);
	TestEqual(TEXT("Same selection coalesces destinations without deleting another selection's move"), Sync->TestOnly_GetQueuedMoveCount(), 2);
	Sync->TestOnly_ReceiveCommandAck(Ack());
	Sync->TestOnly_ApplySelectionSnapshot(State(4, 22));
	TestEqual(TEXT("A still sends its own move before B"), Sync->TestOnly_GetDispatchedMove().ClientCommandId, 9u);
	TestEqual(TEXT("B remains waiting for its own selection"), Sync->TestOnly_GetQueuedMoveCount(), 1);
	FGuLiSelectionRequest SelectionB;
	SelectionB.ClientRequestId = 8;
	SelectionB.KnownSelectionRevision = 4;
	Sync->TestOnly_ConfigureDeferredSelectionMove(SelectionB, MoveB);
	auto AckB = Ack(); AckB.ClientCommandId = 8; AckB.ServerSelectionRevision = 5;
	Sync->TestOnly_ApplySelectionSnapshot(State(5, 33));
	TestEqual(TEXT("B snapshot alone cannot issue a move"), Sync->TestOnly_GetDispatchedMove().ClientCommandId, 9u);
	Sync->TestOnly_ReceiveCommandAck(AckB);
	TestEqual(TEXT("B sends the latest target belonging to B"), Sync->TestOnly_GetDispatchedMove().ClientCommandId, 11u);
	TestEqual(TEXT("B uses its confirmed selection revision"), Sync->TestOnly_GetDispatchedMove().SelectionRevision, 5u);
	TestFalse(TEXT("Both dependencies drained"), Sync->TestOnly_HasQueuedMove());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiSelectionPayloadIdentity,
	"GuLiStrike.Commander.Network.SelectionMove.FullPayloadIdentity", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGuLiSelectionPayloadIdentity::RunTest(const FString& Parameters)
{
	FGuLiSelectionRequest A; A.ClientRequestId = 1;
	auto Same = [&A](const FGuLiSelectionRequest& B) { return UGuLiCommanderNetSyncComponent::TestOnly_IsSameSelectionRequest(A, B); };
	TestTrue(TEXT("Exact retry retained"), Same(A));
	auto B = A; B.Kind = EGuLiSelectionKind::Box;
	TestFalse(TEXT("Changed geometry kind is not retry"), Same(B));
	B = A; B.SeedSoldierId = FGuLiSoldierId(42);
	TestFalse(TEXT("Changed target is not retry"), Same(B));
	B = A; B.BoxBottomLeftRay = FVector(0, 1, 0);
	TestFalse(TEXT("Changed corner ray is not retry"), Same(B));
	B = A; B.Modifier = EGuLiSelectionModifier::Add;
	TestFalse(TEXT("Add cannot masquerade as replace retry"), Same(B));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiMoveAckBeforePrunedSelectionSnapshot,
	"GuLiStrike.Commander.Network.SelectionMove.MoveAckBeforePrunedSnapshot", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGuLiMoveAckBeforePrunedSelectionSnapshot::RunTest(const FString& Parameters)
{
	using namespace GuLiSelectionMoveGateTests;
	(void)Parameters;
	auto* Sync = NewObject<UGuLiCommanderNetSyncComponent>();
	Sync->TestOnly_ApplySelectionSnapshot(State(4u, 22u));

	FGuLiCommandAck MoveAck;
	MoveAck.CommandKind = EGuLiCommandKind::Move;
	MoveAck.ClientCommandId = 90u;
	MoveAck.BatchOrderId = 70u;
	MoveAck.ServerSelectionRevision = 5u;
	MoveAck.Result = EGuLiCommandAckResult::PartiallyAccepted;
	Sync->TestOnly_ReceiveCommandAck(MoveAck);
	TestTrue(TEXT("A move ACK carrying a newer pruned selection revision closes the dispatch gate"),
		Sync->TestOnly_IsAwaitingSelectionSnapshot());
	TestEqual(TEXT("The gate retains the ACK selection high-water revision"),
		Sync->TestOnly_GetAwaitedSelectionRevision(), 5u);

	FGuLiMoveRequest NextMove;
	NextMove.ClientCommandId = 91u;
	NextMove.SelectionRevision = 4u;
	NextMove.Target = FVector(1000.0, 2000.0, 0.0);
	Sync->TestOnly_QueueMoveUntilSelectionSnapshot(NextMove);
	TestEqual(TEXT("No command dispatches against the pre-prune selection"),
		Sync->TestOnly_GetDispatchedMove().ClientCommandId, 0u);
	Sync->TestOnly_ApplySelectionSnapshot(State(4u, 22u));
	TestEqual(TEXT("Repeating the stale property revision keeps the move queued"),
		Sync->TestOnly_GetDispatchedMove().ClientCommandId, 0u);
	Sync->TestOnly_ApplySelectionSnapshot(State(5u, 33u));
	TestEqual(TEXT("The matching pruned snapshot releases the queued move"),
		Sync->TestOnly_GetDispatchedMove().ClientCommandId, 91u);
	TestEqual(TEXT("Released move uses the pruned selection revision"),
		Sync->TestOnly_GetDispatchedMove().SelectionRevision, 5u);
	TestFalse(TEXT("The selection high-water gate clears after the property catches up"),
		Sync->TestOnly_IsAwaitingSelectionSnapshot());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiMovePendingSerializesLatestClick,
	"GuLiStrike.Commander.Network.SelectionMove.PendingMoveSerializesLatestClick",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGuLiMovePendingSerializesLatestClick::RunTest(const FString& Parameters)
{
	using namespace GuLiSelectionMoveGateTests;
	(void)Parameters;
	auto* Sync = NewObject<UGuLiCommanderNetSyncComponent>();
	Sync->TestOnly_ApplySelectionSnapshot(State(4u, 22u));

	FGuLiMoveRequest Current;
	Current.ClientCommandId = 90u;
	Current.SelectionRevision = 4u;
	Current.Target = FVector(1000.0, 2000.0, 0.0);
	Sync->TestOnly_ConfigurePendingMoveIntent(Current);

	FGuLiMoveRequest Deferred = Current;
	Deferred.ClientCommandId = 91u;
	Deferred.Target = FVector(3000.0, 4000.0, 0.0);
	Sync->TestOnly_DeferMoveUntilCurrentAck(Deferred);
	TestEqual(TEXT("The in-flight command retains retry and ACK ownership"),
		Sync->TestOnly_GetPendingMoveCommandId(), 90u);
	TestEqual(TEXT("A later click waits behind the in-flight command"),
		Sync->TestOnly_GetDeferredMoveCommandId(), 91u);
	TestTrue(TEXT("The latest pending-line ID remains active while held in the deferred slot"),
		Sync->IsMoveCommandPending(91u));

	Sync->TestOnly_DeferMoveUntilCurrentAck(Current);
	TestEqual(TEXT("An application resubmit cannot replace the queued later click"),
		Sync->TestOnly_GetDeferredMoveCommandId(), 91u);

	FGuLiMoveRequest Latest = Deferred;
	Latest.ClientCommandId = 92u;
	Latest.Target = FVector(5000.0, 6000.0, 0.0);
	Sync->TestOnly_DeferMoveUntilCurrentAck(Latest);
	TestEqual(TEXT("Additional clicks coalesce latest-wins without touching the current command"),
		Sync->TestOnly_GetDeferredMoveCommandId(), 92u);
	TestFalse(TEXT("The superseded deferred ID no longer keeps pending feedback alive"),
		Sync->IsMoveCommandPending(91u));
	TestTrue(TEXT("The latest deferred ID keeps pending feedback alive"),
		Sync->IsMoveCommandPending(92u));
	TestEqual(TEXT("No prediction starts for a deferred click"),
		Sync->TestOnly_GetDispatchedMove().ClientCommandId, 0u);

	FGuLiCommandAck CurrentAck;
	CurrentAck.CommandKind = EGuLiCommandKind::Move;
	CurrentAck.ClientCommandId = 90u;
	CurrentAck.BatchOrderId = 70u;
	CurrentAck.ServerSelectionRevision = 5u;
	CurrentAck.Result = EGuLiCommandAckResult::PartiallyAccepted;
	FGuLiCohortCommandAck& CohortAck = CurrentAck.CohortResults.AddDefaulted_GetRef();
	CohortAck.CohortId = FGuLiControlCohortId(4u);
	CohortAck.MemberCount = 2u;
	CohortAck.EligibleMemberMask = 0b11u;
	CohortAck.AcceptedMemberMask = 0b01u;
	CohortAck.Result = EGuLiCommandAckResult::PartiallyAccepted;
	bool bAckBroadcastBeforeDeferredDispatch = false;
	Sync->OnCommandAckChanged.AddLambda(
		[Sync, &bAckBroadcastBeforeDeferredDispatch](const FGuLiCommandAck& Ack)
		{
			if (Ack.CommandKind == EGuLiCommandKind::Move && Ack.ClientCommandId == 90u)
			{
				bAckBroadcastBeforeDeferredDispatch =
					Sync->TestOnly_GetDispatchedMove().ClientCommandId == 0u;
			}
		});
	Sync->TestOnly_ReceiveCommandAck(CurrentAck);

	TestTrue(TEXT("The completed ACK is broadcast before the deferred move can replace prediction"),
		bAckBroadcastBeforeDeferredDispatch);
	TestEqual(TEXT("The final ACK releases the original retry state"),
		Sync->TestOnly_GetPendingMoveCommandId(), 0u);
	TestEqual(TEXT("The queued click leaves the single deferred slot"),
		Sync->TestOnly_GetDeferredMoveCommandId(), 0u);
	TestTrue(TEXT("A partial result waits for the pruned selection snapshot"),
		Sync->TestOnly_IsAwaitingSelectionSnapshot());
	TestTrue(TEXT("The latest click is retained by the selection gate"),
		Sync->TestOnly_HasQueuedMove());
	TestTrue(TEXT("Pending feedback survives while the latest click waits for SelectionState"),
		Sync->IsMoveCommandPending(92u));
	TestEqual(TEXT("The latest click still has no prediction before the snapshot"),
		Sync->TestOnly_GetDispatchedMove().ClientCommandId, 0u);

	Sync->TestOnly_ApplySelectionSnapshot(State(4u, 22u));
	TestEqual(TEXT("A stale snapshot cannot release the latest click"),
		Sync->TestOnly_GetDispatchedMove().ClientCommandId, 0u);
	Sync->TestOnly_ApplySelectionSnapshot(State(5u, 33u));
	TestEqual(TEXT("The latest click dispatches after the selection revision catches up"),
		Sync->TestOnly_GetDispatchedMove().ClientCommandId, 92u);
	TestEqual(TEXT("The dispatched click uses the authoritative selection revision"),
		Sync->TestOnly_GetDispatchedMove().SelectionRevision, 5u);
	TestTrue(TEXT("The latest target survives coalescing"),
		FVector(Sync->TestOnly_GetDispatchedMove().Target).Equals(FVector(Latest.Target), 0.5f));
	TestFalse(TEXT("The selection dependency is drained after dispatch"),
		Sync->TestOnly_HasQueuedMove());
	return true;
}
#endif
