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
		Config.ShipGeneration = Group.ShipGeneration;
		Config.GroupGeneration = Group.GroupGeneration;
		Config.AbilitySetRevision = 11u;
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
		Intent.WeaponAbilityId = TAG_GuLi_ShipAbility_Weapon_Basic_Auto;
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
	TestEqual(TEXT("FireIntent client LOS prediction survives the wire"),
		IntentCopy.bClientPredictedLineOfSight,
		Intent.bClientPredictedLineOfSight);

	const FString CandidateHex = BytesToHex(CandidateBytes.GetData(), CandidateBytes.Num());
	const FString FireIntentHex = BytesToHex(FireBytes.GetData(), FireBytes.Num());
	const FString ExpectedCandidateHex = TEXT(
		"0E040E4030201080706050C0B0A09001F0E0D00612080601040112222E0ACA0000000000D05E40041122334455667788080C1036161A8877665544332211010000004030201080706050C0B0A09001F0E0D006120102046400000038FFFFFF2C0100009411000000000000E7FFFFFF6400000028230000D4FEFFFF0101000000BE3333333333C35E401034010000004030201080706050C0B0A09001F0E0D006120102045A00000042FFFFFF220100008A1100000A000000ECFFFFFF6400000028230000D4FEFFFF01");
	const FString ExpectedFireIntentHex = TEXT(
		"0E044030201080706050C0B0A09001F0E0D0061208264030201080706050C0B0A09001F0E0D00612010204041222CACE02010000000200000003000000040000000A0C1F000000536869702E4162696C6974792E576561706F6E2E42617369632E4175746F000C16E8030000000000000000000001");
	TestEqual(TEXT("Candidate protocol-v7 golden bytes stay frozen"), CandidateHex, ExpectedCandidateHex);
	TestEqual(TEXT("FireIntent protocol-v7 golden bytes stay frozen"), FireIntentHex, ExpectedFireIntentHex);
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
