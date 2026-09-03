// Copyright Epic Games, Inc. All Rights Reserved.

#include "Development/GuLiWingmanQAEvidence.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiWingmanQASchemaAllowlistTest,
	"GuLiStrike.Wingman.QA.Evidence.SchemaAllowlist",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiWingmanQASchemaAllowlistTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	TestTrue(TEXT("All seven stream names are stable"),
		FString(FGuLiWingmanQASchema::StreamName(EGuLiWingmanQALogStream::FlightNav))
			== TEXT("LogGuLiFlightNav")
		&& FString(FGuLiWingmanQASchema::StreamName(EGuLiWingmanQALogStream::Wingman))
			== TEXT("LogGuLiWingman")
		&& FString(FGuLiWingmanQASchema::StreamName(EGuLiWingmanQALogStream::WingmanAI))
			== TEXT("LogGuLiWingmanAI")
		&& FString(FGuLiWingmanQASchema::StreamName(EGuLiWingmanQALogStream::WingmanRelay))
			== TEXT("LogGuLiWingmanRelay")
		&& FString(FGuLiWingmanQASchema::StreamName(EGuLiWingmanQALogStream::BattleCombat))
			== TEXT("LogGuLiBattleCombat")
		&& FString(FGuLiWingmanQASchema::StreamName(EGuLiWingmanQALogStream::WingmanNet))
			== TEXT("LogGuLiWingmanNet")
		&& FString(FGuLiWingmanQASchema::StreamName(EGuLiWingmanQALogStream::WingmanQA))
			== TEXT("LogGuLiWingmanQA"));
	TestTrue(TEXT("Required core and supplemental events are known"),
		FGuLiWingmanQASchema::IsKnownEvent(TEXT("QA_RUN_BEGIN"))
		&& FGuLiWingmanQASchema::IsKnownEvent(TEXT("ATOMIC_BATCH_COMMITTED"))
		&& FGuLiWingmanQASchema::IsKnownEvent(TEXT("REMOTE_HIDDEN_AFTER_EXTRAPOLATION"))
		&& FGuLiWingmanQASchema::IsKnownEvent(TEXT("S8_OPENING_SPECTATOR_OBSERVED"))
		&& FGuLiWingmanQASchema::IsKnownEvent(TEXT("S8_NONLETHAL_READY_FOR_OBSERVER"))
		&& FGuLiWingmanQASchema::IsKnownEvent(TEXT("S8_MIDCOMBAT_OBSERVER_CONNECTED"))
		&& FGuLiWingmanQASchema::IsKnownEvent(TEXT("S8_LETHAL_AFTER_OBSERVER_COMMITTED"))
		&& FGuLiWingmanQASchema::IsKnownEvent(TEXT("S8_TRANSFER_OFFER_READY_FOR_OBSERVER"))
		&& FGuLiWingmanQASchema::IsKnownEvent(TEXT("S8_TRANSFER_PENDING_OBSERVER_CONNECTED"))
		&& FGuLiWingmanQASchema::IsKnownEvent(TEXT("S8_TRANSFER_COMMITTED_AFTER_OBSERVER")));
	TestTrue(TEXT("Required common, identity, candidate, movement, combat and performance fields are known"),
		FGuLiWingmanQASchema::IsKnownField(TEXT("campaign_id"))
		&& FGuLiWingmanQASchema::IsKnownField(TEXT("active_lease_id"))
		&& FGuLiWingmanQASchema::IsKnownField(TEXT("candidate_payload_hash"))
		&& FGuLiWingmanQASchema::IsKnownField(TEXT("nearest_clearance"))
		&& FGuLiWingmanQASchema::IsKnownField(TEXT("damage_event_id"))
		&& FGuLiWingmanQASchema::IsKnownField(TEXT("memory_private_bytes"))
		&& FGuLiWingmanQASchema::IsKnownField(TEXT("observer_no_private_authority"))
		&& FGuLiWingmanQASchema::IsKnownField(TEXT("observer_public_cuts_applied"))
		&& FGuLiWingmanQASchema::IsKnownField(TEXT("observer_first_cut_id"))
		&& FGuLiWingmanQASchema::IsKnownField(TEXT("observer_first_bootstrap_elapsed_seconds"))
		&& FGuLiWingmanQASchema::IsKnownField(TEXT("observer_player"))
		&& FGuLiWingmanQASchema::IsKnownField(TEXT("candidate_accept_count"))
		&& FGuLiWingmanQASchema::IsKnownField(TEXT("fire_accept_count"))
		&& FGuLiWingmanQASchema::IsKnownField(TEXT("cut_id"))
		&& FGuLiWingmanQASchema::IsKnownField(TEXT("health_permille"))
		&& FGuLiWingmanQASchema::IsKnownField(TEXT("offer_revision"))
		&& FGuLiWingmanQASchema::IsKnownField(TEXT("preview_cut_id"))
		&& FGuLiWingmanQASchema::IsKnownField(TEXT("preview_frozen"))
		&& FGuLiWingmanQASchema::IsKnownField(TEXT("active_cut_id")));
	TestFalse(TEXT("Unknown aliases fail closed"),
		FGuLiWingmanQASchema::IsKnownEvent(TEXT("CandidateAccepted"))
		|| FGuLiWingmanQASchema::IsKnownField(TEXT("canonical_slot")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiWingmanQAEvidenceWriterTest,
	"GuLiStrike.Wingman.QA.Evidence.WriterArtifactsAndFailClosedVerdict",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiWingmanQAEvidenceWriterTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FString Root = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("WingmanQATests"));
	IFileManager::Get().DeleteDirectory(*Root, false, true);

	FGuLiWingmanQARunDescriptor Descriptor;
	Descriptor.CampaignId = TEXT("writer-contract");
	Descriptor.RunId = TEXT("s1-control");
	Descriptor.SuiteRunRole = TEXT("S1-Control");
	Descriptor.PairId = TEXT("pair-0");
	Descriptor.EndpointId = TEXT("server");
	Descriptor.AcceptanceProfile = TEXT("CoreWingman");
	Descriptor.Map = TEXT("/Game/Maps/LVL_CommanderMassPrototype");
	Descriptor.NetMode = TEXT("DedicatedServer");
	Descriptor.Seed = 1977u;
	Descriptor.bServerEndpoint = true;
	Descriptor.OutputRoot = Root;
	Descriptor.InsightsTracePath = FPaths::Combine(
		Root, Descriptor.CampaignId, Descriptor.RunId, TEXT("endpoint-server.utrace"));

	FGuLiWingmanQAEvidenceWriter Writer;
	FString Error;
	if (!TestTrue(TEXT("Writer starts with a valid catalog role"), Writer.Start(Descriptor, Error)))
	{
		AddError(Error);
		return false;
	}
	const FString RunRoot = Writer.GetRunRoot();
	TestTrue(TEXT("Fixture trace is created"),
		FFileHelper::SaveStringToFile(TEXT("writer-contract-trace"), *Descriptor.InsightsTracePath));
	TestTrue(TEXT("A client event stream is present for formal completeness"),
		FFileHelper::SaveStringToFile(TEXT("{}\n"),
			*(RunRoot / TEXT("client-test.events.jsonl"))));

	FGuLiWingmanQAEvent Begin;
	Begin.Stream = EGuLiWingmanQALogStream::WingmanQA;
	Begin.Event = TEXT("QA_RUN_BEGIN");
	Begin.Fields.Add(TEXT("message"), TEXT("writer automation fixture"));
	TestTrue(TEXT("Known event and field are written"), Writer.RecordEvent(Begin, Error));
	TestTrue(TEXT("Protocol observation is recorded"),
		Writer.RecordGate(TEXT("PROTOCOL_V7"), true, 1, Error));
	TestTrue(TEXT("Dedicated motion observation is recorded"),
		Writer.RecordGate(TEXT("SERVER_WINGMAN_MOTION_ZERO"), true, 60, Error));

	const FGuLiWingmanAcceptanceRoleDefinition* Role =
		FGuLiWingmanAcceptanceCatalogV2::FindRole(Descriptor.SuiteRunRole);
	if (!TestNotNull(TEXT("S1-Control catalog role exists"), Role))
	{
		return false;
	}
	for (const FName Gate : Role->RequiredGateIds)
	{
		TestTrue(*FString::Printf(TEXT("Role gate %s recorded"), *Gate.ToString()),
			Writer.RecordGate(Gate, true, 1, Error));
	}
	for (const FName Invariant : FGuLiWingmanAcceptanceCatalogV2::GetInvariantKeys())
	{
		TestTrue(*FString::Printf(TEXT("Invariant %s sampled"), *Invariant.ToString()),
			Writer.RecordInvariant(Invariant, 0, Error));
	}

	FGuLiWingmanAcceptanceRunEvidence Evidence;
	TestTrue(TEXT("Complete writer fixture passes fail-closed finalization"),
		Writer.Stop(true, true, Evidence, Error));
	if (!Error.IsEmpty())
	{
		AddError(Error);
	}
	TestTrue(TEXT("Final evidence is complete and passed"),
		Evidence.bRunCompleted && Evidence.bRunPassed);
	TestTrue(TEXT("Manifest exists"),
		IFileManager::Get().FileExists(*(RunRoot / TEXT("manifest.json"))));
	TestTrue(TEXT("Acceptance exists"),
		IFileManager::Get().FileExists(*(RunRoot / TEXT("acceptance.json"))));
	TestTrue(TEXT("Assertions exist"),
		IFileManager::Get().FileExists(*(RunRoot / TEXT("assertions.json"))));
	TestTrue(TEXT("Server events contain data"),
		IFileManager::Get().FileSize(*(RunRoot / TEXT("server-events.jsonl"))) > 0);

	FGuLiWingmanQARunDescriptor ServerDescriptor = Descriptor;
	ServerDescriptor.RunId = TEXT("multi-endpoint");
	ServerDescriptor.ExpectedClientEndpoints = 1;
	ServerDescriptor.InsightsTracePath = FPaths::Combine(
		Root, ServerDescriptor.CampaignId, ServerDescriptor.RunId, TEXT("endpoint-server.utrace"));
	FGuLiWingmanQAEvidenceWriter ServerWriter;
	TestTrue(TEXT("Multi-endpoint server writer starts"), ServerWriter.Start(ServerDescriptor, Error));
	TestTrue(TEXT("Multi-endpoint server trace is created"),
		FFileHelper::SaveStringToFile(TEXT("server-trace"), *ServerDescriptor.InsightsTracePath));

	FGuLiWingmanQARunDescriptor ClientDescriptor = ServerDescriptor;
	ClientDescriptor.EndpointId = TEXT("owner-blue");
	ClientDescriptor.NetMode = TEXT("Client");
	ClientDescriptor.bServerEndpoint = false;
	ClientDescriptor.ExpectedClientEndpoints = 0;
	ClientDescriptor.InsightsTracePath = FPaths::Combine(
		Root, ClientDescriptor.CampaignId, ClientDescriptor.RunId,
		TEXT("endpoint-owner-blue.utrace"));
	FGuLiWingmanQAEvidenceWriter ClientWriter;
	TestTrue(TEXT("Client writer starts without touching server artifacts"),
		ClientWriter.Start(ClientDescriptor, Error));
	TestTrue(TEXT("Client endpoint trace is created"),
		FFileHelper::SaveStringToFile(TEXT("client-trace"), *ClientDescriptor.InsightsTracePath));
	FGuLiWingmanQAEvent ClientSample;
	ClientSample.Stream = EGuLiWingmanQALogStream::Wingman;
	ClientSample.Event = TEXT("QA_CHECK");
	ClientSample.Fields.Add(TEXT("sample_index"), TEXT("1"));
	TestTrue(TEXT("Client sample is isolated"), ClientWriter.RecordEvent(ClientSample, Error));
	TestTrue(TEXT("Client protocol gate is recorded"),
		ClientWriter.RecordGate(TEXT("PROTOCOL_V7"), true, 1, Error));
	TestTrue(TEXT("Client role gate is recorded"),
		ClientWriter.RecordGate(TEXT("WINGMAN_FAULT_INJECTION_DISABLED"), true, 1, Error));
	for (const FName Invariant : FGuLiWingmanAcceptanceCatalogV2::GetInvariantKeys())
	{
		ClientWriter.RecordInvariant(Invariant, 0, Error);
	}
	FGuLiWingmanAcceptanceRunEvidence ClientEvidence;
	TestTrue(TEXT("Partial client endpoint finalizes independently"),
		ClientWriter.Stop(true, true, ClientEvidence, Error));

	FGuLiWingmanQAEvent ServerSample;
	ServerSample.Stream = EGuLiWingmanQALogStream::WingmanQA;
	ServerSample.Event = TEXT("QA_CHECK");
	ServerSample.Fields.Add(TEXT("sample_index"), TEXT("1"));
	ServerWriter.RecordEvent(ServerSample, Error);
	ServerWriter.RecordGate(TEXT("PROTOCOL_V7"), true, 1, Error);
	ServerWriter.RecordGate(TEXT("SERVER_WINGMAN_MOTION_ZERO"), true, 1, Error);
	ServerWriter.RecordGate(TEXT("COMMANDER_1800_FIXED_STEPS"), true, 1, Error);
	ServerWriter.RecordGate(TEXT("COMMANDER_DROPPED_STEPS_ZERO"), true, 1, Error);
	for (const FName Invariant : FGuLiWingmanAcceptanceCatalogV2::GetInvariantKeys())
	{
		ServerWriter.RecordInvariant(Invariant, 0, Error);
	}
	FGuLiWingmanAcceptanceRunEvidence AggregatedEvidence;
	TestTrue(TEXT("Server aggregates the exact client endpoint set and passes"),
		ServerWriter.Stop(true, true, AggregatedEvidence, Error));
	if (!Error.IsEmpty())
	{
		AddError(Error);
	}
	const FString MultiRoot = FPaths::Combine(
		Root, ServerDescriptor.CampaignId, ServerDescriptor.RunId);
	TestTrue(TEXT("Client acceptance is endpoint-specific"),
		IFileManager::Get().FileExists(*(MultiRoot / TEXT("endpoint-owner-blue.acceptance.json"))));
	TestTrue(TEXT("Client simulation rows are merged into the canonical artifact"),
		IFileManager::Get().FileSize(*(MultiRoot / TEXT("client-simulation.csv"))) > 109);

	FGuLiWingmanQAEvidenceWriter IncompleteWriter;
	Descriptor.RunId = TEXT("missing-artifacts");
	Descriptor.ExpectedClientEndpoints = 0;
	Descriptor.InsightsTracePath = FPaths::Combine(
		Root, Descriptor.CampaignId, Descriptor.RunId, TEXT("missing.utrace"));
	TestTrue(TEXT("Incomplete writer starts"), IncompleteWriter.Start(Descriptor, Error));
	FGuLiWingmanAcceptanceRunEvidence IncompleteEvidence;
	TestFalse(TEXT("Missing trace/client stream/gates/invariants cannot pass"),
		IncompleteWriter.Stop(true, true, IncompleteEvidence, Error));
	TestFalse(TEXT("Incomplete verdict remains failed"), IncompleteEvidence.bRunPassed);

	IFileManager::Get().DeleteDirectory(*Root, false, true);
	return true;
}

#endif
