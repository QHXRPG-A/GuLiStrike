#include "Battle/Contracts/GuLiWingmanProtocolTypes.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Gameplay/Ship/Abilities/GuLiShipAbilityTags.h"
#include "Misc/AutomationTest.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"

namespace GuLiWingmanProtocolTests
{
	template <typename ContractType>
	bool Serialize(const ContractType& Source, TArray<uint8>& OutBytes)
	{
		OutBytes.Reset();
		FMemoryWriter Writer(OutBytes, true);
		ContractType Copy = Source;
		bool bSuccess = false;
		Copy.NetSerialize(Writer, nullptr, bSuccess);
		return bSuccess && !Writer.IsError();
	}

	template <typename ContractType>
	bool RoundTrip(const ContractType& Source, ContractType& OutCopy, TArray<uint8>& OutBytes)
	{
		if (!Serialize(Source, OutBytes))
		{
			return false;
		}
		FMemoryReader Reader(OutBytes, true);
		bool bSuccess = false;
		OutCopy.NetSerialize(Reader, nullptr, bSuccess);
		return bSuccess && !Reader.IsError() && Reader.AtEnd();
	}

	FGuLiWingmanGroupHandle MakeGroup()
	{
		FGuLiWingmanGroupHandle Group;
		Group.ShipInstanceId = FGuid(0x10203040u, 0x50607080u, 0x90a0b0c0u, 0xd0e0f001u);
		Group.ShipGeneration = 3u;
		Group.GroupGeneration = 9u;
		return Group;
	}

	FGuLiWingmanHandle MakeWingman(const FGuLiWingmanGroupHandle& Group, const uint8 Flight, const uint8 Member)
	{
		FGuLiWingmanHandle Handle;
		Handle.Flight.Group = Group;
		Handle.Flight.FlightIndex = Flight;
		Handle.MemberIndex = Member;
		Handle.EntityGeneration = 2u;
		return Handle;
	}

	FGuLiGroupAbilityConfigSnapshot MakeConfig(const FGuLiWingmanGroupHandle& Group)
	{
		FGuLiGroupAbilityConfigSnapshot Config;
		Config.ShipInstanceId = Group.ShipInstanceId;
		Config.MatchEpoch = 2u;
		Config.Team = EGuLiTeam::Red;
		Config.OwnerPlayerGuid = FGuid(9u, 8u, 7u, 6u);
		Config.WingmanTypeId = TEXT("TestWingman");
		Config.ShipGeneration = Group.ShipGeneration;
		Config.GroupGeneration = Group.GroupGeneration;
		Config.AbilitySetRevision = 11u;
		Config.LoadoutRevision = 12u;
		Config.SnapshotRevision = 1u;
		Config.bGroupAbilitiesValid = true;
		Config.FormationAbilityId = TAG_GuLi_ShipAbility_Formation_DoubleRing;
		Config.BasicWeaponAbilityId = TAG_GuLi_ShipAbility_Weapon_Basic_Auto;
		Config.MissileAbilityId = TAG_GuLi_ShipAbility_Weapon_Missile_Salvo;
		Config.FormationDefinitionRevision = 5u;
		Config.FormationDefinitionChecksum = 0x1122334455667788ull;
		Config.BasicWeaponDefinitionRevision = 6u;
		Config.BasicWeaponDefinitionChecksum = 0x2233445566778899ull;
		Config.MissileDefinitionRevision = 7u;
		Config.MissileDefinitionChecksum = 0x33445566778899aaull;
		Config.FormationCommandRevision = 13u;
		Config.EffectiveClientSimTick = 101u;
		FGuLiWingmanWeaponChannelConfig& Basic = Config.WeaponChannels.AddDefaulted_GetRef();
		Basic.Binding = FGuLiWeaponBindingKey::Wingman(
			Config.MatchEpoch, Config.Team, Config.OwnerPlayerGuid, Config.WingmanTypeId, TEXT("BasicWeapon"));
		Basic.SkillId = TEXT("Test.Basic.Auto");
		Basic.AbilityId = Config.BasicWeaponAbilityId;
		Basic.Kind = EGuLiWingmanWeaponKind::BasicAutomatic;
		Basic.bEnabled = true;
		Basic.ProfileRevision = 9u;
		Basic.DefinitionRevision = Config.BasicWeaponDefinitionRevision;
		Basic.DefinitionChecksum = Config.BasicWeaponDefinitionChecksum;
		Config.BasicWeaponRuntime = Basic.Runtime;
		Config.RefreshHash();
		return Config;
	}

	FGuLiWingmanCandidateBatch MakeCandidate(const FGuLiWingmanGroupHandle& Group)
	{
		FGuLiWingmanCandidateBatch Candidate;
		Candidate.MatchEpoch = 2u;
		Candidate.ConnectionGeneration = 7u;
		Candidate.Group = Group;
		Candidate.LeaseEpoch = 4u;
		Candidate.RosterRevision = 3u;
		Candidate.FlightIndex = 1u;
		Candidate.RequiredMemberMask = static_cast<uint8>(1u << 2u);
		Candidate.RequestedRateClass = EGuLiWingmanUploadRateClass::HighRate10Hz;
		Candidate.ObservedGrantRevision = 9u;
		Candidate.CandidateSequence = 17u;
		Candidate.FrameSequence = 23u;
		Candidate.BaseAcceptedSequence = 5u;
		Candidate.ClientSimTick = 101u;
		Candidate.CaptureEstimatedServerTimeSeconds = 123.25;
		Candidate.NavSchemaRevision = 2u;
		Candidate.NavDataChecksum = 0x8877665544332211ull;
		Candidate.TuningRevision = 4u;
		Candidate.ObstacleRevision = 6u;
		Candidate.CarrierSource.CanonicalEpoch = 8u;
		Candidate.CarrierSource.MoveRevision = 27u;
		Candidate.AbilitySetRevision = 11u;
		Candidate.FormationCommandRevision = 13u;
		Candidate.FormationDefinitionChecksum = 0x1122334455667788ull;
		FGuLiWingmanCandidateSample& Sample = Candidate.Samples.AddDefaulted_GetRef();
		Sample.Wingman = MakeWingman(Group, 1u, 2u);
		Sample.PositionCentimeters = FIntVector(100, -200, 300);
		Sample.VelocityCentimetersPerSecond = FIntVector(4500, 0, -25);
		Sample.RotationCentiDegrees = FIntVector(100, 9000, -300);
		Sample.FlightMode = 1u;
		FGuLiWingmanCandidateTrailSample& Trail = Candidate.TrailSamples.AddDefaulted_GetRef();
		Trail.ClientSimTick = 95u;
		Trail.CaptureEstimatedServerTimeSeconds = 123.05;
		Trail.CarrierSource.CanonicalEpoch = 8u;
		Trail.CarrierSource.MoveRevision = 26u;
		FGuLiWingmanCandidateSample& TrailMember = Trail.Samples.AddDefaulted_GetRef();
		TrailMember = Sample;
		TrailMember.PositionCentimeters = FIntVector(90, -190, 290);
		TrailMember.VelocityCentimetersPerSecond = FIntVector(4490, 10, -20);
		return Candidate;
	}

	FGuLiWingmanFireIntent MakeFireIntent(const FGuLiWingmanGroupHandle& Group)
	{
		FGuLiWingmanFireIntent Intent;
		Intent.MatchEpoch = 2u;
		Intent.Group = Group;
		Intent.LeaseEpoch = 4u;
		Intent.DomainFireSequence = 19u;
		Intent.Emitter = MakeWingman(Group, 1u, 2u);
		Intent.SourceAcceptedState.MatchEpoch = 2u;
		Intent.SourceAcceptedState.GroupGeneration = Group.GroupGeneration;
		Intent.SourceAcceptedState.AcceptedSequence = 17u;
		Intent.SourceAcceptedState.ClientSimTick = 101u;
		Intent.ClientFireTick = 103u;
		Intent.Target.Kind = EGuLiTargetKind::CommanderSoldier;
		Intent.Target.AuthorityId = FGuid(1u, 2u, 3u, 4u);
		Intent.Target.Generation = 5u;
		Intent.Target.LocalId = 6u;
		Intent.TargetAssignmentRevision = 7u;
		Intent.Binding = FGuLiWeaponBindingKey::Wingman(
			2u, EGuLiTeam::Red, FGuid(9u, 8u, 7u, 6u), TEXT("TestWingman"), TEXT("BasicWeapon"));
		Intent.WeaponAbilityId = TAG_GuLi_ShipAbility_Weapon_Basic_Auto;
		Intent.SkillId = TEXT("Test.Basic.Auto");
		Intent.LoadoutRevision = 12u;
		Intent.ProfileRevision = 9u;
		Intent.WeaponDefinitionRevision = 6u;
		Intent.AbilitySetRevision = 11u;
		Intent.AimDirectionMilli = FIntVector(1000, 0, 0);
		Intent.bClientPredictedLineOfSight = true;
		return Intent;
	}

	FGuLiGroupAbilityConfigSnapshot MakeV9Config(const FGuLiWingmanGroupHandle& Group)
	{
		FGuLiGroupAbilityConfigSnapshot Config;
		Config.ShipInstanceId = Group.ShipInstanceId;
		Config.MatchEpoch = 2u;
		Config.Team = EGuLiTeam::Red;
		Config.OwnerPlayerGuid = FGuid(0x11111111u, 0x22222222u, 0x33333333u, 0x44444444u);
		Config.WingmanTypeId = TEXT("TestWingman");
		Config.ShipGeneration = Group.ShipGeneration;
		Config.GroupGeneration = Group.GroupGeneration;
		Config.AbilitySetRevision = 11u;
		Config.LoadoutRevision = 12u;
		Config.SnapshotRevision = 13u;
		Config.bGroupAbilitiesValid = true;
		Config.FormationAbilityId = TAG_GuLi_ShipAbility_Formation_DoubleRing;
		Config.FormationDefinitionRevision = 5u;
		Config.FormationDefinitionChecksum = 0x1122334455667788ull;
		Config.FormationCommandRevision = 13u;
		Config.EffectiveClientSimTick = 101u;

		FGuLiWingmanWeaponChannelConfig& Primary = Config.WeaponChannels.AddDefaulted_GetRef();
		Primary.Binding = FGuLiWeaponBindingKey::Wingman(
			Config.MatchEpoch, Config.Team, Config.OwnerPlayerGuid, Config.WingmanTypeId, TEXT("PrimaryWeapon"));
		Primary.SkillId = TEXT("Test.Primary.Auto");
		Primary.AbilityId = TAG_GuLi_ShipAbility_Weapon_Basic_Auto;
		Primary.Kind = EGuLiWingmanWeaponKind::BasicAutomatic;
		Primary.bEnabled = true;
		Primary.ProfileRevision = 9u;
		Primary.DefinitionRevision = 6u;
		Primary.DefinitionChecksum = 0x2233445566778899ull;

		const FGuLiWingmanWeaponChannelConfig PrimaryCopy = Primary;
		FGuLiWingmanWeaponChannelConfig& Secondary = Config.WeaponChannels.AddDefaulted_GetRef();
		Secondary = PrimaryCopy;
		Secondary.Binding.SlotId = TEXT("SecondaryWeapon");
		Secondary.SkillId = TEXT("Test.Secondary.Auto");
		Secondary.AbilityId = TAG_GuLi_ShipWingman_Weapon_Basic;
		Secondary.ProfileRevision = 10u;
		Secondary.DefinitionRevision = 7u;
		Secondary.DefinitionChecksum = 0x33445566778899aaull;

		Config.BasicWeaponAbilityId = Config.WeaponChannels[0].AbilityId;
		Config.BasicWeaponDefinitionRevision = Config.WeaponChannels[0].DefinitionRevision;
		Config.BasicWeaponDefinitionChecksum = Config.WeaponChannels[0].DefinitionChecksum;
		Config.BasicWeaponRuntime = Config.WeaponChannels[0].Runtime;
		Config.RefreshHash();
		return Config;
	}

	FGuLiWingmanFireIntent MakeV9FireIntent(
		const FGuLiWingmanGroupHandle& Group,
		const FGuLiGroupAbilityConfigSnapshot& Config,
		const FGuLiWingmanWeaponChannelConfig& Channel,
		const uint32 Sequence)
	{
		FGuLiWingmanFireIntent Intent;
		Intent.MatchEpoch = Config.MatchEpoch;
		Intent.Group = Group;
		Intent.LeaseEpoch = 4u;
		Intent.DomainFireSequence = Sequence;
		Intent.Emitter = MakeWingman(Group, 1u, 2u);
		Intent.Binding = Channel.Binding;
		Intent.SourceAcceptedState.MatchEpoch = Config.MatchEpoch;
		Intent.SourceAcceptedState.GroupGeneration = Group.GroupGeneration;
		Intent.SourceAcceptedState.AcceptedSequence = 17u;
		Intent.SourceAcceptedState.ClientSimTick = 101u;
		Intent.ClientFireTick = 103u;
		Intent.Target.Kind = EGuLiTargetKind::CommanderSoldier;
		Intent.Target.AuthorityId = FGuid(1u, 2u, 3u, 4u);
		Intent.Target.Generation = 5u;
		Intent.Target.LocalId = 6u;
		Intent.TargetAssignmentRevision = 7u;
		Intent.WeaponAbilityId = Channel.AbilityId;
		Intent.SkillId = Channel.SkillId;
		Intent.LoadoutRevision = Config.LoadoutRevision;
		Intent.ProfileRevision = Channel.ProfileRevision;
		Intent.WeaponDefinitionRevision = Channel.DefinitionRevision;
		Intent.AbilitySetRevision = Config.AbilitySetRevision;
		Intent.AimDirectionMilli = FIntVector(1000, 0, 0);
		Intent.bClientPredictedLineOfSight = true;
		return Intent;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiWingmanProtocolV7GoldenBytesTest,
	"GuLiStrike.Wingman.Network.ProtocolV7.GoldenBytes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiWingmanProtocolV7GoldenBytesTest::RunTest(const FString& Parameters)
{
	using namespace GuLiWingmanProtocolTests;
	const FGuLiWingmanGroupHandle Group = MakeGroup();
	const FGuLiWingmanCandidateBatch Candidate = MakeCandidate(Group);
	const FGuLiWingmanFireIntent Intent = MakeFireIntent(Group);

	FGuLiWingmanCandidateBatch CandidateCopy;
	TArray<uint8> CandidateBytes;
	TestTrue(TEXT("Candidate protocol-v7 round-trip succeeds"), RoundTrip(Candidate, CandidateCopy, CandidateBytes));
	TArray<uint8> CandidateBytesAgain;
	TestTrue(TEXT("Candidate serializes a second time"), Serialize(Candidate, CandidateBytesAgain));
	TestTrue(TEXT("Candidate wire bytes are deterministic"), CandidateBytes == CandidateBytesAgain);
	TestTrue(TEXT("Candidate identity survives the wire"), CandidateCopy.Group == Candidate.Group);
	TestEqual(TEXT("Candidate ability revision survives the wire"), CandidateCopy.AbilitySetRevision, 11u);
	TestEqual(TEXT("Candidate Flight survives the wire"), CandidateCopy.FlightIndex, 1u);
	TestEqual(TEXT("Candidate Grant observation survives the wire"), CandidateCopy.ObservedGrantRevision, 9u);
	TestEqual(TEXT("Candidate Trail survives the wire"), CandidateCopy.TrailSamples.Num(), 1);
	TestTrue(TEXT("Candidate carries the actual emitter identity"), CandidateCopy.Samples[0].Wingman == Candidate.Samples[0].Wingman);

	FGuLiWingmanFireIntent IntentCopy;
	TArray<uint8> FireBytes;
	TestTrue(TEXT("FireIntent protocol-v7 round-trip succeeds"), RoundTrip(Intent, IntentCopy, FireBytes));
	TArray<uint8> FireBytesAgain;
	TestTrue(TEXT("FireIntent serializes a second time"), Serialize(Intent, FireBytesAgain));
	TestTrue(TEXT("FireIntent wire bytes are deterministic"), FireBytes == FireBytesAgain);
	TestTrue(TEXT("FireIntent stable AbilityId survives the wire"), IntentCopy.WeaponAbilityId == Intent.WeaponAbilityId);
	TestTrue(TEXT("FireIntent actual emitter survives the wire"), IntentCopy.Emitter == Intent.Emitter);
	TestEqual(TEXT("FireIntent client fixed-step survives the wire"),
		IntentCopy.ClientFireTick,
		Intent.ClientFireTick);
	TestEqual(TEXT("FireIntent assignment revision survives the wire"),
		IntentCopy.TargetAssignmentRevision,
		Intent.TargetAssignmentRevision);
	TestEqual(TEXT("FireIntent client LOS prediction survives the wire"),
		IntentCopy.bClientPredictedLineOfSight,
		Intent.bClientPredictedLineOfSight);

	const FString CandidateHex = BytesToHex(CandidateBytes.GetData(), CandidateBytes.Num());
	const FString FireIntentHex = BytesToHex(FireBytes.GetData(), FireBytes.Num());
	const FString ExpectedCandidateHex = TEXT(
		"1A040E4030201080706050C0B0A09001F0E0D00612080601040112222E0ACA0000000000D05E40041122334455667788080C1036161A8877665544332211010000004030201080706050C0B0A09001F0E0D006120102046400000038FFFFFF2C0100009411000000000000E7FFFFFF6400000028230000D4FEFFFF0101000000BE3333333333C35E401034010000004030201080706050C0B0A09001F0E0D006120102045A00000042FFFFFF220100008A1100000A000000ECFFFFFF6400000028230000D4FEFFFF0100000000");
	const FString ExpectedFireIntentHex = TEXT(
		"1A044030201080706050C0B0A09001F0E0D0061208264030201080706050C0B0A09001F0E0D00612010204040109000000080000000700000006000000010C0000005465737457696E676D616E000C0000004261736963576561706F6E00041222CACE02010000000200000003000000040000000A0C0E1F000000536869702E4162696C6974792E576561706F6E2E42617369632E4175746F0010000000546573742E42617369632E4175746F0018120C16E8030000000000000000000001");
	TestEqual(TEXT("Candidate protocol-v7 golden bytes stay frozen"), CandidateHex, ExpectedCandidateHex);
	TestEqual(TEXT("FireIntent protocol-v7 golden bytes stay frozen"), FireIntentHex, ExpectedFireIntentHex);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiWingmanAttackStateV13WireTest,
	"GuLiStrike.Wingman.Network.ProtocolV7.AttackStateV13WireAndBounds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiWingmanAttackStateV13WireTest::RunTest(const FString& Parameters)
{
	using namespace GuLiWingmanProtocolTests;
	const FGuLiWingmanGroupHandle Group = MakeGroup();
	FGuLiWingmanAttackAuthorityState State;
	State.Revision = 9u;
	for (int32 Index = 0; Index < 2; ++Index)
	{
		FGuLiWingmanAutoTargetAssignment& Assignment = State.AutomaticTargets.AddDefaulted_GetRef();
		Assignment.Emitter = MakeWingman(Group, 0u, static_cast<uint8>(Index));
		Assignment.Target.Target.Kind = Index == 0
			? EGuLiTargetKind::Ship : EGuLiTargetKind::CommanderSoldier;
		Assignment.Target.Target.AuthorityId = FGuid(50u, 60u, 70u, static_cast<uint32>(Index + 1));
		Assignment.Target.Target.Generation = 2u;
		Assignment.Target.Target.LocalId = static_cast<uint32>(Index + 1);
		Assignment.Target.Location = FVector(1000.0 * Index, 2000.0, 3000.0);
		Assignment.Target.Radius = 125.0f;
		Assignment.Target.bGround = Index != 0;
		Assignment.Target.Revision = static_cast<uint32>(20 + Index);
		Assignment.Target.ServerTime = 123.5;
	}
	TestTrue(TEXT("A sorted two-member automatic state is valid"), State.IsWellFormed(Group));
	FGuLiWingmanAttackAuthorityState WireCopy;
	TArray<uint8> Bytes;
	TestTrue(TEXT("Protocol-v13 AttackState round-trip succeeds"), RoundTrip(State, WireCopy, Bytes));
	TestEqual(TEXT("AttackState automatic table survives the wire"), WireCopy.AutomaticTargets.Num(), 2);
	TestTrue(TEXT("AttackState member and target identities survive the wire"),
		WireCopy.AutomaticTargets[0].Emitter == State.AutomaticTargets[0].Emitter
		&& WireCopy.AutomaticTargets[1].Target.Target == State.AutomaticTargets[1].Target.Target);
	TestEqual(TEXT("AttackState stable hash survives the wire"),
		WireCopy.ComputeStableHash(), State.ComputeStableHash());

	FGuLiWingmanAttackAuthorityState AtLimit;
	AtLimit.Revision = 1u;
	for (int32 Index = 0; Index < GULI_WINGMAN_GROUP_SIZE; ++Index)
	{
		FGuLiWingmanAutoTargetAssignment Assignment = State.AutomaticTargets[0];
		Assignment.Emitter = MakeWingman(Group,
			static_cast<uint8>(Index / GULI_WINGMAN_MEMBERS_PER_FLIGHT),
			static_cast<uint8>(Index % GULI_WINGMAN_MEMBERS_PER_FLIGHT));
		Assignment.Target.Target.AuthorityId.D = static_cast<uint32>(Index + 1);
		Assignment.Target.Target.LocalId = static_cast<uint32>(Index + 1);
		AtLimit.AutomaticTargets.Add(MoveTemp(Assignment));
	}
	TestTrue(TEXT("Exactly 25 automatic assignments are valid"), AtLimit.IsWellFormed(Group));
	FGuLiWingmanAttackAuthorityState AtLimitCopy;
	TArray<uint8> AtLimitBytes;
	TestTrue(TEXT("Exactly 25 automatic assignments survive the bounded wire"),
		RoundTrip(AtLimit, AtLimitCopy, AtLimitBytes));
	const FGuLiWingmanAutoTargetAssignment OverflowAssignment = AtLimit.AutomaticTargets.Last();
	AtLimit.AutomaticTargets.Add(OverflowAssignment);
	TestFalse(TEXT("A 26th automatic assignment is rejected"), AtLimit.IsWellFormed(Group));
	TestFalse(TEXT("A 26th automatic assignment is rejected by the wire serializer"),
		Serialize(AtLimit, AtLimitBytes));

	FGuLiWingmanAttackAuthorityState Duplicate = State;
	Duplicate.AutomaticTargets[1].Emitter = Duplicate.AutomaticTargets[0].Emitter;
	TestFalse(TEXT("Duplicate automatic member identity is rejected"), Duplicate.IsWellFormed(Group));
	FGuLiWingmanAttackAuthorityState CrossGroup = State;
	++CrossGroup.AutomaticTargets[1].Emitter.Flight.Group.GroupGeneration;
	TestFalse(TEXT("Automatic entries from another group are rejected"), CrossGroup.IsWellFormed(Group));
	FGuLiWingmanAttackAuthorityState WrongClassification = State;
	WrongClassification.AutomaticTargets[0].Target.bGround = true;
	TestFalse(TEXT("An air target cannot claim the ground execution channel"),
		WrongClassification.IsWellFormed(Group));
	FGuLiWingmanAttackAuthorityState MixedMode = State;
	MixedMode.Target = State.AutomaticTargets[0].Target;
	MixedMode.Target.bSpecified = true;
	TestFalse(TEXT("Manual and automatic target modes cannot coexist"), MixedMode.IsWellFormed(Group));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiWingmanEmergencyRebaseV13WireTest,
	"GuLiStrike.Wingman.Network.ProtocolV7.EmergencyRebaseV13Wire",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiWingmanEmergencyRebaseV13WireTest::RunTest(const FString& Parameters)
{
	using namespace GuLiWingmanProtocolTests;
	const FGuLiWingmanGroupHandle Group = MakeGroup();
	FGuLiWingmanEmergencyRebaseRequest Request;
	Request.MatchEpoch = 2u;
	Request.ConnectionGeneration = 7u;
	Request.Wingman = MakeWingman(Group, 3u, 4u);
	Request.LeaseEpoch = 5u;
	Request.RosterRevision = 9u;
	Request.RequestSequence = 11u;
	Request.BaselineAcceptedSequence = 13u;
	Request.Reason = EGuLiWingmanEmergencyRebaseReason::PhysicalObstacleDeadlock;
	FGuLiWingmanEmergencyRebaseRequest RequestCopy;
	TArray<uint8> RequestBytes;
	TestTrue(TEXT("The v13 emergency request round-trips"),
		RoundTrip(Request, RequestCopy, RequestBytes));
	TestTrue(TEXT("The request keeps the exact member generation"),
		RequestCopy.Wingman == Request.Wingman);
	TestEqual(TEXT("The request keeps its Accepted baseline"),
		RequestCopy.BaselineAcceptedSequence, Request.BaselineAcceptedSequence);
	TestEqual(TEXT("The request keeps its diagnostic reason"),
		RequestCopy.Reason, Request.Reason);

	FGuLiWingmanEmergencyRebaseResponse Response;
	Response.Wingman = Request.Wingman;
	Response.LeaseEpoch = Request.LeaseEpoch;
	Response.RequestSequence = Request.RequestSequence;
	Response.Result = EGuLiWingmanEmergencyRebaseResult::Accepted;
	Response.AcceptedSequence = 14u;
	Response.ServerPosition = FVector(12345.0, -6789.0, 4321.0);
	FGuLiWingmanEmergencyRebaseResponse ResponseCopy;
	TArray<uint8> ResponseBytes;
	TestTrue(TEXT("The accepted v13 emergency response round-trips"),
		RoundTrip(Response, ResponseCopy, ResponseBytes));
	TestEqual(TEXT("The response keeps the authority Accepted sequence"),
		ResponseCopy.AcceptedSequence, Response.AcceptedSequence);
	TestEqual(TEXT("The response keeps the authority-selected location"),
		ResponseCopy.ServerPosition, Response.ServerPosition);

	FGuLiWingmanEmergencyRebaseResponse Rejected = Response;
	Rejected.Result = EGuLiWingmanEmergencyRebaseResult::NoSafePoint;
	Rejected.AcceptedSequence = 0u;
	Rejected.ServerPosition = FVector::ZeroVector;
	Rejected.RetryAfterServerTimeSeconds = 44.5;
	TestTrue(TEXT("A typed no-safe-point response is well formed"), Rejected.IsWellFormed());
	FGuLiWingmanEmergencyRebaseResponse RejectedCopy;
	TArray<uint8> RejectedBytes;
	TestTrue(TEXT("The rejected v13 emergency response round-trips"),
		RoundTrip(Rejected, RejectedCopy, RejectedBytes));
	TestEqual(TEXT("The retry boundary survives the wire"),
		RejectedCopy.RetryAfterServerTimeSeconds, Rejected.RetryAfterServerTimeSeconds);

	FGuLiWingmanEmergencyRebaseRequest OldProtocol = Request;
	OldProtocol.ProtocolVersion = GULI_WINGMAN_PROTOCOL_VERSION - 1u;
	TestFalse(TEXT("A v12 emergency request cannot enter a v13 session"),
		Serialize(OldProtocol, RequestBytes));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiWingmanWirePositionQuantizationTest,
	"GuLiStrike.Wingman.Protocol.WirePositionQuantization",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiWingmanWirePositionQuantizationTest::RunTest(const FString& Parameters)
{
	const FVector Source(10.49, -20.49, 30.51);
	const FIntVector Quantized = GuLiWingmanProtocol::QuantizePositionCentimeters(Source);
	TestEqual(TEXT("Wire position rounds X to the nearest centimeter"), Quantized.X, 10);
	TestEqual(TEXT("Wire position rounds negative Y to the nearest centimeter"), Quantized.Y, -20);
	TestEqual(TEXT("Wire position rounds Z to the nearest centimeter"), Quantized.Z, 31);
	TestEqual(TEXT("Expanded wire position is the exact segment endpoint used by authority"),
		GuLiWingmanProtocol::ExpandPositionCentimeters(Quantized), FVector(10.0, -20.0, 31.0));

	const double BeyondInt32 = static_cast<double>(MAX_int32) + 4096.0;
	const FIntVector Clamped = GuLiWingmanProtocol::QuantizePositionCentimeters(
		FVector(BeyondInt32, -BeyondInt32, 0.0));
	TestEqual(TEXT("Positive wire position overflow clamps closed"), Clamped.X, MAX_int32);
	TestEqual(TEXT("Negative wire position overflow clamps closed"), Clamped.Y, MIN_int32);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiWingmanDefaultGroupTransportSentinelTest,
	"GuLiStrike.Wingman.Network.ProtocolV7.DefaultGroupTransportSentinel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiWingmanDefaultGroupTransportSentinelTest::RunTest(const FString& Parameters)
{
	using namespace GuLiWingmanProtocolTests;
	const FGuLiWingmanGroupHandle DefaultSentinel;
	FGuLiWingmanGroupHandle WireCopy;
	TArray<uint8> Bytes;
	TestTrue(TEXT("The optional all-zero Group sentinel crosses the transport"),
		RoundTrip(DefaultSentinel, WireCopy, Bytes));
	TestFalse(TEXT("Transport success never promotes the sentinel into a valid identity"),
		WireCopy.IsValid());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiWingmanAbilityRevisionGateTest,
	"GuLiStrike.Wingman.Network.ProtocolV7.AbilityRevisionGate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiWingmanAbilityRevisionGateTest::RunTest(const FString& Parameters)
{
	using namespace GuLiWingmanProtocolTests;
	const FGuLiWingmanGroupHandle Group = MakeGroup();
	const FGuLiGroupAbilityConfigSnapshot Config = MakeConfig(Group);
	FGuLiWingmanCandidateBatch Candidate = MakeCandidate(Group);
	FGuLiWingmanFireIntent Intent = MakeFireIntent(Group);

	TestTrue(TEXT("A current candidate passes the ability gate"),
		GuLiWingmanProtocol::ValidateCandidateAbilityConfig(Candidate, &Config) == EGuLiWingmanRejectReason::None);
	Candidate.AbilitySetRevision--;
	TestTrue(TEXT("An old candidate is rejected before state reservation"),
		GuLiWingmanProtocol::ValidateCandidateAbilityConfig(Candidate, &Config)
			== EGuLiWingmanRejectReason::StaleAbilitySetRevision);
	Candidate = MakeCandidate(Group);
	Candidate.FormationCommandRevision--;
	TestTrue(TEXT("An old formation command cannot advance a candidate"),
		GuLiWingmanProtocol::ValidateCandidateAbilityConfig(Candidate, &Config)
			== EGuLiWingmanRejectReason::StaleFormationCommand);

	TestTrue(TEXT("A current FireIntent passes the ability gate"),
		GuLiWingmanProtocol::ValidateFireIntentAbilityConfig(Intent, &Config) == EGuLiWingmanRejectReason::None);
	Intent.WeaponDefinitionRevision--;
	TestTrue(TEXT("An old weapon definition cannot reserve a fire sequence"),
		GuLiWingmanProtocol::ValidateFireIntentAbilityConfig(Intent, &Config)
			== EGuLiWingmanRejectReason::WeaponDefinitionMismatch);
	Intent = MakeFireIntent(Group);
	Intent.WeaponAbilityId = TAG_GuLi_ShipAbility_Formation_DoubleRing;
	TestTrue(TEXT("A non-weapon ability cannot authorize an emitter"),
		GuLiWingmanProtocol::ValidateFireIntentAbilityConfig(Intent, &Config)
			== EGuLiWingmanRejectReason::UnknownWeaponAbility);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiWingmanSixScopeBootstrapTest,
	"GuLiStrike.Wingman.Network.ProtocolV7.SixScopeBootstrap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiWingmanSixScopeBootstrapTest::RunTest(const FString& Parameters)
{
	using namespace GuLiWingmanProtocolTests;
	FGuLiWingmanBootstrapCommit Commit;
	Commit.CutId = 44u;
	Commit.Group = MakeGroup();
	for (uint8 Scope = 0u; Scope < static_cast<uint8>(EGuLiWingmanBootstrapScope::Count); ++Scope)
	{
		FGuLiWingmanBootstrapScopeState& State = Commit.Scopes.AddDefaulted_GetRef();
		State.Scope = static_cast<EGuLiWingmanBootstrapScope>(Scope);
		State.Revision = 100u + Scope;
		State.Hash = 200u + Scope;
		State.ChunkCount = 1u;
	}
	TestTrue(TEXT("All six scopes form one valid atomic cut"), Commit.IsWellFormed());
	Commit.Scopes.Pop(EAllowShrinking::No);
	TestFalse(TEXT("A cut missing GroupAbilityConfig is never active"), Commit.IsWellFormed());
	Commit = FGuLiWingmanBootstrapCommit();
	Commit.CutId = 45u;
	Commit.Group = MakeGroup();
	for (uint8 Scope = 0u; Scope < static_cast<uint8>(EGuLiWingmanBootstrapScope::Count); ++Scope)
	{
		FGuLiWingmanBootstrapScopeState& State = Commit.Scopes.AddDefaulted_GetRef();
		State.Scope = Scope == 5u ? EGuLiWingmanBootstrapScope::AcceptedSnapshot
			: static_cast<EGuLiWingmanBootstrapScope>(Scope);
		State.Revision = 1u;
		State.Hash = 1u;
		State.ChunkCount = 1u;
	}
	TestFalse(TEXT("A duplicate scope cannot substitute for GroupAbilityConfig"), Commit.IsWellFormed());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiWingmanMultiWeaponChannelWireTest,
	"GuLiStrike.Wingman.Protocol.MultiWeaponChannelWire",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiWingmanMultiWeaponChannelWireTest::RunTest(const FString& Parameters)
{
	using namespace GuLiWingmanProtocolTests;
	const FGuLiWingmanGroupHandle Group = MakeGroup();
	const FGuLiGroupAbilityConfigSnapshot Config = MakeV9Config(Group);
	if (!TestTrue(TEXT("Protocol-v9 multi-channel config is valid"), Config.IsWellFormed())
		|| !TestEqual(TEXT("Fixture carries two independent bindings"), Config.WeaponChannels.Num(), 2))
	{
		return false;
	}

	const FGuLiWingmanWeaponChannelConfig& Primary = Config.WeaponChannels[0];
	const FGuLiWingmanWeaponChannelConfig& Secondary = Config.WeaponChannels[1];
	const FGuLiWingmanFireIntent PrimaryIntent = MakeV9FireIntent(Group, Config, Primary, 19u);
	FGuLiWingmanFireIntent WireCopy;
	TArray<uint8> WireBytes;
	if (!TestTrue(TEXT("Protocol-v9 FireIntent round-trip succeeds"),
		RoundTrip(PrimaryIntent, WireCopy, WireBytes)))
	{
		return false;
	}
	TArray<uint8> WireBytesAgain;
	TestTrue(TEXT("Protocol-v9 FireIntent serializes deterministically"),
		Serialize(PrimaryIntent, WireBytesAgain) && WireBytes == WireBytesAgain);
	TestTrue(TEXT("Wire preserves the complete weapon binding"), WireCopy.Binding == Primary.Binding);
	TestEqual(TEXT("Wire preserves SkillId"), WireCopy.SkillId, Primary.SkillId);
	TestEqual(TEXT("Wire preserves LoadoutRevision"), WireCopy.LoadoutRevision, Config.LoadoutRevision);
	TestEqual(TEXT("Wire preserves ProfileRevision"), WireCopy.ProfileRevision, Primary.ProfileRevision);
	TestTrue(TEXT("A current binding-exact FireIntent passes protocol validation"),
		GuLiWingmanProtocol::ValidateFireIntentAbilityConfig(WireCopy, &Config)
			== EGuLiWingmanRejectReason::None);

	FGuLiWingmanFireIntent Rejected = PrimaryIntent;
	Rejected.ProtocolVersion = GULI_WINGMAN_PROTOCOL_VERSION - 1u;
	TestTrue(TEXT("An old wire protocol is rejected before current payload validation"),
		GuLiWingmanProtocol::ValidateFireIntentAbilityConfig(Rejected, &Config)
			== EGuLiWingmanRejectReason::ProtocolMismatch);

	Rejected = PrimaryIntent;
	--Rejected.LoadoutRevision;
	TestTrue(TEXT("An old loadout revision is rejected precisely"),
		GuLiWingmanProtocol::ValidateFireIntentAbilityConfig(Rejected, &Config)
			== EGuLiWingmanRejectReason::StaleLoadoutRevision);

	Rejected = PrimaryIntent;
	Rejected.Binding.SlotId = TEXT("UnknownWeapon");
	TestTrue(TEXT("An unknown binding cannot alias a configured AbilityId"),
		GuLiWingmanProtocol::ValidateFireIntentAbilityConfig(Rejected, &Config)
			== EGuLiWingmanRejectReason::UnknownWeaponChannel);

	Rejected = PrimaryIntent;
	Rejected.SkillId = TEXT("Test.Wrong.Skill");
	TestTrue(TEXT("A binding with the wrong skill identity is rejected precisely"),
		GuLiWingmanProtocol::ValidateFireIntentAbilityConfig(Rejected, &Config)
			== EGuLiWingmanRejectReason::WeaponSkillMismatch);

	Rejected = PrimaryIntent;
	++Rejected.ProfileRevision;
	TestTrue(TEXT("A stale or speculative profile revision is rejected precisely"),
		GuLiWingmanProtocol::ValidateFireIntentAbilityConfig(Rejected, &Config)
			== EGuLiWingmanRejectReason::StaleProfileRevision);

	const FGuLiWingmanFireIntent SecondaryIntent = MakeV9FireIntent(Group, Config, Secondary, 20u);
	FGuLiWingmanFireIntent SecondaryWireCopy;
	TArray<uint8> SecondaryBytes;
	TestTrue(TEXT("A second slot for the same member round-trips independently"),
		RoundTrip(SecondaryIntent, SecondaryWireCopy, SecondaryBytes));
	TestTrue(TEXT("The same member uses one monotonic DomainFireSequence across weapon slots"),
		SecondaryWireCopy.Emitter == WireCopy.Emitter
		&& SecondaryWireCopy.Binding != WireCopy.Binding
		&& SecondaryWireCopy.DomainFireSequence == WireCopy.DomainFireSequence + 1u);
	TestTrue(TEXT("The second configured slot also passes exact protocol validation"),
		GuLiWingmanProtocol::ValidateFireIntentAbilityConfig(SecondaryWireCopy, &Config)
			== EGuLiWingmanRejectReason::None);
	return true;
}
#endif
