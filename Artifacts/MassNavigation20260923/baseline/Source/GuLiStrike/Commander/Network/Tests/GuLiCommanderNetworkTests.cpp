// Copyright Epic Games, Inc. All Rights Reserved.

#include "Commander/Network/GuLiCommanderTypes.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Commander/Framework/GuLiCommanderNetSyncComponent.h"
#include "Commander/Framework/GuLiCommanderNetworkGateValidation.h"
#include "Commander/Network/GuLiSoldierStateReplicator.h"
#include "Commander/Presentation/GuLiCommanderPresentationActor.h"
#include "Engine/NetDriver.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/BitReader.h"
#include "Serialization/BitWriter.h"
#include "Commander/Network/GuLiCommanderPoseCodec.h"
#include "UObject/UnrealType.h"

#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiCommanderRosterCacheTest,
	"GuLiStrike.Commander.Network.RosterCache",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiCommanderRosterCacheTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	auto* Source = World->SpawnActor<AGuLiSoldierStateReplicator>();
	TArray<FGuLiSoldierStateItem> States;
	for (uint32 Id = 1; Id <= 3; ++Id)
	{
		auto& State = States.AddDefaulted_GetRef(); State.SoldierId = FGuLiSoldierId(Id);
		State.UnitTypeId = 1; State.Health = State.MaxHealth = 100;
	}
	TArray<FGuLiSoldierRosterDelta> Received;
	Source->OnRosterDelta.AddLambda([&](const auto& Delta) { Received.Add(Delta); });
	Source->ApplyAuthoritySnapshot(States, 1);
	TestTrue(TEXT("First snapshot initializes shared ID cache"), Source->ContainsSoldier(FGuLiSoldierId(3)));
	Received.Reset(); Source->ApplyAuthoritySnapshot(States, 1);
	TestTrue(TEXT("Unchanged roster emits no maintenance"), Received.IsEmpty());
	States[0].DisplacementYaw = 30;
	TestEqual(TEXT("Ordinary facing never dirties reliable roster"), Source->ApplyAuthoritySnapshot(States, 1), 0);
	TestTrue(TEXT("Ordinary facing emits no roster notifications"), Received.IsEmpty());
	States[0].DisplacementFrameFloor = 101; States[0].DisplacementLocation = FVector(100,200,300);
	Source->ApplyAuthoritySnapshot(States, 1);
	TestTrue(TEXT("New displacement floor publishes its discrete event"), EnumHasAnyFlags(Received.Last().Changed.FindRef(States[0].SoldierId), EGuLiSoldierStateChange::Displacement));
	TestEqual(TEXT("Displacement carries its facing"), Source->FindSoldierState(States[0].SoldierId)->DisplacementYaw, 30.0f);
	Received.Reset();
	States[0].Health = 75; States[0].Team = EGuLiTeam::Blue; States[0].UnitTypeId = 2;
	States.RemoveAtSwap(1); // count stays three after replacing the removed ID
	auto Replacement = States[0]; Replacement.SoldierId = FGuLiSoldierId(4); States.Add(Replacement);
	Source->ApplyAuthoritySnapshot(States, 1);
	TestEqual(TEXT("One complete notification per snapshot"), Received.Num(), 1);
	TestFalse(TEXT("Swap removal deletes old ID"), Source->ContainsSoldier(FGuLiSoldierId(2)));
	TestTrue(TEXT("Same-count replacement adds new ID"), Source->ContainsSoldier(FGuLiSoldierId(4)));
	const auto Flags = Received.Last().Changed.FindRef(FGuLiSoldierId(1));
	TestTrue(TEXT("Type/team/health change flags survive merging"), EnumHasAllFlags(Flags,
		EGuLiSoldierStateChange::Type | EGuLiSoldierStateChange::Team | EGuLiSoldierStateChange::Health));
	TestEqual(TEXT("Swap survivor retains its own value"), Source->FindSoldierState(FGuLiSoldierId(3))->Health, 100.0f);
	Received.Reset(); Source->ApplyAuthoritySnapshot(States, 2);
	TestTrue(TEXT("Reused IDs produce a reset on new match"), Received.Num() == 1 && Received[0].bReset);
	Source->OnRosterDelta.Clear();

	// Exercise the actual FastArray callbacks in both independent-property arrival orders.
	auto* Replica = World->SpawnActor<AGuLiSoldierStateReplicator>();
	Replica->SnapshotRevision = 99; Replica->TestOnly_InvokeSnapshotRevisionRepNotify();
	TestFalse(TEXT("Revision notification does not invent missing roster data"), Replica->ContainsSoldier(FGuLiSoldierId(1)));
	Replica->SnapshotMatchEpoch = 2; Replica->PostNetReceive();
	Replica->ReplicatedSoldiers.Items = States;
	TArray<int32> Indices = {0, 1, 2};
	Replica->ReplicatedSoldiers.PostReplicatedAdd(Indices, States.Num());
	TestFalse(TEXT("Partial receive callbacks are not published"), Replica->ContainsSoldier(FGuLiSoldierId(1)));
	FFastArraySerializer::FPostReplicatedReceiveParameters ReceiveParameters{};
	Replica->ReplicatedSoldiers.PostReplicatedReceive(ReceiveParameters);
	TestEqual(TEXT("Completed receive publishes cached values"), Replica->FindSoldierState(FGuLiSoldierId(1))->Health, 75.0f);
	Replica->ReplicatedSoldiers.Items.Reserve(4096);
	Replica->ReplicatedSoldiers.Items[0].Health = 12;
	TestEqual(TEXT("Cache owns its value across array allocation and mutation"), Replica->FindSoldierState(FGuLiSoldierId(1))->Health, 75.0f);
	TArray<int32> Removed = {1}, Changed = {0};
	Replica->ReplicatedSoldiers.PreReplicatedRemove(Removed, 2);
	Replica->ReplicatedSoldiers.PostReplicatedChange(Changed, 2);
	Replica->ReplicatedSoldiers.Items.RemoveAtSwap(1);
	Replica->ReplicatedSoldiers.PostReplicatedReceive(ReceiveParameters);
	Replica->SnapshotMatchEpoch = 3; Replica->PostNetReceive();
	TestEqual(TEXT("Array-first epoch transition preserves received latest value"), Replica->FindSoldierState(FGuLiSoldierId(1))->Health, 12.0f);
	TestFalse(TEXT("Removed ID cannot reappear after epoch refresh"), Replica->ContainsSoldier(FGuLiSoldierId(3)));
	World->DestroyWorld(false);
	return true;
}

namespace GuLiCommanderNetworkTests
{
	template <typename ContractType>
	bool NetSerializeToBytes(const ContractType& Source, TArray<uint8>& OutBytes)
	{
		OutBytes.Reset();
		FMemoryWriter Writer(OutBytes, true);
		ContractType WritableValue = Source;
		bool bWriteSuccess = false;
		WritableValue.NetSerialize(Writer, nullptr, bWriteSuccess);
		return bWriteSuccess && !Writer.IsError();
	}

	template <typename ContractType>
	bool NetDeserializeFromBytes(const TArray<uint8>& Bytes, ContractType& OutValue)
	{
		FMemoryReader Reader(Bytes, true);
		bool bReadSuccess = false;
		OutValue.NetSerialize(Reader, nullptr, bReadSuccess);
		return bReadSuccess && !Reader.IsError();
	}

	template <typename ContractType>
	bool NetSerializeRoundTrip(const ContractType& Source, ContractType& OutValue)
	{
		TArray<uint8> Bytes;
		return NetSerializeToBytes(Source, Bytes)
			&& NetDeserializeFromBytes(Bytes, OutValue);
	}

	FGuLiControlCohortDescriptor MakeCohort(
		const uint32 CohortValue,
		std::initializer_list<uint32> SoldierValues)
	{
		FGuLiControlCohortDescriptor Cohort;
		Cohort.CohortId = FGuLiControlCohortId(CohortValue);
		for (const uint32 SoldierValue : SoldierValues)
		{
			Cohort.MemberIds.Emplace(SoldierValue);
		}
		Cohort.AliveCount = static_cast<uint8>(Cohort.MemberIds.Num());
		return Cohort;
	}

	FGuLiCommandAck MakeAck(
		const EGuLiCommandKind CommandKind,
		const uint32 ClientCommandId,
		const uint32 BatchOrderId = 0u)
	{
		FGuLiCommandAck Ack;
		Ack.CommandKind = CommandKind;
		Ack.ClientCommandId = ClientCommandId;
		Ack.BatchOrderId = BatchOrderId;
		Ack.Result = EGuLiCommandAckResult::Accepted;
		if (CommandKind == EGuLiCommandKind::Move && BatchOrderId != 0u)
		{
			FGuLiCohortCommandAck& CohortAck = Ack.CohortResults.AddDefaulted_GetRef();
			CohortAck.CohortId = FGuLiControlCohortId(1u);
			CohortAck.Result = EGuLiCommandAckResult::Accepted;
			CohortAck.MemberCount = 1u;
			CohortAck.EligibleMemberMask = 1u;
			CohortAck.AcceptedMemberMask = 1u;
		}
		return Ack;
	}

	FGuLiSoldierPoseChunk MakePoseChunk(
		const uint32 MatchEpoch,
		const uint32 FrameSequence,
		const uint32 SoldierIdValue)
	{
		FGuLiSoldierPoseChunk Chunk;
		Chunk.AuthorityEpoch = MatchEpoch;
		Chunk.FrameSequence = FrameSequence;
		Chunk.ServerSimTick = FrameSequence;
		Chunk.ServerTimeSeconds = static_cast<float>(FrameSequence) / GULI_POSE_CAPTURE_RATE_HZ;
		Chunk.ChunkIndex = 0u;
		Chunk.ChunkCount = 1u;
		FGuLiQuantizedSoldierPose& Pose = Chunk.Samples.AddDefaulted_GetRef();
		Pose.SoldierId = FGuLiSoldierId(SoldierIdValue);
		Pose.SetWorldLocationCentimeters(FVector(
			static_cast<double>(FrameSequence) * 10.0,
			0.0,
			0.0));
		return Chunk;
	}

	FGuLiEncodedPoseBlock MakePoseBlock(uint32 MatchEpoch, uint32 FrameSequence, uint32 SoldierId)
	{
		GuLiCommanderPoseCodec::FSender Sender;
		Sender.Reset(1u, MatchEpoch);
		TArray<FGuLiEncodedPoseBlock> Blocks;
		Sender.Encode(MakePoseChunk(MatchEpoch, FrameSequence, SoldierId), Blocks);
		check(Blocks.Num() == 1);
		return MoveTemp(Blocks[0]);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGuLiCommanderDynamicCohortContractTest,
	"GuLiStrike.Commander.Network.DynamicCohortContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiCommanderDynamicCohortContractTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	TestEqual(TEXT("Grouped portrait selection requires battle protocol version 17"),
		GULI_COMMANDER_PROTOCOL_VERSION, static_cast<uint16>(17u));
	TestEqual(TEXT("Control granularity remains capped at 25 soldiers"),
		GULI_CONTROL_COHORT_TARGET_SIZE, static_cast<uint32>(25u));
	TestEqual(TEXT("Authoritative pose contract is captured at 10 Hz"),
		GULI_POSE_CAPTURE_RATE_HZ, static_cast<uint32>(10u));
	TestTrue(TEXT("Soldier zero is invalid"), !FGuLiSoldierId().IsValid());
	TestTrue(TEXT("Cohort zero is invalid"), !FGuLiControlCohortId().IsValid());

	FGuLiControlCohortDescriptor OversizedCohort;
	OversizedCohort.CohortId = FGuLiControlCohortId(7u);
	for (uint32 SoldierValue = 1u; SoldierValue <= 27u; ++SoldierValue)
	{
		OversizedCohort.MemberIds.Emplace(SoldierValue);
	}
	OversizedCohort.MemberIds.Emplace(0u);
	OversizedCohort.MemberIds.Emplace(1u);
	OversizedCohort.AliveCount = MAX_uint8;
	OversizedCohort.ActiveOrderId = 99u;
	OversizedCohort.Sanitize();

	TestEqual(TEXT("A temporary cohort is truncated to 25 members"),
		OversizedCohort.MemberIds.Num(), static_cast<int32>(GULI_CONTROL_COHORT_TARGET_SIZE));
	TestEqual(TEXT("AliveCount cannot exceed frozen membership"),
		OversizedCohort.AliveCount, static_cast<uint8>(GULI_CONTROL_COHORT_TARGET_SIZE));
	TestTrue(TEXT("First deterministic member remains in the cohort"),
		OversizedCohort.MemberIds[0] == FGuLiSoldierId(1u));
	TestTrue(TEXT("Twenty-fifth deterministic member remains in the cohort"),
		OversizedCohort.MemberIds.Last() == FGuLiSoldierId(25u));

	FGuLiCommanderSelectionState Selection;
	Selection.SelectionRevision = 41u;
	Selection.AcceptedClientRequestId = 17u;
	Selection.Cohorts.Add(GuLiCommanderNetworkTests::MakeCohort(1u, { 1u, 2u }));
	Selection.Cohorts.Add(GuLiCommanderNetworkTests::MakeCohort(2u, { 2u, 3u }));
	Selection.Cohorts.Add(GuLiCommanderNetworkTests::MakeCohort(1u, { 4u }));
	Selection.Cohorts.Add(GuLiCommanderNetworkTests::MakeCohort(3u, { 3u }));
	Selection.Sanitize();

	TestEqual(TEXT("Duplicate cohort IDs and emptied cohorts are removed"), Selection.Cohorts.Num(), 2);
	TestEqual(TEXT("A soldier cannot belong to two selected cohorts"), Selection.Cohorts[1].MemberIds.Num(), 1);
	TestTrue(TEXT("The first cohort owns the duplicate soldier deterministically"),
		Selection.Cohorts[0].Contains(FGuLiSoldierId(2u)));
	TestTrue(TEXT("The second cohort retains its unique soldier"),
		Selection.Cohorts[1].Contains(FGuLiSoldierId(3u)));

	FGuLiCommanderSelectionState SelectionCopy;
	TestTrue(TEXT("Dynamic selection NetSerialize completes"),
		GuLiCommanderNetworkTests::NetSerializeRoundTrip(Selection, SelectionCopy));
	TestEqual(TEXT("Selection revision survives the wire"), SelectionCopy.SelectionRevision, Selection.SelectionRevision);
	TestEqual(TEXT("Cohort count survives the wire"), SelectionCopy.Cohorts.Num(), Selection.Cohorts.Num());
	TestEqual(TEXT("Frozen cohort membership survives the wire"),
		SelectionCopy.Cohorts[0].MemberIds.Num(), Selection.Cohorts[0].MemberIds.Num());
	TArray<uint8> FirstSelectionBytes;
	TArray<uint8> SecondSelectionBytes;
	TestTrue(TEXT("Selection serializes for determinism check"),
		GuLiCommanderNetworkTests::NetSerializeToBytes(Selection, FirstSelectionBytes));
	TestTrue(TEXT("Selection serializes identically a second time"),
		GuLiCommanderNetworkTests::NetSerializeToBytes(Selection, SecondSelectionBytes));
	TestTrue(TEXT("Selection NetSerialize is byte deterministic"), FirstSelectionBytes == SecondSelectionBytes);

	FGuLiCommandAck Ack;
	Ack.CommandKind = EGuLiCommandKind::Move;
	Ack.ClientCommandId = 7u;
	Ack.BatchOrderId = 17u;
	Ack.Result = EGuLiCommandAckResult::PartiallyAccepted;
	FGuLiCohortCommandAck& AcceptedAck = Ack.CohortResults.AddDefaulted_GetRef();
	AcceptedAck.CohortId = FGuLiControlCohortId(5u);
	AcceptedAck.Result = EGuLiCommandAckResult::Accepted;
	AcceptedAck.MemberCount = 2u;
	AcceptedAck.EligibleMemberMask = 0b11u;
	AcceptedAck.AcceptedMemberMask = 0b11u;
	FGuLiCohortCommandAck& DuplicateAck = Ack.CohortResults.AddDefaulted_GetRef();
	DuplicateAck.CohortId = FGuLiControlCohortId(5u);
	DuplicateAck.Result = EGuLiCommandAckResult::PathFailed;
	DuplicateAck.MemberCount = 1u;
	DuplicateAck.EligibleMemberMask = 0b1u;
	FGuLiCohortCommandAck& InvalidAck = Ack.CohortResults.AddDefaulted_GetRef();
	InvalidAck.Result = EGuLiCommandAckResult::InvalidRequest;
	FGuLiCohortCommandAck& FailedAck = Ack.CohortResults.AddDefaulted_GetRef();
	FailedAck.CohortId = FGuLiControlCohortId(6u);
	FailedAck.Result = EGuLiCommandAckResult::PathFailed;
	FailedAck.MemberCount = 2u;
	FailedAck.EligibleMemberMask = 0b11u;
	Ack.Sanitize();
	TestEqual(TEXT("ACK results are keyed by unique valid temporary cohorts"), Ack.CohortResults.Num(), 2);
	TestTrue(TEXT("ACK keeps the first deterministic result"),
		Ack.CohortResults[0].Result == EGuLiCommandAckResult::Accepted);
	TestTrue(TEXT("ACK preserves its request namespace"),
		Ack.CommandKind == EGuLiCommandKind::Move && Ack.ClientCommandId == 7u);
	TestTrue(TEXT("Mixed accepted and failed eligible members normalize the top-level result"),
		Ack.Result == EGuLiCommandAckResult::PartiallyAccepted);

	FGuLiCommandAck MissingBatchAck = Ack;
	MissingBatchAck.BatchOrderId = 0u;
	MissingBatchAck.Result = EGuLiCommandAckResult::Accepted;
	MissingBatchAck.Sanitize();
	TestEqual(TEXT("Accepted member masks require a non-zero batch"),
		MissingBatchAck.CohortResults[0].AcceptedMemberMask, 0u);
	TestTrue(TEXT("A missing batch safely rolls the move result back to failure"),
		MissingBatchAck.Result == EGuLiCommandAckResult::PathFailed);

	FGuLiCohortCommandAck PartialAck;
	PartialAck.CohortId = FGuLiControlCohortId(9u);
	PartialAck.Result = EGuLiCommandAckResult::Accepted;
	PartialAck.MemberCount = 30u;
	PartialAck.EligibleMemberMask = MAX_uint32;
	PartialAck.AcceptedMemberMask = (1u << 0u) | (1u << 24u) | (1u << 29u);
	PartialAck.Sanitize();
	TestEqual(TEXT("ACK member count is clamped to the 25-member wire contract"),
		PartialAck.MemberCount, static_cast<uint8>(GULI_CONTROL_COHORT_TARGET_SIZE));
	TestEqual(TEXT("ACK masks drop bits outside frozen membership"),
		PartialAck.AcceptedMemberMask, (1u << 0u) | (1u << 24u));
	TestTrue(TEXT("A strict accepted mask can never contain an ineligible bit"),
		(PartialAck.AcceptedMemberMask & ~PartialAck.EligibleMemberMask) == 0u);
	TestEqual(TEXT("Two accepted members are counted from the sanitized mask"),
		PartialAck.GetAcceptedMemberCount(), static_cast<uint8>(2u));
	TestTrue(TEXT("Per-member acceptance uses the frozen membership index"),
		PartialAck.IsMemberAccepted(0u) && PartialAck.IsMemberAccepted(24u)
			&& !PartialAck.IsMemberAccepted(1u));
	TestTrue(TEXT("A non-full accepted mask is normalized to partial acceptance"),
		PartialAck.Result == EGuLiCommandAckResult::PartiallyAccepted);

	FGuLiCohortCommandAck IneligibleAccepted;
	IneligibleAccepted.CohortId = FGuLiControlCohortId(10u);
	IneligibleAccepted.MemberCount = 3u;
	IneligibleAccepted.EligibleMemberMask = 0b001u;
	IneligibleAccepted.AcceptedMemberMask = 0b111u;
	IneligibleAccepted.Result = EGuLiCommandAckResult::Accepted;
	IneligibleAccepted.Sanitize();
	TestEqual(TEXT("Accepted bits are intersected with eligibility"),
		IneligibleAccepted.AcceptedMemberMask, 0b001u);
	TestTrue(TEXT("All eligible members accepted makes the cohort accepted"),
		IneligibleAccepted.Result == EGuLiCommandAckResult::Accepted);

	FGuLiCommandAck UnauthorizedAck = Ack;
	UnauthorizedAck.Result = EGuLiCommandAckResult::Unauthorized;
	UnauthorizedAck.Sanitize();
	TestTrue(TEXT("An asynchronous authority rejection keeps its explicit reason"),
		UnauthorizedAck.Result == EGuLiCommandAckResult::Unauthorized);
	TestEqual(TEXT("A rejected asynchronous move cannot retain an active batch"),
		UnauthorizedAck.BatchOrderId, 0u);
	for (const FGuLiCohortCommandAck& CohortAck : UnauthorizedAck.CohortResults)
	{
		TestEqual(TEXT("A rejected asynchronous move clears every accepted member bit"),
			CohortAck.AcceptedMemberMask, 0u);
	}

	FGuLiCommandAck OlderDuplicateAck = Ack;
	OlderDuplicateAck.Result = EGuLiCommandAckResult::Duplicate;
	OlderDuplicateAck.Sanitize();
	TestTrue(TEXT("An older move serial remains Duplicate instead of inheriting a cached success"),
		OlderDuplicateAck.Result == EGuLiCommandAckResult::Duplicate);
	TestEqual(TEXT("An older duplicate cannot claim the cached move batch"),
		OlderDuplicateAck.BatchOrderId, 0u);
	for (const FGuLiCohortCommandAck& CohortAck : OlderDuplicateAck.CohortResults)
	{
		TestEqual(TEXT("An older duplicate cannot claim cached accepted members"),
			CohortAck.AcceptedMemberMask, 0u);
	}
	FGuLiCommandAck EmptyUnauthorizedAck;
	EmptyUnauthorizedAck.CommandKind = EGuLiCommandKind::Move;
	EmptyUnauthorizedAck.ClientCommandId = 8u;
	EmptyUnauthorizedAck.BatchOrderId = 99u;
	EmptyUnauthorizedAck.Result = EGuLiCommandAckResult::Unauthorized;
	EmptyUnauthorizedAck.Sanitize();
	TestTrue(TEXT("An empty-cohort authority rejection keeps its explicit reason"),
		EmptyUnauthorizedAck.Result == EGuLiCommandAckResult::Unauthorized);
	TestEqual(TEXT("An empty-cohort authority rejection clears a stray batch"),
		EmptyUnauthorizedAck.BatchOrderId, 0u);

	FGuLiSelectionRequest SelectionRequest;
	SelectionRequest.Center = FVector(100.0, 200.0, 300.0);
	SelectionRequest.ClientRequestId = 1u;
	TestTrue(TEXT("Legacy radius intent remains well formed"), SelectionRequest.IsWellFormed());

	FGuLiMoveRequest MoveRequest;
	MoveRequest.Target = FVector(1000.0, 2000.0, 30.0);
	MoveRequest.SelectionRevision = 41u;
	MoveRequest.ClientCommandId = 2u;
	TestTrue(TEXT("Move intent fields remain valid"), MoveRequest.IsWellFormed());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGuLiCommanderMoveEndpointFastArrayContractTest,
	"GuLiStrike.Commander.Network.MoveEndpointFastArrayContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiCommanderMoveEndpointFastArrayContractTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FGuLiMoveEndpointFastArray Endpoints;
	TestTrue(TEXT("A valid active move endpoint is inserted"), Endpoints.Upsert(
		FGuLiSoldierId(1u), 7u, FVector(100.0, 200.0, 30.0), FVector(400.0, 500.0, 60.0)));
	const FGuLiMoveEndpointItem* First = Endpoints.Find(FGuLiSoldierId(1u));
	TestNotNull(TEXT("Endpoint lookup uses stable SoldierId"), First);
	if (First)
	{
		TestEqual(TEXT("New endpoint starts with a non-zero revision"), First->Revision, 1u);
		TestEqual(TEXT("Endpoint keeps the committed order"), First->ActiveOrderId, 7u);
	}
	TestFalse(TEXT("An identical upsert does not dirty the item"), Endpoints.Upsert(
		FGuLiSoldierId(1u), 7u, FVector(100.0, 200.0, 30.0), FVector(400.0, 500.0, 60.0)));
	TestTrue(TEXT("Changing the actual final slot advances the endpoint"), Endpoints.Upsert(
		FGuLiSoldierId(1u), 7u, FVector(100.0, 200.0, 30.0), FVector(800.0, 500.0, 60.0)));
	First = Endpoints.Find(FGuLiSoldierId(1u));
	if (First)
	{
		TestEqual(TEXT("Changed endpoint increments its per-soldier revision"), First->Revision, 2u);
	}
	TestFalse(TEXT("Zero order cannot create an active endpoint"), Endpoints.Upsert(
		FGuLiSoldierId(2u), 0u, FVector::ZeroVector, FVector::OneVector));
	TestFalse(TEXT("Non-finite endpoint coordinates are rejected"), Endpoints.Upsert(
		FGuLiSoldierId(2u), 8u,
		FVector(std::numeric_limits<double>::quiet_NaN(), 0.0, 0.0), FVector::OneVector));

	FGuLiMoveEndpointItem Replacement;
	Replacement.SoldierId = FGuLiSoldierId(2u);
	Replacement.ActiveOrderId = 9u;
	Replacement.CommandStart = FVector(10.0, 20.0, 30.0);
	Replacement.FinalDestination = FVector(40.0, 50.0, 60.0);
	TestEqual(TEXT("Bootstrap replacement removes stale endpoints and inserts active ones"),
		Endpoints.ReplaceWith(TConstArrayView<FGuLiMoveEndpointItem>(&Replacement, 1)), 2);
	TestNull(TEXT("Bootstrap replacement removes a no-longer-active soldier"),
		Endpoints.Find(FGuLiSoldierId(1u)));
	TestNotNull(TEXT("Bootstrap replacement exposes the current active endpoint"),
		Endpoints.Find(FGuLiSoldierId(2u)));
	TestEqual(TEXT("An identical full endpoint refresh is a no-op"),
		Endpoints.ReplaceWith(TConstArrayView<FGuLiMoveEndpointItem>(&Replacement, 1)), 0);
	const FGuLiMoveEndpointItem* Replaced = Endpoints.Find(FGuLiSoldierId(2u));
	TestNotNull(TEXT("No-op refresh preserves the endpoint"), Replaced);
	if (Replaced)
	{
		TestEqual(TEXT("No-op refresh preserves its revision"), Replaced->Revision, 1u);
	}
	Replacement.FinalDestination = FVector(80.0, 50.0, 60.0);
	TestEqual(TEXT("A changed full endpoint refresh touches only that soldier"),
		Endpoints.ReplaceWith(TConstArrayView<FGuLiMoveEndpointItem>(&Replacement, 1)), 1);
	Replaced = Endpoints.Find(FGuLiSoldierId(2u));
	if (Replaced)
	{
		TestEqual(TEXT("Changed full refresh advances its revision"), Replaced->Revision, 2u);
	}
	TestTrue(TEXT("Endpoint reset marks a non-empty set removed"), Endpoints.ResetEndpoints());
	TestTrue(TEXT("Endpoint reset leaves no stale entries"), Endpoints.Items.IsEmpty());
	TestFalse(TEXT("Resetting an already empty endpoint set is a no-op"), Endpoints.ResetEndpoints());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGuLiCommanderSoldierFastArrayContractTest,
	"GuLiStrike.Commander.Network.SoldierFastArrayContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiCommanderSoldierFastArrayContractTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FGuLiSoldierStateFastArray States;
	FGuLiSoldierStateItem& First = States.Items.AddDefaulted_GetRef();
	First.SoldierId = FGuLiSoldierId(1u);
	First.Team = EGuLiTeam::Red;
	First.UnitTypeId = 7u;
	First.Health = 75.25f;
	First.MaxHealth = 300.5f;
	First.StateRevision = 3u;
	First.ActiveOrderId = 8u;

	FGuLiSoldierStateItem& Duplicate = States.Items.AddDefaulted_GetRef();
	Duplicate.SoldierId = FGuLiSoldierId(1u);
	Duplicate.Team = EGuLiTeam::Blue;

	FGuLiSoldierStateItem& Destroyed = States.Items.AddDefaulted_GetRef();
	Destroyed.SoldierId = FGuLiSoldierId(2u);
	Destroyed.Team = EGuLiTeam::Blue;
	Destroyed.LifeState = EGuLiSoldierLifeState::Destroyed;
	Destroyed.Health = 90u;
	Destroyed.ActiveOrderId = 99u;

	FGuLiSoldierStateItem& ZeroHealth = States.Items.AddDefaulted_GetRef();
	ZeroHealth.SoldierId = FGuLiSoldierId(3u);
	ZeroHealth.Team = EGuLiTeam::Red;
	ZeroHealth.Health = 0u;
	ZeroHealth.ActiveOrderId = 100u;

	FGuLiSoldierStateItem& OverMaximumHealth = States.Items.AddDefaulted_GetRef();
	OverMaximumHealth.SoldierId = FGuLiSoldierId(4u);
	OverMaximumHealth.Team = EGuLiTeam::Blue;
	OverMaximumHealth.Health = 250u;
	OverMaximumHealth.MaxHealth = 120u;

	FGuLiSoldierStateItem& InvalidMaximumHealth = States.Items.AddDefaulted_GetRef();
	InvalidMaximumHealth.SoldierId = FGuLiSoldierId(5u);
	InvalidMaximumHealth.Team = EGuLiTeam::Red;
	InvalidMaximumHealth.Health = 10u;
	InvalidMaximumHealth.MaxHealth = 0u;

	FGuLiSoldierStateItem& Invalid = States.Items.AddDefaulted_GetRef();
	Invalid.SoldierId.Reset();
	States.Sanitize();

	TestEqual(TEXT("FastArray removes invalid and duplicate soldier identities"), States.Items.Num(), 5);
	const FGuLiSoldierStateItem* FirstResult = States.Find(FGuLiSoldierId(1u));
	TestNotNull(TEXT("FastArray lookup uses stable SoldierId"), FirstResult);
	if (FirstResult)
	{
		TestEqual(TEXT("First duplicate occurrence preserves fractional health"), FirstResult->Health, 75.25f);
		TestEqual(TEXT("Maximum health above 255 remains a reliable gameplay fact"), FirstResult->MaxHealth, 300.5f);
		TestEqual(TEXT("Unit type remains a reliable gameplay fact"), FirstResult->UnitTypeId, static_cast<uint16>(7u));
		TestTrue(TEXT("Living soldier remains alive"), FirstResult->IsAlive());
	}

	const FGuLiSoldierStateItem* DestroyedResult = States.Find(FGuLiSoldierId(2u));
	TestNotNull(TEXT("Destroyed soldier remains in reliable roster"), DestroyedResult);
	if (DestroyedResult)
	{
		TestEqual(TEXT("Destroyed state forces zero health"), DestroyedResult->Health, 0.0f);
		TestEqual(TEXT("Destroyed state clears active order"), DestroyedResult->ActiveOrderId, 0u);
	}

	const FGuLiSoldierStateItem* ZeroHealthResult = States.Find(FGuLiSoldierId(3u));
	TestNotNull(TEXT("Zero-health soldier remains addressable"), ZeroHealthResult);
	if (ZeroHealthResult)
	{
		TestTrue(TEXT("Zero health normalizes to Destroyed"),
			ZeroHealthResult->LifeState == EGuLiSoldierLifeState::Destroyed);
		TestEqual(TEXT("Zero health clears active order"), ZeroHealthResult->ActiveOrderId, 0u);
	}

	const FGuLiSoldierStateItem* OverMaximumResult = States.Find(FGuLiSoldierId(4u));
	TestNotNull(TEXT("Over-maximum-health soldier remains addressable"), OverMaximumResult);
	if (OverMaximumResult)
	{
		TestEqual(TEXT("Health is clamped to replicated MaxHealth"),
			OverMaximumResult->Health, 120.0f);
		TestTrue(TEXT("Clamped positive health remains alive"), OverMaximumResult->IsAlive());
	}

	const FGuLiSoldierStateItem* InvalidMaximumResult = States.Find(FGuLiSoldierId(5u));
	TestNotNull(TEXT("Invalid-maximum-health soldier remains addressable"), InvalidMaximumResult);
	if (InvalidMaximumResult)
	{
		TestEqual(TEXT("Zero MaxHealth normalizes to the safe fallback value"),
			InvalidMaximumResult->MaxHealth, 1.0f);
		TestEqual(TEXT("Health is clamped after MaxHealth normalization"),
			InvalidMaximumResult->Health, 1.0f);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGuLiCommanderSoldierSnapshotNotificationTest,
	"GuLiStrike.Commander.Network.SoldierSnapshotNotification",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiCommanderSoldierSnapshotNotificationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UWorld* TestWorld = UWorld::CreateWorld(EWorldType::Game, false);
	TestNotNull(TEXT("An isolated authority World exists"), TestWorld);
	if (!TestWorld)
	{
		return false;
	}

	AGuLiSoldierStateReplicator* Replicator =
		TestWorld->SpawnActor<AGuLiSoldierStateReplicator>();
	TestNotNull(TEXT("The isolated World owns a Soldier state replicator"), Replicator);
	if (!Replicator)
	{
		TestWorld->DestroyWorld(false);
		return false;
	}

	int32 NotificationCount = 0;
	uint32 LastNotifiedRevision = 0u;
	Replicator->OnSoldierStatesChanged.AddLambda(
		[&NotificationCount, &LastNotifiedRevision](const uint32 SnapshotRevision)
		{
			++NotificationCount;
			LastNotifiedRevision = SnapshotRevision;
		});

	TArray<FGuLiSoldierStateItem> Snapshot;
	FGuLiSoldierStateItem& State = Snapshot.AddDefaulted_GetRef();
	State.SoldierId = FGuLiSoldierId(1u);
	State.Team = EGuLiTeam::Red;
	State.UnitTypeId = 7u;
	State.Health = 90.25f;
	State.MaxHealth = 300.5f;
	State.StateRevision = 1u;

	TestEqual(TEXT("The first authority snapshot adds one Soldier"),
		Replicator->ApplyAuthoritySnapshot(Snapshot, 1001u), 1);
	TestEqual(TEXT("A changed authority snapshot emits exactly one batch notification"),
		NotificationCount, 1);
	TestEqual(TEXT("The authority notification carries the applied revision"),
		LastNotifiedRevision, Replicator->GetSnapshotRevision());

	TestEqual(TEXT("An identical authority snapshot changes no Soldier"),
		Replicator->ApplyAuthoritySnapshot(Snapshot, 1001u), 0);
	TestEqual(TEXT("An identical authority snapshot emits no notification"),
		NotificationCount, 1);

	const uint32 RevisionBeforeEpochChange = Replicator->GetSnapshotRevision();
	TestEqual(TEXT("The first snapshot establishes revision one"),
		RevisionBeforeEpochChange, 1u);
	TestEqual(TEXT("A new MatchEpoch can reuse an identical roster"),
		Replicator->ApplyAuthoritySnapshot(Snapshot, 2002u), 0);
	TestEqual(TEXT("A new MatchEpoch still emits one coherent snapshot notification"),
		NotificationCount, 2);
	TestEqual(TEXT("Snapshot revision advances across MatchEpoch so RepNotify cannot be elided"),
		Replicator->GetSnapshotRevision(), RevisionBeforeEpochChange + 1u);
	TestEqual(TEXT("Repeating the identical snapshot in the new MatchEpoch changes nothing"),
		Replicator->ApplyAuthoritySnapshot(Snapshot, 2002u), 0);
	TestEqual(TEXT("The repeated new-MatchEpoch snapshot emits no notification"),
		NotificationCount, 2);

	Snapshot[0].Health = 180.125f;
	Snapshot[0].MaxHealth = 600.5f;
	Snapshot[0].StateRevision = 2u;
	TestEqual(TEXT("A MaxHealth change updates the reliable Soldier item"),
		Replicator->ApplyAuthoritySnapshot(Snapshot, 2002u), 1);
	TestEqual(TEXT("A second changed snapshot emits one additional notification"),
		NotificationCount, 3);
	const FGuLiSoldierStateItem* UpdatedState =
		Replicator->FindSoldierState(FGuLiSoldierId(1u));
	TestNotNull(TEXT("The updated Soldier remains available"), UpdatedState);
	if (UpdatedState)
	{
		TestEqual(TEXT("Snapshot application copies MaxHealth"),
			UpdatedState->MaxHealth, 600.5f);
		TestEqual(TEXT("Snapshot application preserves fractional absolute health"),
			UpdatedState->Health, 180.125f);
		TestEqual(TEXT("New snapshot items copy their authority unit type"),
			UpdatedState->UnitTypeId, static_cast<uint16>(7u));
	}

	Snapshot[0].Team = static_cast<EGuLiTeam>(255u);
	Snapshot[0].LifeState = static_cast<EGuLiSoldierLifeState>(255u);
	Snapshot[0].Health = 250u;
	Snapshot[0].MaxHealth = 0u;
	Snapshot[0].StateRevision = 3u;
	TestEqual(TEXT("The authority path accepts one sanitized Soldier update"),
		Replicator->ApplyAuthoritySnapshot(Snapshot, 2002u), 1);
	TestEqual(TEXT("The sanitized authority update emits one batch notification"),
		NotificationCount, 4);
	const FGuLiSoldierStateItem* SanitizedState =
		Replicator->FindSoldierState(FGuLiSoldierId(1u));
	TestNotNull(TEXT("The sanitized Soldier remains available"), SanitizedState);
	if (SanitizedState)
	{
		TestTrue(TEXT("Invalid team normalizes before replication"),
			SanitizedState->Team == EGuLiTeam::Unassigned);
		TestTrue(TEXT("Invalid life state normalizes before replication"),
			SanitizedState->LifeState == EGuLiSoldierLifeState::Alive);
		TestEqual(TEXT("Zero MaxHealth normalizes in the authority apply path"),
			SanitizedState->MaxHealth, 1.0f);
		TestEqual(TEXT("Health clamps after authority MaxHealth normalization"),
			SanitizedState->Health, 1.0f);
	}

	Snapshot[0].UnitTypeId = 9u;
	TestEqual(TEXT("A type-only change dirties the reliable item even without a state revision change"),
		Replicator->ApplyAuthoritySnapshot(Snapshot, 2002u), 1);
	const FGuLiSoldierStateItem* RetypedState = Replicator->FindSoldierState(FGuLiSoldierId(1u));
	TestNotNull(TEXT("Retyped Soldier remains available"), RetypedState);
	if (RetypedState)
	{
		TestEqual(TEXT("Type-only update reaches the replicated roster"), RetypedState->UnitTypeId, static_cast<uint16>(9u));
	}
	TestEqual(TEXT("Unchanged unit type does not dirty the roster again"),
		Replicator->ApplyAuthoritySnapshot(Snapshot, 2002u), 0);

	const int32 NotificationsBeforeOnRep = NotificationCount;
	UFunction* OnRepFunction = Replicator->FindFunction(TEXT("OnRep_SnapshotRevision"));
	TestNotNull(TEXT("SnapshotRevision exposes a RepNotify handler"), OnRepFunction);
	Replicator->TestOnly_InvokeSnapshotRevisionRepNotify();
	TestEqual(TEXT("One replicated revision emits one client-side batch notification"),
		NotificationCount, NotificationsBeforeOnRep + 1);
	TestEqual(TEXT("The RepNotify notification carries the replicated revision"),
		LastNotifiedRevision, Replicator->GetSnapshotRevision());

	TestWorld->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGuLiCommanderPoseChunkContractTest,
	"GuLiStrike.Commander.Network.PoseChunkContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiCommanderPoseChunkContractTest::RunTest(const FString& Parameters)
{
	using namespace GuLiCommanderPoseCodec;
	FSender Sender, SameSender;
	FReceiver Receiver;
	Sender.Reset(7, 3); SameSender.Reset(7, 3); Receiver.Reset(7, 3);
	FGuLiSoldierPoseChunk Source = GuLiCommanderNetworkTests::MakePoseChunk(3, 123456, 1);
	Source.Samples.Reset();
	Source.ServerSimTick = 987654; Source.ServerTimeSeconds = 321.125f;
	Source.ChunkIndex = 4; Source.ChunkCount = 16;
	for (uint32 Index = 0; Index < 32; ++Index)
	{
		auto& Pose = Source.Samples.AddDefaulted_GetRef();
		Pose.SoldierId = FGuLiSoldierId(MAX_uint32 - Index * 10000000);
		Pose.WorldXUnits = MAX_int32 - Index; Pose.WorldYUnits = MIN_int32 + Index;
		Pose.WorldZUnits = MAX_int32;
		Pose.VelocityXUnits = MAX_int16; Pose.VelocityYUnits = MIN_int16;
		Pose.VelocityZUnits = MAX_int16;
		Pose.ActiveOrderId = MAX_uint32 - Index; Pose.FacingYaw = uint8(Index * 8);
		Pose.State = EGuLiSoldierPoseState::Moving;
		Pose.Flags = Index == 0 ? GULI_SOLDIER_POSE_FLAG_TELEPORT : 0;
	}
	TArray<FGuLiEncodedPoseBlock> Blocks, SameBlocks;
	Sender.Encode(Source, Blocks); SameSender.Encode(Source, SameBlocks);
	TestTrue(TEXT("Worst-case values split by actual wire size"), Blocks.Num() > 1);
	TestEqual(TEXT("Deterministic split count"), Blocks.Num(), SameBlocks.Num());
	int32 DecodedCount = 0;
	for (int32 Index = 0; Index < Blocks.Num(); ++Index)
	{
		TestTrue(TEXT("Byte-deterministic encoding"), Blocks[Index].Data == SameBlocks[Index].Data);
		FBitWriter Writer(8192, true);
		bool Success = false;
		Blocks[Index].NetSerialize(Writer, nullptr, Success);
		TestTrue(TEXT("Actual bit archive respects total 1000-byte payload budget"), Success && Writer.GetNumBytes() <= 1000);
		FBitReader Reader(Writer.GetData(), Writer.GetNumBits());
		FGuLiEncodedPoseBlock Wire;
		Wire.NetSerialize(Reader, nullptr, Success);
		TestTrue(TEXT("Bounded encoded RPC round trip"), Success && Wire.Data == Blocks[Index].Data);
		FGuLiSoldierPoseChunk Decoded;
		TestTrue(TEXT("Each split block decodes independently"), Receiver.Decode(Wire, Decoded) == EDecodeResult::Decoded);
		TestEqual(TEXT("Capture frame preserved"), Decoded.FrameSequence, Source.FrameSequence);
		TestEqual(TEXT("Simulation tick preserved"), Decoded.ServerSimTick, Source.ServerSimTick);
		TestEqual(TEXT("Simulation time preserved"), Decoded.ServerTimeSeconds, Source.ServerTimeSeconds);
		TestTrue(TEXT("Per-block sample cap"), Decoded.Samples.Num() <= 32);
		for (const auto& Copy : Decoded.Samples)
		{
			const auto* Original = Source.Samples.FindByPredicate([&Copy](const auto& P) { return P.SoldierId == Copy.SoldierId; });
			TestTrue(TEXT("Every quantized field survives"), Original && Copy.GetWorldLocationCentimeters() == Original->GetWorldLocationCentimeters()
				&& Copy.GetVelocityCentimetersPerSecond() == Original->GetVelocityCentimetersPerSecond()
				&& Copy.FacingYaw == Original->FacingYaw && Copy.ActiveOrderId == Original->ActiveOrderId
				&& Copy.State == Original->State && Copy.Flags == Original->Flags);
		}
		DecodedCount += Decoded.Samples.Num();
	}
	TestEqual(TEXT("All source units decoded exactly once"), DecodedCount, 32);
	FGuLiPoseAcknowledgment Ack, WireAck;
	TestTrue(TEXT("Decoded blocks generate ACK"), Receiver.BuildAcknowledgment(Ack));
	TestTrue(TEXT("ACK serializer preserves bitmap and session"), GuLiCommanderNetworkTests::NetSerializeRoundTrip(Ack, WireAck)
		&& WireAck.LatestSequence == Ack.LatestSequence && WireAck.SyncGeneration == 7 && WireAck.MatchEpoch == 3
		&& FMemory::Memcmp(Ack.ReceivedBits, WireAck.ReceivedBits, sizeof(Ack.ReceivedBits)) == 0);
	TestTrue(TEXT("Server confirms decoded samples"), Sender.Confirm(WireAck));
	auto WrongVersion = Blocks[0]; WrongVersion.Data[0] = 8;
	FGuLiSoldierPoseChunk Ignored;
	TestTrue(TEXT("Old protocol is rejected"), Receiver.Decode(WrongVersion, Ignored) == EDecodeResult::WrongSession);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGuLiCommanderAckRoutingAndFifoTest,
	"GuLiStrike.Commander.Network.AckRoutingAndFifo",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiCommanderAckRoutingAndFifoTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UGuLiCommanderNetSyncComponent* NetSync =
		NewObject<UGuLiCommanderNetSyncComponent>(GetTransientPackage());
	TestNotNull(TEXT("A transient NetSync component can host the client ACK queue"), NetSync);
	if (!NetSync)
	{
		return false;
	}

	TArray<FGuLiCommandAck> ExpectedAcks;
	ExpectedAcks.Add(GuLiCommanderNetworkTests::MakeAck(
		EGuLiCommandKind::Selection,
		42u));
	ExpectedAcks.Add(GuLiCommanderNetworkTests::MakeAck(
		EGuLiCommandKind::Move,
		42u,
		7001u));
	for (uint32 Index = 0u; Index < 10u; ++Index)
	{
		const EGuLiCommandKind Kind = (Index & 1u) == 0u
			? EGuLiCommandKind::Selection
			: EGuLiCommandKind::Move;
		ExpectedAcks.Add(GuLiCommanderNetworkTests::MakeAck(
			Kind,
			100u + Index,
			Kind == EGuLiCommandKind::Move ? 8000u + Index : 0u));
	}

	for (const FGuLiCommandAck& Ack : ExpectedAcks)
	{
		NetSync->TestOnly_ReceiveCommandAck(Ack);
	}

	TArray<FGuLiCommandAck> ReceivedAcks;
	NetSync->ConsumePendingCommandAcks(ReceivedAcks);
	TestEqual(TEXT("Every consecutive reliable ACK remains in the FIFO"),
		ReceivedAcks.Num(), ExpectedAcks.Num());
	for (int32 Index = 0; Index < FMath::Min(ReceivedAcks.Num(), ExpectedAcks.Num()); ++Index)
	{
		TestTrue(
			*FString::Printf(TEXT("ACK %d preserves CommandKind"), Index),
			ReceivedAcks[Index].CommandKind == ExpectedAcks[Index].CommandKind);
		TestEqual(
			*FString::Printf(TEXT("ACK %d preserves ClientCommandId"), Index),
			ReceivedAcks[Index].ClientCommandId,
			ExpectedAcks[Index].ClientCommandId);
		TestEqual(
			*FString::Printf(TEXT("ACK %d preserves BatchOrderId"), Index),
			ReceivedAcks[Index].BatchOrderId,
			ExpectedAcks[Index].BatchOrderId);
	}
	if (ReceivedAcks.Num() >= 2)
	{
		TestEqual(TEXT("Selection and Move may share the same numeric client id"),
			ReceivedAcks[0].ClientCommandId, ReceivedAcks[1].ClientCommandId);
		TestTrue(TEXT("CommandKind keeps equal numeric ids in independent namespaces"),
			ReceivedAcks[0].CommandKind == EGuLiCommandKind::Selection
				&& ReceivedAcks[1].CommandKind == EGuLiCommandKind::Move);
	}

	TArray<FGuLiCommandAck> SecondDrain;
	NetSync->ConsumePendingCommandAcks(SecondDrain);
	TestTrue(TEXT("Draining the ACK FIFO consumes it exactly once"), SecondDrain.IsEmpty());
	NetSync->TestOnly_ReceiveCommandAck(ExpectedAcks.Last());
	NetSync->TestOnly_ReceiveCommandAck(ExpectedAcks.Last());
	NetSync->ConsumePendingCommandAcks(SecondDrain);
	TestTrue(TEXT("Redundant fast/reliable delivery does not duplicate a completed ACK"),
		SecondDrain.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGuLiCommanderBootstrapSnapshotGateTest,
	"GuLiStrike.Commander.Network.BootstrapSnapshotGate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiCommanderBootstrapSnapshotGateTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	constexpr uint32 MatchEpoch = 0x12345678u;
	constexpr uint32 OtherMatchEpoch = 0x87654321u;
	constexpr uint32 SnapshotRevision = 19u;
	constexpr uint16 RosterCount = 500u;
	const auto PassesGate = [](
		const uint16 ProtocolVersion,
		const uint32 GameStateMatchEpoch,
		const uint32 SnapshotMatchEpoch,
		const uint32 AppliedSnapshotRevision,
		const int32 AppliedRosterCount,
		const uint32 ExpectedMatchEpoch,
		const uint32 ExpectedSnapshotRevision,
		const uint16 ExpectedRosterCount)
	{
		return UGuLiCommanderNetSyncComponent::TestOnly_IsBootstrapSnapshotCompatible(
			ProtocolVersion,
			GameStateMatchEpoch,
			SnapshotMatchEpoch,
			AppliedSnapshotRevision,
			AppliedRosterCount,
			ExpectedMatchEpoch,
			ExpectedSnapshotRevision,
			ExpectedRosterCount);
	};

	TestTrue(TEXT("Matching protocol, MatchEpoch, snapshot revision, and roster count open bootstrap"),
		PassesGate(
			GULI_COMMANDER_PROTOCOL_VERSION,
			MatchEpoch,
			MatchEpoch,
			SnapshotRevision,
			RosterCount,
			MatchEpoch,
			SnapshotRevision,
			RosterCount));
	TestTrue(TEXT("A newer applied snapshot revision also satisfies the high-water marker"),
		PassesGate(
			GULI_COMMANDER_PROTOCOL_VERSION,
			MatchEpoch,
			MatchEpoch,
			SnapshotRevision + 1u,
			RosterCount,
			MatchEpoch,
			SnapshotRevision,
			RosterCount));
	TestTrue(TEXT("Snapshot revision comparison remains wrap-safe"),
		PassesGate(
			GULI_COMMANDER_PROTOCOL_VERSION,
			MatchEpoch,
			MatchEpoch,
			1u,
			RosterCount,
			MatchEpoch,
			MAX_uint32,
			RosterCount));
	TestFalse(TEXT("A stale snapshot revision cannot open bootstrap"),
		PassesGate(
			GULI_COMMANDER_PROTOCOL_VERSION,
			MatchEpoch,
			MatchEpoch,
			SnapshotRevision - 1u,
			RosterCount,
			MatchEpoch,
			SnapshotRevision,
			RosterCount));
	TestFalse(TEXT("A zero snapshot revision cannot open bootstrap"),
		PassesGate(
			GULI_COMMANDER_PROTOCOL_VERSION,
			MatchEpoch,
			MatchEpoch,
			0u,
			RosterCount,
			MatchEpoch,
			SnapshotRevision,
			RosterCount));
	TestFalse(TEXT("A snapshot from another MatchEpoch cannot open bootstrap"),
		PassesGate(
			GULI_COMMANDER_PROTOCOL_VERSION,
			MatchEpoch,
			OtherMatchEpoch,
			SnapshotRevision,
			RosterCount,
			MatchEpoch,
			SnapshotRevision,
			RosterCount));
	TestFalse(TEXT("A stale GameState MatchEpoch cannot open bootstrap"),
		PassesGate(
			GULI_COMMANDER_PROTOCOL_VERSION,
			OtherMatchEpoch,
			MatchEpoch,
			SnapshotRevision,
			RosterCount,
			MatchEpoch,
			SnapshotRevision,
			RosterCount));
	TestFalse(TEXT("An underfilled reliable roster cannot open bootstrap"),
		PassesGate(
			GULI_COMMANDER_PROTOCOL_VERSION,
			MatchEpoch,
			MatchEpoch,
			SnapshotRevision,
			RosterCount - 1,
			MatchEpoch,
			SnapshotRevision,
			RosterCount));
	TestFalse(TEXT("A mismatched protocol cannot open bootstrap"),
		PassesGate(
			GULI_COMMANDER_PROTOCOL_VERSION + 1u,
			MatchEpoch,
			MatchEpoch,
			SnapshotRevision,
			RosterCount,
			MatchEpoch,
			SnapshotRevision,
			RosterCount));
	TestFalse(TEXT("A protocol-v8 client cannot join the v9 wire contract"),
		PassesGate(
			GULI_COMMANDER_PROTOCOL_VERSION - 1u,
			MatchEpoch,
			MatchEpoch,
			SnapshotRevision,
			RosterCount,
			MatchEpoch,
			SnapshotRevision,
			RosterCount));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGuLiCommanderPoseMatchEpochGateTest,
	"GuLiStrike.Commander.Network.PoseMatchEpochGate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiCommanderPoseMatchEpochGateTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UGuLiCommanderNetSyncComponent* NetSync =
		NewObject<UGuLiCommanderNetSyncComponent>(GetTransientPackage());
	TestNotNull(TEXT("A transient NetSync component can host the pose admission gate"), NetSync);
	if (!NetSync)
	{
		return false;
	}

	constexpr uint32 MatchEpoch = 1001u;
	constexpr uint32 OtherMatchEpoch = 2002u;
	NetSync->TestOnly_ConfigureClientPoseGate(true, MatchEpoch);
	NetSync->TestOnly_ReceivePoseBlock(
		GuLiCommanderNetworkTests::MakePoseBlock(MatchEpoch, 1u, 1u));
	TArray<FGuLiSoldierPoseChunk> AcceptedChunks;
	NetSync->ConsumePendingPoseChunks(AcceptedChunks);
	TestEqual(TEXT("A pose from the accepted MatchEpoch enters the client queue"),
		AcceptedChunks.Num(), 1);
	TestEqual(TEXT("The first accepted chunk advances one fresh pose frame"),
		NetSync->GetAcceptedPoseFrameCount(), static_cast<uint64>(1u));

	NetSync->TestOnly_ReceivePoseBlock(
		GuLiCommanderNetworkTests::MakePoseBlock(OtherMatchEpoch, 2u, 1u));
	TArray<FGuLiSoldierPoseChunk> CrossMatchChunks;
	NetSync->ConsumePendingPoseChunks(CrossMatchChunks);
	TestTrue(TEXT("A pose from another MatchEpoch is rejected before interpolation"),
		CrossMatchChunks.IsEmpty());
	TestEqual(TEXT("A rejected cross-MatchEpoch chunk cannot advance pose freshness"),
		NetSync->GetAcceptedPoseFrameCount(), static_cast<uint64>(1u));

	FGuLiEncodedPoseBlock WrongProtocolBlock =
		GuLiCommanderNetworkTests::MakePoseBlock(MatchEpoch, 3u, 1u);
	// The first 16 bits are the real wire version; no decoded DTO can bypass this boundary.
	WrongProtocolBlock.Data[0] = uint8(GULI_COMMANDER_PROTOCOL_VERSION - 1u);
	NetSync->TestOnly_ReceivePoseBlock(WrongProtocolBlock);
	TArray<FGuLiSoldierPoseChunk> WrongProtocolChunks;
	NetSync->ConsumePendingPoseChunks(WrongProtocolChunks);
	TestTrue(TEXT("A pose with another protocol version is rejected by the runtime gate"),
		WrongProtocolChunks.IsEmpty());

	NetSync->TestOnly_ConfigureClientPoseGate(false, MatchEpoch);
	NetSync->TestOnly_ReceivePoseBlock(
		GuLiCommanderNetworkTests::MakePoseBlock(MatchEpoch, 4u, 1u));
	TArray<FGuLiSoldierPoseChunk> PreBootstrapChunks;
	NetSync->ConsumePendingPoseChunks(PreBootstrapChunks);
	TestTrue(TEXT("A matching pose is rejected until the roster/bootstrap gate is ready"),
		PreBootstrapChunks.IsEmpty());

	NetSync->TestOnly_ConfigureClientPoseGate(true, OtherMatchEpoch);
	NetSync->TestOnly_ReceivePoseBlock(
		GuLiCommanderNetworkTests::MakePoseBlock(OtherMatchEpoch, 5u, 1u));
	TArray<FGuLiSoldierPoseChunk> NewMatchChunks;
	NetSync->ConsumePendingPoseChunks(NewMatchChunks);
	TestEqual(TEXT("The same pose is accepted after the client explicitly adopts the new MatchEpoch"),
		NewMatchChunks.Num(), 1);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGuLiCommanderBootstrapMatchEpochLifecycleTest,
	"GuLiStrike.Commander.Network.BootstrapMatchEpochLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiCommanderBootstrapMatchEpochLifecycleTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	constexpr uint32 OldGeneration = 17u;
	constexpr uint32 OldMatchEpoch = 1001u;
	constexpr uint32 NewMatchEpoch = 2002u;
	constexpr bool bCopiedPlayerStateSyncReady = false;
	TestTrue(TEXT("A persistent old generation starts a fresh bootstrap for the copied not-ready PlayerState in a new MatchEpoch"),
		UGuLiCommanderNetSyncComponent::TestOnly_ShouldStartNewServerBootstrap(
			OldGeneration,
			OldMatchEpoch,
			NewMatchEpoch,
			bCopiedPlayerStateSyncReady));
	TestFalse(TEXT("A pending bootstrap for the current MatchEpoch is resent instead of incremented"),
		UGuLiCommanderNetSyncComponent::TestOnly_ShouldStartNewServerBootstrap(
			OldGeneration,
			NewMatchEpoch,
			NewMatchEpoch,
			false));
	TestFalse(TEXT("A ready bootstrap for the current MatchEpoch remains stable"),
		UGuLiCommanderNetSyncComponent::TestOnly_ShouldStartNewServerBootstrap(
			OldGeneration,
			NewMatchEpoch,
			NewMatchEpoch,
			true));
	TestTrue(TEXT("First connection with no generation starts the current MatchEpoch"),
		UGuLiCommanderNetSyncComponent::TestOnly_ShouldStartNewServerBootstrap(
			0u,
			0u,
			NewMatchEpoch,
			false));
	TestFalse(TEXT("An uninitialized zero authority epoch never starts bootstrap"),
		UGuLiCommanderNetSyncComponent::TestOnly_ShouldStartNewServerBootstrap(
			OldGeneration,
			OldMatchEpoch,
			0u,
			false));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGuLiCommanderNetworkGateEvidenceTest,
	"GuLiStrike.Commander.Network.NetworkGateEvidence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiCommanderNetworkGateEvidenceTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FPacketSimulationSettings RequiredSettings;
	RequiredSettings.PktLag = 100;
	RequiredSettings.PktJitter = 30;
	RequiredSettings.PktLoss = 5;
	RequiredSettings.PktOrder = 1;
	const FGuLiCommanderNetworkImpairmentEvidence RequiredImpairment =
		GuLiCommanderNetworkGateValidation::EvaluateImpairment(
			RequiredSettings,
			true,
			100.0f);
	TestTrue(TEXT("Runtime packet simulation at 100ms RTT / 30ms jitter / 5% loss is valid"),
		GuLiCommanderNetworkGateValidation::MeetsRequiredImpairment(RequiredImpairment));
	const FGuLiCommanderNetworkImpairmentEvidence DisconnectedImpairment =
		GuLiCommanderNetworkGateValidation::EvaluateImpairment(
			RequiredSettings,
			false,
			100.0f);
	TestFalse(TEXT("Configured switches without an active server connection cannot pass"),
		GuLiCommanderNetworkGateValidation::MeetsRequiredImpairment(DisconnectedImpairment));

	FPacketSimulationSettings NaturalLatencyOnly;
	const FGuLiCommanderNetworkImpairmentEvidence MissingConfiguredDamage =
		GuLiCommanderNetworkGateValidation::EvaluateImpairment(
			NaturalLatencyOnly,
			true,
			100.0f);
	TestFalse(TEXT("Natural ping alone cannot masquerade as configured packet impairment"),
		GuLiCommanderNetworkGateValidation::MeetsRequiredImpairment(MissingConfiguredDamage));

	FPacketSimulationSettings SplitLagSettings;
	SplitLagSettings.PktLag = 50;
	SplitLagSettings.PktJitter = 30;
	SplitLagSettings.PktLoss = 5;
	SplitLagSettings.PktOrder = 1;
	const FGuLiCommanderNetworkImpairmentEvidence SplitLagObserved =
		GuLiCommanderNetworkGateValidation::EvaluateImpairment(
			SplitLagSettings,
			true,
			100.0f);
	TestTrue(TEXT("Measured 100ms RTT can confirm a split client/server lag configuration"),
		GuLiCommanderNetworkGateValidation::MeetsRequiredImpairment(SplitLagObserved));

	FPacketSimulationSettings MissingReorderingSettings = RequiredSettings;
	MissingReorderingSettings.PktOrder = 0;
	const FGuLiCommanderNetworkImpairmentEvidence MissingReordering =
		GuLiCommanderNetworkGateValidation::EvaluateImpairment(
			MissingReorderingSettings,
			true,
			100.0f);
	TestFalse(TEXT("Lag, jitter and loss without packet reordering cannot satisfy the full gate"),
		GuLiCommanderNetworkGateValidation::MeetsRequiredImpairment(MissingReordering));

	FGuLiCommanderNetworkGateEvidence PassingGate;
	PassingGate.bCommandsSucceeded = true;
	PassingGate.bRuntimeImpairmentValid = true;
	PassingGate.ReceivedMoveAckSamples = 5;
	PassingGate.DesiredMoveAckSamples = 5;
	PassingGate.AckP95Milliseconds = 140.0;
	PassingGate.BandwidthSampleCount = 1;
	PassingGate.BandwidthAverageMegabits = 1.4;
	PassingGate.BandwidthP95Megabits = 1.9;
	PassingGate.FreshPoseFrameCount = 1u;
	PassingGate.PresentedStepSampleCount = 60;
	PassingGate.PresentedTravelDistanceCentimeters = 120.0;
	PassingGate.PresentedStepP95Centimeters = 12.0;
	PassingGate.PresentationClockRoundTripMilliseconds = 100.0;
	PassingGate.MaximumSeedPoseGapSeconds = 0.2;
	PassingGate.UntaggedHardSnapCount = 0u;
	TestTrue(TEXT("Complete transport and smooth moving presentation evidence passes"),
		GuLiCommanderNetworkGateValidation::CanPass(PassingGate));

	FGuLiCommanderNetworkGateEvidence ZeroBandwidthSamples = PassingGate;
	ZeroBandwidthSamples.BandwidthSampleCount = 0;
	TestFalse(TEXT("A zero-sample bandwidth average cannot produce a false PASS"),
		GuLiCommanderNetworkGateValidation::CanPass(ZeroBandwidthSamples));
	FGuLiCommanderNetworkGateEvidence ZeroFreshPoseFrames = PassingGate;
	ZeroFreshPoseFrames.FreshPoseFrameCount = 0u;
	TestFalse(TEXT("A held transform without a newly received pose frame cannot produce a false PASS"),
		GuLiCommanderNetworkGateValidation::CanPass(ZeroFreshPoseFrames));
	FGuLiCommanderNetworkGateEvidence NoPresentedMovement = PassingGate;
	NoPresentedMovement.PresentedTravelDistanceCentimeters = 0.0;
	TestFalse(TEXT("Fresh poses without actual presented movement cannot produce a false PASS"),
		GuLiCommanderNetworkGateValidation::CanPass(NoPresentedMovement));
	FGuLiCommanderNetworkGateEvidence TooFewPresentedSteps = PassingGate;
	TooFewPresentedSteps.PresentedStepSampleCount =
		GuLiCommanderNetworkGateValidation::RequiredPresentedStepSamples - 1;
	TestFalse(TEXT("Too few moving presentation samples cannot produce a false PASS"),
		GuLiCommanderNetworkGateValidation::CanPass(TooFewPresentedSteps));
	FGuLiCommanderNetworkGateEvidence StairSteppedPresentation = PassingGate;
	StairSteppedPresentation.PresentedStepP95Centimeters = 36.0;
	TestFalse(TEXT("A 10 Hz 180cm presentation staircase cannot pass the smoothness gate"),
		GuLiCommanderNetworkGateValidation::CanPass(StairSteppedPresentation));
	FGuLiCommanderNetworkGateEvidence MissingClockRoundTrip = PassingGate;
	MissingClockRoundTrip.PresentationClockRoundTripMilliseconds = 0.0;
	TestFalse(TEXT("A zero RTT presentation clock cannot pass the smoothness gate"),
		GuLiCommanderNetworkGateValidation::CanPass(MissingClockRoundTrip));
	FGuLiCommanderNetworkGateEvidence StarvedSeedPose = PassingGate;
	StarvedSeedPose.MaximumSeedPoseGapSeconds = 0.6;
	TestFalse(TEXT("A seed Soldier pose gap long enough to force correction cannot pass"),
		GuLiCommanderNetworkGateValidation::CanPass(StarvedSeedPose));
	FGuLiCommanderNetworkGateEvidence UntaggedHardSnap = PassingGate;
	UntaggedHardSnap.UntaggedHardSnapCount = 1u;
	TestFalse(TEXT("An untagged hard snap cannot be hidden by a good p95"),
		GuLiCommanderNetworkGateValidation::CanPass(UntaggedHardSnap));
	FGuLiCommanderNetworkGateEvidence MissingImpairment = PassingGate;
	MissingImpairment.bRuntimeImpairmentValid = false;
	TestFalse(TEXT("A healthy un-impaired connection cannot produce a false PASS"),
		GuLiCommanderNetworkGateValidation::CanPass(MissingImpairment));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGuLiCommanderPresentationServerClockTest,
	"GuLiStrike.Commander.Network.PresentationServerClock",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiCommanderPresentationServerClockTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	constexpr double SampleServerTimeSeconds = 100.0;
	constexpr float RoundTripMilliseconds = 100.0f;
	constexpr float BackTimeSeconds = 0.1f;
	const double EstimatedServerNowSeconds =
		AGuLiCommanderPresentationActor::TestOnly_EstimateServerNowAtPoseReceipt(
			SampleServerTimeSeconds,
			RoundTripMilliseconds);
	const double RenderServerTimeSeconds =
		AGuLiCommanderPresentationActor::TestOnly_CalculateRenderServerTime(
			EstimatedServerNowSeconds,
			BackTimeSeconds);
	TestTrue(TEXT("RTT/2 advances the received sample estimate by 50ms"),
		FMath::IsNearlyEqual(EstimatedServerNowSeconds, 100.05, 1.e-6));
	TestTrue(TEXT("Render time is 100ms behind estimated current server time, not 150ms"),
		FMath::IsNearlyEqual(
			EstimatedServerNowSeconds - RenderServerTimeSeconds,
			0.1,
			1.e-6));
	TestTrue(TEXT("At 100ms RTT the 100ms render delay resolves to server time 99.95, not 99.90"),
		FMath::IsNearlyEqual(RenderServerTimeSeconds, 99.95, 1.e-6));
	TestTrue(TEXT("A missing RTT safely falls back to the sample timestamp"),
		FMath::IsNearlyEqual(
			AGuLiCommanderPresentationActor::TestOnly_EstimateServerNowAtPoseReceipt(
				SampleServerTimeSeconds,
				0.0f),
			SampleServerTimeSeconds,
			1.e-6));
	TestTrue(TEXT("The latest clock measurement advances with local time instead of freezing between 10Hz poses"),
		FMath::IsNearlyEqual(
			AGuLiCommanderPresentationActor::TestOnly_AdvanceEstimatedServerTime(
				EstimatedServerNowSeconds,
				EstimatedServerNowSeconds,
				1.0 / 60.0,
				1.0f / 60.0f),
			EstimatedServerNowSeconds + 1.0 / 60.0,
			1.e-5));
	bool bFromConnectionStats = false;
	TestTrue(TEXT("Connection RTT remains available when PlayerState ping has not populated"),
		FMath::IsNearlyEqual(
			AGuLiCommanderPresentationActor::TestOnly_SelectClockRoundTripMilliseconds(
				0.1f,
				0.0,
				0.0f,
				bFromConnectionStats),
			100.0f,
			0.01f));
	TestTrue(TEXT("The RTT/2 clock identifies connection statistics as its source"),
		bFromConnectionStats);
	bFromConnectionStats = true;
	TestTrue(TEXT("PlayerState ping remains a fallback before connection statistics are ready"),
		FMath::IsNearlyEqual(
			AGuLiCommanderPresentationActor::TestOnly_SelectClockRoundTripMilliseconds(
				0.0f,
				0.0,
				100.0f,
				bFromConnectionStats),
			100.0f,
			0.01f));
	TestFalse(TEXT("The fallback source is reported as PlayerState"), bFromConnectionStats);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGuLiCommanderFloatHealthContractTest,
	"GuLiStrike.Commander.Network.FloatHealthContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiCommanderFloatHealthContractTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	TestNotNull(TEXT("Reliable Health is a reflected float, not an integer or normalized byte"),
		FindFProperty<FFloatProperty>(FGuLiSoldierStateItem::StaticStruct(),
			GET_MEMBER_NAME_CHECKED(FGuLiSoldierStateItem, Health)));
	TestNotNull(TEXT("Reliable MaxHealth is a reflected float"),
		FindFProperty<FFloatProperty>(FGuLiSoldierStateItem::StaticStruct(),
			GET_MEMBER_NAME_CHECKED(FGuLiSoldierStateItem, MaxHealth)));

	FGuLiSoldierStateItem Small;
	Small.Health = 0.25f;
	Small.MaxHealth = 0.5f;
	Small.Sanitize();
	TestEqual(TEXT("Positive maximum below one is not silently raised"), Small.MaxHealth, 0.5f);
	TestEqual(TEXT("Sub-unit health remains fractional"), Small.Health, 0.25f);
	TestTrue(TEXT("Sub-unit health is alive"), Small.IsAlive());
	for (const float InvalidHealth : {-1.0f, std::numeric_limits<float>::quiet_NaN(),
		std::numeric_limits<float>::infinity()})
	{
		FGuLiSoldierStateItem Invalid;
		Invalid.Health = InvalidHealth;
		Invalid.ActiveOrderId = 10u;
		Invalid.Sanitize();
		TestEqual(TEXT("Invalid health is finite zero"), Invalid.Health, 0.0f);
		TestFalse(TEXT("Invalid health cannot leave a live unit"), Invalid.IsAlive());
		TestEqual(TEXT("Invalid health clears active orders on death"), Invalid.ActiveOrderId, 0u);
	}
	Small.Health = 0.0f;
	Small.Sanitize();
	Small.MaxHealth = 300.5f;
	Small.Sanitize();
	TestFalse(TEXT("Increasing a dead unit's maximum never revives it"), Small.IsAlive());

	UWorld* TestWorld = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("An isolated replication World exists"), TestWorld))
	{
		return false;
	}
	AGuLiSoldierStateReplicator* Replicator = TestWorld->SpawnActor<AGuLiSoldierStateReplicator>();
	if (!TestNotNull(TEXT("Float-health snapshot replicator exists"), Replicator))
	{
		TestWorld->DestroyWorld(false);
		return false;
	}
	TArray<FGuLiSoldierStateItem> Snapshot;
	FGuLiSoldierStateItem& State = Snapshot.AddDefaulted_GetRef();
	State.SoldierId = FGuLiSoldierId(1u);
	State.Team = EGuLiTeam::Red;
	State.MaxHealth = 300.5f;
	State.Health = 299.75f;
	TestEqual(TEXT("Initial fractional snapshot creates one item"), Replicator->ApplyAuthoritySnapshot(Snapshot, 1u), 1);
	State.Health -= 0.125f;
	TestEqual(TEXT("Fractional damage alone dirties the reliable item without a revision change"),
		Replicator->ApplyAuthoritySnapshot(Snapshot, 1u), 1);
	const FGuLiSoldierStateItem* Stored = Replicator->FindSoldierState(State.SoldierId);
	if (TestNotNull(TEXT("Fractional snapshot is addressable"), Stored))
	{
		TestEqual(TEXT("Fractional damage is not truncated at the replication boundary"), Stored->Health, 299.625f);
	}
	State.Health = std::numeric_limits<float>::quiet_NaN();
	State.MaxHealth = std::numeric_limits<float>::infinity();
	TestEqual(TEXT("Malformed float snapshot is sanitized once"), Replicator->ApplyAuthoritySnapshot(Snapshot, 1u), 1);
	TestEqual(TEXT("Repeated NaN input does not dirty or notify every capture"),
		Replicator->ApplyAuthoritySnapshot(Snapshot, 1u), 0);
	Stored = Replicator->FindSoldierState(State.SoldierId);
	if (TestNotNull(TEXT("Sanitized snapshot remains addressable"), Stored))
	{
		TestEqual(TEXT("Non-finite maximum falls back safely"), Stored->MaxHealth, 1.0f);
		TestEqual(TEXT("Non-finite health is never replicated"), Stored->Health, 0.0f);
	}
	TestWorld->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiPoseQuantizationTest,
	"GuLiStrike.Commander.Network.PoseQuantization", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGuLiPoseQuantizationTest::RunTest(const FString& Parameters)
{
	FGuLiQuantizedSoldierPose Pose;
	TestTrue(TEXT("Quantize signed half steps"), GuLiCommanderPoseCodec::Quantize(FVector(10, -10, -1), FVector(10, -10, 30), 359.8f, Pose));
	TestEqual(TEXT("XY positive half rounds away"), Pose.WorldXUnits, 1);
	TestEqual(TEXT("XY negative half rounds away"), Pose.WorldYUnits, -1);
	TestEqual(TEXT("Z negative half rounds away"), Pose.WorldZUnits, -1);
	TestEqual(TEXT("Velocity uses meters/second"), Pose.VelocityXUnits, int16(1));
	TestEqual(TEXT("Signed velocity"), Pose.VelocityYUnits, int16(-1));
	TestEqual(TEXT("Yaw wraps to zero"), Pose.FacingYaw, uint8(0));
	TestEqual(TEXT("Negative yaw wraps"), GuLiCommanderProtocol::QuantizeYawDegrees(-1.40625f), uint8(255));
	for (int32 Index = -1000; Index <= 1000; ++Index)
	{
		const FVector Position(Index * 53.7, Index * -71.1, Index * 3.1);
		const FVector Velocity(Index * 1.7, Index * -2.3, Index * 0.8);
		const float Yaw = Index * 0.37f;
		if (!GuLiCommanderPoseCodec::Quantize(Position, Velocity, Yaw, Pose)) return false;
		const FVector Error = (Pose.GetWorldLocationCentimeters() - Position).GetAbs();
		TestTrue(TEXT("Position error stays within 10/10/1 cm"), Error.X <= 10.0001 && Error.Y <= 10.0001 && Error.Z <= 1.0001);
		TestTrue(TEXT("Velocity component error stays within 0.1m/s"), (Pose.GetVelocityCentimetersPerSecond() - Velocity).GetAbs().GetMax() <= 10.0001);
		TestTrue(TEXT("Yaw error stays within half an 8-bit step"), FMath::Abs(FMath::FindDeltaAngleDegrees(Yaw,
			GuLiCommanderProtocol::DequantizeYawDegrees(Pose.FacingYaw))) <= 0.7032f);
	}
	TestFalse(TEXT("Out-of-range world coordinate is rejected, not saturated"), Pose.SetWorldLocationCentimeters(FVector(1e15, 0, 0)));
	TestFalse(TEXT("Out-of-range velocity is rejected"), Pose.SetVelocityCentimetersPerSecond(FVector(1e15, 0, 0)));
	TestFalse(TEXT("Non-finite network sample is rejected"), GuLiCommanderPoseCodec::Quantize(FVector::ZeroVector,
		FVector::ZeroVector, std::numeric_limits<float>::quiet_NaN(), Pose));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiPosePredictionTest,
	"GuLiStrike.Commander.Network.PosePredictionAndFields", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGuLiPosePredictionTest::RunTest(const FString& Parameters)
{
	using namespace GuLiCommanderPoseCodec;
	FSender Sender; FReceiver Receiver;
	Sender.Reset(2, 4); Receiver.Reset(2, 4);
	auto Source = GuLiCommanderNetworkTests::MakePoseChunk(4, 1, 99);
	auto& Pose = Source.Samples[0];
	Pose.WorldXUnits = -100; Pose.WorldYUnits = 100; Pose.WorldZUnits = 7;
	Pose.VelocityXUnits = 10; Pose.VelocityYUnits = -10; Pose.VelocityZUnits = 2;
	Pose.FacingYaw = 255; Pose.State = EGuLiSoldierPoseState::Moving; Pose.ActiveOrderId = 123;
	TArray<FGuLiEncodedPoseBlock> Blocks;
	Sender.Encode(Source, Blocks);
	const int32 AbsoluteBytes = Blocks[0].Data.Num();
	FGuLiSoldierPoseChunk Decoded;
	TestTrue(TEXT("Initial absolute sample"), Receiver.Decode(Blocks[0], Decoded) == EDecodeResult::Decoded);
	FGuLiPoseAcknowledgment Ack;
	Receiver.BuildAcknowledgment(Ack); Sender.Confirm(Ack);
	Source.FrameSequence = 2; Source.ServerSimTick = 2; Source.ServerTimeSeconds = 0.2f;
	++Pose.WorldXUnits; --Pose.WorldYUnits; Pose.WorldZUnits += 2;
	Sender.Encode(Source, Blocks);
	const int32 PredictionBytes = Blocks[0].Data.Num();
	TestTrue(TEXT("Constant-velocity fields compress"), PredictionBytes < AbsoluteBytes);
	TestTrue(TEXT("Current-tick prediction decodes"), Receiver.Decode(Blocks[0], Decoded) == EDecodeResult::Decoded);
	TestTrue(TEXT("Omitted positions still advance at the sample cadence"), Decoded.Samples[0].GetWorldLocationCentimeters() == Pose.GetWorldLocationCentimeters()
		&& Decoded.ServerSimTick == 2 && Decoded.ServerTimeSeconds == 0.2f);
	Receiver.BuildAcknowledgment(Ack); Sender.Confirm(Ack);
	Source.FrameSequence = 3; Source.ServerSimTick = 3; Source.ServerTimeSeconds = 0.3f;
	Pose.WorldXUnits += 2; Pose.WorldYUnits += 3; Pose.WorldZUnits -= 2;
	Pose.VelocityXUnits = -36; Pose.VelocityYUnits = 21; Pose.VelocityZUnits = -3;
	Pose.FacingYaw = 0; Pose.ActiveOrderId = MAX_uint32;
	Sender.Encode(Source, Blocks);
	TestTrue(TEXT("Turning and order changes decode"), Receiver.Decode(Blocks[0], Decoded) == EDecodeResult::Decoded);
	TestTrue(TEXT("All component deltas are exact"), Decoded.Samples[0].GetWorldLocationCentimeters() == Pose.GetWorldLocationCentimeters()
		&& Decoded.Samples[0].GetVelocityCentimetersPerSecond() == Pose.GetVelocityCentimetersPerSecond()
		&& Decoded.Samples[0].FacingYaw == 0 && Decoded.Samples[0].ActiveOrderId == MAX_uint32);
	Receiver.BuildAcknowledgment(Ack); Sender.Confirm(Ack);
	Source.FrameSequence = 4; Source.ServerSimTick = 4;
	Pose.SetVelocityCentimetersPerSecond(FVector::ZeroVector); Pose.State = EGuLiSoldierPoseState::Idle; Pose.ActiveOrderId = 0;
	Sender.Encode(Source, Blocks); Receiver.Decode(Blocks[0], Decoded);
	TestTrue(TEXT("Stop cancels predicted motion with exact residuals"), Decoded.Samples[0].GetWorldLocationCentimeters() == Pose.GetWorldLocationCentimeters()
		&& Decoded.Samples[0].GetVelocityCentimetersPerSecond().IsZero() && Decoded.Samples[0].State == EGuLiSoldierPoseState::Idle);
	Receiver.BuildAcknowledgment(Ack); Sender.Confirm(Ack);
	Source.FrameSequence = 5; Source.ServerSimTick = 5;
	Sender.Encode(Source, Blocks); Receiver.Decode(Blocks[0], Decoded);
	TestTrue(TEXT("Unchanged fields still create a complete current sample"), Decoded.ServerSimTick == 5
		&& Decoded.Samples[0].GetWorldLocationCentimeters() == Pose.GetWorldLocationCentimeters() && Blocks[0].Data.Num() <= PredictionBytes);
	Pose.Flags = GULI_SOLDIER_POSE_FLAG_TELEPORT; Pose.WorldXUnits = 98765;
	++Source.FrameSequence; ++Source.ServerSimTick;
	Sender.Encode(Source, Blocks);
	FReceiver Fresh; Fresh.Reset(2, 4);
	TestTrue(TEXT("Teleport has no historical dependency"), Fresh.Decode(Blocks[0], Decoded) == EDecodeResult::Decoded
		&& Decoded.Samples[0].IsTeleport() && Decoded.Samples[0].WorldXUnits == 98765);
	Receiver.Decode(Blocks[0], Decoded); Receiver.BuildAcknowledgment(Ack); Sender.Confirm(Ack);
	Pose.Flags = 0; ++Source.FrameSequence; ++Source.ServerSimTick;
	Sender.Encode(Source, Blocks); Receiver.Decode(Blocks[0], Decoded);
	TestFalse(TEXT("Teleport flag clears through its own field delta"), Decoded.Samples[0].IsTeleport());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiPoseLossAndSessionTest,
	"GuLiStrike.Commander.Network.PoseLossReorderingAndSession", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGuLiPoseLossAndSessionTest::RunTest(const FString& Parameters)
{
	using namespace GuLiCommanderPoseCodec;
	FSender Sender; FReceiver Receiver;
	Sender.Reset(1, 3); Receiver.Reset(1, 3);
	auto Source = GuLiCommanderNetworkTests::MakePoseChunk(3, 1, 9);
	TArray<FGuLiEncodedPoseBlock> First, Second, Third;
	Sender.Encode(Source, First);
	++Source.FrameSequence; ++Source.ServerSimTick;
	Sender.Encode(Source, Second); // The first block was lost: no speculative baseline.
	FGuLiSoldierPoseChunk Decoded;
	TestTrue(TEXT("Loss before any ACK sends a fresh absolute"), Receiver.Decode(Second[0], Decoded) == EDecodeResult::Decoded);
	TestTrue(TEXT("Out-of-order old absolute can fill history"), Receiver.Decode(First[0], Decoded) == EDecodeResult::Decoded);
	TestTrue(TEXT("Duplicate is identified without duplicate sample insertion"), Receiver.Decode(Second[0], Decoded) == EDecodeResult::Duplicate);
	FGuLiPoseAcknowledgment Ack;
	Receiver.BuildAcknowledgment(Ack);
	TestTrue(TEXT("ACK bitmap includes both decoded blocks"), Ack.LatestSequence == 2 && (Ack.ReceivedBits[0] & 3) == 3);
	TestTrue(TEXT("Confirm complete decoded history"), Sender.Confirm(Ack));
	auto FutureAck = Ack; FutureAck.LatestSequence = 9999;
	TestFalse(TEXT("Future ACK cannot promote a baseline"), Sender.Confirm(FutureAck));
	++Source.FrameSequence; ++Source.ServerSimTick;
	Sender.Encode(Source, Third);
	FReceiver Missing; Missing.Reset(1, 3);
	TestTrue(TEXT("Missing baseline drops whole delta block"), Missing.Decode(Third[0], Decoded) == EDecodeResult::MissingBaseline);
	TestFalse(TEXT("Incomplete decoding is not acknowledged"), Missing.BuildAcknowledgment(FutureAck));
	// A malformed new block cannot confirm its already decoded prefix.
	auto Truncated = Third[0]; Truncated.Data.Pop();
	TestTrue(TEXT("Truncated block is rejected"), Receiver.Decode(Truncated, Decoded) == EDecodeResult::InvalidPayload);
	Receiver.BuildAcknowledgment(FutureAck);
	TestEqual(TEXT("Failed block did not enter ACK history"), FutureAck.LatestSequence, 2u);
	TestTrue(TEXT("Valid block still decodes after malformed copy"), Receiver.Decode(Third[0], Decoded) == EDecodeResult::Decoded);
	TestTrue(TEXT("An old ACK is idempotent"), Sender.Confirm(Ack));
	Receiver.Reset(2, 3);
	TestEqual(TEXT("Reconnect releases receive allocations"), Receiver.GetAllocatedBytes(), uint64(0));
	TestTrue(TEXT("Old connection block is rejected"), Receiver.Decode(First[0], Decoded) == EDecodeResult::WrongSession);
	Sender.Reset(2, 3);
	TestEqual(TEXT("Reconnect returns sender storage to empty-container size"), Sender.GetAllocatedBytes(), FSender().GetAllocatedBytes());
	TestFalse(TEXT("Old connection ACK is rejected"), Sender.Confirm(Ack));
	Sender.Encode(Source, First);
	TestTrue(TEXT("New connection starts absolute"), Receiver.Decode(First[0], Decoded) == EDecodeResult::Decoded);
	Receiver.Reset(2, 4);
	TestTrue(TEXT("Old match is rejected even with matching connection generation"), Receiver.Decode(First[0], Decoded) == EDecodeResult::WrongSession);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiPoseHistoryExpiryTest,
	"GuLiStrike.Commander.Network.PoseHistoryExpiryAndCleanup", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGuLiPoseHistoryExpiryTest::RunTest(const FString& Parameters)
{
	using namespace GuLiCommanderPoseCodec;
	FSender Sender; FReceiver Receiver;
	Sender.Reset(1, 1); Receiver.Reset(1, 1);
	auto Source = GuLiCommanderNetworkTests::MakePoseChunk(1, 1, 1);
	TArray<FGuLiEncodedPoseBlock> Blocks;
	Sender.Encode(Source, Blocks);
	const auto Old = Blocks[0];
	FGuLiSoldierPoseChunk Decoded;
	Receiver.Decode(Blocks[0], Decoded);
	FGuLiPoseAcknowledgment Ack;
	Receiver.BuildAcknowledgment(Ack); Sender.Confirm(Ack);
	Source.ServerSimTick = 22; ++Source.FrameSequence;
	Sender.Encode(Source, Blocks);
	FReceiver Fresh; Fresh.Reset(1, 1);
	TestTrue(TEXT("More than 20 steps forces an absolute sample"), Fresh.Decode(Blocks[0], Decoded) == EDecodeResult::Decoded);
	Receiver.Decode(Blocks[0], Decoded); Receiver.BuildAcknowledgment(Ack); Sender.Confirm(Ack);
	// Hold simulation tick fixed to isolate the block-distance expiry rule.
	for (uint32 Index = 0; Index < MaxBaselineBlocks + 1; ++Index)
	{
		++Source.FrameSequence; Sender.Encode(Source, Blocks);
		Receiver.Decode(Blocks[0], Decoded);
	}
	Fresh.Reset(1, 1);
	TestTrue(TEXT("More than 1024 blocks forces absolute regardless of step age"), Fresh.Decode(Blocks[0], Decoded) == EDecodeResult::Decoded);
	Receiver.BuildAcknowledgment(Ack); Sender.Confirm(Ack);
	TArray<FGuLiSoldierId> Removed { FGuLiSoldierId(1) };
	Sender.Forget(Removed); Receiver.Forget(Removed);
	++Source.FrameSequence; Sender.Encode(Source, Blocks);
	Fresh.Reset(1, 1);
	TestTrue(TEXT("Recycled unit has no sender baseline"), Fresh.Decode(Blocks[0], Decoded) == EDecodeResult::Decoded);
	// A delayed pre-removal ACK must not resurrect a removed baseline.
	Sender.Confirm(Ack); ++Source.FrameSequence; Sender.Encode(Source, Blocks);
	Fresh.Reset(1, 1);
	TestTrue(TEXT("Old ACK cannot restore forgotten samples"), Fresh.Decode(Blocks[0], Decoded) == EDecodeResult::Decoded);
	for (uint32 Index = 0; Index < HistoryCapacity + 10; ++Index)
	{
		++Source.FrameSequence; Sender.Encode(Source, Blocks); Receiver.Decode(Blocks[0], Decoded);
	}
	TestTrue(TEXT("Out-of-window block cannot overwrite a recent ring slot"), Receiver.Decode(Old, Decoded) == EDecodeResult::TooOld);
	TestTrue(TEXT("Only 2048 blocks plus one unit baseline are retained"), Sender.GetAllocatedBytes() < 1024u * 1024u && Receiver.GetAllocatedBytes() < 1024u * 1024u);
	Receiver.Reset(); Sender.Reset();
	TestFalse(TEXT("Cache reset removes ACK readiness"), Receiver.BuildAcknowledgment(Ack));
	TestEqual(TEXT("Cache reset returns to empty-container storage"), Sender.GetAllocatedBytes() + Receiver.GetAllocatedBytes(), FSender().GetAllocatedBytes());
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
