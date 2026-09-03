// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Battle/Relay/GuLiWingmanRelayServer.h"

class AActor;
class UWorld;

namespace GuLiWingmanWorldValidation
{
	/** Aggregate, read-only failure-stage telemetry populated only by -GuLiListenSmoke. */
	struct FListenSmokeDiagnostics
	{
		uint64 InvocationCount = 0u;
		uint64 ContextRejectCount = 0u;
		uint64 InvalidRadiusRejectCount = 0u;
		uint64 MissingNavigationSubsystemRejectCount = 0u;
		uint64 StaticCollisionRejectCount = 0u;
		uint64 DynamicObstacleRejectCount = 0u;
		uint64 NavigationRejectCount = 0u;
		uint64 AcceptedCount = 0u;
		int32 LastNavigationStatus = INDEX_NONE;
		FString LastStaticHitActor;
		FString LastStaticHitComponent;
	};

	/** Only WorldDynamic objects carrying this exact Actor/Component tag block a Candidate. */
	GULISTRIKE_API FName GetDynamicObstacleTag();
	GULISTRIKE_API FListenSmokeDiagnostics GetListenSmokeDiagnostics();

	/**
	 * Builds the durable per-group validator installed by its Ship. The returned closure owns only weak
	 * UObject references and is safe to retain across PlayerController destruction and lease takeover.
	 */
	GULISTRIKE_API FGuLiCandidateWorldValidator MakeValidator(
		UWorld* World,
		AActor* CarrierActor);

#if WITH_DEV_AUTOMATION_TESTS
	/** Test-only seam for a prebuilt immutable FlightNav query. */
	using FFlightNavSegmentValidatorForTests = TFunction<bool(
		const FVector& Start, const FVector& End, float AgentRadiusCentimeters)>;

	/**
	 * Replaces FlightNav only for the supplied transient standalone automation
	 * World. PIE and authored Worlds cannot use this entry point.
	 */
	GULISTRIKE_API FGuLiCandidateWorldValidator MakeValidatorForTests(
		UWorld* TransientTestWorld,
		AActor* CarrierActor,
		FFlightNavSegmentValidatorForTests FlightNavOverride);
#endif
}
