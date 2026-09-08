// Copyright Epic Games, Inc. All Rights Reserved.

#include "Gameplay/Wingman/GuLiWingmanSimulationSubsystem.h"

#include "Battle/Relay/GuLiWingmanRelayTypes.h"
#include "Gameplay/Wingman/Behavior/GuLiWingmanGroupBehaviorRunner.h"
#include "Gameplay/Wingman/Behavior/GuLiWingmanBehaviorStateTree.h"
#include "Development/GuLiWingmanQAEvidence.h"
#include "Gameplay/Wingman/Mass/GuLiWingmanMassFragments.h"
#include "Gameplay/Wingman/Mass/GuLiWingmanSwarmFlow.h"
#include "Engine/World.h"
#include "GuLiFlightNavigationSubsystem.h"
#include "MassCommonFragments.h"
#include "MassEntityManager.h"
#include "MassEntitySubsystem.h"
#include "StateTree.h"
#include "Subsystems/SubsystemCollection.h"

struct FGuLiWingmanFlightNavigationRuntime
{
	TSharedPtr<FGuLiFlightNavCancellationToken, ESPMode::ThreadSafe> CancellationToken;
	TSharedPtr<TFuture<FGuLiFlightNavPathResult>> PendingResult;
	TArray<FVector> PathPoints;
	FVector RequestedStart = FVector::ZeroVector;
	FVector RequestedGoal = FVector::ZeroVector;
	FVector ActivePathGoal = FVector::ZeroVector;
	double NextRequestSeconds = 0.0;
	uint32 RequestSerial = 0u;
	uint32 PendingRequestSerial = 0u;
	uint32 RequestedAbilitySetRevision = 0u;
	uint32 RequestedFormationCommandRevision = 0u;
	int32 NextPathPointIndex = INDEX_NONE;
	bool bRequestPending = false;
	bool bUsingSafeFallback = false;
};

namespace
{
	constexpr float NavigationEvaluationPeriodSeconds = 0.1f;
	constexpr double MinimumRepathIntervalSeconds = 0.5;
	constexpr double FailedPathRetrySeconds = 1.0;
	constexpr float MinimumGoalDriftForRepathCentimeters = 10000.0f;
	constexpr float MinimumWaypointReachDistanceCentimeters = 5000.0f;
	constexpr int32 MaximumNavigationExpandedNodes = 8192;
	constexpr uint32 MaximumInitialSwarmCandidateCount = 64u;

	bool ConfigMatchesGroup(
		const FGuLiGroupAbilityConfigSnapshot& Config,
		const FGuLiWingmanGroupHandle& Group)
	{
		return Config.ShipInstanceId == Group.ShipInstanceId
			&& Config.ShipGeneration == Group.ShipGeneration
			&& Config.GroupGeneration == Group.GroupGeneration;
	}

	uint64 FormationProjectionHash(const FGuLiGroupAbilityConfigSnapshot& Config)
	{
		uint64 Hash = GuLiShipAbilityHash::OffsetBasis;
		GuLiShipAbilityHash::AddTag(Hash, Config.FormationAbilityId);
		GuLiShipAbilityHash::AddUInt32(Hash, Config.FormationDefinitionRevision);
		GuLiShipAbilityHash::AddUInt64(Hash, Config.FormationDefinitionChecksum);
		GuLiShipAbilityHash::AddUInt32(Hash, Config.FormationCommandRevision);
		Config.FormationRuntime.AddToStableHash(Hash);
		return GuLiShipAbilityHash::Finish(Hash);
	}

	int32 QuantizeToInt32(const double Value)
	{
		return static_cast<int32>(FMath::Clamp(
			FMath::RoundToDouble(Value),
			static_cast<double>(MIN_int32),
			static_cast<double>(MAX_int32)));
	}

	bool RequiresEmergencyAvoidance(
		const FGuLiWingmanAvoidanceFragment& Avoidance,
		const FGuLiWingmanNavigationGuidanceFragment& Navigation)
	{
		// Acceleration is intentionally not an emergency signal. Normal formation
		// convergence and separation both publish non-zero steering acceleration.
		// Threat flags are also diagnostics when another sampled heading is safe;
		// they become an emergency only when this entity has no safe next heading.
		const bool bDetectedThreatWithoutSafeHeading = !Avoidance.bHasNextStepSafeDirection
			&& (Avoidance.bDetectedWorldStatic
				|| Avoidance.bDetectedWorldDynamic
				|| Avoidance.bDetectedFlightNavBoundary);
		return Avoidance.bControlledRecovery
			|| Avoidance.ConsecutiveBlockedSeconds > 0.0f
			|| bDetectedThreatWithoutSafeHeading
			|| Navigation.bUsingSafeFallback;
	}

	const TCHAR* GetFlightNavSegmentStatusName(const EGuLiFlightNavSegmentStatus Status)
	{
		switch (Status)
		{
		case EGuLiFlightNavSegmentStatus::Valid: return TEXT("Valid");
		case EGuLiFlightNavSegmentStatus::InvalidData: return TEXT("InvalidData");
		case EGuLiFlightNavSegmentStatus::EndpointOutsideNavigation: return TEXT("EndpointOutsideNavigation");
		case EGuLiFlightNavSegmentStatus::InsufficientClearance: return TEXT("InsufficientClearance");
		case EGuLiFlightNavSegmentStatus::Disconnected: return TEXT("Disconnected");
		case EGuLiFlightNavSegmentStatus::MissingLink: return TEXT("MissingLink");
		case EGuLiFlightNavSegmentStatus::InvalidPortal: return TEXT("InvalidPortal");
		default: return TEXT("Unknown");
		}
	}

	bool BuildInitialFormationPositions(
		const UGuLiFlightNavigationSubsystem* Navigation,
		const bool bRequireNavigation,
		const FTransform& CarrierTransform,
		const FGuLiWingmanFormationRuntimeConfig& Formation,
		TArray<FVector>& OutPositions,
		int32& OutFailedMemberIndex,
		FVector& OutFailedPosition,
		EGuLiFlightNavSegmentStatus& OutFailureStatus)
	{
		OutPositions.Reset();
		OutPositions.Reserve(GULI_WINGMAN_GROUP_SIZE);
		OutFailedMemberIndex = INDEX_NONE;
		OutFailedPosition = CarrierTransform.GetLocation();
		OutFailureStatus = EGuLiFlightNavSegmentStatus::InvalidData;
		const float AgentRadius = Formation.AgentRadiusCentimeters;
		if (!FMath::IsFinite(AgentRadius) || AgentRadius <= 0.0f)
		{
			return false;
		}
		if (bRequireNavigation)
		{
			if (!Navigation)
			{
				return false;
			}
			OutFailureStatus = Navigation->ValidateAuthoritativeSegment(
				CarrierTransform.GetLocation(), CarrierTransform.GetLocation(), AgentRadius);
			if (OutFailureStatus != EGuLiFlightNavSegmentStatus::Valid)
			{
				return false;
			}
		}
		if (Formation.Model == EGuLiWingmanFormationModel::SwarmOrbit)
		{
			const float MinimumSpacing = FMath::Max(
				Formation.SeparationRadiusCentimeters,
				Formation.AgentRadiusCentimeters * 2.0f);
			for (int32 GroupMemberIndex = 0;
				GroupMemberIndex < GULI_WINGMAN_GROUP_SIZE;
				++GroupMemberIndex)
			{
				FGuLiWingmanHandle Handle;
				Handle.Flight.FlightIndex = static_cast<uint8>(
					GroupMemberIndex / GULI_WINGMAN_MEMBERS_PER_FLIGHT);
				Handle.MemberIndex = static_cast<uint8>(
					GroupMemberIndex % GULI_WINGMAN_MEMBERS_PER_FLIGHT);
				Handle.EntityGeneration = 1u;
				FGuLiWingmanSwarmAgentFragment Agent;
				GuLiWingmanSwarmFlow::InitializeAgent(Formation, Handle, 1u, Agent);
				OutFailedMemberIndex = GroupMemberIndex;
				bool bResolved = false;
				const uint32 CandidateCount = bRequireNavigation
					? MaximumInitialSwarmCandidateCount : 1u;
				for (uint32 CandidateIndex = 0u; CandidateIndex < CandidateCount; ++CandidateIndex)
				{
					const FVector Position = CarrierTransform.GetLocation()
						+ GuLiWingmanSwarmFlow::BuildInitialOffset(
							Formation, Handle, Agent, CandidateIndex);
					OutFailedPosition = Position;
					if (Position.ContainsNaN())
					{
						OutFailureStatus = EGuLiFlightNavSegmentStatus::InvalidData;
						continue;
					}
					const bool bHasSpacing = !OutPositions.ContainsByPredicate(
						[&Position, MinimumSpacing](const FVector& Existing)
						{
							return FVector::DistSquared(Position, Existing)
								< FMath::Square(MinimumSpacing);
						});
					if (!bHasSpacing)
					{
						continue;
					}
					OutFailureStatus = bRequireNavigation
						? Navigation->ValidateEndpointsInSameComponent(
							CarrierTransform.GetLocation(), Position, AgentRadius)
						: EGuLiFlightNavSegmentStatus::Valid;
					if (OutFailureStatus == EGuLiFlightNavSegmentStatus::Valid)
					{
						OutPositions.Add(Position);
						bResolved = true;
						break;
					}
				}
				if (!bResolved)
				{
					OutPositions.Reset();
					return false;
				}
			}
			OutFailedMemberIndex = INDEX_NONE;
			OutFailureStatus = EGuLiFlightNavSegmentStatus::Valid;
			return true;
		}

		const int32 InnerRingSlots = static_cast<int32>(Formation.InnerRingSlots);
		const int32 OuterRingSlots = static_cast<int32>(Formation.OuterRingSlots);
		if (InnerRingSlots <= 0 || OuterRingSlots <= 0
			|| InnerRingSlots + OuterRingSlots != GULI_WINGMAN_GROUP_SIZE)
		{
			OutFailedMemberIndex = -2;
			OutFailureStatus = EGuLiFlightNavSegmentStatus::InvalidData;
			return false;
		}

		for (int32 GroupMemberIndex = 0;
			GroupMemberIndex < GULI_WINGMAN_GROUP_SIZE;
			++GroupMemberIndex)
		{
			const bool bInnerRing = GroupMemberIndex < InnerRingSlots;
			const int32 RingIndex = bInnerRing
				? GroupMemberIndex : GroupMemberIndex - InnerRingSlots;
			const int32 RingCount = bInnerRing ? InnerRingSlots : OuterRingSlots;
			const float Radius = bInnerRing
				? Formation.InnerRingRadiusCentimeters
				: Formation.OuterRingRadiusCentimeters;
			const float Height = bInnerRing
				? Formation.InnerRingHeightCentimeters
				: Formation.OuterRingHeightCentimeters;
			const float Phase = static_cast<float>(RingIndex) * UE_TWO_PI
				/ static_cast<float>(RingCount);
			const FVector WorldOffset(
				Radius * FMath::Cos(Phase), Radius * FMath::Sin(Phase), Height);
			// Formation slots follow only the carrier position. Keeping the offset in
			// world space prevents ship yaw/pitch/roll from sweeping every target slot.
			const FVector Position = CarrierTransform.GetLocation() + WorldOffset;
			OutFailedMemberIndex = GroupMemberIndex;
			OutFailedPosition = Position;
			OutFailureStatus = Position.ContainsNaN()
				? EGuLiFlightNavSegmentStatus::InvalidData
				: bRequireNavigation
					? Navigation->ValidateEndpointsInSameComponent(
						CarrierTransform.GetLocation(), Position, AgentRadius)
					: EGuLiFlightNavSegmentStatus::Valid;
			if (OutFailureStatus != EGuLiFlightNavSegmentStatus::Valid)
			{
				OutPositions.Reset();
				return false;
			}
			OutPositions.Add(Position);
		}
		OutFailedMemberIndex = INDEX_NONE;
		OutFailureStatus = EGuLiFlightNavSegmentStatus::Valid;
		return true;
	}
}

bool UGuLiWingmanSimulationSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	return Super::ShouldCreateSubsystem(Outer) && World && World->IsGameWorld()
		&& IsOwnerSimulationNetMode(World->GetNetMode());
}

void UGuLiWingmanSimulationSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency<UMassEntitySubsystem>();
	MassEntitySubsystem = GetWorld() ? GetWorld()->GetSubsystem<UMassEntitySubsystem>() : nullptr;
	if (!MassEntitySubsystem || !CanOwnSimulation())
	{
		return;
	}

	TArray<const UScriptStruct*> FragmentAndTagTypes = {
		FTransformFragment::StaticStruct(),
		FGuLiWingmanIdentityFragment::StaticStruct(),
		FGuLiWingmanAbilityFragment::StaticStruct(),
		FGuLiWingmanTuningFragment::StaticStruct(),
		FGuLiWingmanCarrierFragment::StaticStruct(),
		FGuLiWingmanFormationSlotFragment::StaticStruct(),
		FGuLiWingmanSwarmAgentFragment::StaticStruct(),
		FGuLiWingmanGuidanceFragment::StaticStruct(),
		FGuLiWingmanNavigationGuidanceFragment::StaticStruct(),
		FGuLiWingmanAvoidanceFragment::StaticStruct(),
		FGuLiWingmanFlightDynamicsFragment::StaticStruct(),
		FGuLiWingmanWeaponStateFragment::StaticStruct(),
		FGuLiWingmanAttackFragment::StaticStruct(),
		FGuLiWingmanOwnerMassTag::StaticStruct()
	};
	FMassArchetypeCreationParams Parameters;
	Parameters.DebugName = TEXT("GuLiClientOwnedWingman25");
	OwnerArchetype = MassEntitySubsystem->GetMutableEntityManager().CreateArchetype(FragmentAndTagTypes, Parameters);
}

void UGuLiWingmanSimulationSubsystem::Deinitialize()
{
	DestroyAllOwnedGroups();
	InitialFormationNavigationFailuresLogged.Reset();
	OwnerArchetype = FMassArchetypeHandle();
	MassEntitySubsystem = nullptr;
	Super::Deinitialize();
}

bool UGuLiWingmanSimulationSubsystem::CanOwnSimulation() const
{
	const UWorld* World = GetWorld();
	return World && World->IsGameWorld() && IsOwnerSimulationNetMode(World->GetNetMode());
}

bool UGuLiWingmanSimulationSubsystem::IsOwnerSimulationNetMode(const ENetMode NetMode)
{
	return AGuLiWingmanGroupBehaviorRunner::CanRunInNetMode(NetMode);
}

bool UGuLiWingmanSimulationSubsystem::CreateOrResetOwnedGroup(
	const FGuLiWingmanGroupHandle& Group,
	const FGuLiGroupAbilityConfigSnapshot& AbilityConfig,
	const FTransform& CarrierTransform,
	const FVector& CarrierVelocity,
	const FGuLiCarrierSourceRef& CarrierSource)
{
	if (!CanOwnSimulation() || !MassEntitySubsystem || !OwnerArchetype.IsValid() || !Group.IsValid()
		|| !AbilityConfig.IsUsableByLeaseOwner() || !ConfigMatchesGroup(AbilityConfig, Group)
		|| CarrierTransform.ContainsNaN() || CarrierVelocity.ContainsNaN() || !CarrierSource.IsValid())
	{
		return false;
	}
	bool bBypassNavigation = false;
#if WITH_DEV_AUTOMATION_TESTS
	bBypassNavigation = bBypassNavigationRequirementForTests;
#endif
	const UGuLiFlightNavigationSubsystem* Navigation = GetWorld()
		? GetWorld()->GetSubsystem<UGuLiFlightNavigationSubsystem>() : nullptr;
	TArray<FVector> InitialPositions;
	int32 FailedMemberIndex = INDEX_NONE;
	FVector FailedPosition = CarrierTransform.GetLocation();
	EGuLiFlightNavSegmentStatus FailureStatus = EGuLiFlightNavSegmentStatus::InvalidData;
	const bool bHasUsableNavigation = BuildInitialFormationPositions(
		Navigation,
		!bBypassNavigation,
		CarrierTransform,
		AbilityConfig.FormationRuntime,
		InitialPositions,
		FailedMemberIndex,
		FailedPosition,
		FailureStatus);
	if (!bHasUsableNavigation)
	{
		if (!InitialFormationNavigationFailuresLogged.Contains(Group))
		{
			InitialFormationNavigationFailuresLogged.Add(Group);
			const FGuLiWingmanFormationRuntimeConfig& Formation = AbilityConfig.FormationRuntime;
			const FString NavigationDiagnostics = Navigation
				? Navigation->DescribeNavigationAt(FailedPosition)
				: TEXT("FlightNav subsystem missing");
			UE_LOG(LogTemp, Warning,
				TEXT("[GULI_WINGMAN_INITIAL_NAV_GATE] Ship=%s ShipGen=%u GroupGen=%u Navigation=%s "
					"FailedMember=%d Status=%s(%d) Position=%s Carrier=%s AgentRadius=%.1f "
					"Model=%d SwarmBand=(%.1f..%.1f Z=%.1f Hull=%.1f) "
					"Inner=(Slots=%u Radius=%.1f Height=%.1f) Outer=(Slots=%u Radius=%.1f Height=%.1f) "
					"Diagnostics={%s}"),
				*Group.ShipInstanceId.ToString(),
				Group.ShipGeneration,
				Group.GroupGeneration,
				Navigation ? TEXT("Present") : TEXT("Missing"),
				FailedMemberIndex,
				GetFlightNavSegmentStatusName(FailureStatus),
				static_cast<int32>(FailureStatus),
				*FailedPosition.ToCompactString(),
				*CarrierTransform.GetLocation().ToCompactString(),
				Formation.AgentRadiusCentimeters,
				static_cast<int32>(Formation.Model),
				Formation.SwarmOrbit.InnerSoftRadiusCentimeters,
				Formation.SwarmOrbit.OuterSoftRadiusCentimeters,
				Formation.SwarmOrbit.VerticalHalfExtentCentimeters,
				Formation.SwarmOrbit.HullExclusionRadiusCentimeters,
				Formation.InnerRingSlots,
				Formation.InnerRingRadiusCentimeters,
				Formation.InnerRingHeightCentimeters,
				Formation.OuterRingSlots,
				Formation.OuterRingRadiusCentimeters,
				Formation.OuterRingHeightCentimeters,
				*NavigationDiagnostics);
		}
		return false;
	}
	if (InitialPositions.Num() != GULI_WINGMAN_GROUP_SIZE)
	{
		return false;
	}
	InitialFormationNavigationFailuresLogged.Remove(Group);
	if (FGuLiWingmanLocalGroupRuntime* Existing = OwnedGroups.Find(Group))
	{
		if (Existing->Entities.Num() == GULI_WINGMAN_GROUP_SIZE
			&& Existing->AbilityConfig.HasSameVersion(AbilityConfig))
		{
			return UpdateOwnedGroupCarrier(Group, CarrierTransform, CarrierVelocity, CarrierSource);
		}
		DestroyOwnedGroup(Group);
	}

	FMassEntityManager& EntityManager = MassEntitySubsystem->GetMutableEntityManager();
	FMassArchetypeSharedFragmentValues SharedValues;
	TArray<FMassEntityHandle> Entities;
	Entities.Reserve(GULI_WINGMAN_GROUP_SIZE);
	TSharedRef<FMassEntityManager::FEntityCreationContext> CreationContext = EntityManager.BatchCreateEntities(
		OwnerArchetype, SharedValues, GULI_WINGMAN_GROUP_SIZE, Entities);
	if (Entities.Num() != GULI_WINGMAN_GROUP_SIZE)
	{
		if (!Entities.IsEmpty()) EntityManager.BatchDestroyEntities(Entities);
		return false;
	}
	for (int32 Index = 0; Index < Entities.Num(); ++Index)
	{
		if (!InitializeOwnedEntity(Entities[Index], Group, Index, AbilityConfig,
			InitialPositions[Index],
			CarrierTransform, CarrierVelocity, CarrierSource))
		{
			EntityManager.BatchDestroyEntities(Entities);
			return false;
		}
	}
	FGuLiWingmanLocalGroupRuntime& Runtime = OwnedGroups.Add(Group);
	Runtime.AbilityConfig = AbilityConfig;
	Runtime.Entities = MoveTemp(Entities);
	Runtime.FlightNavigation.Reserve(GULI_WINGMAN_FLIGHT_COUNT);
	for (uint8 FlightIndex = 0u; FlightIndex < GULI_WINGMAN_FLIGHT_COUNT; ++FlightIndex)
	{
		Runtime.FlightNavigation.Add(MakeShared<FGuLiWingmanFlightNavigationRuntime>());
	}
	if (!StartBehaviorRunner(Runtime, Group))
	{
		DestroyOwnedGroup(Group);
		return false;
	}
	return true;
}

bool UGuLiWingmanSimulationSubsystem::StartBehaviorRunner(
	FGuLiWingmanLocalGroupRuntime& Runtime,
	const FGuLiWingmanGroupHandle& Group)
{
	UWorld* World = GetWorld();
	if (!World || !CanOwnSimulation())
	{
		return false;
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.Name = MakeUniqueObjectName(
		World->PersistentLevel, AGuLiWingmanGroupBehaviorRunner::StaticClass(), TEXT("WingmanGroupBehavior"));
	SpawnParameters.ObjectFlags |= RF_Transient;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AGuLiWingmanGroupBehaviorRunner* Runner = World->SpawnActor<AGuLiWingmanGroupBehaviorRunner>(
		AGuLiWingmanGroupBehaviorRunner::StaticClass(), FTransform::Identity, SpawnParameters);
	if (!Runner)
	{
		return false;
	}

	UStateTree* StateTree = GroupBehaviorStateTree.IsNull() ? nullptr : GroupBehaviorStateTree.LoadSynchronous();
	if (!GroupBehaviorStateTree.IsNull() && !StateTree)
	{
		UE_LOG(LogTemp, Error, TEXT("Wingman authored StateTree failed to load: %s"),
			*GroupBehaviorStateTree.ToSoftObjectPath().ToString());
	}
	if (!Runner->InitializeRunner(this, Group, StateTree))
	{
		Runner->Destroy();
		return false;
	}
	Runtime.BehaviorRunner = Runner;
	return true;
}

bool UGuLiWingmanSimulationSubsystem::InitializeOwnedEntity(
	const FMassEntityHandle Entity,
	const FGuLiWingmanGroupHandle& Group,
	const int32 GroupMemberIndex,
	const FGuLiGroupAbilityConfigSnapshot& AbilityConfig,
	const FVector& InitialPosition,
	const FTransform& CarrierTransform,
	const FVector& CarrierVelocity,
	const FGuLiCarrierSourceRef& CarrierSource)
{
	if (!MassEntitySubsystem || GroupMemberIndex < 0 || GroupMemberIndex >= GULI_WINGMAN_GROUP_SIZE)
	{
		return false;
	}
	FMassEntityManager& EntityManager = MassEntitySubsystem->GetMutableEntityManager();
	if (!EntityManager.IsEntityValid(Entity))
	{
		return false;
	}

	FGuLiWingmanHandle Handle;
	Handle.Flight.Group = Group;
	Handle.Flight.FlightIndex = static_cast<uint8>(GroupMemberIndex / GULI_WINGMAN_MEMBERS_PER_FLIGHT);
	Handle.MemberIndex = static_cast<uint8>(GroupMemberIndex % GULI_WINGMAN_MEMBERS_PER_FLIGHT);
	Handle.EntityGeneration = 1u;
	FGuLiWingmanIdentityFragment& Identity =
		EntityManager.GetFragmentDataChecked<FGuLiWingmanIdentityFragment>(Entity);
	Identity.Handle = Handle;
	Identity.WingmanTypeId = AbilityConfig.WingmanTypeId;

	FGuLiWingmanAbilityFragment& Ability = EntityManager.GetFragmentDataChecked<FGuLiWingmanAbilityFragment>(Entity);
	Ability.AbilitySetRevision = AbilityConfig.AbilitySetRevision;
	Ability.LoadoutRevision = AbilityConfig.LoadoutRevision;
	Ability.FormationCommandRevision = AbilityConfig.FormationCommandRevision;
	Ability.FormationDefinitionChecksum = AbilityConfig.FormationDefinitionChecksum;
	FGuLiWingmanTuningFragment& Tuning =
		EntityManager.GetFragmentDataChecked<FGuLiWingmanTuningFragment>(Entity);
	Tuning.Formation = AbilityConfig.FormationRuntime;
	Tuning.BasicWeapon = AbilityConfig.BasicWeaponRuntime;

	FGuLiWingmanCarrierFragment& Carrier = EntityManager.GetFragmentDataChecked<FGuLiWingmanCarrierFragment>(Entity);
	Carrier.Transform = CarrierTransform;
	Carrier.Velocity = CarrierVelocity;
	Carrier.Source = CarrierSource;

	FGuLiWingmanFormationSlotFragment& Slot =
		EntityManager.GetFragmentDataChecked<FGuLiWingmanFormationSlotFragment>(Entity);
	FGuLiWingmanSwarmAgentFragment& SwarmAgent =
		EntityManager.GetFragmentDataChecked<FGuLiWingmanSwarmAgentFragment>(Entity);
	GuLiWingmanSwarmFlow::InitializeAgent(
		AbilityConfig.FormationRuntime,
		Handle,
		AbilityConfig.EffectiveClientSimTick,
		SwarmAgent);

	FVector Position = InitialPosition;
	FVector Forward = FVector::ForwardVector;
	FVector InitialVelocity = FVector::ZeroVector;
	if (AbilityConfig.FormationRuntime.Model == EGuLiWingmanFormationModel::DoubleRingLegacy)
	{
		const int32 InnerRingSlots = static_cast<int32>(AbilityConfig.FormationRuntime.InnerRingSlots);
		const bool bInnerRing = GroupMemberIndex < InnerRingSlots;
		const int32 RingIndex = bInnerRing ? GroupMemberIndex : GroupMemberIndex - InnerRingSlots;
		const int32 RingCount = bInnerRing ? InnerRingSlots
			: static_cast<int32>(AbilityConfig.FormationRuntime.OuterRingSlots);
		Slot.RadiusCentimeters = bInnerRing
			? AbilityConfig.FormationRuntime.InnerRingRadiusCentimeters
			: AbilityConfig.FormationRuntime.OuterRingRadiusCentimeters;
		Slot.HeightCentimeters = bInnerRing
			? AbilityConfig.FormationRuntime.InnerRingHeightCentimeters
			: AbilityConfig.FormationRuntime.OuterRingHeightCentimeters;
		Slot.PhaseRadians = static_cast<float>(RingIndex) * UE_TWO_PI / static_cast<float>(RingCount);
		Slot.AngularSpeedRadiansPerSecond = bInnerRing
			? AbilityConfig.FormationRuntime.InnerAngularSpeedRadiansPerSecond
			: AbilityConfig.FormationRuntime.OuterAngularSpeedRadiansPerSecond;
		Slot.bClockwise = bInnerRing;
		const float DirectionSign = Slot.bClockwise ? -1.0f : 1.0f;
		const FVector WorldTangent = DirectionSign
			* FVector(-FMath::Sin(Slot.PhaseRadians), FMath::Cos(Slot.PhaseRadians), 0.0f);
		Forward = WorldTangent.GetSafeNormal();
		InitialVelocity = Forward * AbilityConfig.FormationRuntime.CruiseSpeedCentimetersPerSecond;
	}
	else
	{
		InitialVelocity = GuLiWingmanSwarmFlow::BuildPreferredVelocity(
			Position,
			CarrierVelocity,
			CarrierTransform.GetLocation(),
			CarrierVelocity,
			SwarmAgent,
			AbilityConfig.FormationRuntime,
			EGuLiWingmanFlightMode::Orbit);
		Forward = InitialVelocity.GetSafeNormal();
	}
	if (Forward.IsNearlyZero())
	{
		Forward = FVector::ForwardVector;
	}
	FTransform InitialTransform(
		Forward.Rotation().Quaternion(), Position);
	EntityManager.GetFragmentDataChecked<FTransformFragment>(Entity).SetTransform(InitialTransform);

	FGuLiWingmanGuidanceFragment& Guidance = EntityManager.GetFragmentDataChecked<FGuLiWingmanGuidanceFragment>(Entity);
	Guidance.bUsesVelocityField =
		AbilityConfig.FormationRuntime.Model == EGuLiWingmanFormationModel::SwarmOrbit;
	Guidance.PreferredVelocity = InitialVelocity;
	Guidance.DesiredPosition = Guidance.bUsesVelocityField
		? Position + InitialVelocity * AbilityConfig.FormationRuntime.SwarmOrbit.ResponseTimeSeconds
		: Position;
	Guidance.DesiredForward = Forward;
	Guidance.DesiredSpeedCentimetersPerSecond = FMath::Clamp(
		static_cast<float>(InitialVelocity.Size()),
		AbilityConfig.FormationRuntime.MinimumSpeedCentimetersPerSecond,
		AbilityConfig.FormationRuntime.CruiseSpeedCentimetersPerSecond);
	FGuLiWingmanFlightDynamicsFragment& Dynamics =
		EntityManager.GetFragmentDataChecked<FGuLiWingmanFlightDynamicsFragment>(Entity);
	// SwarmOrbit is spawned from rest and accelerates through the same finite
	// Integration envelope used after bootstrap. Publishing a full cruise-speed
	// tangent before FlightNav has measured braking room can create an accepted
	// state whose very first legal turn is already outside navigation.
	Dynamics.Velocity = AbilityConfig.FormationRuntime.Model == EGuLiWingmanFormationModel::SwarmOrbit
		? FVector::ZeroVector
		: Forward * Guidance.DesiredSpeedCentimetersPerSecond;
	Dynamics.Mode = EGuLiWingmanFlightMode::Orbit;
	Dynamics.bAlive = true;
	return true;
}

bool UGuLiWingmanSimulationSubsystem::UpdateOwnedGroupCarrier(
	const FGuLiWingmanGroupHandle& Group,
	const FTransform& CarrierTransform,
	const FVector& CarrierVelocity,
	const FGuLiCarrierSourceRef& CarrierSource)
{
	FGuLiWingmanLocalGroupRuntime* Runtime = OwnedGroups.Find(Group);
	if (!Runtime || !MassEntitySubsystem || CarrierTransform.ContainsNaN()
		|| CarrierVelocity.ContainsNaN() || !CarrierSource.IsValid())
	{
		return false;
	}
	FMassEntityManager& EntityManager = MassEntitySubsystem->GetMutableEntityManager();
	for (const FMassEntityHandle Entity : Runtime->Entities)
	{
		if (!EntityManager.IsEntityValid(Entity)) return false;
		FGuLiWingmanCarrierFragment& Carrier = EntityManager.GetFragmentDataChecked<FGuLiWingmanCarrierFragment>(Entity);
		Carrier.Transform = CarrierTransform;
		Carrier.Velocity = CarrierVelocity;
		Carrier.Source = CarrierSource;
	}
	return true;
}

bool UGuLiWingmanSimulationSubsystem::ApplyCommittedAbilityConfig(
	const FGuLiWingmanGroupHandle& Group,
	const FGuLiGroupAbilityConfigSnapshot& AbilityConfig)
{
	FGuLiWingmanLocalGroupRuntime* Runtime = OwnedGroups.Find(Group);
	if (!Runtime || !MassEntitySubsystem || !AbilityConfig.IsUsableByLeaseOwner()
		|| !ConfigMatchesGroup(AbilityConfig, Group)
		|| (Runtime->AbilityConfig.IsUsableByLeaseOwner()
			&& Runtime->AbilityConfig.WingmanTypeId != AbilityConfig.WingmanTypeId))
	{
		return false;
	}
	if (Runtime->AbilityConfig.HasSameVersion(AbilityConfig))
	{
		return true;
	}
	const FGuLiGroupAbilityConfigSnapshot PreviousConfig = Runtime->AbilityConfig;
	const bool bFormationChanged =
		FormationProjectionHash(PreviousConfig) != FormationProjectionHash(AbilityConfig);
	if (bFormationChanged)
	{
		// Only a formation cut invalidates navigation. Weapon-only commits keep
		// paths, formation seed, transforms and member identity intact.
		CancelNavigationForRuntime(*Runtime, true);
	}
	const double NowSeconds = GetWorld()
		? static_cast<double>(GetWorld()->GetTimeSeconds()) : 0.0;
	FMassEntityManager& EntityManager = MassEntitySubsystem->GetMutableEntityManager();
	for (const FMassEntityHandle Entity : Runtime->Entities)
	{
		if (!EntityManager.IsEntityValid(Entity)) return false;
		FGuLiWingmanAbilityFragment& Ability = EntityManager.GetFragmentDataChecked<FGuLiWingmanAbilityFragment>(Entity);
		Ability.AbilitySetRevision = AbilityConfig.AbilitySetRevision;
		Ability.LoadoutRevision = AbilityConfig.LoadoutRevision;
		Ability.FormationCommandRevision = AbilityConfig.FormationCommandRevision;
		Ability.FormationDefinitionChecksum = AbilityConfig.FormationDefinitionChecksum;
		FGuLiWingmanTuningFragment& Tuning =
			EntityManager.GetFragmentDataChecked<FGuLiWingmanTuningFragment>(Entity);
		Tuning.Formation = AbilityConfig.FormationRuntime;
		Tuning.BasicWeapon = AbilityConfig.BasicWeaponRuntime;

		FGuLiWingmanIdentityFragment& Identity =
			EntityManager.GetFragmentDataChecked<FGuLiWingmanIdentityFragment>(Entity);
		Identity.WingmanTypeId = AbilityConfig.WingmanTypeId;
		const FGuLiWingmanHandle& Handle = Identity.Handle;
		if (bFormationChanged)
		{
			FGuLiWingmanSwarmAgentFragment& SwarmAgent =
				EntityManager.GetFragmentDataChecked<FGuLiWingmanSwarmAgentFragment>(Entity);
			GuLiWingmanSwarmFlow::InitializeAgent(
				AbilityConfig.FormationRuntime,
				Handle,
				AbilityConfig.EffectiveClientSimTick,
				SwarmAgent);
			if (AbilityConfig.FormationRuntime.Model == EGuLiWingmanFormationModel::DoubleRingLegacy)
			{
				const int32 GroupMemberIndex = Handle.GetGroupMemberIndex();
				const int32 InnerRingSlots = static_cast<int32>(AbilityConfig.FormationRuntime.InnerRingSlots);
				const bool bInnerRing = GroupMemberIndex < InnerRingSlots;
				FGuLiWingmanFormationSlotFragment& Slot =
					EntityManager.GetFragmentDataChecked<FGuLiWingmanFormationSlotFragment>(Entity);
				Slot.RadiusCentimeters = bInnerRing
					? AbilityConfig.FormationRuntime.InnerRingRadiusCentimeters
					: AbilityConfig.FormationRuntime.OuterRingRadiusCentimeters;
				Slot.HeightCentimeters = bInnerRing
					? AbilityConfig.FormationRuntime.InnerRingHeightCentimeters
					: AbilityConfig.FormationRuntime.OuterRingHeightCentimeters;
				Slot.AngularSpeedRadiansPerSecond = bInnerRing
					? AbilityConfig.FormationRuntime.InnerAngularSpeedRadiansPerSecond
					: AbilityConfig.FormationRuntime.OuterAngularSpeedRadiansPerSecond;
				Slot.bClockwise = bInnerRing;
			}
		}
		FGuLiWingmanWeaponStateFragment& Weapon =
			EntityManager.GetFragmentDataChecked<FGuLiWingmanWeaponStateFragment>(Entity);
		for (const FGuLiWingmanWeaponChannelConfig& Channel : AbilityConfig.WeaponChannels)
		{
			if (!Channel.bEnabled || Channel.Kind != EGuLiWingmanWeaponKind::BasicAutomatic)
			{
				continue;
			}
			const FGuLiWingmanWeaponChannelConfig* Previous =
				PreviousConfig.FindWeaponChannel(Channel.Binding);
			double* ExistingNext = Weapon.FindNextFireSeconds(Channel.Binding.SlotId);
			if (!Previous || !Previous->bEnabled)
			{
				Weapon.SetNextFireSeconds(
					Channel.Binding.SlotId, NowSeconds + Channel.Runtime.CooldownSeconds);
			}
			else if (ExistingNext && Previous->SkillId != Channel.SkillId)
			{
				*ExistingNext = FMath::Max(
					*ExistingNext, NowSeconds + Channel.Runtime.CooldownSeconds);
			}
			else if (ExistingNext && !FMath::IsNearlyEqual(
				Previous->Runtime.CooldownSeconds, Channel.Runtime.CooldownSeconds))
			{
				const double RemainingRatio = FMath::Clamp(
					(*ExistingNext - NowSeconds) / Previous->Runtime.CooldownSeconds, 0.0, 1.0);
				*ExistingNext = NowSeconds + RemainingRatio * Channel.Runtime.CooldownSeconds;
			}
		}
		if (bFormationChanged)
		{
			// A formation mutation requires a new accepted pose; weapon-only
			// changes retain the same authoritative movement cut.
			Weapon.SourceAcceptedState = FGuLiAcceptedStateRef();
			Weapon.SourceAcceptedAbilitySetRevision = 0u;
			Weapon.SourceAcceptedFormationCommandRevision = 0u;
			Weapon.SourceAcceptedFormationDefinitionChecksum = 0u;
		}
	}
	Runtime->AbilityConfig = AbilityConfig;
	if (bFormationChanged)
	{
		Runtime->NavigationEvaluationAccumulator = 0.0f;
	}
	return true;
}

bool UGuLiWingmanSimulationSubsystem::ApplyRosterCut(
	const FGuLiWingmanGroupHandle& Group,
	const TArray<FGuLiWingmanRosterEntry>& Roster)
{
	FGuLiWingmanLocalGroupRuntime* Runtime = OwnedGroups.Find(Group);
	if (!Runtime || !MassEntitySubsystem || Roster.Num() != GULI_WINGMAN_GROUP_SIZE
		|| Runtime->Entities.Num() != GULI_WINGMAN_GROUP_SIZE)
	{
		return false;
	}

	TStaticArray<const FGuLiWingmanRosterEntry*, GULI_WINGMAN_GROUP_SIZE> ByStableSlot{};
	for (const FGuLiWingmanRosterEntry& Entry : Roster)
	{
		const int32 StableSlot = Entry.Wingman.GetGroupMemberIndex();
		if (!Entry.IsWellFormed(Group) || Entry.WingmanTypeId != Runtime->AbilityConfig.WingmanTypeId
			|| StableSlot < 0 || StableSlot >= GULI_WINGMAN_GROUP_SIZE
			|| ByStableSlot[StableSlot] != nullptr)
		{
			return false;
		}
		ByStableSlot[StableSlot] = &Entry;
	}

	FMassEntityManager& EntityManager = MassEntitySubsystem->GetMutableEntityManager();
	for (int32 StableSlot = 0; StableSlot < Runtime->Entities.Num(); ++StableSlot)
	{
		const FMassEntityHandle Entity = Runtime->Entities[StableSlot];
		const FGuLiWingmanRosterEntry* Entry = ByStableSlot[StableSlot];
		if (!Entry || !EntityManager.IsEntityValid(Entity))
		{
			return false;
		}
		FGuLiWingmanIdentityFragment& Identity =
			EntityManager.GetFragmentDataChecked<FGuLiWingmanIdentityFragment>(Entity);
		Identity.WingmanTypeId = Entry->WingmanTypeId;
		FGuLiWingmanFlightDynamicsFragment& Dynamics =
			EntityManager.GetFragmentDataChecked<FGuLiWingmanFlightDynamicsFragment>(Entity);
		if (Identity.Handle != Entry->Wingman)
		{
			Identity.Handle = Entry->Wingman;
			if (Runtime->AbilityConfig.FormationRuntime.Model
				== EGuLiWingmanFormationModel::SwarmOrbit)
			{
				FGuLiWingmanSwarmAgentFragment& SwarmAgent =
					EntityManager.GetFragmentDataChecked<FGuLiWingmanSwarmAgentFragment>(Entity);
				const uint32 SimulationTick = SwarmAgent.FlowSimulationTick;
				const float StepAccumulator = SwarmAgent.FlowStepAccumulator;
				GuLiWingmanSwarmFlow::InitializeAgent(
					Runtime->AbilityConfig.FormationRuntime,
					Entry->Wingman,
					SimulationTick,
					SwarmAgent);
				SwarmAgent.FlowStepAccumulator = StepAccumulator;
			}
			// EntityGeneration owns its independent cooldown/source domain. A stable-slot
			// replacement inherits the current local pose, never the dead entity's weapon state.
			FGuLiWingmanWeaponStateFragment& Weapon =
				EntityManager.GetFragmentDataChecked<FGuLiWingmanWeaponStateFragment>(Entity);
			Weapon = FGuLiWingmanWeaponStateFragment{};
			const double NowSeconds = GetWorld()
				? static_cast<double>(GetWorld()->GetTimeSeconds()) : 0.0;
			for (const FGuLiWingmanWeaponChannelConfig& Channel : Runtime->AbilityConfig.WeaponChannels)
			{
				if (Channel.bEnabled && Channel.Kind == EGuLiWingmanWeaponKind::BasicAutomatic)
				{
					Weapon.SetNextFireSeconds(
						Channel.Binding.SlotId, NowSeconds + Channel.Runtime.CooldownSeconds);
				}
			}
		}
		// No Transform, Velocity, Bank, mode, fixed-step clock or navigation state is
		// touched by this reliable identity cut.
		Dynamics.bAlive = !Entry->bDead;
	}
	return true;
}

bool UGuLiWingmanSimulationSubsystem::InvalidateOwnedGroupAbilities(const FGuLiWingmanGroupHandle& Group)
{
	FGuLiWingmanLocalGroupRuntime* Runtime = OwnedGroups.Find(Group);
	if (!Runtime || !MassEntitySubsystem)
	{
		return false;
	}
	CancelNavigationForRuntime(*Runtime, true);
	FMassEntityManager& EntityManager = MassEntitySubsystem->GetMutableEntityManager();
	for (const FMassEntityHandle Entity : Runtime->Entities)
	{
		if (!EntityManager.IsEntityValid(Entity)) continue;
		FGuLiWingmanAbilityFragment& Ability = EntityManager.GetFragmentDataChecked<FGuLiWingmanAbilityFragment>(Entity);
		Ability = FGuLiWingmanAbilityFragment();
		EntityManager.GetFragmentDataChecked<FGuLiWingmanFlightDynamicsFragment>(Entity).Mode = EGuLiWingmanFlightMode::Stale;
	}
	if (AGuLiWingmanGroupBehaviorRunner* Runner = Runtime->BehaviorRunner.Get())
	{
		Runner->Destroy();
	}
	Runtime->BehaviorRunner.Reset();
	Runtime->AbilityConfig = FGuLiGroupAbilityConfigSnapshot();
	return true;
}

void UGuLiWingmanSimulationSubsystem::ClearFlightNavigationGuidance(
	FGuLiWingmanLocalGroupRuntime& Runtime,
	const uint8 FlightIndex,
	const bool bUsingSafeFallback)
{
	if (!MassEntitySubsystem || FlightIndex >= GULI_WINGMAN_FLIGHT_COUNT)
	{
		return;
	}
	FMassEntityManager& EntityManager = MassEntitySubsystem->GetMutableEntityManager();
	for (const FMassEntityHandle Entity : Runtime.Entities)
	{
		if (!EntityManager.IsEntityValid(Entity))
		{
			continue;
		}
		const FGuLiWingmanIdentityFragment& Identity =
			EntityManager.GetFragmentDataChecked<FGuLiWingmanIdentityFragment>(Entity);
		if (Identity.Handle.Flight.FlightIndex != FlightIndex)
		{
			continue;
		}
		FGuLiWingmanNavigationGuidanceFragment& Guidance =
			EntityManager.GetFragmentDataChecked<FGuLiWingmanNavigationGuidanceFragment>(Entity);
		Guidance = FGuLiWingmanNavigationGuidanceFragment();
		Guidance.bUsingSafeFallback = bUsingSafeFallback;
	}
}

void UGuLiWingmanSimulationSubsystem::PublishFlightNavigationGuidance(
	FGuLiWingmanLocalGroupRuntime& Runtime,
	const uint8 FlightIndex,
	const FVector& Waypoint,
	const FVector& PathGoal,
	const uint32 RequestSerial,
	const uint16 PathPointIndex)
{
	if (!MassEntitySubsystem || FlightIndex >= GULI_WINGMAN_FLIGHT_COUNT
		|| Waypoint.ContainsNaN() || PathGoal.ContainsNaN()
		|| !Runtime.AbilityConfig.IsUsableByLeaseOwner())
	{
		return;
	}
	FMassEntityManager& EntityManager = MassEntitySubsystem->GetMutableEntityManager();
	for (const FMassEntityHandle Entity : Runtime.Entities)
	{
		if (!EntityManager.IsEntityValid(Entity))
		{
			continue;
		}
		const FGuLiWingmanIdentityFragment& Identity =
			EntityManager.GetFragmentDataChecked<FGuLiWingmanIdentityFragment>(Entity);
		if (Identity.Handle.Flight.FlightIndex != FlightIndex)
		{
			continue;
		}
		FGuLiWingmanNavigationGuidanceFragment& Guidance =
			EntityManager.GetFragmentDataChecked<FGuLiWingmanNavigationGuidanceFragment>(Entity);
		Guidance.Waypoint = Waypoint;
		Guidance.PathGoal = PathGoal;
		Guidance.AbilitySetRevision = Runtime.AbilityConfig.AbilitySetRevision;
		Guidance.FormationCommandRevision = Runtime.AbilityConfig.FormationCommandRevision;
		Guidance.RequestSerial = RequestSerial;
		Guidance.PathPointIndex = PathPointIndex;
		Guidance.bHasPath = true;
		Guidance.bUsingSafeFallback = false;
	}
}

void UGuLiWingmanSimulationSubsystem::CancelNavigationForRuntime(
	FGuLiWingmanLocalGroupRuntime& Runtime,
	const bool bClearEntityGuidance)
{
	for (int32 FlightIndex = 0; FlightIndex < Runtime.FlightNavigation.Num(); ++FlightIndex)
	{
		TSharedPtr<FGuLiWingmanFlightNavigationRuntime>& State = Runtime.FlightNavigation[FlightIndex];
		if (!State.IsValid())
		{
			State = MakeShared<FGuLiWingmanFlightNavigationRuntime>();
		}
		if (State->bRequestPending && State->CancellationToken.IsValid()
			&& !State->CancellationToken->IsCancelled())
		{
			State->CancellationToken->Cancel();
			++NavigationPendingRequestsCancelled;
		}
		*State = FGuLiWingmanFlightNavigationRuntime();
		if (bClearEntityGuidance && FlightIndex < GULI_WINGMAN_FLIGHT_COUNT)
		{
			ClearFlightNavigationGuidance(Runtime, static_cast<uint8>(FlightIndex), false);
		}
	}
	Runtime.NavigationEvaluationAccumulator = 0.0f;
}

bool UGuLiWingmanSimulationSubsystem::DestroyOwnedGroup(const FGuLiWingmanGroupHandle& Group)
{
	FGuLiWingmanLocalGroupRuntime* ExistingRuntime = OwnedGroups.Find(Group);
	if (!ExistingRuntime)
	{
		return false;
	}
	CancelNavigationForRuntime(*ExistingRuntime, true);
	FGuLiWingmanLocalGroupRuntime Runtime;
	if (!OwnedGroups.RemoveAndCopyValue(Group, Runtime))
	{
		return false;
	}
	if (AGuLiWingmanGroupBehaviorRunner* Runner = Runtime.BehaviorRunner.Get())
	{
		Runner->Destroy();
	}
	if (MassEntitySubsystem && !Runtime.Entities.IsEmpty())
	{
		MassEntitySubsystem->GetMutableEntityManager().BatchDestroyEntities(Runtime.Entities);
	}
	return true;
}

void UGuLiWingmanSimulationSubsystem::DestroyAllOwnedGroups()
{
	// Some transient automation worlds never finish MassEntitySubsystem initialization.
	// Avoid touching its checked EntityManager when this subsystem owns no entities.
	if (OwnedGroups.IsEmpty())
	{
		return;
	}
	if (MassEntitySubsystem)
	{
		FMassEntityManager& EntityManager = MassEntitySubsystem->GetMutableEntityManager();
		for (TPair<FGuLiWingmanGroupHandle, FGuLiWingmanLocalGroupRuntime>& Pair : OwnedGroups)
		{
			CancelNavigationForRuntime(Pair.Value, true);
			if (AGuLiWingmanGroupBehaviorRunner* Runner = Pair.Value.BehaviorRunner.Get()) Runner->Destroy();
			if (!Pair.Value.Entities.IsEmpty()) EntityManager.BatchDestroyEntities(Pair.Value.Entities);
		}
	}
	OwnedGroups.Reset();
}

bool UGuLiWingmanSimulationSubsystem::BuildCandidate(
	const FGuLiWingmanGroupHandle& Group,
	const uint32 MatchEpoch,
	const uint32 LeaseEpoch,
	const uint32 CandidateSequence,
	const uint32 ClientSimTick,
	FGuLiWingmanCandidateBatch& OutCandidate) const
{
	OutCandidate = FGuLiWingmanCandidateBatch();
	const FGuLiWingmanLocalGroupRuntime* Runtime = OwnedGroups.Find(Group);
	if (!Runtime || !MassEntitySubsystem || !Runtime->AbilityConfig.IsUsableByLeaseOwner()
		|| MatchEpoch == 0u || LeaseEpoch == 0u || CandidateSequence == 0u || ClientSimTick == 0u)
	{
		return false;
	}
	const FMassEntityManager& EntityManager = MassEntitySubsystem->GetEntityManager();
	OutCandidate.MatchEpoch = MatchEpoch;
	OutCandidate.Group = Group;
	OutCandidate.LeaseEpoch = LeaseEpoch;
	OutCandidate.CandidateSequence = CandidateSequence;
	OutCandidate.ClientSimTick = ClientSimTick;
	OutCandidate.AbilitySetRevision = Runtime->AbilityConfig.AbilitySetRevision;
	OutCandidate.FormationCommandRevision = Runtime->AbilityConfig.FormationCommandRevision;
	OutCandidate.FormationDefinitionChecksum = Runtime->AbilityConfig.FormationDefinitionChecksum;
	for (const FMassEntityHandle Entity : Runtime->Entities)
	{
		if (!EntityManager.IsEntityValid(Entity)) return false;
		const FGuLiWingmanFlightDynamicsFragment& Dynamics =
			EntityManager.GetFragmentDataChecked<FGuLiWingmanFlightDynamicsFragment>(Entity);
		if (!Dynamics.bAlive || Dynamics.Mode == EGuLiWingmanFlightMode::Stale) continue;
		const FGuLiWingmanCarrierFragment& Carrier = EntityManager.GetFragmentDataChecked<FGuLiWingmanCarrierFragment>(Entity);
		if (!OutCandidate.CarrierSource.IsValid()) OutCandidate.CarrierSource = Carrier.Source;
		if (OutCandidate.CarrierSource.CanonicalEpoch != Carrier.Source.CanonicalEpoch
			|| OutCandidate.CarrierSource.MoveRevision != Carrier.Source.MoveRevision) return false;
		const FTransform& Transform = EntityManager.GetFragmentDataChecked<FTransformFragment>(Entity).GetTransform();
		const FRotator Rotation = Transform.Rotator().GetNormalized();
		FGuLiWingmanCandidateSample& Sample = OutCandidate.Samples.AddDefaulted_GetRef();
		Sample.Wingman = EntityManager.GetFragmentDataChecked<FGuLiWingmanIdentityFragment>(Entity).Handle;
		Sample.PositionCentimeters = FIntVector(
			QuantizeToInt32(Transform.GetLocation().X), QuantizeToInt32(Transform.GetLocation().Y), QuantizeToInt32(Transform.GetLocation().Z));
		Sample.VelocityCentimetersPerSecond = FIntVector(
			QuantizeToInt32(Dynamics.Velocity.X), QuantizeToInt32(Dynamics.Velocity.Y), QuantizeToInt32(Dynamics.Velocity.Z));
		Sample.RotationCentiDegrees = FIntVector(
			QuantizeToInt32(Rotation.Pitch * 100.0), QuantizeToInt32(Rotation.Yaw * 100.0), QuantizeToInt32(Rotation.Roll * 100.0));
		Sample.FlightMode = static_cast<uint8>(Dynamics.Mode);
	}
	return OutCandidate.IsWellFormed();
}

bool UGuLiWingmanSimulationSubsystem::BuildFlightCandidate(
	const FGuLiWingmanGroupHandle& Group,
	const uint32 MatchEpoch,
	const uint32 LeaseEpoch,
	const uint32 ConnectionGeneration,
	const uint32 RosterRevision,
	const uint8 FlightIndex,
	const uint8 RequiredMemberMask,
	const EGuLiWingmanUploadRateClass RequestedRateClass,
	const uint32 ObservedGrantRevision,
	const uint32 CandidateSequence,
	const uint32 FrameSequence,
	const uint32 BaseAcceptedSequence,
	const uint32 ClientSimTick,
	const double CaptureEstimatedServerTimeSeconds,
	const uint32 NavSchemaRevision,
	const uint64 NavDataChecksum,
	const uint32 TuningRevision,
	const uint32 ObstacleRevision,
	FGuLiWingmanCandidateBatch& OutCandidate) const
{
	OutCandidate = FGuLiWingmanCandidateBatch{};
	const FGuLiWingmanLocalGroupRuntime* Runtime = OwnedGroups.Find(Group);
	if (!Runtime || !MassEntitySubsystem || !Runtime->AbilityConfig.IsUsableByLeaseOwner()
		|| MatchEpoch == 0u || LeaseEpoch == 0u || ConnectionGeneration == 0u
		|| RosterRevision == 0u || FlightIndex >= GULI_WINGMAN_FLIGHT_COUNT
		|| RequiredMemberMask == 0u || RequiredMemberMask >= (1u << GULI_WINGMAN_MEMBERS_PER_FLIGHT)
		|| ObservedGrantRevision == 0u || CandidateSequence == 0u || FrameSequence == 0u
		|| ClientSimTick == 0u || !FMath::IsFinite(CaptureEstimatedServerTimeSeconds)
		|| CaptureEstimatedServerTimeSeconds < 0.0 || NavSchemaRevision == 0u
		|| NavDataChecksum == 0u || TuningRevision == 0u || ObstacleRevision == 0u)
	{
		return false;
	}

	OutCandidate.MatchEpoch = MatchEpoch;
	OutCandidate.ConnectionGeneration = ConnectionGeneration;
	OutCandidate.Group = Group;
	OutCandidate.LeaseEpoch = LeaseEpoch;
	OutCandidate.RosterRevision = RosterRevision;
	OutCandidate.FlightIndex = FlightIndex;
	OutCandidate.RequiredMemberMask = RequiredMemberMask;
	OutCandidate.RequestedRateClass = RequestedRateClass;
	OutCandidate.ObservedGrantRevision = ObservedGrantRevision;
	OutCandidate.CandidateSequence = CandidateSequence;
	OutCandidate.FrameSequence = FrameSequence;
	OutCandidate.BaseAcceptedSequence = BaseAcceptedSequence;
	OutCandidate.ClientSimTick = ClientSimTick;
	OutCandidate.CaptureEstimatedServerTimeSeconds = CaptureEstimatedServerTimeSeconds;
	OutCandidate.NavSchemaRevision = NavSchemaRevision;
	OutCandidate.NavDataChecksum = NavDataChecksum;
	OutCandidate.TuningRevision = TuningRevision;
	OutCandidate.ObstacleRevision = ObstacleRevision;
	OutCandidate.AbilitySetRevision = Runtime->AbilityConfig.AbilitySetRevision;
	OutCandidate.FormationCommandRevision = Runtime->AbilityConfig.FormationCommandRevision;
	OutCandidate.FormationDefinitionChecksum = Runtime->AbilityConfig.FormationDefinitionChecksum;

	const FMassEntityManager& EntityManager = MassEntitySubsystem->GetEntityManager();
	for (const FMassEntityHandle Entity : Runtime->Entities)
	{
		if (!EntityManager.IsEntityValid(Entity)) return false;
		const FGuLiWingmanIdentityFragment& Identity =
			EntityManager.GetFragmentDataChecked<FGuLiWingmanIdentityFragment>(Entity);
		if (Identity.Handle.Flight.FlightIndex != FlightIndex) continue;
		if ((RequiredMemberMask & (1u << Identity.Handle.MemberIndex)) == 0u) continue;
		const FGuLiWingmanFlightDynamicsFragment& Dynamics =
			EntityManager.GetFragmentDataChecked<FGuLiWingmanFlightDynamicsFragment>(Entity);
		if (!Dynamics.bAlive || Dynamics.Mode == EGuLiWingmanFlightMode::Stale) continue;
		const FGuLiWingmanCarrierFragment& Carrier =
			EntityManager.GetFragmentDataChecked<FGuLiWingmanCarrierFragment>(Entity);
		if (!OutCandidate.CarrierSource.IsValid()) OutCandidate.CarrierSource = Carrier.Source;
		if (OutCandidate.CarrierSource.CanonicalEpoch != Carrier.Source.CanonicalEpoch
			|| OutCandidate.CarrierSource.MoveRevision != Carrier.Source.MoveRevision) return false;
		const FTransform& Transform =
			EntityManager.GetFragmentDataChecked<FTransformFragment>(Entity).GetTransform();
		const FRotator Rotation = Transform.Rotator().GetNormalized();
		FGuLiWingmanCandidateSample& Sample = OutCandidate.Samples.AddDefaulted_GetRef();
		Sample.Wingman = Identity.Handle;
		Sample.PositionCentimeters = FIntVector(
			QuantizeToInt32(Transform.GetLocation().X), QuantizeToInt32(Transform.GetLocation().Y),
			QuantizeToInt32(Transform.GetLocation().Z));
		Sample.VelocityCentimetersPerSecond = FIntVector(
			QuantizeToInt32(Dynamics.Velocity.X), QuantizeToInt32(Dynamics.Velocity.Y),
			QuantizeToInt32(Dynamics.Velocity.Z));
		Sample.RotationCentiDegrees = FIntVector(
			QuantizeToInt32(Rotation.Pitch * 100.0), QuantizeToInt32(Rotation.Yaw * 100.0),
			QuantizeToInt32(Rotation.Roll * 100.0));
		Sample.FlightMode = static_cast<uint8>(Dynamics.Mode);
	}
	return OutCandidate.IsWellFormed();
}

FMassEntityHandle UGuLiWingmanSimulationSubsystem::FindOwnedEntity(
	const FGuLiWingmanLocalGroupRuntime& Runtime,
	const FGuLiWingmanHandle& Wingman) const
{
	if (!MassEntitySubsystem || !Wingman.IsValid())
	{
		return FMassEntityHandle();
	}
	const FMassEntityManager& EntityManager = MassEntitySubsystem->GetEntityManager();
	for (const FMassEntityHandle Entity : Runtime.Entities)
	{
		if (EntityManager.IsEntityValid(Entity)
			&& EntityManager.GetFragmentDataChecked<FGuLiWingmanIdentityFragment>(Entity).Handle == Wingman)
		{
			return Entity;
		}
	}
	return FMassEntityHandle();
}

bool UGuLiWingmanSimulationSubsystem::ApplyAcceptedBatch(const FGuLiWingmanAcceptedBatch& AcceptedBatch)
{
	FGuLiWingmanLocalGroupRuntime* Runtime = OwnedGroups.Find(AcceptedBatch.Group);
	if (!Runtime || !MassEntitySubsystem || !AcceptedBatch.IsWellFormed()
		|| !Runtime->AbilityConfig.IsUsableByLeaseOwner()
		|| AcceptedBatch.AbilitySetRevision != Runtime->AbilityConfig.AbilitySetRevision
		|| AcceptedBatch.FormationCommandRevision != Runtime->AbilityConfig.FormationCommandRevision
		|| AcceptedBatch.FormationDefinitionChecksum != Runtime->AbilityConfig.FormationDefinitionChecksum)
	{
		return false;
	}

	FMassEntityManager& EntityManager = MassEntitySubsystem->GetMutableEntityManager();
	TArray<FMassEntityHandle, TInlineAllocator<GULI_WINGMAN_GROUP_SIZE>> SampleEntities;
	SampleEntities.Reserve(AcceptedBatch.Samples.Num());
	for (const FGuLiWingmanCandidateSample& Sample : AcceptedBatch.Samples)
	{
		const FMassEntityHandle Entity = FindOwnedEntity(*Runtime, Sample.Wingman);
		if (!EntityManager.IsEntityValid(Entity))
		{
			return false;
		}
		SampleEntities.Add(Entity);
	}

	for (int32 Index = 0; Index < AcceptedBatch.Samples.Num(); ++Index)
	{
		const FMassEntityHandle Entity = SampleEntities[Index];
		FGuLiWingmanWeaponStateFragment& Weapon =
			EntityManager.GetFragmentDataChecked<FGuLiWingmanWeaponStateFragment>(Entity);
		if (Weapon.SourceAcceptedState.IsValid()
			&& Weapon.SourceAcceptedState.MatchEpoch == AcceptedBatch.StateRef.MatchEpoch
			&& Weapon.SourceAcceptedState.AcceptedSequence >= AcceptedBatch.StateRef.AcceptedSequence)
		{
			continue;
		}
		Weapon.SourceAcceptedState = AcceptedBatch.StateRef;
		Weapon.SourceAcceptedAbilitySetRevision = AcceptedBatch.AbilitySetRevision;
		Weapon.SourceAcceptedFormationCommandRevision = AcceptedBatch.FormationCommandRevision;
		Weapon.SourceAcceptedFormationDefinitionChecksum = AcceptedBatch.FormationDefinitionChecksum;
	}
	return true;
}

bool UGuLiWingmanSimulationSubsystem::TryBuildWeaponFireIntent(
	const FGuLiWingmanGroupHandle& Group,
	const FGuLiWingmanHandle& Emitter,
	const uint32 MatchEpoch,
	const uint32 LeaseEpoch,
	const FGuLiWingmanWeaponChannelConfig& Channel,
	const FGuLiTargetHandle& Target,
	const FVector& TargetLocation,
	const double NowSeconds,
	const uint32 ClientFireTick,
	const bool bClientPredictedLineOfSight,
	FGuLiWingmanFireIntent& OutIntent)
{
	OutIntent = FGuLiWingmanFireIntent();
	FGuLiWingmanLocalGroupRuntime* Runtime = OwnedGroups.Find(Group);
	const FGuLiWingmanWeaponChannelConfig* CurrentChannel = Runtime
		? Runtime->AbilityConfig.FindWeaponChannel(Channel.Binding) : nullptr;
	if (!Runtime || !MassEntitySubsystem || !Runtime->AbilityConfig.IsUsableByLeaseOwner()
		|| !Emitter.IsValid() || Emitter.Flight.Group != Group
		|| !Channel.IsWellFormed() || !Channel.bEnabled
		|| Channel.Kind != EGuLiWingmanWeaponKind::BasicAutomatic
		|| Channel.Binding.MatchEpoch != MatchEpoch
		|| !CurrentChannel || CurrentChannel->SkillId != Channel.SkillId
		|| CurrentChannel->AbilityId != Channel.AbilityId
		|| CurrentChannel->ProfileRevision != Channel.ProfileRevision
		|| CurrentChannel->DefinitionRevision != Channel.DefinitionRevision
		|| CurrentChannel->DefinitionChecksum != Channel.DefinitionChecksum
		|| MatchEpoch == 0u || LeaseEpoch == 0u || !Target.IsValid()
		|| TargetLocation.ContainsNaN() || ClientFireTick == 0u || !FMath::IsFinite(NowSeconds)
		|| Runtime->AbilityConfig.MatchEpoch != MatchEpoch)
	{
		return false;
	}

	const FMassEntityHandle Entity = FindOwnedEntity(*Runtime, Emitter);
	FMassEntityManager& EntityManager = MassEntitySubsystem->GetMutableEntityManager();
	if (!EntityManager.IsEntityValid(Entity))
	{
		return false;
	}
	const FGuLiWingmanFlightDynamicsFragment& Dynamics =
		EntityManager.GetFragmentDataChecked<FGuLiWingmanFlightDynamicsFragment>(Entity);
	const FGuLiWingmanAbilityFragment& Ability =
		EntityManager.GetFragmentDataChecked<FGuLiWingmanAbilityFragment>(Entity);
	FGuLiWingmanWeaponStateFragment& Weapon =
		EntityManager.GetFragmentDataChecked<FGuLiWingmanWeaponStateFragment>(Entity);
	if (!Dynamics.bAlive || Dynamics.Mode == EGuLiWingmanFlightMode::Stale
		|| Ability.AbilitySetRevision != Runtime->AbilityConfig.AbilitySetRevision
		|| Ability.LoadoutRevision != Runtime->AbilityConfig.LoadoutRevision
		|| Ability.FormationCommandRevision != Runtime->AbilityConfig.FormationCommandRevision
		|| Ability.FormationDefinitionChecksum != Runtime->AbilityConfig.FormationDefinitionChecksum
		|| !Weapon.SourceAcceptedState.IsValid()
		|| Weapon.SourceAcceptedState.MatchEpoch != MatchEpoch
		|| Weapon.SourceAcceptedState.GroupGeneration != Group.GroupGeneration
		|| Weapon.SourceAcceptedAbilitySetRevision != Runtime->AbilityConfig.AbilitySetRevision
		|| Weapon.SourceAcceptedFormationCommandRevision != Runtime->AbilityConfig.FormationCommandRevision
		|| Weapon.SourceAcceptedFormationDefinitionChecksum != Runtime->AbilityConfig.FormationDefinitionChecksum
		|| NowSeconds < Weapon.GetNextFireSeconds(Channel.Binding.SlotId))
	{
		return false;
	}

	const FVector EmitterLocation =
		EntityManager.GetFragmentDataChecked<FTransformFragment>(Entity).GetTransform().GetLocation();
	const FVector AimDirection = (TargetLocation - EmitterLocation).GetSafeNormal();
	if (AimDirection.IsNearlyZero())
	{
		return false;
	}

	uint32 NextSequence = Weapon.DomainFireSequence + 1u;
	if (NextSequence == 0u)
	{
		NextSequence = 1u;
	}
	FGuLiWingmanFireIntent Candidate;
	Candidate.MatchEpoch = MatchEpoch;
	Candidate.Group = Group;
	Candidate.LeaseEpoch = LeaseEpoch;
	Candidate.DomainFireSequence = NextSequence;
	Candidate.Emitter = Emitter;
	Candidate.SourceAcceptedState = Weapon.SourceAcceptedState;
	Candidate.ClientFireTick = ClientFireTick;
	Candidate.Target = Target;
	Candidate.Binding = Channel.Binding;
	Candidate.WeaponAbilityId = Channel.AbilityId;
	Candidate.SkillId = Channel.SkillId;
	Candidate.LoadoutRevision = Runtime->AbilityConfig.LoadoutRevision;
	Candidate.ProfileRevision = Channel.ProfileRevision;
	Candidate.WeaponDefinitionRevision = Channel.DefinitionRevision;
	Candidate.AbilitySetRevision = Runtime->AbilityConfig.AbilitySetRevision;
	Candidate.AimDirectionMilli = FIntVector(
		FMath::Clamp(FMath::RoundToInt(AimDirection.X * 1000.0), -1000, 1000),
		FMath::Clamp(FMath::RoundToInt(AimDirection.Y * 1000.0), -1000, 1000),
		FMath::Clamp(FMath::RoundToInt(AimDirection.Z * 1000.0), -1000, 1000));
	Candidate.bClientPredictedLineOfSight = bClientPredictedLineOfSight;
	if (!Candidate.IsWellFormed())
	{
		return false;
	}

	Weapon.DomainFireSequence = NextSequence;
	Weapon.SetNextFireSeconds(
		Channel.Binding.SlotId, NowSeconds + Channel.Runtime.CooldownSeconds);
	if (Channel.Binding.SlotId == GuLiGetDefaultWeaponSlotId(EGuLiShipAbilitySlot::BasicWeapon))
	{
		Weapon.NextBasicFireSeconds = Weapon.GetNextFireSeconds(Channel.Binding.SlotId);
	}
	Weapon.Target = Target;
	OutIntent = MoveTemp(Candidate);
	return true;
}

bool UGuLiWingmanSimulationSubsystem::TryBuildBasicFireIntent(
	const FGuLiWingmanGroupHandle& Group,
	const FGuLiWingmanHandle& Emitter,
	const uint32 MatchEpoch,
	const uint32 LeaseEpoch,
	const FGuLiTargetHandle& Target,
	const FVector& TargetLocation,
	const double NowSeconds,
	const double CooldownSeconds,
	const uint32 ClientFireTick,
	const bool bClientPredictedLineOfSight,
	FGuLiWingmanFireIntent& OutIntent)
{
	(void)CooldownSeconds;
	const FGuLiWingmanLocalGroupRuntime* Runtime = OwnedGroups.Find(Group);
	const FGuLiWingmanWeaponChannelConfig* Channel = Runtime
		? Runtime->AbilityConfig.FindFirstWeaponChannel(EGuLiWingmanWeaponKind::BasicAutomatic) : nullptr;
	return Channel && TryBuildWeaponFireIntent(
		Group, Emitter, MatchEpoch, LeaseEpoch, *Channel, Target, TargetLocation,
		NowSeconds, ClientFireTick, bClientPredictedLineOfSight, OutIntent);
}

void UGuLiWingmanSimulationSubsystem::TickNavigationBehavior(
	const FGuLiWingmanGroupHandle& Group,
	const float DeltaSeconds)
{
	if (const UWorld* World = GetWorld(); World && World->GetNetMode() == NM_DedicatedServer)
	{
		FGuLiWingmanQAInvariantRegistry::Add(TEXT("SERVER_WINGMAN_PATHFINDING_EXECUTED"));
	}
	FGuLiWingmanLocalGroupRuntime* Runtime = OwnedGroups.Find(Group);
	if (!Runtime || !MassEntitySubsystem || !CanOwnSimulation()
		|| !Runtime->AbilityConfig.IsUsableByLeaseOwner())
	{
		return;
	}

	Runtime->NavigationEvaluationAccumulator += FMath::Clamp(DeltaSeconds, 0.0f, 0.25f);
	if (Runtime->NavigationEvaluationAccumulator < NavigationEvaluationPeriodSeconds)
	{
		return;
	}
	Runtime->NavigationEvaluationAccumulator = FMath::Fmod(
		Runtime->NavigationEvaluationAccumulator, NavigationEvaluationPeriodSeconds);

	if (Runtime->FlightNavigation.Num() != GULI_WINGMAN_FLIGHT_COUNT)
	{
		CancelNavigationForRuntime(*Runtime, true);
		Runtime->FlightNavigation.Reset(GULI_WINGMAN_FLIGHT_COUNT);
		for (uint8 FlightIndex = 0u; FlightIndex < GULI_WINGMAN_FLIGHT_COUNT; ++FlightIndex)
		{
			Runtime->FlightNavigation.Add(MakeShared<FGuLiWingmanFlightNavigationRuntime>());
		}
	}

	struct FFlightObservation
	{
		FVector PositionSum = FVector::ZeroVector;
		FVector FormationGoalSum = FVector::ZeroVector;
		float AgentRadiusCentimeters = 0.0f;
		int32 AliveMemberCount = 0;
		bool bNeedsNavigationPath = false;
	};
	FFlightObservation Observations[GULI_WINGMAN_FLIGHT_COUNT];
	FMassEntityManager& EntityManager = MassEntitySubsystem->GetMutableEntityManager();
	for (const FMassEntityHandle Entity : Runtime->Entities)
	{
		if (!EntityManager.IsEntityValid(Entity))
		{
			continue;
		}
		const FGuLiWingmanIdentityFragment& Identity =
			EntityManager.GetFragmentDataChecked<FGuLiWingmanIdentityFragment>(Entity);
		const uint8 FlightIndex = Identity.Handle.Flight.FlightIndex;
		if (FlightIndex >= GULI_WINGMAN_FLIGHT_COUNT)
		{
			continue;
		}
		const FGuLiWingmanFlightDynamicsFragment& Dynamics =
			EntityManager.GetFragmentDataChecked<FGuLiWingmanFlightDynamicsFragment>(Entity);
		if (!Dynamics.bAlive || Dynamics.Mode == EGuLiWingmanFlightMode::Stale)
		{
			continue;
		}

		FFlightObservation& Observation = Observations[FlightIndex];
		const FTransform& Transform =
			EntityManager.GetFragmentDataChecked<FTransformFragment>(Entity).GetTransform();
		const FGuLiWingmanCarrierFragment& Carrier =
			EntityManager.GetFragmentDataChecked<FGuLiWingmanCarrierFragment>(Entity);
		const FGuLiWingmanTuningFragment& Tuning =
			EntityManager.GetFragmentDataChecked<FGuLiWingmanTuningFragment>(Entity);
		FVector FormationOffset = FVector::ZeroVector;
		if (Tuning.Formation.Model == EGuLiWingmanFormationModel::SwarmOrbit)
		{
			FormationOffset = GuLiWingmanSwarmFlow::BuildFlightRecoveryOffset(
				Tuning.Formation, FlightIndex);
		}
		else
		{
			const FGuLiWingmanFormationSlotFragment& Slot =
				EntityManager.GetFragmentDataChecked<FGuLiWingmanFormationSlotFragment>(Entity);
			FormationOffset = FVector(
				Slot.RadiusCentimeters * FMath::Cos(Slot.PhaseRadians),
				Slot.RadiusCentimeters * FMath::Sin(Slot.PhaseRadians),
				Slot.HeightCentimeters);
		}
		Observation.PositionSum += Transform.GetLocation();
		Observation.FormationGoalSum += Tuning.Formation.Model == EGuLiWingmanFormationModel::SwarmOrbit
			? Carrier.Transform.GetLocation() + FormationOffset
			: Carrier.Transform.GetLocation() + FormationOffset;
		Observation.AgentRadiusCentimeters = FMath::Max(
			Observation.AgentRadiusCentimeters, Tuning.Formation.AgentRadiusCentimeters);
		++Observation.AliveMemberCount;
		Observation.bNeedsNavigationPath |=
			Dynamics.Mode == EGuLiWingmanFlightMode::CatchUp
			|| Dynamics.Mode == EGuLiWingmanFlightMode::Recover;
	}

	const double NowSeconds = GetWorld() ? static_cast<double>(GetWorld()->GetTimeSeconds()) : 0.0;
	const UGuLiFlightNavigationSubsystem* Navigation =
		GetWorld() ? GetWorld()->GetSubsystem<UGuLiFlightNavigationSubsystem>() : nullptr;
	for (uint8 FlightIndex = 0u; FlightIndex < GULI_WINGMAN_FLIGHT_COUNT; ++FlightIndex)
	{
		FFlightObservation& Observation = Observations[FlightIndex];
		TSharedPtr<FGuLiWingmanFlightNavigationRuntime>& StatePtr = Runtime->FlightNavigation[FlightIndex];
		if (!StatePtr.IsValid())
		{
			StatePtr = MakeShared<FGuLiWingmanFlightNavigationRuntime>();
		}
		FGuLiWingmanFlightNavigationRuntime& State = *StatePtr;
		if (Observation.AliveMemberCount == 0 || !Observation.bNeedsNavigationPath)
		{
			if (State.bRequestPending && State.CancellationToken.IsValid()
				&& !State.CancellationToken->IsCancelled())
			{
				State.CancellationToken->Cancel();
				++NavigationPendingRequestsCancelled;
			}
			State = FGuLiWingmanFlightNavigationRuntime();
			ClearFlightNavigationGuidance(*Runtime, FlightIndex, false);
			continue;
		}

		const FVector Start = Observation.PositionSum / static_cast<float>(Observation.AliveMemberCount);
		const FVector Goal = Observation.FormationGoalSum / static_cast<float>(Observation.AliveMemberCount);
		if (Start.ContainsNaN() || Goal.ContainsNaN())
		{
			State.PathPoints.Reset();
			State.NextPathPointIndex = INDEX_NONE;
			State.bUsingSafeFallback = true;
			State.NextRequestSeconds = NowSeconds + FailedPathRetrySeconds;
			ClearFlightNavigationGuidance(*Runtime, FlightIndex, true);
			continue;
		}

		const float RepathDistance = FMath::Max(
			MinimumGoalDriftForRepathCentimeters, Observation.AgentRadiusCentimeters * 4.0f);
		const float WaypointReachDistance = FMath::Max(
			MinimumWaypointReachDistanceCentimeters, Observation.AgentRadiusCentimeters * 2.0f);

		// If the moving formation goal invalidated an in-flight request, cancel
		// only after the bounded repath interval. A prior accepted path remains
		// usable until its replacement is accepted.
		if (State.bRequestPending
			&& FVector::DistSquared(Goal, State.RequestedGoal) > FMath::Square(RepathDistance)
			&& NowSeconds >= State.NextRequestSeconds)
		{
			if (State.CancellationToken.IsValid() && !State.CancellationToken->IsCancelled())
			{
				State.CancellationToken->Cancel();
				++NavigationPendingRequestsCancelled;
			}
			State.PendingResult.Reset();
			State.CancellationToken.Reset();
			State.bRequestPending = false;
		}

		if (State.bRequestPending && State.PendingResult.IsValid()
			&& State.PendingResult->IsReady())
		{
			const uint32 CompletedRequestSerial = State.PendingRequestSerial;
			FGuLiFlightNavPathResult Result = State.PendingResult->Get();
			State.PendingResult.Reset();
			State.CancellationToken.Reset();
			State.bRequestPending = false;
			const bool bMatchingProjection =
				CompletedRequestSerial != 0u
				&& CompletedRequestSerial == State.RequestSerial
				&& State.RequestedAbilitySetRevision == Runtime->AbilityConfig.AbilitySetRevision
				&& State.RequestedFormationCommandRevision == Runtime->AbilityConfig.FormationCommandRevision;
			bool bFinitePath = Result.IsSuccess() && Result.Points.Num() >= 2;
			for (const FVector& Point : Result.Points)
			{
				bFinitePath &= !Point.ContainsNaN();
			}
			if (bMatchingProjection && bFinitePath)
			{
				State.PathPoints = MoveTemp(Result.Points);
				State.ActivePathGoal = State.RequestedGoal;
				State.NextPathPointIndex = 1;
				State.bUsingSafeFallback = false;
				++NavigationResultsAccepted;
			}
			else
			{
				State.PathPoints.Reset();
				State.NextPathPointIndex = INDEX_NONE;
				State.bUsingSafeFallback = true;
				State.NextRequestSeconds = NowSeconds + FailedPathRetrySeconds;
				++NavigationSafeFallbackResults;
				ClearFlightNavigationGuidance(*Runtime, FlightIndex, true);
			}
		}

		if (State.PathPoints.IsValidIndex(State.NextPathPointIndex))
		{
			while (State.NextPathPointIndex + 1 < State.PathPoints.Num()
				&& FVector::DistSquared(Start, State.PathPoints[State.NextPathPointIndex])
					<= FMath::Square(WaypointReachDistance))
			{
				++State.NextPathPointIndex;
			}
			PublishFlightNavigationGuidance(*Runtime, FlightIndex,
				State.PathPoints[State.NextPathPointIndex], State.ActivePathGoal,
				State.RequestSerial, static_cast<uint16>(FMath::Min(State.NextPathPointIndex, 65535)));
		}
		else if (!State.bRequestPending)
		{
			ClearFlightNavigationGuidance(*Runtime, FlightIndex, State.bUsingSafeFallback);
		}

		const bool bHasActivePath = State.PathPoints.IsValidIndex(State.NextPathPointIndex);
		const bool bGoalDrifted = bHasActivePath
			&& FVector::DistSquared(Goal, State.ActivePathGoal) > FMath::Square(RepathDistance);
		if (State.bRequestPending || NowSeconds < State.NextRequestSeconds
			|| (bHasActivePath && !bGoalDrifted))
		{
			continue;
		}

		if (!Navigation)
		{
			State.bUsingSafeFallback = true;
			State.NextRequestSeconds = NowSeconds + FailedPathRetrySeconds;
			ClearFlightNavigationGuidance(*Runtime, FlightIndex, true);
			continue;
		}

		uint32 NextRequestSerial = State.RequestSerial + 1u;
		if (NextRequestSerial == 0u)
		{
			NextRequestSerial = 1u;
		}
		State.RequestSerial = NextRequestSerial;
		State.PendingRequestSerial = NextRequestSerial;
		State.RequestedStart = Start;
		State.RequestedGoal = Goal;
		State.RequestedAbilitySetRevision = Runtime->AbilityConfig.AbilitySetRevision;
		State.RequestedFormationCommandRevision = Runtime->AbilityConfig.FormationCommandRevision;
		State.CancellationToken =
			MakeShared<FGuLiFlightNavCancellationToken, ESPMode::ThreadSafe>();
		FGuLiFlightNavPathQueryOptions Options;
		Options.AgentRadius = FMath::Max(0.0f, Observation.AgentRadiusCentimeters);
		Options.MaximumExpandedNodes = MaximumNavigationExpandedNodes;
		Options.bSmoothPath = true;
		TFuture<FGuLiFlightNavPathResult> Future = Navigation->FindPathAsync(
			Start, Goal, Options, State.CancellationToken);
		State.PendingResult = MakeShared<TFuture<FGuLiFlightNavPathResult>>(MoveTemp(Future));
		State.bRequestPending = true;
		State.bUsingSafeFallback = false;
		State.NextRequestSeconds = NowSeconds + MinimumRepathIntervalSeconds;
		++NavigationAsyncRequestsIssued;
	}
}

void UGuLiWingmanSimulationSubsystem::TickFallbackBehavior(
	const FGuLiWingmanGroupHandle& Group,
	const float DeltaSeconds)
{
	FGuLiWingmanLocalGroupRuntime* Runtime = OwnedGroups.Find(Group);
	if (!Runtime || !MassEntitySubsystem || !CanOwnSimulation())
	{
		return;
	}
	const float SafeDeltaSeconds = FMath::Clamp(DeltaSeconds, 0.0f, 0.25f);
	FMassEntityManager& EntityManager = MassEntitySubsystem->GetMutableEntityManager();
	for (const FMassEntityHandle Entity : Runtime->Entities)
	{
		if (!EntityManager.IsEntityValid(Entity)) continue;
		const FTransform& Transform = EntityManager.GetFragmentDataChecked<FTransformFragment>(Entity).GetTransform();
		const FGuLiWingmanCarrierFragment& Carrier =
			EntityManager.GetFragmentDataChecked<FGuLiWingmanCarrierFragment>(Entity);
		const FGuLiWingmanAbilityFragment& Ability =
			EntityManager.GetFragmentDataChecked<FGuLiWingmanAbilityFragment>(Entity);
		const FGuLiWingmanTuningFragment& Tuning =
			EntityManager.GetFragmentDataChecked<FGuLiWingmanTuningFragment>(Entity);
		FGuLiWingmanFlightDynamicsFragment& Dynamic =
			EntityManager.GetFragmentDataChecked<FGuLiWingmanFlightDynamicsFragment>(Entity);
		Dynamic.ModeEvaluationAccumulator += SafeDeltaSeconds;
		if (Dynamic.ModeEvaluationAccumulator < 0.2f) continue;
		Dynamic.ModeEvaluationAccumulator = FMath::Fmod(Dynamic.ModeEvaluationAccumulator, 0.2f);
		if (!Dynamic.bAlive || !Carrier.Source.IsValid() || Ability.AbilitySetRevision == 0u
			|| Ability.FormationCommandRevision == 0u)
		{
			Dynamic.Mode = EGuLiWingmanFlightMode::Stale;
			continue;
		}
		const FGuLiWingmanAvoidanceFragment& Avoidance =
			EntityManager.GetFragmentDataChecked<FGuLiWingmanAvoidanceFragment>(Entity);
		const FGuLiWingmanNavigationGuidanceFragment& Navigation =
			EntityManager.GetFragmentDataChecked<FGuLiWingmanNavigationGuidanceFragment>(Entity);
		if (RequiresEmergencyAvoidance(Avoidance, Navigation))
		{
			Dynamic.Mode = EGuLiWingmanFlightMode::Recover;
			continue;
		}
        if (EntityManager.GetFragmentDataChecked<FGuLiWingmanAttackFragment>(Entity).bGuiding)
        { Dynamic.Mode = EGuLiWingmanFlightMode::Follow; continue; }
		const double Distance = FVector::Distance(Transform.GetLocation(), Carrier.Transform.GetLocation());
		if (Distance > Tuning.Formation.RecoveryDistanceCentimeters) Dynamic.Mode = EGuLiWingmanFlightMode::Recover;
		else if (Distance > Tuning.Formation.CatchUpDistanceCentimeters) Dynamic.Mode = EGuLiWingmanFlightMode::CatchUp;
		else if (Carrier.Velocity.SizeSquared() > FMath::Square(100.0)) Dynamic.Mode = EGuLiWingmanFlightMode::Follow;
		else Dynamic.Mode = EGuLiWingmanFlightMode::Orbit;
	}
}

EGuLiWingmanBehaviorPolicy UGuLiWingmanSimulationSubsystem::EvaluateBehaviorPolicy(
	const FGuLiWingmanGroupHandle& Group) const
{
	const FGuLiWingmanLocalGroupRuntime* Runtime = OwnedGroups.Find(Group);
	if (!Runtime || !MassEntitySubsystem || !CanOwnSimulation()
		|| !Runtime->AbilityConfig.IsUsableByLeaseOwner())
	{
		return EGuLiWingmanBehaviorPolicy::OwnerUnavailable;
	}

	const FMassEntityManager& EntityManager = MassEntitySubsystem->GetEntityManager();
	int32 AliveCount = 0;
	bool bOwnerUnavailable = false;
	bool bEmergencyAvoid = false;
	bool bJoiningEscort = false;
	for (const FMassEntityHandle Entity : Runtime->Entities)
	{
		if (!EntityManager.IsEntityValid(Entity))
		{
			bOwnerUnavailable = true;
			continue;
		}
		const FGuLiWingmanFlightDynamicsFragment& Dynamics =
			EntityManager.GetFragmentDataChecked<FGuLiWingmanFlightDynamicsFragment>(Entity);
		if (!Dynamics.bAlive)
		{
			continue;
		}
		++AliveCount;
		const FGuLiWingmanAbilityFragment& Ability =
			EntityManager.GetFragmentDataChecked<FGuLiWingmanAbilityFragment>(Entity);
		const FGuLiWingmanCarrierFragment& Carrier =
			EntityManager.GetFragmentDataChecked<FGuLiWingmanCarrierFragment>(Entity);
		if (!Carrier.Source.IsValid()
			|| Ability.AbilitySetRevision != Runtime->AbilityConfig.AbilitySetRevision
			|| Ability.FormationCommandRevision != Runtime->AbilityConfig.FormationCommandRevision
			|| Ability.FormationDefinitionChecksum != Runtime->AbilityConfig.FormationDefinitionChecksum)
		{
			bOwnerUnavailable = true;
			continue;
		}
		const FGuLiWingmanAvoidanceFragment& Avoidance =
			EntityManager.GetFragmentDataChecked<FGuLiWingmanAvoidanceFragment>(Entity);
		const FGuLiWingmanNavigationGuidanceFragment& Navigation =
			EntityManager.GetFragmentDataChecked<FGuLiWingmanNavigationGuidanceFragment>(Entity);
		bEmergencyAvoid |= RequiresEmergencyAvoidance(Avoidance, Navigation);
		const FTransform& Transform =
			EntityManager.GetFragmentDataChecked<FTransformFragment>(Entity).GetTransform();
		const FGuLiWingmanTuningFragment& Tuning =
			EntityManager.GetFragmentDataChecked<FGuLiWingmanTuningFragment>(Entity);
		bJoiningEscort |= FVector::DistSquared(
			Transform.GetLocation(), Carrier.Transform.GetLocation())
			> FMath::Square(Tuning.Formation.CatchUpDistanceCentimeters);
	}
	if (AliveCount == 0)
	{
		return EGuLiWingmanBehaviorPolicy::Dead;
	}
	if (bOwnerUnavailable)
	{
		return EGuLiWingmanBehaviorPolicy::OwnerUnavailable;
	}
	if (bEmergencyAvoid)
	{
		return EGuLiWingmanBehaviorPolicy::EmergencyAvoid;
	}
	if (bJoiningEscort)
	{
		return EGuLiWingmanBehaviorPolicy::JoiningEscort;
	}
	return EGuLiWingmanBehaviorPolicy::EscortOrbit;
}

bool UGuLiWingmanSimulationSubsystem::ApplyStateTreePolicy(
	const FGuLiWingmanGroupHandle& Group,
	const EGuLiWingmanBehaviorPolicy Policy,
	const float DeltaSeconds)
{
	FGuLiWingmanLocalGroupRuntime* Runtime = OwnedGroups.Find(Group);
	if (!Runtime || !MassEntitySubsystem || !CanOwnSimulation())
	{
		return false;
	}
	FMassEntityManager& EntityManager = MassEntitySubsystem->GetMutableEntityManager();
	const float SafeDeltaSeconds = FMath::Clamp(DeltaSeconds, 0.0f, 0.25f);
	for (const FMassEntityHandle Entity : Runtime->Entities)
	{
		if (!EntityManager.IsEntityValid(Entity))
		{
			return false;
		}
		FGuLiWingmanFlightDynamicsFragment& Dynamics =
			EntityManager.GetFragmentDataChecked<FGuLiWingmanFlightDynamicsFragment>(Entity);
		Dynamics.ModeEvaluationAccumulator = FMath::Fmod(
			Dynamics.ModeEvaluationAccumulator + SafeDeltaSeconds, 0.2f);
		if (!Dynamics.bAlive)
		{
			Dynamics.Mode = EGuLiWingmanFlightMode::Stale;
			continue;
		}
		const FGuLiWingmanCarrierFragment& Carrier =
			EntityManager.GetFragmentDataChecked<FGuLiWingmanCarrierFragment>(Entity);
		const FTransform& Transform =
			EntityManager.GetFragmentDataChecked<FTransformFragment>(Entity).GetTransform();
		const FGuLiWingmanTuningFragment& Tuning =
			EntityManager.GetFragmentDataChecked<FGuLiWingmanTuningFragment>(Entity);
		const FGuLiWingmanAvoidanceFragment& Avoidance =
			EntityManager.GetFragmentDataChecked<FGuLiWingmanAvoidanceFragment>(Entity);
		const FGuLiWingmanNavigationGuidanceFragment& Navigation =
			EntityManager.GetFragmentDataChecked<FGuLiWingmanNavigationGuidanceFragment>(Entity);
        if (Policy != EGuLiWingmanBehaviorPolicy::OwnerUnavailable && Policy != EGuLiWingmanBehaviorPolicy::Dead
            && EntityManager.GetFragmentDataChecked<FGuLiWingmanAttackFragment>(Entity).bGuiding
            && !RequiresEmergencyAvoidance(Avoidance, Navigation))
        { Dynamics.Mode = EGuLiWingmanFlightMode::Follow; continue; }
		const double Distance = FVector::Distance(
			Transform.GetLocation(), Carrier.Transform.GetLocation());
		const auto SelectDistanceMode = [&Carrier, &Tuning, Distance]()
		{
			if (Distance > Tuning.Formation.RecoveryDistanceCentimeters)
			{
				return EGuLiWingmanFlightMode::Recover;
			}
			if (Distance > Tuning.Formation.CatchUpDistanceCentimeters)
			{
				return EGuLiWingmanFlightMode::CatchUp;
			}
			return Carrier.Velocity.SizeSquared() > FMath::Square(100.0f)
				? EGuLiWingmanFlightMode::Follow : EGuLiWingmanFlightMode::Orbit;
		};

		switch (Policy)
		{
		case EGuLiWingmanBehaviorPolicy::Dead:
		case EGuLiWingmanBehaviorPolicy::OwnerUnavailable:
			Dynamics.Mode = EGuLiWingmanFlightMode::Stale;
			break;
		case EGuLiWingmanBehaviorPolicy::EmergencyAvoid:
			// EmergencyAvoid is a group-level StateTree observation. Apply its
			// Recover request only to the member that actually raised the signal;
			// healthy wingmen retain independent escort flight modes.
			Dynamics.Mode = RequiresEmergencyAvoidance(Avoidance, Navigation)
				? EGuLiWingmanFlightMode::Recover : SelectDistanceMode();
			break;
		case EGuLiWingmanBehaviorPolicy::JoiningEscort:
			if (Distance > Tuning.Formation.RecoveryDistanceCentimeters)
			{
				Dynamics.Mode = EGuLiWingmanFlightMode::Recover;
			}
			else if (Distance > Tuning.Formation.CatchUpDistanceCentimeters)
			{
				Dynamics.Mode = EGuLiWingmanFlightMode::CatchUp;
			}
			else
			{
				Dynamics.Mode = EGuLiWingmanFlightMode::Follow;
			}
			break;
		case EGuLiWingmanBehaviorPolicy::EscortOrbit:
		default:
			Dynamics.Mode = Carrier.Velocity.SizeSquared() > FMath::Square(100.0f)
				? EGuLiWingmanFlightMode::Follow
				: EGuLiWingmanFlightMode::Orbit;
			break;
		}
	}
	return true;
}

bool UGuLiWingmanSimulationSubsystem::IsGroupUsingStateTree(const FGuLiWingmanGroupHandle& Group) const
{
	const FGuLiWingmanLocalGroupRuntime* Runtime = OwnedGroups.Find(Group);
	const AGuLiWingmanGroupBehaviorRunner* Runner = Runtime ? Runtime->BehaviorRunner.Get() : nullptr;
	return Runner && Runner->IsUsingStateTree();
}

bool UGuLiWingmanSimulationSubsystem::IsGroupUsingControlledFallback(const FGuLiWingmanGroupHandle& Group) const
{
	const FGuLiWingmanLocalGroupRuntime* Runtime = OwnedGroups.Find(Group);
	const AGuLiWingmanGroupBehaviorRunner* Runner = Runtime ? Runtime->BehaviorRunner.Get() : nullptr;
	return Runner && Runner->IsUsingControlledFallback();
}

bool UGuLiWingmanSimulationSubsystem::IsGroupNavigationCoordinationEnabled(
	const FGuLiWingmanGroupHandle& Group) const
{
	const FGuLiWingmanLocalGroupRuntime* Runtime = OwnedGroups.Find(Group);
	const AGuLiWingmanGroupBehaviorRunner* Runner = Runtime ? Runtime->BehaviorRunner.Get() : nullptr;
	return Runner && Runner->IsNavigationCoordinationTickEnabled();
}

bool UGuLiWingmanSimulationSubsystem::GetNavigationDiagnostics(
	const FGuLiWingmanGroupHandle& Group,
	FGuLiWingmanNavigationDiagnostics& OutDiagnostics) const
{
	OutDiagnostics = FGuLiWingmanNavigationDiagnostics();
	OutDiagnostics.AsyncRequestsIssued = NavigationAsyncRequestsIssued;
	OutDiagnostics.ResultsAccepted = NavigationResultsAccepted;
	OutDiagnostics.SafeFallbackResults = NavigationSafeFallbackResults;
	OutDiagnostics.PendingRequestsCancelled = NavigationPendingRequestsCancelled;
	const FGuLiWingmanLocalGroupRuntime* Runtime = OwnedGroups.Find(Group);
	if (!Runtime)
	{
		return false;
	}
	for (const TSharedPtr<FGuLiWingmanFlightNavigationRuntime>& State : Runtime->FlightNavigation)
	{
		if (!State.IsValid())
		{
			continue;
		}
		OutDiagnostics.PendingFlightRequests += State->bRequestPending ? 1 : 0;
		OutDiagnostics.ActiveFlightPaths +=
			State->PathPoints.IsValidIndex(State->NextPathPointIndex) ? 1 : 0;
	}
	if (MassEntitySubsystem)
	{
		const FMassEntityManager& EntityManager = MassEntitySubsystem->GetEntityManager();
		for (const FMassEntityHandle Entity : Runtime->Entities)
		{
			if (EntityManager.IsEntityValid(Entity)
				&& EntityManager.GetFragmentDataChecked<FGuLiWingmanNavigationGuidanceFragment>(Entity).bHasPath)
			{
				++OutDiagnostics.NavigationGuidedEntities;
			}
		}
	}
	return true;
}

bool UGuLiWingmanSimulationSubsystem::GetAvoidanceDiagnostics(
	const FGuLiWingmanGroupHandle& Group,
	FGuLiWingmanAvoidanceDiagnostics& OutDiagnostics) const
{
	OutDiagnostics = FGuLiWingmanAvoidanceDiagnostics();
	const FGuLiWingmanLocalGroupRuntime* Runtime = OwnedGroups.Find(Group);
	if (!Runtime || !MassEntitySubsystem || !CanOwnSimulation())
	{
		return false;
	}

	TSet<FIntVector> OccupiedCells;
	const FMassEntityManager& EntityManager = MassEntitySubsystem->GetEntityManager();
	for (const FMassEntityHandle Entity : Runtime->Entities)
	{
		if (!EntityManager.IsEntityValid(Entity))
		{
			continue;
		}
		const FGuLiWingmanFlightDynamicsFragment& Dynamics =
			EntityManager.GetFragmentDataChecked<FGuLiWingmanFlightDynamicsFragment>(Entity);
		if (!Dynamics.bAlive)
		{
			continue;
		}
		const FGuLiWingmanAvoidanceFragment& Avoidance =
			EntityManager.GetFragmentDataChecked<FGuLiWingmanAvoidanceFragment>(Entity);
		++OutDiagnostics.EvaluatedEntities;
		OccupiedCells.Add(Avoidance.SpatialCell);
		OutDiagnostics.SeparationContributors += Avoidance.SeparationNeighborCount;
		OutDiagnostics.FullLookAheadBlockedEntities +=
			Avoidance.ConsecutiveBlockedSeconds > 0.0f ? 1 : 0;
		OutDiagnostics.SafeHeadingEntities += Avoidance.bHasSafeDirection ? 1 : 0;
		OutDiagnostics.ControlledRecoveryEntities += Avoidance.bControlledRecovery ? 1 : 0;
		OutDiagnostics.VerifiedRecoveryPointEntities += Avoidance.bHasVerifiedSafePoint ? 1 : 0;
		OutDiagnostics.FlightNavBoundaryThreatEntities +=
			Avoidance.bDetectedFlightNavBoundary ? 1 : 0;
		OutDiagnostics.WorldStaticThreatEntities += Avoidance.bDetectedWorldStatic ? 1 : 0;
		OutDiagnostics.WorldDynamicThreatEntities += Avoidance.bDetectedWorldDynamic ? 1 : 0;
		OutDiagnostics.SpatialNeighborTests += Avoidance.SpatialNeighborTests;
		OutDiagnostics.HeadingProbes += Avoidance.HeadingProbeCount;
	}
	OutDiagnostics.OccupiedSpatialCells = OccupiedCells.Num();
	return true;
}

bool UGuLiWingmanSimulationSubsystem::GetMotionDiagnostics(
	FGuLiWingmanMotionDiagnostics& OutDiagnostics) const
{
	OutDiagnostics = FGuLiWingmanMotionDiagnostics();
	if (!MassEntitySubsystem || !CanOwnSimulation())
	{
		return false;
	}

	double CarrierDistanceSum = 0.0;
	double SpeedSum = 0.0;
	float MinimumCarrierDistance = MAX_flt;
	float MaximumCarrierDistance = 0.0f;
	const FMassEntityManager& EntityManager = MassEntitySubsystem->GetEntityManager();
	for (const TPair<FGuLiWingmanGroupHandle, FGuLiWingmanLocalGroupRuntime>& Pair : OwnedGroups)
	{
		for (const FMassEntityHandle Entity : Pair.Value.Entities)
		{
			if (!EntityManager.IsEntityValid(Entity))
			{
				continue;
			}
			const FTransformFragment& Transform =
				EntityManager.GetFragmentDataChecked<FTransformFragment>(Entity);
			const FGuLiWingmanCarrierFragment& Carrier =
				EntityManager.GetFragmentDataChecked<FGuLiWingmanCarrierFragment>(Entity);
			const FGuLiWingmanFlightDynamicsFragment& Dynamics =
				EntityManager.GetFragmentDataChecked<FGuLiWingmanFlightDynamicsFragment>(Entity);
			++OutDiagnostics.EvaluatedEntities;
			if (!Dynamics.bAlive)
			{
				continue;
			}
			++OutDiagnostics.AliveEntities;
			switch (Dynamics.Mode)
			{
			case EGuLiWingmanFlightMode::Orbit: ++OutDiagnostics.OrbitEntities; break;
			case EGuLiWingmanFlightMode::Follow: ++OutDiagnostics.FollowEntities; break;
			case EGuLiWingmanFlightMode::CatchUp: ++OutDiagnostics.CatchUpEntities; break;
			case EGuLiWingmanFlightMode::Recover: ++OutDiagnostics.RecoverEntities; break;
			case EGuLiWingmanFlightMode::Stale: ++OutDiagnostics.StaleEntities; break;
			default: break;
			}
			const FVector Location = Transform.GetTransform().GetLocation();
			const float CarrierDistance = static_cast<float>(FVector::Distance(
				Location, Carrier.Transform.GetLocation()));
			CarrierDistanceSum += CarrierDistance;
			SpeedSum += Dynamics.Velocity.Size();
			MinimumCarrierDistance = FMath::Min(MinimumCarrierDistance, CarrierDistance);
			MaximumCarrierDistance = FMath::Max(MaximumCarrierDistance, CarrierDistance);
			if (OutDiagnostics.AliveEntities == 1)
			{
				OutDiagnostics.FirstAliveLocation = Location;
			}
		}
	}
	if (OutDiagnostics.AliveEntities > 0)
	{
		const double InverseAliveCount = 1.0 / static_cast<double>(OutDiagnostics.AliveEntities);
		OutDiagnostics.MinimumCarrierDistanceCentimeters = MinimumCarrierDistance;
		OutDiagnostics.MeanCarrierDistanceCentimeters = static_cast<float>(
			CarrierDistanceSum * InverseAliveCount);
		OutDiagnostics.MaximumCarrierDistanceCentimeters = MaximumCarrierDistance;
		OutDiagnostics.MeanSpeedCentimetersPerSecond = static_cast<float>(
			SpeedSum * InverseAliveCount);
	}
	return OutDiagnostics.EvaluatedEntities > 0;
}

#if WITH_DEV_AUTOMATION_TESTS
void UGuLiWingmanSimulationSubsystem::SetGroupBehaviorStateTreeForTests(UStateTree* StateTree)
{
	GroupBehaviorStateTree = StateTree;
}

bool UGuLiWingmanSimulationSubsystem::SetGroupBehaviorPolicyInputsForTests(
	const FGuLiWingmanGroupHandle& Group,
	const FVector& SteeringAcceleration,
	const float ConsecutiveBlockedSeconds,
	const bool bControlledRecovery,
	const bool bDetectedWorldStatic,
	const bool bDetectedWorldDynamic,
	const bool bNavigationSafeFallback)
{
	FGuLiWingmanLocalGroupRuntime* Runtime = OwnedGroups.Find(Group);
	if (!Runtime || !MassEntitySubsystem || SteeringAcceleration.ContainsNaN()
		|| !FMath::IsFinite(ConsecutiveBlockedSeconds) || ConsecutiveBlockedSeconds < 0.0f)
	{
		return false;
	}

	FMassEntityManager& EntityManager = MassEntitySubsystem->GetMutableEntityManager();
	for (const FMassEntityHandle Entity : Runtime->Entities)
	{
		if (!EntityManager.IsEntityValid(Entity))
		{
			return false;
		}
		FGuLiWingmanAvoidanceFragment& Avoidance =
			EntityManager.GetFragmentDataChecked<FGuLiWingmanAvoidanceFragment>(Entity);
		Avoidance.Acceleration = SteeringAcceleration;
		Avoidance.ConsecutiveBlockedSeconds = ConsecutiveBlockedSeconds;
		Avoidance.bControlledRecovery = bControlledRecovery;
		Avoidance.bDetectedWorldStatic = bDetectedWorldStatic;
		Avoidance.bDetectedWorldDynamic = bDetectedWorldDynamic;
		EntityManager.GetFragmentDataChecked<FGuLiWingmanNavigationGuidanceFragment>(Entity)
			.bUsingSafeFallback = bNavigationSafeFallback;
	}
	return true;
}

const AGuLiWingmanGroupBehaviorRunner* UGuLiWingmanSimulationSubsystem::GetBehaviorRunnerForTests(
	const FGuLiWingmanGroupHandle& Group) const
{
	const FGuLiWingmanLocalGroupRuntime* Runtime = OwnedGroups.Find(Group);
	return Runtime ? Runtime->BehaviorRunner.Get() : nullptr;
}

bool UGuLiWingmanSimulationSubsystem::PrimeFlightNavigationStateForTests(
	const FGuLiWingmanGroupHandle& Group,
	const uint8 FlightIndex,
	const TArray<FVector>& PathPoints,
	const bool bPendingRequest)
{
	FGuLiWingmanLocalGroupRuntime* Runtime = OwnedGroups.Find(Group);
	if (!Runtime || FlightIndex >= GULI_WINGMAN_FLIGHT_COUNT
		|| !Runtime->FlightNavigation.IsValidIndex(FlightIndex)
		|| (!PathPoints.IsEmpty() && PathPoints.Num() < 2))
	{
		return false;
	}
	for (const FVector& Point : PathPoints)
	{
		if (Point.ContainsNaN())
		{
			return false;
		}
	}
	TSharedPtr<FGuLiWingmanFlightNavigationRuntime>& StatePtr = Runtime->FlightNavigation[FlightIndex];
	if (!StatePtr.IsValid())
	{
		StatePtr = MakeShared<FGuLiWingmanFlightNavigationRuntime>();
	}
	FGuLiWingmanFlightNavigationRuntime& State = *StatePtr;
	if (State.bRequestPending && State.CancellationToken.IsValid()
		&& !State.CancellationToken->IsCancelled())
	{
		State.CancellationToken->Cancel();
		++NavigationPendingRequestsCancelled;
	}
	uint32 NextSerial = State.RequestSerial + 1u;
	if (NextSerial == 0u)
	{
		NextSerial = 1u;
	}
	State = FGuLiWingmanFlightNavigationRuntime();
		State.RequestSerial = NextSerial;
		State.PendingRequestSerial = bPendingRequest ? NextSerial : 0u;
	State.RequestedAbilitySetRevision = Runtime->AbilityConfig.AbilitySetRevision;
	State.RequestedFormationCommandRevision = Runtime->AbilityConfig.FormationCommandRevision;
	State.PathPoints = PathPoints;
	if (!PathPoints.IsEmpty())
	{
		State.RequestedStart = PathPoints[0];
		State.RequestedGoal = PathPoints.Last();
		State.ActivePathGoal = PathPoints.Last();
		State.NextPathPointIndex = 1;
		PublishFlightNavigationGuidance(*Runtime, FlightIndex, PathPoints[1], PathPoints.Last(),
			State.RequestSerial, 1u);
	}
	else
	{
		ClearFlightNavigationGuidance(*Runtime, FlightIndex, false);
	}
	if (bPendingRequest)
	{
		State.CancellationToken =
			MakeShared<FGuLiFlightNavCancellationToken, ESPMode::ThreadSafe>();
		State.bRequestPending = true;
	}
	return true;
}
#endif

bool UGuLiWingmanSimulationSubsystem::HasOwnedGroup(const FGuLiWingmanGroupHandle& Group) const
{
	return OwnedGroups.Contains(Group);
}

int32 UGuLiWingmanSimulationSubsystem::GetOwnedEntityCount(const FGuLiWingmanGroupHandle& Group) const
{
	const FGuLiWingmanLocalGroupRuntime* Runtime = OwnedGroups.Find(Group);
	return Runtime ? Runtime->Entities.Num() : 0;
}

int32 UGuLiWingmanSimulationSubsystem::GetTotalOwnedEntityCount() const
{
	int32 Total = 0;
	for (const TPair<FGuLiWingmanGroupHandle, FGuLiWingmanLocalGroupRuntime>& Pair : OwnedGroups)
	{
		Total += Pair.Value.Entities.Num();
	}
	return Total;
}
