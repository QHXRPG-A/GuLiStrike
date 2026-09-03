// Copyright Epic Games, Inc. All Rights Reserved.

#include "Development/GuLiWingmanAcceptanceCatalog.h"

namespace GuLiWingmanAcceptanceCatalog
{
	TArray<FName> Names(std::initializer_list<const TCHAR*> Values)
	{
		TArray<FName> Result;
		Result.Reserve(static_cast<int32>(Values.size()));
		for (const TCHAR* Value : Values)
		{
			Result.Emplace(Value);
		}
		return Result;
	}

	FGuLiWingmanAcceptanceRoleDefinition Role(
		const TCHAR* RoleId,
		std::initializer_list<const TCHAR*> RequiredGates)
	{
		FGuLiWingmanAcceptanceRoleDefinition Result;
		Result.RoleId = FName(RoleId);
		Result.RequiredGateIds = Names(RequiredGates);
		return Result;
	}

	bool ContainsForbiddenLegacyGateToken(const FName GateId)
	{
		const FString Text = GateId.ToString();
		return Text.Contains(TEXT("CANONICAL_SLOT"), ESearchCase::IgnoreCase)
			|| Text.Contains(TEXT("SERVER_MOTION_FALLBACK"), ESearchCase::IgnoreCase)
			|| Text.Contains(TEXT("BRIDGE"), ESearchCase::IgnoreCase);
	}

	uint32 RotateRight(const uint32 Value, const uint32 Count)
	{
		return (Value >> Count) | (Value << (32u - Count));
	}
}

const TArray<FName>& FGuLiWingmanAcceptanceCatalogV2::GetCommonRequiredGateIds()
{
	static const TArray<FName> Gates = GuLiWingmanAcceptanceCatalog::Names({
		TEXT("MANIFEST_VALID"),
		TEXT("EVENT_SCHEMA_VALID"),
		TEXT("EVIDENCE_COMPLETE"),
		TEXT("PROTOCOL_V7"),
		TEXT("SERVER_WINGMAN_MOTION_ZERO"),
		TEXT("INVARIANTS_ZERO")
	});
	return Gates;
}

const TArray<FName>& FGuLiWingmanAcceptanceCatalogV2::GetInvariantKeys()
{
	static const TArray<FName> Keys = GuLiWingmanAcceptanceCatalog::Names({
		TEXT("SERVER_MOTION_SIM_EXECUTED"),
		TEXT("SERVER_WINGMAN_STATETREE_EXECUTED"),
		TEXT("SERVER_WINGMAN_PATHFINDING_EXECUTED"),
		TEXT("SERVER_WINGMAN_STEERING_EXECUTED"),
		TEXT("SERVER_WINGMAN_INTEGRATION_EXECUTED"),
		TEXT("SERVER_GROUND_MACHINE_STATETREE_EXECUTED"),
		TEXT("SERVER_GROUND_MACHINE_PATHFINDING_EXECUTED"),
		TEXT("SERVER_GROUND_MACHINE_STEERING_EXECUTED"),
		TEXT("SERVER_GROUND_MACHINE_INTEGRATION_EXECUTED"),
		TEXT("SERVER_GENERATED_RUNTIME_TRANSFORM"),
		TEXT("ACTIVE_WITHOUT_FULL_BOOTSTRAP"),
		TEXT("PARTIAL_ATOMIC_BATCH_COMMIT"),
		TEXT("BATCH_BASELINE_MISMATCH"),
		TEXT("BATCH_ROSTER_MISMATCH"),
		TEXT("TAKEOVER_DEADLINE_BYPASSED"),
		TEXT("NEW_LEASE_REVOKED_FROM_OLD_FRESHNESS"),
		TEXT("PENDING_OFFER_PAUSED_ACTIVE_FRESHNESS"),
		TEXT("OLD_LEASE_ADVANCED_FIRE_SEQUENCE"),
		TEXT("ACCEPTED_SNAPSHOT_REGRESSION"),
		TEXT("SNAPSHOT_ADVANCED_WITHOUT_ACCEPTED_CANDIDATE"),
		TEXT("REJECTED_STATE_RELAYED"),
		TEXT("UNVALIDATED_STATE_RELAYED"),
		TEXT("DAMAGE_FROM_UNACCEPTED_POSE"),
		TEXT("DUPLICATE_SIM_WRITER"),
		TEXT("WATCHDOG_SCAN_RATE_VIOLATION"),
		TEXT("STALE_COMBAT_ACCEPTED"),
		TEXT("UNAVAILABLE_TARGET_ACQUIRED"),
		TEXT("RESUME_BASE_MISMATCH"),
		TEXT("POST_DEATH_ACTIVITY"),
		TEXT("MIN_SPEED_VIOLATION"),
		TEXT("TURN_RATE_VIOLATION"),
		TEXT("TRANSFORM_JUMP"),
		TEXT("CLEARANCE_VIOLATION"),
		TEXT("FIRE_OUT_OF_RANGE"),
		TEXT("FIRE_WITHOUT_LOS"),
		TEXT("FRIENDLY_DAMAGE"),
		TEXT("DUPLICATE_DAMAGE_COMMIT"),
		TEXT("VISUAL_EXTRAPOLATION_TIMEOUT"),
		TEXT("COMMANDER_FIXED_STEP_DROP"),
		TEXT("NAV_RUNTIME_REBUILD")
	});
	return Keys;
}

const TArray<FGuLiWingmanAcceptanceRoleDefinition>& FGuLiWingmanAcceptanceCatalogV2::GetRoles()
{
	using namespace GuLiWingmanAcceptanceCatalog;
	static const TArray<FGuLiWingmanAcceptanceRoleDefinition> Roles = {
		Role(TEXT("Main-S0-S5"), {
			TEXT("DEDICATED_SERVER_READY"), TEXT("BOOTSTRAP_SIX_SCOPE_ATOMIC"),
			TEXT("OWNER_30HZ_SIMULATION"), TEXT("FORMATION_DOUBLE_RING"),
			TEXT("NAVIGATION_VALID"), TEXT("COMBAT_LEDGER_SINGLE_COMMIT"),
			TEXT("DEATH_ROSTER_COMMIT"), TEXT("REPLENISH_15_SECONDS")}),
		Role(TEXT("S1-Control"), {
			TEXT("COMMANDER_1800_FIXED_STEPS"), TEXT("COMMANDER_DROPPED_STEPS_ZERO"),
			TEXT("WINGMAN_FAULT_INJECTION_DISABLED")}),
		Role(TEXT("S3C-AB"), {
			TEXT("COMBAT_BARRIER_RELEASE_AB"), TEXT("MIRRORED_LEDGER_SINGLE_COMMIT")}),
		Role(TEXT("S3C-BA"), {
			TEXT("COMBAT_BARRIER_RELEASE_BA"), TEXT("MIRRORED_LEDGER_SINGLE_COMMIT")}),
		Role(TEXT("S4-Idempotency-FireProposalReplay"), {
			TEXT("FIRE_PROPOSAL_REPLAY_SINGLE_COMMIT")}),
		Role(TEXT("S4-Idempotency-ProjectileOverlapReplay"), {
			TEXT("PROJECTILE_OVERLAP_REPLAY_SINGLE_COMMIT")}),
		Role(TEXT("S4-Idempotency-Concurrent"), {
			TEXT("CONCURRENT_DUPLICATE_SINGLE_COMMIT")}),
		Role(TEXT("S6-CorrectionReverse"), {
			TEXT("BASELINE_SEQUENCE_REGRESSION_REJECTED")}),
		Role(TEXT("S6-DeathBeforePose"), {
			TEXT("POST_DEATH_POSE_REJECTED")}),
		Role(TEXT("S6-LeaseBeforeOldProposal"), {
			TEXT("OLD_LEASE_PROPOSAL_REJECTED")}),
		Role(TEXT("S6-RespawnBeforeOldPose"), {
			TEXT("OLD_GROUP_POSE_REJECTED")}),
		Role(TEXT("S7-LeaseLoss"), {
			TEXT("LEASE_ACTIVE_STALE_UNAVAILABLE_REVOKE"),
			TEXT("TAKEOVER_RECOVERS_ACTIVE"), TEXT("OLD_OWNER_PERMANENTLY_REJECTED")}),
		Role(TEXT("S7-Graceful-FireBeforeCommit"), {
			TEXT("TRANSFER_OFFER_READY_COMMIT_ACK_TAKEOVER"),
			TEXT("INFLIGHT_FIRE_SINGLE_COMMIT")}),
		Role(TEXT("S7-Graceful-DamageAckRace"), {
			TEXT("DAMAGE_ACK_TRANSFER_RACE_SINGLE_COMMIT")}),
		Role(TEXT("S7-ActualDisconnect"), {
			TEXT("OWNER_SOCKET_DISCONNECT_DETECTED"), TEXT("BACKUP_TAKEOVER_AFTER_DISCONNECT"),
			TEXT("DEFERRED_REPLENISH_SINGLE_SPAWN")}),
		Role(TEXT("S8A-OpeningSpectator"), {
			TEXT("OPENING_SPECTATOR_ATOMIC_BOOTSTRAP")}),
		Role(TEXT("S8B-MidCombatJoin"), {
			TEXT("MID_COMBAT_JOIN_ATOMIC_BOOTSTRAP"), TEXT("LATEJOIN_DAMAGE_LEDGER_CONSISTENT")}),
		Role(TEXT("S8C-TransferPendingJoin"), {
			TEXT("TRANSFER_PENDING_JOIN_FROZEN_CUT")}),
		Role(TEXT("S9-Control"), {
			TEXT("COMMANDER_500_CONTROL"), TEXT("WINGMAN_COUNT_ZERO")}),
		Role(TEXT("S9-Feature"), {
			TEXT("FOUR_SHIPS_100_WINGMEN_500_COMMANDER"),
			TEXT("TEN_MINUTE_PERFORMANCE_CAPTURE"), TEXT("NETWORK_BUDGET_CAPTURE")}),
		Role(TEXT("H4000-OwnerSimulation"), {
			TEXT("OWNER_4000_HEADLESS_30HZ"), TEXT("OWNER_FRAME_P95_WITHIN_BUDGET")}),
		Role(TEXT("H4000-ServerValidator"), {
			TEXT("SERVER_4000_RECORDED_CANDIDATES"), TEXT("VALIDATE_STORE_RELAY_P95_WITHIN_BUDGET")})
	};
	static_assert(ExpectedRoleCount == 22);
	return Roles;
}

FString FGuLiWingmanAcceptanceCatalogV2::BuildCanonicalCatalog()
{
	FString Canonical = FString::Printf(TEXT("WingmanAcceptanceCatalogV%d\n"), Version);
	Canonical += TEXT("common=");
	Canonical += FString::JoinBy(GetCommonRequiredGateIds(), TEXT(","),
		[](const FName Name) { return Name.ToString(); });
	Canonical += TEXT("\n");
	for (const FGuLiWingmanAcceptanceRoleDefinition& Role : GetRoles())
	{
		Canonical += TEXT("role=");
		Canonical += Role.RoleId.ToString();
		Canonical += TEXT("|");
		Canonical += FString::JoinBy(Role.RequiredGateIds, TEXT(","),
			[](const FName Name) { return Name.ToString(); });
		Canonical += TEXT("\n");
	}
	Canonical += TEXT("invariants=");
	Canonical += FString::JoinBy(GetInvariantKeys(), TEXT(","),
		[](const FName Name) { return Name.ToString(); });
	Canonical += TEXT("\n");
	return Canonical;
}

FString FGuLiWingmanAcceptanceCatalogV2::GetCatalogHashSha256()
{
	return ComputeSha256Hex(BuildCanonicalCatalog());
}

FString FGuLiWingmanAcceptanceCatalogV2::ComputeSha256Hex(const FStringView Utf8Text)
{
	using namespace GuLiWingmanAcceptanceCatalog;
	static constexpr uint32 RoundConstants[64] = {
		0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u,
		0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
		0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
		0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
		0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
		0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
		0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
		0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
		0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
		0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
		0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u,
		0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
		0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u,
		0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
		0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
		0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u
	};
	uint32 State[8] = {
		0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
		0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u
	};

	const FString OwnedText(Utf8Text);
	const FTCHARToUTF8 Utf8(*OwnedText);
	TArray<uint8> Message;
	Message.Append(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
	const uint64 OriginalBitLength = static_cast<uint64>(Message.Num()) * 8ull;
	Message.Add(0x80u);
	while ((Message.Num() % 64) != 56)
	{
		Message.Add(0u);
	}
	for (int32 Shift = 56; Shift >= 0; Shift -= 8)
	{
		Message.Add(static_cast<uint8>(OriginalBitLength >> Shift));
	}

	for (int32 BlockOffset = 0; BlockOffset < Message.Num(); BlockOffset += 64)
	{
		uint32 Words[64]{};
		for (int32 Index = 0; Index < 16; ++Index)
		{
			const int32 ByteOffset = BlockOffset + Index * 4;
			Words[Index] = (static_cast<uint32>(Message[ByteOffset]) << 24u)
				| (static_cast<uint32>(Message[ByteOffset + 1]) << 16u)
				| (static_cast<uint32>(Message[ByteOffset + 2]) << 8u)
				| static_cast<uint32>(Message[ByteOffset + 3]);
		}
		for (int32 Index = 16; Index < 64; ++Index)
		{
			const uint32 S0 = RotateRight(Words[Index - 15], 7u)
				^ RotateRight(Words[Index - 15], 18u)
				^ (Words[Index - 15] >> 3u);
			const uint32 S1 = RotateRight(Words[Index - 2], 17u)
				^ RotateRight(Words[Index - 2], 19u)
				^ (Words[Index - 2] >> 10u);
			Words[Index] = Words[Index - 16] + S0 + Words[Index - 7] + S1;
		}

		uint32 A = State[0];
		uint32 B = State[1];
		uint32 C = State[2];
		uint32 D = State[3];
		uint32 E = State[4];
		uint32 F = State[5];
		uint32 G = State[6];
		uint32 H = State[7];
		for (int32 Index = 0; Index < 64; ++Index)
		{
			const uint32 Sum1 = RotateRight(E, 6u) ^ RotateRight(E, 11u) ^ RotateRight(E, 25u);
			const uint32 Choice = (E & F) ^ ((~E) & G);
			const uint32 Temp1 = H + Sum1 + Choice + RoundConstants[Index] + Words[Index];
			const uint32 Sum0 = RotateRight(A, 2u) ^ RotateRight(A, 13u) ^ RotateRight(A, 22u);
			const uint32 Majority = (A & B) ^ (A & C) ^ (B & C);
			const uint32 Temp2 = Sum0 + Majority;
			H = G;
			G = F;
			F = E;
			E = D + Temp1;
			D = C;
			C = B;
			B = A;
			A = Temp1 + Temp2;
		}
		State[0] += A;
		State[1] += B;
		State[2] += C;
		State[3] += D;
		State[4] += E;
		State[5] += F;
		State[6] += G;
		State[7] += H;
	}

	return FString::Printf(
		TEXT("%08x%08x%08x%08x%08x%08x%08x%08x"),
		State[0], State[1], State[2], State[3],
		State[4], State[5], State[6], State[7]);
}

const FGuLiWingmanAcceptanceRoleDefinition* FGuLiWingmanAcceptanceCatalogV2::FindRole(
	const FName RoleId)
{
	return GetRoles().FindByPredicate([RoleId](
		const FGuLiWingmanAcceptanceRoleDefinition& Role)
	{
		return Role.RoleId == RoleId;
	});
}

bool FGuLiWingmanAcceptanceCatalogV2::ValidateRun(
	const FGuLiWingmanAcceptanceRunEvidence& Run,
	TArray<FString>& OutErrors)
{
	OutErrors.Reset();
	const FGuLiWingmanAcceptanceRoleDefinition* Role = FindRole(Run.RoleId);
	if (!Role)
	{
		OutErrors.Add(FString::Printf(TEXT("Unknown suite_run_role: %s"), *Run.RoleId.ToString()));
		return false;
	}

	const TArray<FName>& CommonGates = GetCommonRequiredGateIds();
	const TArray<FName>& Invariants = GetInvariantKeys();
	const FString ExpectedHash = GetCatalogHashSha256();
	if (Run.RunId.IsEmpty())
	{
		OutErrors.Add(FString::Printf(TEXT("Role %s has no run_id."), *Run.RoleId.ToString()));
	}
	if (Run.CatalogVersion != Version || !Run.CatalogHash.Equals(ExpectedHash, ESearchCase::IgnoreCase))
	{
		OutErrors.Add(FString::Printf(TEXT("Catalog version/hash mismatch for role %s."),
			*Run.RoleId.ToString()));
	}
	if (Run.EvidencePath.IsEmpty() || !Run.bRunCompleted || !Run.bRunPassed)
	{
		OutErrors.Add(FString::Printf(TEXT("Role %s has incomplete or failed evidence."),
			*Run.RoleId.ToString()));
	}

	TSet<FName> AllowedGates;
	for (const FName Gate : CommonGates)
	{
		AllowedGates.Add(Gate);
	}
	for (const FName Gate : Role->RequiredGateIds)
	{
		AllowedGates.Add(Gate);
	}
	for (const TPair<FName, int64>& Gate : Run.GateSampleCounts)
	{
		if (!AllowedGates.Contains(Gate.Key)
			|| GuLiWingmanAcceptanceCatalog::ContainsForbiddenLegacyGateToken(Gate.Key))
		{
			OutErrors.Add(FString::Printf(TEXT("Role %s contains unknown/legacy gate %s."),
				*Run.RoleId.ToString(),
				*Gate.Key.ToString()));
		}
	}
	for (const FName RequiredGate : AllowedGates)
	{
		const int64* SampleCount = Run.GateSampleCounts.Find(RequiredGate);
		if (!SampleCount || *SampleCount <= 0)
		{
			OutErrors.Add(FString::Printf(TEXT("Role %s has no samples for required gate %s."),
				*Run.RoleId.ToString(),
				*RequiredGate.ToString()));
		}
	}

	for (const TPair<FName, int64>& Invariant : Run.InvariantCounts)
	{
		if (!Invariants.Contains(Invariant.Key))
		{
			OutErrors.Add(FString::Printf(TEXT("Role %s contains unknown invariant %s."),
				*Run.RoleId.ToString(),
				*Invariant.Key.ToString()));
		}
		else if (Invariant.Value != 0)
		{
			OutErrors.Add(FString::Printf(TEXT("Role %s invariant %s is non-zero (%lld)."),
				*Run.RoleId.ToString(),
				*Invariant.Key.ToString(),
				Invariant.Value));
		}
	}
	for (const FName RequiredInvariant : Invariants)
	{
		if (!Run.InvariantCounts.Contains(RequiredInvariant))
		{
			OutErrors.Add(FString::Printf(TEXT("Role %s is missing invariant %s."),
				*Run.RoleId.ToString(),
				*RequiredInvariant.ToString()));
		}
	}
	return OutErrors.IsEmpty();
}

bool FGuLiWingmanAcceptanceCatalogV2::ValidateCampaign(
	const TConstArrayView<FGuLiWingmanAcceptanceRunEvidence> Runs,
	TArray<FString>& OutErrors)
{
	OutErrors.Reset();
	const TArray<FGuLiWingmanAcceptanceRoleDefinition>& Roles = GetRoles();
	const FString ExpectedHash = GetCatalogHashSha256();
	if (Roles.Num() != ExpectedRoleCount || ExpectedHash.Len() != 64)
	{
		OutErrors.Add(TEXT("Catalog definition is internally invalid."));
		return false;
	}

	TSet<FName> ExpectedRoleIds;
	for (const FGuLiWingmanAcceptanceRoleDefinition& Role : Roles)
	{
		if (Role.RoleId.IsNone() || ExpectedRoleIds.Contains(Role.RoleId))
		{
			OutErrors.Add(FString::Printf(TEXT("Catalog contains an invalid or duplicate role: %s"),
				*Role.RoleId.ToString()));
		}
		ExpectedRoleIds.Add(Role.RoleId);
	}
	if (Runs.Num() != ExpectedRoleCount)
	{
		OutErrors.Add(FString::Printf(TEXT("Expected exactly %d runs, received %d."),
			ExpectedRoleCount,
			Runs.Num()));
	}

	TSet<FName> SeenRoles;
	TSet<FString> SeenRunIds;
	for (const FGuLiWingmanAcceptanceRunEvidence& Run : Runs)
	{
		if (SeenRoles.Contains(Run.RoleId))
		{
			OutErrors.Add(FString::Printf(TEXT("Duplicate suite_run_role: %s"), *Run.RoleId.ToString()));
		}
		SeenRoles.Add(Run.RoleId);
		if (Run.RunId.IsEmpty() || SeenRunIds.Contains(Run.RunId))
		{
			OutErrors.Add(FString::Printf(TEXT("Missing or duplicate run_id for role %s."),
				*Run.RoleId.ToString()));
		}
		SeenRunIds.Add(Run.RunId);
		TArray<FString> RunErrors;
		ValidateRun(Run, RunErrors);
		OutErrors.Append(MoveTemp(RunErrors));
	}

	for (const FName ExpectedRole : ExpectedRoleIds)
	{
		if (!SeenRoles.Contains(ExpectedRole))
		{
			OutErrors.Add(FString::Printf(TEXT("Missing suite_run_role: %s"), *ExpectedRole.ToString()));
		}
	}
	return OutErrors.IsEmpty();
}
