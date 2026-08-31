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
		Chunk.ServerSimTick = FrameSequence * 3u;
		Chunk.ServerTimeSeconds = static_cast<float>(FrameSequence) / GULI_POSE_CAPTURE_RATE_HZ;
		Chunk.ChunkIndex = 0u;
		Chunk.ChunkCount = 1u;
		FGuLiCompressedSoldierPose& Pose = Chunk.Samples.AddDefaulted_GetRef();
		Pose.SoldierId = FGuLiSoldierId(SoldierIdValue);
		Pose.SetRelativeLocationCentimeters(FVector(
			static_cast<double>(FrameSequence) * 10.0,
			0.0,
			0.0));
		return Chunk;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGuLiCommanderDynamicCohortContractTest,
	"GuLiStrike.Commander.Network.DynamicCohortContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiCommanderDynamicCohortContractTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	TestEqual(TEXT("Dynamic soldier/cohort protocol is version 3"), GULI_COMMANDER_PROTOCOL_VERSION, static_cast<uint16>(3u));
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
	FGuLiCohortCommandAck& AcceptedAck = Ack.CohortResults.AddDefaulted_GetRef();
	AcceptedAck.CohortId = FGuLiControlCohortId(5u);
	AcceptedAck.Result = EGuLiCommandAckResult::Accepted;
	FGuLiCohortCommandAck& DuplicateAck = Ack.CohortResults.AddDefaulted_GetRef();
	DuplicateAck.CohortId = FGuLiControlCohortId(5u);
	DuplicateAck.Result = EGuLiCommandAckResult::PathFailed;
	FGuLiCohortCommandAck& InvalidAck = Ack.CohortResults.AddDefaulted_GetRef();
	InvalidAck.Result = EGuLiCommandAckResult::InvalidRequest;
	FGuLiCohortCommandAck& FailedAck = Ack.CohortResults.AddDefaulted_GetRef();
	FailedAck.CohortId = FGuLiControlCohortId(6u);
	FailedAck.Result = EGuLiCommandAckResult::PathFailed;
	Ack.Sanitize();
	TestEqual(TEXT("ACK results are keyed by unique valid temporary cohorts"), Ack.CohortResults.Num(), 2);
	TestTrue(TEXT("ACK keeps the first deterministic result"),
		Ack.CohortResults[0].Result == EGuLiCommandAckResult::Accepted);
	TestTrue(TEXT("ACK preserves its request namespace"),
		Ack.CommandKind == EGuLiCommandKind::Move && Ack.ClientCommandId == 7u);

	FGuLiSelectionRequest SelectionRequest;
	SelectionRequest.Center = FVector(100.0, 200.0, 300.0);
	SelectionRequest.ClientRequestId = 1u;
	TestTrue(TEXT("Selection intent remains world-space only and well formed"), SelectionRequest.IsWellFormed());

	FGuLiMoveRequest MoveRequest;
	MoveRequest.Target = FVector(1000.0, 2000.0, 30.0);
	MoveRequest.SelectionRevision = 41u;
	MoveRequest.ClientCommandId = 2u;
	TestTrue(TEXT("Move intent fields remain valid"), MoveRequest.IsWellFormed());

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
	First.Health = 75u;
	First.MaxHealth = 120u;
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
		TestEqual(TEXT("First duplicate occurrence wins deterministically"), FirstResult->Health, static_cast<uint8>(75u));
		TestEqual(TEXT("Maximum health remains a reliable gameplay fact"), FirstResult->MaxHealth, static_cast<uint8>(120u));
		TestTrue(TEXT("Living soldier remains alive"), FirstResult->IsAlive());
	}

	const FGuLiSoldierStateItem* DestroyedResult = States.Find(FGuLiSoldierId(2u));
	TestNotNull(TEXT("Destroyed soldier remains in reliable roster"), DestroyedResult);
	if (DestroyedResult)
	{
		TestEqual(TEXT("Destroyed state forces zero health"), DestroyedResult->Health, static_cast<uint8>(0u));
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
			OverMaximumResult->Health, static_cast<uint8>(120u));
		TestTrue(TEXT("Clamped positive health remains alive"), OverMaximumResult->IsAlive());
	}

	const FGuLiSoldierStateItem* InvalidMaximumResult = States.Find(FGuLiSoldierId(5u));
	TestNotNull(TEXT("Invalid-maximum-health soldier remains addressable"), InvalidMaximumResult);
	if (InvalidMaximumResult)
	{
		TestEqual(TEXT("Zero MaxHealth normalizes to the minimum wire value"),
			InvalidMaximumResult->MaxHealth, static_cast<uint8>(1u));
		TestEqual(TEXT("Health is clamped after MaxHealth normalization"),
			InvalidMaximumResult->Health, static_cast<uint8>(1u));
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
	State.Health = 90u;
	State.MaxHealth = 100u;
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

	Snapshot[0].Health = 180u;
	Snapshot[0].MaxHealth = 200u;
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
			UpdatedState->MaxHealth, static_cast<uint8>(200u));
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
			SanitizedState->MaxHealth, static_cast<uint8>(1u));
		TestEqual(TEXT("Health clamps after authority MaxHealth normalization"),
			SanitizedState->Health, static_cast<uint8>(1u));
	}

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
	(void)Parameters;

	FGuLiCompressedSoldierPose QuantizedPose;
	QuantizedPose.SoldierId = FGuLiSoldierId(1u);
	QuantizedPose.SetRelativeLocationCentimeters(FVector(100000000.0, -100000000.0, 15.0));
	QuantizedPose.SetVelocityCentimetersPerSecond(FVector(123.0, -456.0, 0.0));
	TestEqual(TEXT("Positive relative position saturates safely"), QuantizedPose.RelativeXDecimeters, MAX_int16);
	TestEqual(TEXT("Negative relative position saturates safely"), QuantizedPose.RelativeYDecimeters, MIN_int16);
	TestEqual(TEXT("Ten-centimeter position quantization rounds deterministically"),
		QuantizedPose.RelativeZDecimeters, static_cast<int16>(2));
	TestEqual(TEXT("Velocity uses the same ten-centimeter quantization"),
		QuantizedPose.VelocityXDecimetersPerSecond, static_cast<int16>(12));

	FGuLiSoldierPoseChunk SourceChunk;
	SourceChunk.AuthorityEpoch = 3u;
	SourceChunk.FrameSequence = 123456u;
	SourceChunk.ServerSimTick = 987654u;
	SourceChunk.ServerTimeSeconds = 321.125f;
	SourceChunk.ChunkIndex = 4u;
	SourceChunk.ChunkCount = 16u;
	SourceChunk.Anchor = FVector(100000.0, -200000.0, 3000.0);
	for (uint32 Index = 0u; Index < GULI_MAX_POSE_SAMPLES_PER_CHUNK; ++Index)
	{
		FGuLiCompressedSoldierPose& Sample = SourceChunk.Samples.AddDefaulted_GetRef();
		Sample.SoldierId = FGuLiSoldierId(MAX_uint32 - Index);
		Sample.SetRelativeLocationCentimeters(FVector(
			static_cast<double>(Index) * 100.0,
			-static_cast<double>(Index) * 80.0,
			30.0));
		Sample.SetVelocityCentimetersPerSecond(FVector(1200.0, -800.0, 10.0));
		Sample.FacingYaw = GuLiCommanderProtocol::QuantizeYawDegrees(static_cast<float>(Index) * 11.25f);
		Sample.ActiveOrderId = MAX_uint32 - Index;
		Sample.State = EGuLiSoldierPoseState::Moving;
		Sample.Flags = Index == 0u ? static_cast<uint8>(GULI_SOLDIER_POSE_FLAG_TELEPORT | 0x80u) : 0u;
	}

	TArray<uint8> FirstBytes;
	TArray<uint8> SecondBytes;
	TestTrue(TEXT("Full 32-soldier pose chunk serializes"),
		GuLiCommanderNetworkTests::NetSerializeToBytes(SourceChunk, FirstBytes));
	TestTrue(TEXT("Identical pose chunk serializes a second time"),
		GuLiCommanderNetworkTests::NetSerializeToBytes(SourceChunk, SecondBytes));
	TestTrue(TEXT("Pose chunk serialization is byte deterministic"), FirstBytes == SecondBytes);
	TestTrue(TEXT("Worst-case 32-soldier chunk stays below the 1000-byte budget"), FirstBytes.Num() <= 1000);

	FGuLiSoldierPoseChunk ChunkCopy;
	TestTrue(TEXT("Pose chunk NetSerialize round-trip completes"),
		GuLiCommanderNetworkTests::NetDeserializeFromBytes(FirstBytes, ChunkCopy));
	TestEqual(TEXT("FrameSequence survives the wire"), ChunkCopy.FrameSequence, SourceChunk.FrameSequence);
	TestEqual(TEXT("AuthorityEpoch survives the wire"), ChunkCopy.AuthorityEpoch, SourceChunk.AuthorityEpoch);
	TestEqual(TEXT("ServerSimTick survives the wire"), ChunkCopy.ServerSimTick, SourceChunk.ServerSimTick);
	TestEqual(TEXT("ServerTimeSeconds survives the wire"),
		ChunkCopy.ServerTimeSeconds, SourceChunk.ServerTimeSeconds);
	TestEqual(TEXT("ChunkIndex survives the wire"), ChunkCopy.ChunkIndex, SourceChunk.ChunkIndex);
	TestEqual(TEXT("ChunkCount survives the wire"), ChunkCopy.ChunkCount, SourceChunk.ChunkCount);
	TestTrue(TEXT("Chunk anchor survives centimeter quantization"),
		FVector(ChunkCopy.Anchor).Equals(FVector(SourceChunk.Anchor), 1.0f));
	TestEqual(TEXT("All 32 pose samples survive the wire"),
		ChunkCopy.Samples.Num(), static_cast<int32>(GULI_MAX_POSE_SAMPLES_PER_CHUNK));
	bool bEverySampleRoundTrips =
		ChunkCopy.Samples.Num() == SourceChunk.Samples.Num();
	for (int32 Index = 0; bEverySampleRoundTrips && Index < SourceChunk.Samples.Num(); ++Index)
	{
		const FGuLiCompressedSoldierPose& SourceSample = SourceChunk.Samples[Index];
		const FGuLiCompressedSoldierPose& CopySample = ChunkCopy.Samples[Index];
		const uint8 ExpectedFlags = Index == 0
			? GULI_SOLDIER_POSE_FLAG_TELEPORT
			: 0u;
		bEverySampleRoundTrips = CopySample.SoldierId == SourceSample.SoldierId
			&& CopySample.RelativeXDecimeters == SourceSample.RelativeXDecimeters
			&& CopySample.RelativeYDecimeters == SourceSample.RelativeYDecimeters
			&& CopySample.RelativeZDecimeters == SourceSample.RelativeZDecimeters
			&& CopySample.VelocityXDecimetersPerSecond
				== SourceSample.VelocityXDecimetersPerSecond
			&& CopySample.VelocityYDecimetersPerSecond
				== SourceSample.VelocityYDecimetersPerSecond
			&& CopySample.VelocityZDecimetersPerSecond
				== SourceSample.VelocityZDecimetersPerSecond
			&& CopySample.FacingYaw == SourceSample.FacingYaw
			&& CopySample.ActiveOrderId == SourceSample.ActiveOrderId
			&& CopySample.State == SourceSample.State
			&& CopySample.Flags == ExpectedFlags;
	}
	TestTrue(TEXT("Every field of all 32 pose samples survives the wire"),
		bEverySampleRoundTrips);

	FGuLiSoldierPoseChunk OversizedChunk;
	OversizedChunk.ChunkCount = 0u;
	OversizedChunk.ChunkIndex = MAX_uint16;
	OversizedChunk.ServerTimeSeconds = -1.0f;
	for (uint32 Index = 0u; Index < GULI_MAX_POSE_SAMPLES_PER_CHUNK + 4u; ++Index)
	{
		FGuLiCompressedSoldierPose& Sample = OversizedChunk.Samples.AddDefaulted_GetRef();
		Sample.SoldierId = FGuLiSoldierId(Index + 1u);
	}
	OversizedChunk.Samples.AddDefaulted_GetRef().SoldierId = FGuLiSoldierId(1u);
	OversizedChunk.Samples.AddDefaulted_GetRef().SoldierId.Reset();
	OversizedChunk.Sanitize();
	TestEqual(TEXT("Pose chunks are capped at 32 unique soldiers"),
		OversizedChunk.Samples.Num(), static_cast<int32>(GULI_MAX_POSE_SAMPLES_PER_CHUNK));
	TestEqual(TEXT("Invalid zero ChunkCount normalizes to one"), OversizedChunk.ChunkCount, static_cast<uint16>(1u));
	TestEqual(TEXT("ChunkIndex normalizes inside ChunkCount"), OversizedChunk.ChunkIndex, static_cast<uint16>(0u));
	TestEqual(TEXT("Invalid server time normalizes to zero"), OversizedChunk.ServerTimeSeconds, 0.0f);

	if (FirstBytes.Num() >= static_cast<int32>(sizeof(uint16)))
	{
		TArray<uint8> WrongVersionBytes = FirstBytes;
		WrongVersionBytes[0] = 1u;
		WrongVersionBytes[1] = 0u;
		FGuLiSoldierPoseChunk WrongVersionChunk;
		TestFalse(TEXT("Mismatched pose protocol version is rejected"),
			GuLiCommanderNetworkTests::NetDeserializeFromBytes(WrongVersionBytes, WrongVersionChunk));
	}
	if (FirstBytes.Num() > static_cast<int32>(sizeof(uint16)))
	{
		TArray<uint8> ZeroEpochBytes = FirstBytes;
		ZeroEpochBytes[sizeof(uint16)] = 0u;
		FGuLiSoldierPoseChunk ZeroEpochChunk;
		TestFalse(TEXT("Zero authority epoch is rejected"),
			GuLiCommanderNetworkTests::NetDeserializeFromBytes(ZeroEpochBytes, ZeroEpochChunk));
	}

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
	TestFalse(TEXT("A protocol-v2 client cannot join the MaxHealth wire contract"),
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
	NetSync->TestOnly_ReceivePoseChunk(
		GuLiCommanderNetworkTests::MakePoseChunk(MatchEpoch, 1u, 1u));
	TArray<FGuLiSoldierPoseChunk> AcceptedChunks;
	NetSync->ConsumePendingPoseChunks(AcceptedChunks);
	TestEqual(TEXT("A pose from the accepted MatchEpoch enters the client queue"),
		AcceptedChunks.Num(), 1);
	TestEqual(TEXT("The first accepted chunk advances one fresh pose frame"),
		NetSync->GetAcceptedPoseFrameCount(), static_cast<uint64>(1u));

	NetSync->TestOnly_ReceivePoseChunk(
		GuLiCommanderNetworkTests::MakePoseChunk(OtherMatchEpoch, 2u, 1u));
	TArray<FGuLiSoldierPoseChunk> CrossMatchChunks;
	NetSync->ConsumePendingPoseChunks(CrossMatchChunks);
	TestTrue(TEXT("A pose from another MatchEpoch is rejected before interpolation"),
		CrossMatchChunks.IsEmpty());
	TestEqual(TEXT("A rejected cross-MatchEpoch chunk cannot advance pose freshness"),
		NetSync->GetAcceptedPoseFrameCount(), static_cast<uint64>(1u));

	FGuLiSoldierPoseChunk WrongProtocolChunk =
		GuLiCommanderNetworkTests::MakePoseChunk(MatchEpoch, 3u, 1u);
	WrongProtocolChunk.ProtocolVersion = GULI_COMMANDER_PROTOCOL_VERSION + 1u;
	NetSync->TestOnly_ReceivePoseChunk(WrongProtocolChunk);
	TArray<FGuLiSoldierPoseChunk> WrongProtocolChunks;
	NetSync->ConsumePendingPoseChunks(WrongProtocolChunks);
	TestTrue(TEXT("A pose with another protocol version is rejected by the runtime gate"),
		WrongProtocolChunks.IsEmpty());

	NetSync->TestOnly_ConfigureClientPoseGate(false, MatchEpoch);
	NetSync->TestOnly_ReceivePoseChunk(
		GuLiCommanderNetworkTests::MakePoseChunk(MatchEpoch, 4u, 1u));
	TArray<FGuLiSoldierPoseChunk> PreBootstrapChunks;
	NetSync->ConsumePendingPoseChunks(PreBootstrapChunks);
	TestTrue(TEXT("A matching pose is rejected until the roster/bootstrap gate is ready"),
		PreBootstrapChunks.IsEmpty());

	NetSync->TestOnly_ConfigureClientPoseGate(true, OtherMatchEpoch);
	NetSync->TestOnly_ReceivePoseChunk(
		GuLiCommanderNetworkTests::MakePoseChunk(OtherMatchEpoch, 5u, 1u));
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
	PassingGate.PresentedTravelDistanceCentimeters = 600.0;
	PassingGate.PresentedStepP95Centimeters = 60.0;
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
	StairSteppedPresentation.PresentedStepP95Centimeters = 180.0;
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

#endif // WITH_DEV_AUTOMATION_TESTS
