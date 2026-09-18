// Copyright Epic Games, Inc. All Rights Reserved.

#include "Gameplay/Building/GuLiBuildingTypes.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

namespace GuLiBuildingPlacementPolicyTests
{
	FGuLiBuildingPlacementRequest MakeRequest(
		const uint32 RequestId,
		const EGuLiBuildingType Type = EGuLiBuildingType::MissileTurret,
		const FVector Location = FVector(100.0f, 200.0f, 300.0f),
		const float YawDegrees = 45.0f)
	{
		FGuLiBuildingPlacementRequest Request;
		Request.ClientRequestId = RequestId;
		Request.Type = Type;
		Request.DesiredGroundLocation = Location;
		Request.CompressedYaw = FRotator::CompressAxisToShort(YawDegrees);
		return Request;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBuildingPlacementPolicyTests,
	"GuLiStrike.Building.BuildingPlacementPolicyTests",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBuildingPlacementPolicyTests::RunTest(const FString& Parameters)
{
	using namespace GuLiBuildingPlacementPolicy;
	using namespace GuLiBuildingPlacementPolicyTests;

	TestTrue(TEXT("Commander may build"), IsBuildingRole(EGuLiCommanderRole::Commander));
	TestTrue(TEXT("Ground may build"), IsBuildingRole(EGuLiCommanderRole::Ground));
	TestFalse(TEXT("Air may not build"), IsBuildingRole(EGuLiCommanderRole::Air));
	TestFalse(TEXT("Observer may not build"), IsBuildingRole(EGuLiCommanderRole::Observer));
	TestFalse(TEXT("Unassigned may not build"), IsBuildingRole(EGuLiCommanderRole::Unassigned));

	TestTrue(TEXT("Missile turret is a known type"), IsKnownBuildingType(EGuLiBuildingType::MissileTurret));
	TestTrue(TEXT("Sentry turret is a known type"), IsKnownBuildingType(EGuLiBuildingType::SentryTurret));
	TestTrue(TEXT("Outpost is a known type"), IsKnownBuildingType(EGuLiBuildingType::Outpost));
	TestFalse(TEXT("Invalid is not a building type"), IsKnownBuildingType(EGuLiBuildingType::Invalid));

	const FNumberKeyDecision BuildOne = ResolveNumberKey(1, true, EGuLiCommanderRole::Commander);
	TestTrue(TEXT("Build-mode key 1 is consumed"), BuildOne.bHandled);
	TestEqual(TEXT("Build-mode key 1 selects missile turret"), BuildOne.SelectedType, EGuLiBuildingType::MissileTurret);
	const FNumberKeyDecision BuildThree = ResolveNumberKey(3, true, EGuLiCommanderRole::Ground);
	TestTrue(TEXT("Ground build-mode key 3 is consumed"), BuildThree.bHandled);
	TestEqual(TEXT("Build-mode key 3 selects outpost"), BuildThree.SelectedType, EGuLiBuildingType::Outpost);
	TestFalse(TEXT("Air cannot consume build-mode number keys"),
		ResolveNumberKey(2, true, EGuLiCommanderRole::Air).bHandled);
	const FNumberKeyDecision CommanderOne = ResolveNumberKey(1, false, EGuLiCommanderRole::Commander);
	TestFalse(TEXT("Commander key 1 outside build mode is not consumed as building selection"), CommanderOne.bHandled);
	TestTrue(TEXT("Commander key 1 outside build mode retains move-tool meaning"), CommanderOne.bArmCommanderMove);
	TestFalse(TEXT("Commander key 2 outside build mode has no building action"),
		ResolveNumberKey(2, false, EGuLiCommanderRole::Commander).bHandled);
	TestFalse(TEXT("Ground key 1 outside build mode does not arm Commander movement"),
		ResolveNumberKey(1, false, EGuLiCommanderRole::Ground).bArmCommanderMove);

	const float LimitRadians = FMath::DegreesToRadians(MaximumSlopeDegrees);
	const FVector LimitNormal(FMath::Sin(LimitRadians), 0.0f, FMath::Cos(LimitRadians));
	const float BeyondRadians = FMath::DegreesToRadians(MaximumSlopeDegrees + 0.1f);
	const FVector BeyondNormal(FMath::Sin(BeyondRadians), 0.0f, FMath::Cos(BeyondRadians));
	TestTrue(TEXT("A level surface is legal"), IsSlopeAllowed(FVector::UpVector));
	TestTrue(TEXT("The exact 15-degree slope boundary is legal"), IsSlopeAllowed(LimitNormal));
	TestFalse(TEXT("A slope beyond 15 degrees is illegal"), IsSlopeAllowed(BeyondNormal));
	TestFalse(TEXT("A zero normal is illegal"), IsSlopeAllowed(FVector::ZeroVector));

	TestTrue(TEXT("The exact 100-meter Ground range boundary is legal"),
		IsGroundPlacementInRange(FVector::ZeroVector, FVector(GroundMaximumRangeCentimeters, 0.0f, 0.0f)));
	TestFalse(TEXT("A Ground location beyond 100 meters is illegal"),
		IsGroundPlacementInRange(FVector::ZeroVector, FVector(GroundMaximumRangeCentimeters + 0.1f, 0.0f, 0.0f)));
	TestEqual(TEXT("Placement clearance is exactly 20 cm"), PlacementClearanceCentimeters, 20.0f);

	TestEqual(TEXT("Counts below both limits are accepted"), ValidateCapacity(5, 23),
		EGuLiBuildingPlacementRejectReason::None);
	TestEqual(TEXT("Six buildings reaches the per-builder limit"), ValidateCapacity(6, 6),
		EGuLiBuildingPlacementRejectReason::BuilderLimitReached);
	TestEqual(TEXT("Twenty-four buildings reaches the global limit"), ValidateCapacity(0, 24),
		EGuLiBuildingPlacementRejectReason::WorldLimitReached);
	TestEqual(TEXT("Global capacity rejection takes precedence"), ValidateCapacity(6, 24),
		EGuLiBuildingPlacementRejectReason::WorldLimitReached);
	TestEqual(TEXT("Request budget is exactly ten per second"), MaximumRequestsPerSecond, 10);

	const FGuLiBuildingPlacementRequest Request = MakeRequest(7u);
	TestTrue(TEXT("A finite known request is well formed"), Request.IsWellFormed());
	TestTrue(TEXT("An identical request is recognized for result replay"), AreSameRequest(Request, Request));
	FGuLiBuildingPlacementRequest ChangedRequest = Request;
	ChangedRequest.Type = EGuLiBuildingType::SentryTurret;
	TestFalse(TEXT("A reused ID with changed payload is not the same request"),
		AreSameRequest(Request, ChangedRequest));
	TestTrue(TEXT("A higher request ID is newer"), IsNewerRequestId(8u, 7u));
	TestFalse(TEXT("The same request ID is not newer"), IsNewerRequestId(7u, 7u));
	TestFalse(TEXT("An older request ID is not newer"), IsNewerRequestId(6u, 7u));
	TestTrue(TEXT("Request IDs advance correctly across uint32 wrap"), IsNewerRequestId(1u, MAX_uint32));
	FGuLiBuildingPlacementRequest ZeroIdRequest = Request;
	ZeroIdRequest.ClientRequestId = 0u;
	TestFalse(TEXT("Request ID zero is malformed"), ZeroIdRequest.IsWellFormed());
	FGuLiBuildingPlacementRequest InvalidTypeRequest = Request;
	InvalidTypeRequest.Type = EGuLiBuildingType::Invalid;
	TestFalse(TEXT("Unknown building type is malformed"), InvalidTypeRequest.IsWellFormed());

	for (uint8 ReasonValue = static_cast<uint8>(EGuLiBuildingPlacementRejectReason::None);
		ReasonValue <= static_cast<uint8>(EGuLiBuildingPlacementRejectReason::SpawnFailed);
		++ReasonValue)
	{
		const auto Reason = static_cast<EGuLiBuildingPlacementRejectReason>(ReasonValue);
		TestFalse(
			FString::Printf(TEXT("Reject/accept reason %u has visible feedback"), ReasonValue),
			GetGuLiBuildingPlacementReasonText(Reason).IsEmpty());
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
