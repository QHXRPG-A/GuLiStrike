// Copyright Epic Games, Inc. All Rights Reserved.

#include "Battle/Relay/GuLiWingmanWorldValidator.h"

#include "CollisionQueryParams.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GuLiFlightNavigationQuery.h"
#include "GuLiFlightNavigationSubsystem.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "UObject/Package.h"
#endif

namespace
{
	const FName DynamicObstacleTag(TEXT("GuLi.Wingman.DynamicObstacle"));
	GuLiWingmanWorldValidation::FListenSmokeDiagnostics ListenSmokeDiagnostics;

	bool HasExplicitDynamicObstacleTag(const FHitResult& Hit)
	{
		const UPrimitiveComponent* Component = Hit.GetComponent();
		const AActor* Actor = Hit.GetActor();
		return (Component && Component->ComponentHasTag(DynamicObstacleTag))
			|| (Actor && Actor->ActorHasTag(DynamicObstacleTag));
	}

	bool SweepObjectType(
		UWorld& World,
		const FVector& Start,
		const FVector& End,
		const float Radius,
		const ECollisionChannel ObjectType,
		const AActor* IgnoredActor,
		TArray<FHitResult>& OutHits)
	{
		FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(GuLiWingmanCandidateWorldGate), false);
		QueryParams.bFindInitialOverlaps = true;
		if (IgnoredActor)
		{
			QueryParams.AddIgnoredActor(IgnoredActor);
		}
		FCollisionObjectQueryParams ObjectQuery;
		ObjectQuery.AddObjectTypesToQuery(ObjectType);
		OutHits.Reset();
		return World.SweepMultiByObjectType(
			OutHits,
			Start,
			End,
			FQuat::Identity,
			ObjectQuery,
			FCollisionShape::MakeSphere(Radius),
			QueryParams);
	}

#if WITH_DEV_AUTOMATION_TESTS
	bool IsTransientAutomationTestWorld(const UWorld* World)
	{
		if (!World || World->WorldType != EWorldType::Game
			|| World->GetNetMode() != NM_Standalone)
		{
			return false;
		}
		const UPackage* WorldPackage = World->GetOutermost();
		return World->HasAnyFlags(RF_Transient)
			// UWorld::CreateWorld gives automation Worlds a dedicated /Temp package;
			// they are not outered directly to the global TransientPackage.
			|| (WorldPackage && WorldPackage->GetName().StartsWith(TEXT("/Temp/")));
	}
#endif
}

FName GuLiWingmanWorldValidation::GetDynamicObstacleTag()
{
	return DynamicObstacleTag;
}

GuLiWingmanWorldValidation::FListenSmokeDiagnostics
GuLiWingmanWorldValidation::GetListenSmokeDiagnostics()
{
	return ListenSmokeDiagnostics;
}

static FGuLiCandidateWorldValidator MakeValidatorInternal(
	UWorld* World,
	AActor* CarrierActor
#if WITH_DEV_AUTOMATION_TESTS
	, GuLiWingmanWorldValidation::FFlightNavSegmentValidatorForTests TestFlightNavOverride
#endif
)
{
	const TWeakObjectPtr<UWorld> WeakWorld(World);
	const TWeakObjectPtr<AActor> WeakCarrier(CarrierActor);
	const bool bRequireCarrierActor = CarrierActor != nullptr;
	const bool bListenSmokeDiagnostics =
#if UE_BUILD_SHIPPING
		false;
#else
		FParse::Param(FCommandLine::Get(), TEXT("GuLiListenSmoke"));
#endif
	return [WeakWorld, WeakCarrier, bRequireCarrierActor, bListenSmokeDiagnostics
#if WITH_DEV_AUTOMATION_TESTS
		, TestFlightNavOverride = MoveTemp(TestFlightNavOverride)
#endif
	](
		const FGuLiWingmanCandidateWorldValidationContext& Context)
	{
		if (bListenSmokeDiagnostics)
		{
			++ListenSmokeDiagnostics.InvocationCount;
		}
		UWorld* ValidationWorld = WeakWorld.Get();
		if (!ValidationWorld || (bRequireCarrierActor && !WeakCarrier.IsValid())
			|| !Context.IsWellFormed() || !Context.ConfirmedAbilityConfig)
		{
			if (bListenSmokeDiagnostics)
			{
				++ListenSmokeDiagnostics.ContextRejectCount;
			}
			return EGuLiWingmanRejectReason::InvalidIdentity;
		}

		const float AgentRadius = Context.ConfirmedAbilityConfig->FormationRuntime.AgentRadiusCentimeters;
		if (!FMath::IsFinite(AgentRadius) || AgentRadius <= 0.0f)
		{
			if (bListenSmokeDiagnostics)
			{
				++ListenSmokeDiagnostics.InvalidRadiusRejectCount;
			}
			return EGuLiWingmanRejectReason::InvalidIdentity;
		}

#if WITH_DEV_AUTOMATION_TESTS
		const bool bUseTestFlightNav = TestFlightNavOverride
			&& IsTransientAutomationTestWorld(ValidationWorld);
#endif
		UGuLiFlightNavigationSubsystem* FlightNavigation =
#if WITH_DEV_AUTOMATION_TESTS
			bUseTestFlightNav ? nullptr :
#endif
			ValidationWorld->GetSubsystem<UGuLiFlightNavigationSubsystem>();
		if (
#if WITH_DEV_AUTOMATION_TESTS
			!bUseTestFlightNav &&
#endif
			!FlightNavigation)
		{
			if (bListenSmokeDiagnostics)
			{
				++ListenSmokeDiagnostics.MissingNavigationSubsystemRejectCount;
			}
			return EGuLiWingmanRejectReason::InvalidIdentity;
		}

		TArray<FHitResult> Hits;
		for (const FGuLiWingmanCandidateWorldSegment& Segment : Context.Segments)
		{
			if (SweepObjectType(
				*ValidationWorld,
				Segment.PreviousPosition,
				Segment.CurrentPosition,
				AgentRadius,
				ECC_WorldStatic,
				WeakCarrier.Get(),
				Hits))
			{
				if (bListenSmokeDiagnostics)
				{
					++ListenSmokeDiagnostics.StaticCollisionRejectCount;
					if (!Hits.IsEmpty())
					{
						ListenSmokeDiagnostics.LastStaticHitActor = GetNameSafe(Hits[0].GetActor());
						ListenSmokeDiagnostics.LastStaticHitComponent = GetNameSafe(Hits[0].GetComponent());
					}
				}
				return EGuLiWingmanRejectReason::InvalidIdentity;
			}

			if (SweepObjectType(
				*ValidationWorld,
				Segment.PreviousPosition,
				Segment.CurrentPosition,
				AgentRadius,
				ECC_WorldDynamic,
				WeakCarrier.Get(),
				Hits))
			{
				for (const FHitResult& Hit : Hits)
				{
					if (HasExplicitDynamicObstacleTag(Hit))
					{
						if (bListenSmokeDiagnostics)
						{
							++ListenSmokeDiagnostics.DynamicObstacleRejectCount;
						}
						return EGuLiWingmanRejectReason::InvalidIdentity;
					}
				}
			}

			EGuLiFlightNavSegmentStatus NavigationStatus = EGuLiFlightNavSegmentStatus::InvalidData;
			const bool bNavigationValid =
#if WITH_DEV_AUTOMATION_TESTS
				bUseTestFlightNav
					? TestFlightNavOverride(
						Segment.PreviousPosition, Segment.CurrentPosition, AgentRadius)
					:
#endif
					(NavigationStatus = FlightNavigation->ValidateAuthoritativeSegment(
					Segment.PreviousPosition, Segment.CurrentPosition, AgentRadius))
					== EGuLiFlightNavSegmentStatus::Valid;
			if (!bNavigationValid)
			{
				if (bListenSmokeDiagnostics)
				{
					++ListenSmokeDiagnostics.NavigationRejectCount;
					ListenSmokeDiagnostics.LastNavigationStatus =
#if WITH_DEV_AUTOMATION_TESTS
						bUseTestFlightNav ? INDEX_NONE :
#endif
						static_cast<int32>(NavigationStatus);
				}
				return EGuLiWingmanRejectReason::InvalidIdentity;
			}
		}
		if (bListenSmokeDiagnostics)
		{
			++ListenSmokeDiagnostics.AcceptedCount;
		}
		return EGuLiWingmanRejectReason::None;
	};
}

FGuLiCandidateWorldValidator GuLiWingmanWorldValidation::MakeValidator(
	UWorld* World,
	AActor* CarrierActor)
{
#if WITH_DEV_AUTOMATION_TESTS
	return MakeValidatorInternal(World, CarrierActor, FFlightNavSegmentValidatorForTests{});
#else
	return MakeValidatorInternal(World, CarrierActor);
#endif
}

#if WITH_DEV_AUTOMATION_TESTS
FGuLiCandidateWorldValidator GuLiWingmanWorldValidation::MakeValidatorForTests(
	UWorld* TransientTestWorld,
	AActor* CarrierActor,
	FFlightNavSegmentValidatorForTests FlightNavOverride)
{
	if (!IsTransientAutomationTestWorld(TransientTestWorld) || !FlightNavOverride)
	{
		ensureMsgf(false,
			TEXT("FlightNav validator overrides require a transient standalone automation Game World"));
		return FGuLiCandidateWorldValidator{};
	}
	return MakeValidatorInternal(TransientTestWorld, CarrierActor, MoveTemp(FlightNavOverride));
}
#endif
