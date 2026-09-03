// Copyright Epic Games, Inc. All Rights Reserved.

#include "Development/GuLiWingmanQAEvidence.h"

#include "Algo/AllOf.h"
#include "Dom/JsonObject.h"
#include "GuLiFlightNavigationLog.h"
#include "GuLiStrike.h"
#include "HAL/CriticalSection.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace GuLiWingmanQAEvidence
{
	constexpr TCHAR SchemaName[] = TEXT("guli.wingman.qa-event.v1");
	constexpr TCHAR ManifestSchema[] = TEXT("guli.wingman.qa-manifest.v1");
	constexpr TCHAR AcceptanceSchema[] = TEXT("guli.wingman.qa-acceptance.v1");
	constexpr TCHAR AssertionsSchema[] = TEXT("guli.wingman.qa-assertions.v1");

	TSet<FName> MakeNames(const TCHAR* Text)
	{
		TArray<FString> Tokens;
		FString(Text).ParseIntoArrayWS(Tokens);
		TSet<FName> Result;
		Result.Reserve(Tokens.Num());
		for (const FString& Token : Tokens)
		{
			Result.Add(FName(*Token));
		}
		return Result;
	}

	bool IsSafeIdentity(const FString& Value)
	{
		if (Value.IsEmpty() || Value.Len() > 96 || Value.Contains(TEXT("..")))
		{
			return false;
		}
		for (const TCHAR Character : Value)
		{
			if (!(FChar::IsAlnum(Character) || Character == TEXT('-') || Character == TEXT('_')
				|| Character == TEXT('.')))
			{
				return false;
			}
		}
		return true;
	}

	FString JsonString(const TSharedRef<FJsonObject>& Object, const bool bCondensed)
	{
		FString Result;
		if (bCondensed)
		{
			const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
				TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Result);
			FJsonSerializer::Serialize(Object, Writer);
		}
		else
		{
			const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Result);
			FJsonSerializer::Serialize(Object, Writer);
		}
		return Result;
	}

	TSharedRef<FJsonObject> NameIntMap(const TMap<FName, int64>& Values)
	{
		TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
		TArray<FName> Keys;
		Values.GetKeys(Keys);
		Keys.Sort(FNameLexicalLess());
		for (const FName Key : Keys)
		{
			Object->SetNumberField(Key.ToString(), static_cast<double>(Values.FindChecked(Key)));
		}
		return Object;
	}

	TSharedRef<FJsonObject> NameBoolMap(const TMap<FName, bool>& Values)
	{
		TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
		TArray<FName> Keys;
		Values.GetKeys(Keys);
		Keys.Sort(FNameLexicalLess());
		for (const FName Key : Keys)
		{
			Object->SetBoolField(Key.ToString(), Values.FindChecked(Key));
		}
		return Object;
	}

	FString CsvEscape(FString Value)
	{
		Value.ReplaceInline(TEXT("\""), TEXT("\"\""));
		return FString::Printf(TEXT("\"%s\""), *Value);
	}

	void LogEvent(const EGuLiWingmanQALogStream Stream, const FString& Line)
	{
		switch (Stream)
		{
		case EGuLiWingmanQALogStream::FlightNav:
			UE_LOG(LogGuLiFlightNav, Verbose, TEXT("[GULI_WINGMAN_QA] %s"), *Line);
			break;
		case EGuLiWingmanQALogStream::Wingman:
			UE_LOG(LogGuLiWingman, Verbose, TEXT("[GULI_WINGMAN_QA] %s"), *Line);
			break;
		case EGuLiWingmanQALogStream::WingmanAI:
			UE_LOG(LogGuLiWingmanAI, Verbose, TEXT("[GULI_WINGMAN_QA] %s"), *Line);
			break;
		case EGuLiWingmanQALogStream::WingmanRelay:
			UE_LOG(LogGuLiWingmanRelay, Verbose, TEXT("[GULI_WINGMAN_QA] %s"), *Line);
			break;
		case EGuLiWingmanQALogStream::BattleCombat:
			UE_LOG(LogGuLiBattleCombat, Verbose, TEXT("[GULI_WINGMAN_QA] %s"), *Line);
			break;
		case EGuLiWingmanQALogStream::WingmanNet:
			UE_LOG(LogGuLiWingmanNet, Verbose, TEXT("[GULI_WINGMAN_QA] %s"), *Line);
			break;
		case EGuLiWingmanQALogStream::WingmanQA:
		default:
			UE_LOG(LogGuLiWingmanQA, Verbose, TEXT("[GULI_WINGMAN_QA] %s"), *Line);
			break;
		}
	}

	bool IsAcceptedSnapshotEvent(const FName Event)
	{
		return Event == TEXT("CANDIDATE_ACCEPTED") || Event == TEXT("ACCEPTED_SNAPSHOT_STORED")
			|| Event == TEXT("ACCEPTED_SNAPSHOT_RELAYED") || Event == TEXT("ACCEPTED_SNAPSHOT_APPLIED");
	}

	bool IsValidatorEvent(const FName Event)
	{
		return Event == TEXT("CANDIDATE_RECEIVED") || Event == TEXT("CANDIDATE_ACCEPTED")
			|| Event == TEXT("CANDIDATE_REJECTED") || Event == TEXT("ACCEPTED_SNAPSHOT_STORED")
			|| Event == TEXT("ACCEPTED_SNAPSHOT_RELAYED");
	}

	bool IsAtomicEvent(const FName Event)
	{
		return Event.ToString().StartsWith(TEXT("ATOMIC_BATCH_"))
			|| Event == TEXT("FIRST_BOOTSTRAP_BATCH_ACCEPTED");
	}

	bool IsWatchdogEvent(const FName Event)
	{
		return Event == TEXT("LEASE_WATCHDOG_TICK") || Event == TEXT("SIM_GROUP_STALE")
			|| Event == TEXT("SIM_GROUP_UNAVAILABLE") || Event == TEXT("LEASE_REVOKED")
			|| Event.ToString().EndsWith(TEXT("DEADLINE_EXPIRED"));
	}

	bool IsLeaseTransactionEvent(const FName Event)
	{
		return Event.ToString().StartsWith(TEXT("LEASE_TRANSFER_"))
			|| Event.ToString().StartsWith(TEXT("RESUME_"))
			|| Event.ToString().StartsWith(TEXT("REVISED_BASELINE_"))
			|| Event == TEXT("SIM_BASELINE_REVISED") || Event == TEXT("BACKUP_SELECTION_RETRIED")
			|| Event == TEXT("SIM_GROUP_NO_OWNER");
	}

	struct FInvariantRegistryState
	{
		FCriticalSection Mutex;
		TMap<FName, int64> Counts;
	};

	FInvariantRegistryState& InvariantRegistry()
	{
		static FInvariantRegistryState State;
		return State;
	}
}

const TSet<FName>& FGuLiWingmanQASchema::GetEventNames()
{
	static const TSet<FName> Names = GuLiWingmanQAEvidence::MakeNames(TEXT(
		"QA_RUN_BEGIN PROFILE_SELECTED PROCESS_READY MAP_LOADED FIXTURE_READY NAV_READY CARRIER_READY "
		"GROUP_SPAWNED ROSTER_COMMITTED BASELINE_SENT BASELINE_ACKED LEASE_GRANTED LEASE_RENEWED "
		"LEASE_WATCHDOG_TICK SIM_GROUP_STALE SIM_GROUP_UNAVAILABLE LEASE_REVOKED LEASE_TRANSFER_OFFERED "
		"LEASE_TRANSFER_READY LEASE_TRANSFER_COMMIT ACCEPTED_SNAPSHOT_FROZEN RESUME_SNAPSHOT_SENT "
		"RESUME_SNAPSHOT_ACKED CLIENT_SIM_RESUME_BEGIN FIRST_RESUME_PROPOSAL_ACCEPTED CLIENT_SIM_RESUMED "
		"HIGH_RATE_REQUESTED HIGH_RATE_GRANTED HIGH_RATE_EXPIRED SUBMISSION_MODE_CHANGED CANDIDATE_SUBMITTED "
		"CANDIDATE_RECEIVED CANDIDATE_ACCEPTED CANDIDATE_REJECTED ACCEPTED_SNAPSHOT_STORED "
		"ACCEPTED_SNAPSHOT_RELAYED ACCEPTED_SNAPSHOT_APPLIED REBASE_SENT REBASE_APPLIED "
		"REMOTE_EXTRAPOLATION_ENDED PATH_REQUESTED PATH_RESULT PATH_INVALIDATED EMERGENCY_ENTER EMERGENCY_EXIT "
		"TARGET_ELIGIBLE TARGET_ACQUIRED FIRE_INTENT_SUBMITTED FIRE_INTENT_RECEIVED SHOT_ACCEPTED SHOT_REJECTED "
		"DAMAGE_APPLIED DAMAGE_DEDUPED HEALTH_ZERO DEATH_COMMITTED REPLENISH_SCHEDULED REPLENISH_SPAWNED "
		"SHOT_PRESENTED HIT_PRESENTED DEATH_PRESENTED LATEJOIN_CONNECT_BEGIN BOOTSTRAP_CUT_CREATED "
		"BOOTSTRAP_COMMIT LATEJOIN_READY QA_CHECK QA_RUN_END SIM_GROUP_INITIALIZING "
		"FIRST_BOOTSTRAP_BATCH_ACCEPTED INITIAL_CANDIDATE_DEADLINE_EXPIRED FIRST_TAKEOVER_CANDIDATE_ACCEPTED "
		"ATOMIC_BATCH_FRAGMENT_RECEIVED ATOMIC_BATCH_ASSEMBLED ATOMIC_BATCH_REJECTED ATOMIC_BATCH_COMMITTED "
		"ATOMIC_BATCH_DEADLINE_EXPIRED SIM_BASELINE_REVISED REVISED_BASELINE_ACKED "
		"ACTIVE_LEASE_STALE_DURING_PENDING_OFFER OFFER_READY_DEADLINE_EXPIRED TAKEOVER_ACK_DEADLINE_EXPIRED "
		"REVISED_BASELINE_ACK_DEADLINE_EXPIRED TAKEOVER_CANDIDATE_DEADLINE_EXPIRED "
		"TAKEOVER_OVERALL_DEADLINE_EXPIRED BACKUP_SELECTION_RETRIED SIM_GROUP_NO_OWNER "
		"REPLENISH_DEFERRED_UNAVAILABLE REPLENISH_RELEASED_AFTER_ACTIVE FIRE_DEDUPE_HIT "
		"FIRE_SEQUENCE_RESERVED FIRE_REJECTED_BEFORE_SEQUENCE_RESERVE REMOTE_HIDDEN_AFTER_EXTRAPOLATION "
		"S8_OPENING_SPECTATOR_OBSERVED S8_NONLETHAL_READY_FOR_OBSERVER "
		"S8_MIDCOMBAT_OBSERVER_CONNECTED S8_LETHAL_AFTER_OBSERVER_COMMITTED "
		"S8_TRANSFER_OFFER_READY_FOR_OBSERVER S8_TRANSFER_PENDING_OBSERVER_CONNECTED "
		"S8_TRANSFER_COMMITTED_AFTER_OBSERVER"));
	return Names;
}

const TSet<FName>& FGuLiWingmanQASchema::GetFieldNames()
{
	static const TSet<FName> Names = GuLiWingmanQAEvidence::MakeNames(TEXT(
		"schema_version campaign_id run_id pair_id suite_run_role event map net_mode pid utc_us server_time_s "
		"match_epoch connection_generation protocol seed acceptance_profile catalog_version catalog_hash gate_id "
		"gate_status invariant_key invariant_count authority_mode unit_kind team player_guid owner_connection_id "
		"simulation_group_id group_generation roster_revision required_active_flight_mask required_active_member_mask "
		"flight_id slot wingman_id entity_generation submitted_authority_epoch submitted_lease_id active_authority_epoch "
		"active_lease_id pending_authority_epoch pending_lease_id availability_state requested_submission_mode "
		"observed_high_rate_grant_revision allowed_upload_rate_class high_rate_grant_revision "
		"high_rate_effective_client_sim_tick high_rate_grant_expiry_ms expected_submission_hz capture_interval_client_ticks "
		"client_sim_tick capture_time_ms frame_sequence base_accepted_snapshot_sequence accepted_snapshot_sequence "
		"accepted_snapshot_hash validated_payload_hash candidate_payload_hash observer_applied_payload_hash batch_id "
		"batch_kind baseline_kind baseline_revision baseline_hash frozen_roster_revision frozen_required_flight_mask "
		"frozen_required_member_mask_hash included_flight_mask committed_flight_mask fragment_index fragment_count "
		"batch_payload_hash batch_assembly_deadline_ms atomic_commit batch_staged_count last_valid_candidate_time_ms "
		"last_accepted_snapshot_time_ms source_state_age_ms target_state_age_ms source_availability_state "
		"target_availability_state validator_trigger validation_duration_us validation_result reject_reason "
		"accepted_store_count relay_count server_generated_runtime_transform_count server_wingman_motion_step_count "
		"server_wingman_state_tree_tick_count server_wingman_path_request_count server_wingman_steering_step_count "
		"server_wingman_flight_integration_step_count server_ground_machine_motion_step_count "
		"server_ground_machine_state_tree_tick_count server_ground_machine_path_request_count "
		"server_ground_machine_steering_step_count server_ground_machine_movement_integration_step_count "
		"watchdog_scan_id watchdog_interval_ms initial_candidate_deadline_ms stale_deadline_ms unavailable_deadline_ms "
		"revoke_deadline_ms offer_time_ms offer_ready_deadline_ms offer_ready_time_ms takeover_ack_deadline_ms "
		"revised_baseline_ack_deadline_ms takeover_candidate_deadline_ms takeover_overall_deadline_ms "
		"initial_detection_lag_ms stale_detection_lag_ms unavailable_detection_lag_ms revoke_detection_lag_ms "
		"availability_before availability_after active_lease_transaction_state pending_lease_transaction_state "
		"backup_selection_attempt preview_baseline_revision transfer_baseline_revision transfer_baseline_hash "
		"transfer_commit_time_ms transfer_baseline_ack_time_ms baseline_revised_reason revised_baseline_sent_time_ms "
		"revised_baseline_ack_time_ms resume_snapshot_sequence resume_snapshot_hash first_resume_proposal_id "
		"first_takeover_candidate_id resume_latency_ms takeover_latency_ms position velocity quaternion speed heading "
		"heading_rate bank accel desired_slot radial_error vertical_error nearest_clearance nav_checksum corridor_revision "
		"current_leaf current_portal obstacle_revision remote_extrapolation_age_ms remote_fade_duration_ms "
		"remote_hidden_time_ms source_kind source_id source_generation source_state_ref_type "
		"source_accepted_snapshot_sequence target_kind target_id target_generation client_fire_tick fire_sequence "
		"submission_dedupe_key dedupe_lookup_result reservation_created fire_sequence_high_water_before "
		"fire_sequence_high_water_after distance range los shot_id damage_event_id damage health_before health_after "
		"commit_count death_event_id reward_event_id replenish_schedule_id replenish_due_time_ms "
		"deferred_replenish_due_count pre_active_spawn_count post_active_spawn_count sample_count minimum_sample_count "
		"quantile_method client_sim_ms server_validator_ms server_store_relay_ms server_combat_ms lease_watchdog_ms "
		"commander_fixed_step_ms queue_depth queue_capacity oldest_item_age_ms memory_private_bytes bytes stream_tag "
		"measurement_direction endpoint_id log_stream elapsed_seconds role_ready ship_count wingman_count "
		"commander_count relay_group_count active_relay_group_count well_formed_bootstrap_count "
		"atomic_committed_count strict_flight_group_count minimum_accepted_frame owner_mass_entity_count trace_path artifact_path "
		"message sample_index process_role completed scenario_passed observer_no_private_authority "
		"observer_public_cuts_applied observer_first_cut_id observer_first_bootstrap_elapsed_seconds "
		"observer_player candidate_accept_count fire_accept_count cut_id health_permille group "
		"offer_revision preview_cut_id preview_frozen active_cut_id"));
	return Names;
}

bool FGuLiWingmanQASchema::IsKnownEvent(const FName EventName)
{
	return !EventName.IsNone() && GetEventNames().Contains(EventName);
}

bool FGuLiWingmanQASchema::IsKnownField(const FName FieldName)
{
	return !FieldName.IsNone() && GetFieldNames().Contains(FieldName);
}

const TCHAR* FGuLiWingmanQASchema::StreamName(const EGuLiWingmanQALogStream Stream)
{
	switch (Stream)
	{
	case EGuLiWingmanQALogStream::FlightNav: return TEXT("LogGuLiFlightNav");
	case EGuLiWingmanQALogStream::Wingman: return TEXT("LogGuLiWingman");
	case EGuLiWingmanQALogStream::WingmanAI: return TEXT("LogGuLiWingmanAI");
	case EGuLiWingmanQALogStream::WingmanRelay: return TEXT("LogGuLiWingmanRelay");
	case EGuLiWingmanQALogStream::BattleCombat: return TEXT("LogGuLiBattleCombat");
	case EGuLiWingmanQALogStream::WingmanNet: return TEXT("LogGuLiWingmanNet");
	case EGuLiWingmanQALogStream::WingmanQA: return TEXT("LogGuLiWingmanQA");
	default: return TEXT("Invalid");
	}
}

void FGuLiWingmanQAInvariantRegistry::Reset()
{
	GuLiWingmanQAEvidence::FInvariantRegistryState& State =
		GuLiWingmanQAEvidence::InvariantRegistry();
	FScopeLock Lock(&State.Mutex);
	State.Counts.Reset();
	for (const FName Key : FGuLiWingmanAcceptanceCatalogV2::GetInvariantKeys())
	{
		State.Counts.Add(Key, 0);
	}
}

bool FGuLiWingmanQAInvariantRegistry::Add(const FName InvariantKey, const int64 Amount)
{
#if UE_BUILD_SHIPPING
	(void)InvariantKey;
	(void)Amount;
	return false;
#else
	if (!FGuLiWingmanAcceptanceCatalogV2::GetInvariantKeys().Contains(InvariantKey) || Amount <= 0)
	{
		UE_LOG(LogGuLiWingmanQA, Error, TEXT("Rejected unknown/invalid invariant increment: %s/%lld"),
			*InvariantKey.ToString(), Amount);
		return false;
	}
	GuLiWingmanQAEvidence::FInvariantRegistryState& State =
		GuLiWingmanQAEvidence::InvariantRegistry();
	FScopeLock Lock(&State.Mutex);
	State.Counts.FindOrAdd(InvariantKey) += Amount;
	return true;
#endif
}

TMap<FName, int64> FGuLiWingmanQAInvariantRegistry::Snapshot()
{
	GuLiWingmanQAEvidence::FInvariantRegistryState& State =
		GuLiWingmanQAEvidence::InvariantRegistry();
	FScopeLock Lock(&State.Mutex);
	if (State.Counts.IsEmpty())
	{
		for (const FName Key : FGuLiWingmanAcceptanceCatalogV2::GetInvariantKeys())
		{
			State.Counts.Add(Key, 0);
		}
	}
	return State.Counts;
}

struct FGuLiWingmanQAEvidenceWriter::FImpl
{
	mutable FCriticalSection Mutex;
	FGuLiWingmanQARunDescriptor Descriptor;
	FString RunRoot;
	FString EventPath;
	FString TracePath;
	TMap<FName, int64> GateSamples;
	TMap<FName, bool> GateStatuses;
	TMap<FName, int64> InvariantCounts;
	TMap<FName, int64> InvariantSamples;
	TArray<FString> WriterErrors;
	int64 EventCount = 0;
	bool bActive = false;

	FString Path(const TCHAR* Name) const
	{
		return RunRoot / Name;
	}

	FString EndpointArtifactName(const TCHAR* Name) const
	{
		return Descriptor.bServerEndpoint
			? FString(Name)
			: FString::Printf(TEXT("endpoint-%s.%s"), *Descriptor.EndpointId, Name);
	}

	FString ArtifactPath(const TCHAR* Name) const
	{
		return Path(*EndpointArtifactName(Name));
	}

	bool Append(const FString& Filename, const FString& Line, FString& OutError)
	{
		constexpr int32 MaxAttempts = 5;
		for (int32 Attempt = 0; Attempt < MaxAttempts; ++Attempt)
		{
			if (FFileHelper::SaveStringToFile(Line + LINE_TERMINATOR, *Filename,
				FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,
				&IFileManager::Get(), FILEWRITE_Append | FILEWRITE_AllowRead))
			{
				return true;
			}
			if (Attempt + 1 < MaxAttempts)
			{
				// Endpoint files are process-unique, but virus scanners/indexers can briefly
				// deny a Windows append. Persistent failures still fail closed.
				FPlatformProcess::SleepNoStats(0.005f * static_cast<float>(Attempt + 1));
			}
		}
		OutError = FString::Printf(TEXT("Failed to append Wingman QA evidence: %s"), *Filename);
		WriterErrors.Add(OutError);
		return false;
	}

	bool SaveJson(const FString& Filename, const TSharedRef<FJsonObject>& Object, FString& OutError)
	{
		const FString Text = GuLiWingmanQAEvidence::JsonString(Object, false);
		if (!FFileHelper::SaveStringToFile(Text, *Filename,
			FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
		{
			OutError = FString::Printf(TEXT("Failed to save Wingman QA JSON: %s"), *Filename);
			WriterErrors.Add(OutError);
			return false;
		}
		return true;
	}

	bool LoadJson(const FString& Filename, TSharedPtr<FJsonObject>& OutObject, FString& OutError) const
	{
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *Filename))
		{
			OutError = FString::Printf(TEXT("Failed to read Wingman QA JSON: %s"), *Filename);
			return false;
		}
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
		if (!FJsonSerializer::Deserialize(Reader, OutObject) || !OutObject.IsValid())
		{
			OutError = FString::Printf(TEXT("Invalid Wingman QA JSON: %s"), *Filename);
			return false;
		}
		return true;
	}

	TSet<FName> AllowedGates() const
	{
		TSet<FName> Result;
		for (const FName Gate : FGuLiWingmanAcceptanceCatalogV2::GetCommonRequiredGateIds())
		{
			Result.Add(Gate);
		}
		if (const FGuLiWingmanAcceptanceRoleDefinition* Role =
			FGuLiWingmanAcceptanceCatalogV2::FindRole(Descriptor.SuiteRunRole))
		{
			for (const FName Gate : Role->RequiredGateIds)
			{
				Result.Add(Gate);
			}
		}
		return Result;
	}

	bool RecordGateUnlocked(const FName GateId, const bool bPassed,
		const int64 SampleCount, FString& OutError)
	{
		if (!AllowedGates().Contains(GateId) || SampleCount <= 0)
		{
			OutError = FString::Printf(TEXT("Unknown gate or invalid sample count: %s/%lld"),
				*GateId.ToString(), SampleCount);
			WriterErrors.Add(OutError);
			return false;
		}
		GateSamples.FindOrAdd(GateId) += SampleCount;
		if (bool* ExistingStatus = GateStatuses.Find(GateId))
		{
			*ExistingStatus = *ExistingStatus && bPassed;
		}
		else
		{
			GateStatuses.Add(GateId, bPassed);
		}
		return true;
	}

	bool WriteManifest(FString& OutError)
	{
		TSharedRef<FJsonObject> Manifest = MakeShared<FJsonObject>();
		Manifest->SetStringField(TEXT("schema"), GuLiWingmanQAEvidence::ManifestSchema);
		Manifest->SetNumberField(TEXT("schema_version"), FGuLiWingmanQASchema::Version);
		Manifest->SetStringField(TEXT("campaign_id"), Descriptor.CampaignId);
		Manifest->SetStringField(TEXT("run_id"), Descriptor.RunId);
		Manifest->SetStringField(TEXT("suite_run_role"), Descriptor.SuiteRunRole.ToString());
		Manifest->SetStringField(TEXT("pair_id"), Descriptor.PairId);
		Manifest->SetStringField(TEXT("endpoint_id"), Descriptor.EndpointId);
		Manifest->SetBoolField(TEXT("server_endpoint"), Descriptor.bServerEndpoint);
		Manifest->SetNumberField(TEXT("expected_client_endpoints"), Descriptor.ExpectedClientEndpoints);
		Manifest->SetStringField(TEXT("acceptance_profile"), Descriptor.AcceptanceProfile);
		Manifest->SetStringField(TEXT("map"), Descriptor.Map);
		Manifest->SetStringField(TEXT("net_mode"), Descriptor.NetMode);
		Manifest->SetNumberField(TEXT("pid"), FPlatformProcess::GetCurrentProcessId());
		Manifest->SetNumberField(TEXT("seed"), Descriptor.Seed);
		Manifest->SetNumberField(TEXT("catalog_version"), FGuLiWingmanAcceptanceCatalogV2::Version);
		Manifest->SetStringField(TEXT("catalog_hash"),
			FGuLiWingmanAcceptanceCatalogV2::GetCatalogHashSha256());
		Manifest->SetStringField(TEXT("started_utc"), FDateTime::UtcNow().ToIso8601());
		Manifest->SetStringField(TEXT("insights_trace"), TracePath);

		TArray<TSharedPtr<FJsonValue>> Artifacts;
		const TCHAR* ArtifactNames[] = {
			TEXT("manifest.json"), TEXT("acceptance.json"), TEXT("server-events.jsonl"),
			TEXT("client-simulation.csv"), TEXT("server-accepted-snapshots.csv"),
			TEXT("server-validator-relay.csv"), TEXT("atomic-batches.csv"),
			TEXT("lease-watchdog.csv"), TEXT("lease-transactions.csv"),
			TEXT("combat-ledger.jsonl"), TEXT("network-metrics.csv"),
			TEXT("performance.csv"), TEXT("assertions.json"), TEXT("media-index.json")
		};
		for (const TCHAR* Artifact : ArtifactNames)
		{
			Artifacts.Add(MakeShared<FJsonValueString>(ArtifactPath(Artifact)));
		}
		Artifacts.Add(MakeShared<FJsonValueString>(EventPath));
		Artifacts.Add(MakeShared<FJsonValueString>(TracePath));
		Manifest->SetArrayField(TEXT("artifacts"), MoveTemp(Artifacts));
		return SaveJson(ArtifactPath(TEXT("manifest.json")), Manifest, OutError);
	}

	bool CreateArtifacts(FString& OutError)
	{
		if (!IFileManager::Get().MakeDirectory(*RunRoot, true))
		{
			OutError = FString::Printf(TEXT("Failed to create Wingman QA run directory: %s"), *RunRoot);
			return false;
		}

		const FString CsvHeader = TEXT("schema_version,campaign_id,run_id,pair_id,suite_run_role,event,utc_us,server_time_s,endpoint_id,fields_json") LINE_TERMINATOR;
		const TCHAR* CsvNames[] = {
			TEXT("client-simulation.csv"), TEXT("server-accepted-snapshots.csv"),
			TEXT("server-validator-relay.csv"), TEXT("atomic-batches.csv"),
			TEXT("lease-watchdog.csv"), TEXT("lease-transactions.csv"),
			TEXT("network-metrics.csv"), TEXT("performance.csv")
		};
		for (const TCHAR* CsvName : CsvNames)
		{
			const FString CsvPath = ArtifactPath(CsvName);
			if (!IFileManager::Get().FileExists(*CsvPath)
				&& !FFileHelper::SaveStringToFile(CsvHeader, *CsvPath,
					FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
			{
				OutError = FString::Printf(TEXT("Failed to create Wingman QA CSV: %s"), *CsvPath);
				return false;
			}
		}

		const TCHAR* EmptyNames[] = {
			TEXT("server-events.jsonl"), TEXT("combat-ledger.jsonl")
		};
		for (const TCHAR* EmptyName : EmptyNames)
		{
			const FString EmptyPath = ArtifactPath(EmptyName);
			if (!IFileManager::Get().FileExists(*EmptyPath)
				&& !FFileHelper::SaveStringToFile(FString(), *EmptyPath,
					FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
			{
				OutError = FString::Printf(TEXT("Failed to create Wingman QA stream: %s"), *EmptyPath);
				return false;
			}
		}

		TSharedRef<FJsonObject> Pending = MakeShared<FJsonObject>();
		Pending->SetStringField(TEXT("status"), TEXT("pending"));
		if (!SaveJson(ArtifactPath(TEXT("acceptance.json")), Pending, OutError)
			|| !SaveJson(ArtifactPath(TEXT("assertions.json")), Pending, OutError))
		{
			return false;
		}
		TSharedRef<FJsonObject> Media = MakeShared<FJsonObject>();
		Media->SetStringField(TEXT("schema"), TEXT("guli.wingman.qa-media-index.v1"));
		Media->SetArrayField(TEXT("captures"), TArray<TSharedPtr<FJsonValue>>{});
		return SaveJson(ArtifactPath(TEXT("media-index.json")), Media, OutError);
	}

	bool AppendCsv(const FString& Filename, const FGuLiWingmanQAEvent& Event,
		const int64 UtcMicroseconds, const FString& FieldsJson, FString& OutError)
	{
		const FString Row = FString::Printf(TEXT("%d,%s,%s,%s,%s,%s,%lld,%.6f,%s,%s"),
			FGuLiWingmanQASchema::Version,
			*GuLiWingmanQAEvidence::CsvEscape(Descriptor.CampaignId),
			*GuLiWingmanQAEvidence::CsvEscape(Descriptor.RunId),
			*GuLiWingmanQAEvidence::CsvEscape(Descriptor.PairId),
			*GuLiWingmanQAEvidence::CsvEscape(Descriptor.SuiteRunRole.ToString()),
			*GuLiWingmanQAEvidence::CsvEscape(Event.Event.ToString()),
			UtcMicroseconds,
			Event.ServerTimeSeconds,
			*GuLiWingmanQAEvidence::CsvEscape(Descriptor.EndpointId),
			*GuLiWingmanQAEvidence::CsvEscape(FieldsJson));
		return Append(ArtifactPath(*Filename), Row, OutError);
	}

	bool MergeClientEvidence(FString& OutError)
	{
		check(Descriptor.bServerEndpoint);
		OutError.Reset();
		TArray<FString> AcceptanceFiles;
		IFileManager::Get().FindFiles(AcceptanceFiles,
			*(RunRoot / TEXT("endpoint-*.acceptance.json")), true, false);
		AcceptanceFiles.Sort();
		if (AcceptanceFiles.Num() != Descriptor.ExpectedClientEndpoints)
		{
			OutError = FString::Printf(TEXT("Expected %d client endpoint verdicts, found %d."),
				Descriptor.ExpectedClientEndpoints, AcceptanceFiles.Num());
			WriterErrors.Add(OutError);
			return false;
		}

		auto MergeBoolObject = [this](const TSharedPtr<FJsonObject>& Parent,
			const TCHAR* FieldName, FString& Error) -> bool
		{
			const TSharedPtr<FJsonObject>* Values = nullptr;
			if (!Parent->TryGetObjectField(FieldName, Values) || !Values || !Values->IsValid())
			{
				Error = FString::Printf(TEXT("Endpoint assertions are missing %s."), FieldName);
				return false;
			}
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*Values)->Values)
			{
				const FName Key(*Pair.Key);
				bool Value = false;
				if (!AllowedGates().Contains(Key) || !Pair.Value.IsValid()
					|| !Pair.Value->TryGetBool(Value))
				{
					Error = FString::Printf(TEXT("Endpoint assertions contain invalid gate %s."), *Pair.Key);
					return false;
				}
				if (bool* Existing = GateStatuses.Find(Key))
				{
					*Existing = *Existing && Value;
				}
				else
				{
					GateStatuses.Add(Key, Value);
				}
			}
			return true;
		};
		auto MergeIntObject = [this](const TSharedPtr<FJsonObject>& Parent,
			const TCHAR* FieldName, const bool bInvariant, const bool bSamples,
			FString& Error) -> bool
		{
			const TSharedPtr<FJsonObject>* Values = nullptr;
			if (!Parent->TryGetObjectField(FieldName, Values) || !Values || !Values->IsValid())
			{
				Error = FString::Printf(TEXT("Endpoint assertions are missing %s."), FieldName);
				return false;
			}
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*Values)->Values)
			{
				const FName Key(*Pair.Key);
				double RawValue = 0.0;
				const bool bKnown = bInvariant
					? FGuLiWingmanAcceptanceCatalogV2::GetInvariantKeys().Contains(Key)
					: AllowedGates().Contains(Key);
				if (!bKnown || !Pair.Value.IsValid() || !Pair.Value->TryGetNumber(RawValue)
					|| !FMath::IsFinite(RawValue) || RawValue < 0.0)
				{
					Error = FString::Printf(TEXT("Endpoint assertions contain invalid counter %s."),
						*Pair.Key);
					return false;
				}
				const int64 Value = static_cast<int64>(RawValue);
				if (bInvariant)
				{
					if (bSamples)
					{
						InvariantSamples.FindOrAdd(Key) += Value;
					}
					else
					{
						InvariantCounts.FindOrAdd(Key) = FMath::Max(InvariantCounts.FindRef(Key), Value);
					}
				}
				else
				{
					GateSamples.FindOrAdd(Key) += Value;
				}
			}
			return true;
		};
		auto MergeArtifact = [this](const FString& SourcePath, const FString& DestinationPath,
			const bool bSkipHeader, FString& Error) -> bool
		{
			FString Contents;
			if (!FFileHelper::LoadFileToString(Contents, *SourcePath))
			{
				Error = FString::Printf(TEXT("Failed to read client endpoint artifact: %s"), *SourcePath);
				return false;
			}
			if (bSkipHeader)
			{
				int32 HeaderEnd = INDEX_NONE;
				if (!Contents.FindChar(TEXT('\n'), HeaderEnd))
				{
					return true;
				}
				Contents.RightChopInline(HeaderEnd + 1, EAllowShrinking::No);
			}
			if (Contents.IsEmpty())
			{
				return true;
			}
			if (!FFileHelper::SaveStringToFile(Contents, *DestinationPath,
				FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,
				&IFileManager::Get(), FILEWRITE_Append))
			{
				Error = FString::Printf(TEXT("Failed to merge client endpoint artifact: %s"),
					*DestinationPath);
				return false;
			}
			return true;
		};

		for (const FString& AcceptanceName : AcceptanceFiles)
		{
			const FString AcceptancePath = RunRoot / AcceptanceName;
			TSharedPtr<FJsonObject> Acceptance;
			FString Error;
			if (!LoadJson(AcceptancePath, Acceptance, Error))
			{
				WriterErrors.Add(Error);
				continue;
			}
			FString CampaignId;
			FString RunId;
			FString RoleId;
			FString EndpointId;
			FString Trace;
			bool bCompleted = false;
			bool bScenarioPassed = false;
			bool bPassed = false;
			const bool bIdentityValid = Acceptance->TryGetStringField(TEXT("campaign_id"), CampaignId)
				&& Acceptance->TryGetStringField(TEXT("run_id"), RunId)
				&& Acceptance->TryGetStringField(TEXT("suite_run_role"), RoleId)
				&& Acceptance->TryGetStringField(TEXT("endpoint_id"), EndpointId)
				&& Acceptance->TryGetStringField(TEXT("trace_path"), Trace)
				&& Acceptance->TryGetBoolField(TEXT("run_completed"), bCompleted)
				&& Acceptance->TryGetBoolField(TEXT("scenario_passed"), bScenarioPassed)
				&& Acceptance->TryGetBoolField(TEXT("passed"), bPassed)
				&& CampaignId == Descriptor.CampaignId && RunId == Descriptor.RunId
				&& RoleId == Descriptor.SuiteRunRole.ToString()
				&& GuLiWingmanQAEvidence::IsSafeIdentity(EndpointId)
				&& !EndpointId.Equals(TEXT("server"), ESearchCase::IgnoreCase);
			if (!bIdentityValid || !bCompleted || !bScenarioPassed || !bPassed)
			{
				WriterErrors.Add(FString::Printf(TEXT("Client endpoint verdict is invalid or failed: %s"),
					*AcceptancePath));
				continue;
			}
			const FString Prefix = FString::Printf(TEXT("endpoint-%s."), *EndpointId);
			const FString AssertionsPath = RunRoot / (Prefix + TEXT("assertions.json"));
			const FString ManifestPath = RunRoot / (Prefix + TEXT("manifest.json"));
			const FString EventsPath = RunRoot / FString::Printf(TEXT("client-%s.events.jsonl"), *EndpointId);
			if (IFileManager::Get().FileSize(*Trace) <= 0
				|| IFileManager::Get().FileSize(*ManifestPath) <= 0
				|| IFileManager::Get().FileSize(*EventsPath) <= 0)
			{
				WriterErrors.Add(FString::Printf(TEXT("Client endpoint core artifacts are missing: %s"),
					*EndpointId));
				continue;
			}
			TSharedPtr<FJsonObject> Assertions;
			if (!LoadJson(AssertionsPath, Assertions, Error)
				|| !MergeBoolObject(Assertions, TEXT("gate_status"), Error)
				|| !MergeIntObject(Assertions, TEXT("gate_sample_counts"), false, true, Error)
				|| !MergeIntObject(Assertions, TEXT("invariant_counts"), true, false, Error)
				|| !MergeIntObject(Assertions, TEXT("invariant_sample_counts"), true, true, Error))
			{
				WriterErrors.Add(Error);
				continue;
			}
			double ClientEventCount = 0.0;
			if (Assertions->TryGetNumberField(TEXT("event_count"), ClientEventCount)
				&& FMath::IsFinite(ClientEventCount) && ClientEventCount >= 0.0)
			{
				EventCount += static_cast<int64>(ClientEventCount);
			}

			const TCHAR* CsvNames[] = {
				TEXT("client-simulation.csv"), TEXT("server-accepted-snapshots.csv"),
				TEXT("server-validator-relay.csv"), TEXT("atomic-batches.csv"),
				TEXT("lease-watchdog.csv"), TEXT("lease-transactions.csv"),
				TEXT("network-metrics.csv"), TEXT("performance.csv")
			};
			for (const TCHAR* CsvName : CsvNames)
			{
				if (!MergeArtifact(RunRoot / (Prefix + CsvName), Path(CsvName), true, Error))
				{
					WriterErrors.Add(Error);
				}
			}
			if (!MergeArtifact(RunRoot / (Prefix + TEXT("combat-ledger.jsonl")),
				Path(TEXT("combat-ledger.jsonl")), false, Error))
			{
				WriterErrors.Add(Error);
			}
		}
		OutError = FString::Join(WriterErrors, TEXT(" | "));
		return WriterErrors.IsEmpty();
	}

	bool RequiredArtifactsPresent(TArray<FString>& OutMissing) const
	{
		OutMissing.Reset();
		const TCHAR* RequiredNames[] = {
			TEXT("manifest.json"), TEXT("acceptance.json"), TEXT("server-events.jsonl"),
			TEXT("client-simulation.csv"), TEXT("server-accepted-snapshots.csv"),
			TEXT("server-validator-relay.csv"), TEXT("atomic-batches.csv"),
			TEXT("lease-watchdog.csv"), TEXT("lease-transactions.csv"),
			TEXT("combat-ledger.jsonl"), TEXT("network-metrics.csv"),
			TEXT("performance.csv"), TEXT("assertions.json"), TEXT("media-index.json")
		};
		for (const TCHAR* Name : RequiredNames)
		{
			const FString Filename = ArtifactPath(Name);
			if (!IFileManager::Get().FileExists(*Filename))
			{
				OutMissing.Add(Filename);
			}
		}
		if (Descriptor.bServerEndpoint)
		{
			TArray<FString> ClientAcceptances;
			IFileManager::Get().FindFiles(ClientAcceptances,
				*(RunRoot / TEXT("endpoint-*.acceptance.json")), true, false);
			if (ClientAcceptances.Num() != Descriptor.ExpectedClientEndpoints)
			{
				OutMissing.Add(FString::Printf(TEXT("%s (expected %d endpoint acceptance files, found %d)"),
					*(RunRoot / TEXT("endpoint-*.acceptance.json")),
					Descriptor.ExpectedClientEndpoints, ClientAcceptances.Num()));
			}
		}
		if (TracePath.IsEmpty() || IFileManager::Get().FileSize(*TracePath) <= 0)
		{
			OutMissing.Add(TracePath.IsEmpty() ? TEXT("<insights-trace-not-configured>") : TracePath);
		}
		return OutMissing.IsEmpty();
	}
};

FGuLiWingmanQAEvidenceWriter::FGuLiWingmanQAEvidenceWriter()
	: Impl(MakeUnique<FImpl>())
{
}

FGuLiWingmanQAEvidenceWriter::~FGuLiWingmanQAEvidenceWriter() = default;

bool FGuLiWingmanQAEvidenceWriter::Start(
	const FGuLiWingmanQARunDescriptor& Descriptor,
	FString& OutError)
{
#if UE_BUILD_SHIPPING
	OutError = TEXT("Wingman QA evidence is not compiled into Shipping builds.");
	return false;
#else
	FScopeLock Lock(&Impl->Mutex);
	OutError.Reset();
	if (Impl->bActive)
	{
		OutError = TEXT("A Wingman QA evidence run is already active in this process.");
		return false;
	}
	if (!GuLiWingmanQAEvidence::IsSafeIdentity(Descriptor.CampaignId)
		|| !GuLiWingmanQAEvidence::IsSafeIdentity(Descriptor.RunId)
		|| !GuLiWingmanQAEvidence::IsSafeIdentity(Descriptor.EndpointId)
		|| !FGuLiWingmanAcceptanceCatalogV2::FindRole(Descriptor.SuiteRunRole)
		|| Descriptor.ExpectedClientEndpoints < 0 || Descriptor.ExpectedClientEndpoints > 64)
	{
		OutError = TEXT("Invalid campaign_id, run_id, endpoint_id, or suite_run_role.");
		return false;
	}

	Impl->Descriptor = Descriptor;
	const FString OutputRoot = Descriptor.OutputRoot.IsEmpty()
		? FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("WingmanQA"))
		: Descriptor.OutputRoot;
	Impl->RunRoot = FPaths::ConvertRelativePathToFull(
		FPaths::Combine(OutputRoot, Descriptor.CampaignId, Descriptor.RunId));
	Impl->EventPath = Descriptor.bServerEndpoint
		? Impl->Path(TEXT("server-events.jsonl"))
		: Impl->Path(*FString::Printf(TEXT("client-%s.events.jsonl"), *Descriptor.EndpointId));
	Impl->TracePath = Descriptor.InsightsTracePath.IsEmpty()
		? Impl->Path(*FString::Printf(TEXT("endpoint-%s.utrace"), *Descriptor.EndpointId))
		: FPaths::ConvertRelativePathToFull(Descriptor.InsightsTracePath);
	const FString ManifestPath = Impl->ArtifactPath(TEXT("manifest.json"));
	if (IFileManager::Get().FileExists(*ManifestPath)
		|| IFileManager::Get().FileExists(*Impl->EventPath))
	{
		OutError = FString::Printf(TEXT("Wingman QA endpoint output already exists; use a new run_id: %s"),
			*ManifestPath);
		Impl->Descriptor = FGuLiWingmanQARunDescriptor{};
		return false;
	}
	Impl->GateSamples.Reset();
	Impl->GateStatuses.Reset();
	Impl->InvariantCounts.Reset();
	Impl->InvariantSamples.Reset();
	Impl->WriterErrors.Reset();
	Impl->EventCount = 0;

	if (!Impl->CreateArtifacts(OutError) || !Impl->WriteManifest(OutError))
	{
		Impl->Descriptor = FGuLiWingmanQARunDescriptor{};
		return false;
	}
	if (!IFileManager::Get().FileExists(*Impl->EventPath)
		&& !FFileHelper::SaveStringToFile(FString(), *Impl->EventPath,
			FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		OutError = FString::Printf(TEXT("Failed to create endpoint event stream: %s"), *Impl->EventPath);
		return false;
	}
	Impl->bActive = true;
	Impl->RecordGateUnlocked(TEXT("MANIFEST_VALID"), true, 1, OutError);
	Impl->RecordGateUnlocked(TEXT("EVENT_SCHEMA_VALID"), true, 1, OutError);
	UE_LOG(LogGuLiWingmanQA, Display,
		TEXT("Wingman QA run started: campaign=%s run=%s role=%s endpoint=%s root=%s"),
		*Descriptor.CampaignId, *Descriptor.RunId, *Descriptor.SuiteRunRole.ToString(),
		*Descriptor.EndpointId, *Impl->RunRoot);
	return true;
#endif
}

bool FGuLiWingmanQAEvidenceWriter::RecordEvent(
	const FGuLiWingmanQAEvent& Event,
	FString& OutError)
{
	FScopeLock Lock(&Impl->Mutex);
	OutError.Reset();
	if (!Impl->bActive)
	{
		OutError = TEXT("No active Wingman QA evidence run.");
		return false;
	}
	if (!FGuLiWingmanQASchema::IsKnownEvent(Event.Event))
	{
		OutError = FString::Printf(TEXT("Unknown Wingman QA event: %s"), *Event.Event.ToString());
		Impl->WriterErrors.Add(OutError);
		return false;
	}
	for (const TPair<FName, FString>& Field : Event.Fields)
	{
		if (!FGuLiWingmanQASchema::IsKnownField(Field.Key))
		{
			OutError = FString::Printf(TEXT("Unknown Wingman QA field: %s"), *Field.Key.ToString());
			Impl->WriterErrors.Add(OutError);
			return false;
		}
	}
	if (!Event.GateId.IsNone() && !Impl->AllowedGates().Contains(Event.GateId))
	{
		OutError = FString::Printf(TEXT("Event references an unknown gate: %s"), *Event.GateId.ToString());
		Impl->WriterErrors.Add(OutError);
		return false;
	}
	if (!Event.InvariantKey.IsNone()
		&& !FGuLiWingmanAcceptanceCatalogV2::GetInvariantKeys().Contains(Event.InvariantKey))
	{
		OutError = FString::Printf(TEXT("Event references an unknown invariant: %s"),
			*Event.InvariantKey.ToString());
		Impl->WriterErrors.Add(OutError);
		return false;
	}

	const int64 UtcMicroseconds = FDateTime::UtcNow().GetTicks() / ETimespan::TicksPerMicrosecond;
	TSharedRef<FJsonObject> Fields = MakeShared<FJsonObject>();
	TArray<FName> FieldNames;
	Event.Fields.GetKeys(FieldNames);
	FieldNames.Sort(FNameLexicalLess());
	for (const FName FieldName : FieldNames)
	{
		Fields->SetStringField(FieldName.ToString(), Event.Fields[FieldName]);
	}

	TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
	Object->SetStringField(TEXT("schema"), GuLiWingmanQAEvidence::SchemaName);
	Object->SetNumberField(TEXT("schema_version"), FGuLiWingmanQASchema::Version);
	Object->SetStringField(TEXT("campaign_id"), Impl->Descriptor.CampaignId);
	Object->SetStringField(TEXT("run_id"), Impl->Descriptor.RunId);
	Object->SetStringField(TEXT("pair_id"), Impl->Descriptor.PairId);
	Object->SetStringField(TEXT("suite_run_role"), Impl->Descriptor.SuiteRunRole.ToString());
	Object->SetStringField(TEXT("event"), Event.Event.ToString());
	Object->SetStringField(TEXT("map"), Impl->Descriptor.Map);
	Object->SetStringField(TEXT("net_mode"), Impl->Descriptor.NetMode);
	Object->SetNumberField(TEXT("pid"), FPlatformProcess::GetCurrentProcessId());
	Object->SetNumberField(TEXT("utc_us"), static_cast<double>(UtcMicroseconds));
	Object->SetNumberField(TEXT("server_time_s"), Event.ServerTimeSeconds);
	Object->SetNumberField(TEXT("seed"), Impl->Descriptor.Seed);
	Object->SetStringField(TEXT("acceptance_profile"), Impl->Descriptor.AcceptanceProfile);
	Object->SetNumberField(TEXT("catalog_version"), FGuLiWingmanAcceptanceCatalogV2::Version);
	Object->SetStringField(TEXT("catalog_hash"),
		FGuLiWingmanAcceptanceCatalogV2::GetCatalogHashSha256());
	Object->SetStringField(TEXT("endpoint_id"), Impl->Descriptor.EndpointId);
	Object->SetStringField(TEXT("log_stream"), FGuLiWingmanQASchema::StreamName(Event.Stream));
	if (!Event.GateId.IsNone())
	{
		Object->SetStringField(TEXT("gate_id"), Event.GateId.ToString());
	}
	if (!Event.InvariantKey.IsNone())
	{
		Object->SetStringField(TEXT("invariant_key"), Event.InvariantKey.ToString());
		Object->SetNumberField(TEXT("invariant_count"), static_cast<double>(Event.InvariantCount));
		Impl->InvariantCounts.FindOrAdd(Event.InvariantKey) = FMath::Max(
			Impl->InvariantCounts.FindRef(Event.InvariantKey), Event.InvariantCount);
		++Impl->InvariantSamples.FindOrAdd(Event.InvariantKey);
	}
	Object->SetObjectField(TEXT("fields"), Fields);
	const FString Line = GuLiWingmanQAEvidence::JsonString(Object, true);
	if (!Impl->Append(Impl->EventPath, Line, OutError))
	{
		return false;
	}
	if (Event.Stream == EGuLiWingmanQALogStream::BattleCombat
		&& !Impl->Append(Impl->ArtifactPath(TEXT("combat-ledger.jsonl")), Line, OutError))
	{
		return false;
	}

	const FString FieldsJson = GuLiWingmanQAEvidence::JsonString(Fields, true);
	if (!Impl->Descriptor.bServerEndpoint
		&& (Event.Stream == EGuLiWingmanQALogStream::Wingman
			|| Event.Stream == EGuLiWingmanQALogStream::WingmanAI))
	{
		Impl->AppendCsv(TEXT("client-simulation.csv"), Event, UtcMicroseconds, FieldsJson, OutError);
	}
	if (Impl->Descriptor.bServerEndpoint && GuLiWingmanQAEvidence::IsAcceptedSnapshotEvent(Event.Event))
	{
		Impl->AppendCsv(TEXT("server-accepted-snapshots.csv"), Event, UtcMicroseconds, FieldsJson, OutError);
	}
	if (Impl->Descriptor.bServerEndpoint && GuLiWingmanQAEvidence::IsValidatorEvent(Event.Event))
	{
		Impl->AppendCsv(TEXT("server-validator-relay.csv"), Event, UtcMicroseconds, FieldsJson, OutError);
	}
	if (GuLiWingmanQAEvidence::IsAtomicEvent(Event.Event))
	{
		Impl->AppendCsv(TEXT("atomic-batches.csv"), Event, UtcMicroseconds, FieldsJson, OutError);
	}
	if (GuLiWingmanQAEvidence::IsWatchdogEvent(Event.Event))
	{
		Impl->AppendCsv(TEXT("lease-watchdog.csv"), Event, UtcMicroseconds, FieldsJson, OutError);
	}
	if (GuLiWingmanQAEvidence::IsLeaseTransactionEvent(Event.Event))
	{
		Impl->AppendCsv(TEXT("lease-transactions.csv"), Event, UtcMicroseconds, FieldsJson, OutError);
	}
	if (Event.Stream == EGuLiWingmanQALogStream::WingmanNet)
	{
		Impl->AppendCsv(TEXT("network-metrics.csv"), Event, UtcMicroseconds, FieldsJson, OutError);
	}
	if (Event.Stream == EGuLiWingmanQALogStream::WingmanQA && Event.Event == TEXT("QA_CHECK"))
	{
		Impl->AppendCsv(TEXT("performance.csv"), Event, UtcMicroseconds, FieldsJson, OutError);
	}
	++Impl->EventCount;
	GuLiWingmanQAEvidence::LogEvent(Event.Stream, Line);
	return OutError.IsEmpty();
}

bool FGuLiWingmanQAEvidenceWriter::RecordGate(
	const FName GateId,
	const bool bPassed,
	const int64 SampleCount,
	FString& OutError)
{
	FScopeLock Lock(&Impl->Mutex);
	OutError.Reset();
	if (!Impl->bActive)
	{
		OutError = TEXT("No active Wingman QA evidence run.");
		return false;
	}
	return Impl->RecordGateUnlocked(GateId, bPassed, SampleCount, OutError);
}

bool FGuLiWingmanQAEvidenceWriter::RecordInvariant(
	const FName InvariantKey,
	const int64 Count,
	FString& OutError)
{
	FScopeLock Lock(&Impl->Mutex);
	OutError.Reset();
	if (!Impl->bActive)
	{
		OutError = TEXT("No active Wingman QA evidence run.");
		return false;
	}
	if (!FGuLiWingmanAcceptanceCatalogV2::GetInvariantKeys().Contains(InvariantKey) || Count < 0)
	{
		OutError = FString::Printf(TEXT("Unknown invariant or negative count: %s/%lld"),
			*InvariantKey.ToString(), Count);
		Impl->WriterErrors.Add(OutError);
		return false;
	}
	Impl->InvariantCounts.FindOrAdd(InvariantKey) = FMath::Max(
		Impl->InvariantCounts.FindRef(InvariantKey), Count);
	++Impl->InvariantSamples.FindOrAdd(InvariantKey);
	return true;
}

bool FGuLiWingmanQAEvidenceWriter::Stop(
	const bool bRunCompleted,
	const bool bScenarioPassed,
	FGuLiWingmanAcceptanceRunEvidence& OutEvidence,
	FString& OutError)
{
	FScopeLock Lock(&Impl->Mutex);
	OutError.Reset();
	OutEvidence = {};
	if (!Impl->bActive)
	{
		OutError = TEXT("No active Wingman QA evidence run.");
		return false;
	}

	if (Impl->Descriptor.bServerEndpoint)
	{
		FString MergeError;
		Impl->MergeClientEvidence(MergeError);
	}
	TArray<FString> MissingArtifacts;
	const bool bArtifactsComplete = Impl->RequiredArtifactsPresent(MissingArtifacts);
	FString GateError;
	Impl->RecordGateUnlocked(TEXT("EVIDENCE_COMPLETE"), bArtifactsComplete, 1, GateError);
	const bool bEveryInvariantObserved = Algo::AllOf(
		FGuLiWingmanAcceptanceCatalogV2::GetInvariantKeys(),
		[this](const FName Key) { return Impl->InvariantSamples.FindRef(Key) > 0; });
	const bool bInvariantsZero = bEveryInvariantObserved && Algo::AllOf(
		FGuLiWingmanAcceptanceCatalogV2::GetInvariantKeys(),
		[this](const FName Key) { return Impl->InvariantCounts.FindRef(Key) == 0; });
	Impl->RecordGateUnlocked(TEXT("INVARIANTS_ZERO"), bInvariantsZero, 1, GateError);

	OutEvidence.RunId = Impl->Descriptor.RunId;
	OutEvidence.RoleId = Impl->Descriptor.SuiteRunRole;
	OutEvidence.CatalogVersion = FGuLiWingmanAcceptanceCatalogV2::Version;
	OutEvidence.CatalogHash = FGuLiWingmanAcceptanceCatalogV2::GetCatalogHashSha256();
	OutEvidence.EvidencePath = Impl->RunRoot;
	OutEvidence.bRunCompleted = bRunCompleted;
	OutEvidence.GateSampleCounts = Impl->GateSamples;
	OutEvidence.InvariantCounts = Impl->InvariantCounts;
	const bool bEveryGatePassed = Algo::AllOf(Impl->GateStatuses,
		[](const TPair<FName, bool>& Gate) { return Gate.Value; });
	OutEvidence.bRunPassed = bRunCompleted && bScenarioPassed && bArtifactsComplete
		&& bEveryGatePassed && bInvariantsZero && Impl->WriterErrors.IsEmpty();

	TArray<FString> ValidationErrors;
	if (Impl->Descriptor.bServerEndpoint)
	{
		FGuLiWingmanAcceptanceCatalogV2::ValidateRun(OutEvidence, ValidationErrors);
		if (!ValidationErrors.IsEmpty())
		{
			OutEvidence.bRunPassed = false;
		}
	}
	ValidationErrors.Append(Impl->WriterErrors);
	for (const FString& Missing : MissingArtifacts)
	{
		ValidationErrors.Add(FString::Printf(TEXT("Missing or empty required artifact: %s"), *Missing));
	}

	TSharedRef<FJsonObject> Assertions = MakeShared<FJsonObject>();
	Assertions->SetStringField(TEXT("schema"), GuLiWingmanQAEvidence::AssertionsSchema);
	Assertions->SetStringField(TEXT("campaign_id"), Impl->Descriptor.CampaignId);
	Assertions->SetStringField(TEXT("run_id"), Impl->Descriptor.RunId);
	Assertions->SetStringField(TEXT("suite_run_role"), Impl->Descriptor.SuiteRunRole.ToString());
	Assertions->SetStringField(TEXT("endpoint_id"), Impl->Descriptor.EndpointId);
	Assertions->SetObjectField(TEXT("gate_status"),
		GuLiWingmanQAEvidence::NameBoolMap(Impl->GateStatuses));
	Assertions->SetObjectField(TEXT("gate_sample_counts"),
		GuLiWingmanQAEvidence::NameIntMap(Impl->GateSamples));
	Assertions->SetObjectField(TEXT("invariant_counts"),
		GuLiWingmanQAEvidence::NameIntMap(Impl->InvariantCounts));
	Assertions->SetObjectField(TEXT("invariant_sample_counts"),
		GuLiWingmanQAEvidence::NameIntMap(Impl->InvariantSamples));
	Assertions->SetNumberField(TEXT("event_count"), static_cast<double>(Impl->EventCount));
	TArray<TSharedPtr<FJsonValue>> ErrorValues;
	for (const FString& Error : ValidationErrors)
	{
		ErrorValues.Add(MakeShared<FJsonValueString>(Error));
	}
	Assertions->SetArrayField(TEXT("errors"), ErrorValues);

	TSharedRef<FJsonObject> Acceptance = MakeShared<FJsonObject>();
	Acceptance->SetStringField(TEXT("schema"), GuLiWingmanQAEvidence::AcceptanceSchema);
	Acceptance->SetStringField(TEXT("campaign_id"), Impl->Descriptor.CampaignId);
	Acceptance->SetStringField(TEXT("run_id"), OutEvidence.RunId);
	Acceptance->SetStringField(TEXT("suite_run_role"), OutEvidence.RoleId.ToString());
	Acceptance->SetStringField(TEXT("endpoint_id"), Impl->Descriptor.EndpointId);
	Acceptance->SetStringField(TEXT("trace_path"), Impl->TracePath);
	Acceptance->SetNumberField(TEXT("catalog_version"), OutEvidence.CatalogVersion);
	Acceptance->SetStringField(TEXT("catalog_hash"), OutEvidence.CatalogHash);
	Acceptance->SetStringField(TEXT("evidence_path"), OutEvidence.EvidencePath);
	Acceptance->SetBoolField(TEXT("run_completed"), OutEvidence.bRunCompleted);
	Acceptance->SetBoolField(TEXT("scenario_passed"), bScenarioPassed);
	Acceptance->SetBoolField(TEXT("passed"), OutEvidence.bRunPassed);
	Acceptance->SetObjectField(TEXT("gate_sample_counts"),
		GuLiWingmanQAEvidence::NameIntMap(OutEvidence.GateSampleCounts));
	Acceptance->SetObjectField(TEXT("invariant_counts"),
		GuLiWingmanQAEvidence::NameIntMap(OutEvidence.InvariantCounts));
	Acceptance->SetStringField(TEXT("finished_utc"), FDateTime::UtcNow().ToIso8601());
	Acceptance->SetArrayField(TEXT("errors"), MoveTemp(ErrorValues));

	FString SaveError;
	const bool bFilesSaved = Impl->SaveJson(
		Impl->ArtifactPath(TEXT("assertions.json")), Assertions, SaveError)
		&& Impl->SaveJson(Impl->ArtifactPath(TEXT("acceptance.json")), Acceptance, SaveError);
	Impl->bActive = false;
	OutError = FString::Join(ValidationErrors, TEXT(" | "));
	if (!SaveError.IsEmpty())
	{
		OutError = OutError.IsEmpty() ? SaveError : OutError + TEXT(" | ") + SaveError;
	}
	UE_LOG(LogGuLiWingmanQA, Display,
		TEXT("Wingman QA run finalized: campaign=%s run=%s role=%s passed=%d events=%lld root=%s errors=%s"),
		*Impl->Descriptor.CampaignId, *Impl->Descriptor.RunId,
		*Impl->Descriptor.SuiteRunRole.ToString(), OutEvidence.bRunPassed ? 1 : 0,
		Impl->EventCount, *Impl->RunRoot, OutError.IsEmpty() ? TEXT("none") : *OutError);
	return bFilesSaved && OutEvidence.bRunPassed;
}

bool FGuLiWingmanQAEvidenceWriter::IsActive() const
{
	FScopeLock Lock(&Impl->Mutex);
	return Impl->bActive;
}

FString FGuLiWingmanQAEvidenceWriter::GetRunRoot() const
{
	FScopeLock Lock(&Impl->Mutex);
	return Impl->RunRoot;
}

FString FGuLiWingmanQAEvidenceWriter::GetInsightsTracePath() const
{
	FScopeLock Lock(&Impl->Mutex);
	return Impl->TracePath;
}
