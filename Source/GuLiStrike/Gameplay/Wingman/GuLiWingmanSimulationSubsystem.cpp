// Copyright Epic Games, Inc. All Rights Reserved.

#include "Gameplay/Wingman/GuLiWingmanSimulationSubsystem.h"

#include "Battle/Relay/GuLiWingmanRelayTypes.h"
#include "Development/GuLiWingmanQAEvidence.h"
#include "Engine/World.h"
#include "Gameplay/Wingman/GuLiWingmanPawn.h"
#include "Gameplay/Wingman/Movement/GuLiWingmanFlightMovementComponent.h"
#include "Gameplay/Wingman/Movement/GuLiWingmanSteering.h"
#include "Gameplay/Wingman/Movement/GuLiWingmanSwarmFlow.h"
#include "GuLiFlightNavigationSubsystem.h"
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

	bool WingmanLess(const AGuLiWingmanPawn& Lhs, const AGuLiWingmanPawn& Rhs)
	{
		const FGuLiWingmanHandle& A = Lhs.GetWingmanHandle();
		const FGuLiWingmanHandle& B = Rhs.GetWingmanHandle();
		if (A.Flight.FlightIndex != B.Flight.FlightIndex)
		{
			return A.Flight.FlightIndex < B.Flight.FlightIndex;
		}
		if (A.MemberIndex != B.MemberIndex)
		{
			return A.MemberIndex < B.MemberIndex;
		}
		return A.EntityGeneration < B.EntityGeneration;
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

	FVector BuildDoubleRingOffset(
		const FGuLiWingmanFormationRuntimeConfig& Formation,
		const int32 StableSlot)
	{
		const int32 InnerCount = static_cast<int32>(Formation.InnerRingSlots);
		const bool bInner = StableSlot < InnerCount;
		const int32 RingIndex = bInner ? StableSlot : StableSlot - InnerCount;
		const int32 RingCount = bInner
			? InnerCount : static_cast<int32>(Formation.OuterRingSlots);
		const float Radius = bInner
			? Formation.InnerRingRadiusCentimeters : Formation.OuterRingRadiusCentimeters;
		const float Height = bInner
			? Formation.InnerRingHeightCentimeters : Formation.OuterRingHeightCentimeters;
		const float Phase = static_cast<float>(RingIndex) * UE_TWO_PI
			/ static_cast<float>(FMath::Max(1, RingCount));
		return FVector(Radius * FMath::Cos(Phase), Radius * FMath::Sin(Phase), Height);
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
		if (!Formation.IsWellFormed())
		{
			return false;
		}
		if (bRequireNavigation && !Navigation)
		{
			return false;
		}

		for (int32 StableSlot = 0; StableSlot < GULI_WINGMAN_GROUP_SIZE; ++StableSlot)
		{
			FGuLiWingmanHandle Handle;
			Handle.Flight.FlightIndex = static_cast<uint8>(
				StableSlot / GULI_WINGMAN_MEMBERS_PER_FLIGHT);
			Handle.MemberIndex = static_cast<uint8>(
				StableSlot % GULI_WINGMAN_MEMBERS_PER_FLIGHT);
			Handle.EntityGeneration = 1u;
			FGuLiWingmanSwarmAgentState Agent;
			GuLiWingmanSwarmFlow::InitializeAgent(Formation, Handle, 1u, Agent);
			const uint32 CandidateCount = Formation.Model == EGuLiWingmanFormationModel::SwarmOrbit
				&& bRequireNavigation ? MaximumInitialSwarmCandidateCount : 1u;
			bool bResolved = false;
			for (uint32 CandidateIndex = 0u; CandidateIndex < CandidateCount; ++CandidateIndex)
			{
				const FVector Offset = Formation.Model == EGuLiWingmanFormationModel::SwarmOrbit
					? GuLiWingmanSwarmFlow::BuildInitialOffset(
						Formation, Handle, Agent, CandidateIndex)
					: BuildDoubleRingOffset(Formation, StableSlot);
				const FVector Position = CarrierTransform.GetLocation() + Offset;
				OutFailedMemberIndex = StableSlot;
				OutFailedPosition = Position;
				OutFailureStatus = Position.ContainsNaN()
					? EGuLiFlightNavSegmentStatus::InvalidData
					: bRequireNavigation
						? Navigation->ValidateEndpointsInSameComponent(
							CarrierTransform.GetLocation(), Position,
							Formation.AgentRadiusCentimeters)
						: EGuLiFlightNavSegmentStatus::Valid;
				if (OutFailureStatus != EGuLiFlightNavSegmentStatus::Valid)
				{
					continue;
				}
				const float MinimumSpacing = FMath::Max(
					Formation.SeparationRadiusCentimeters,
					Formation.AgentRadiusCentimeters * 2.0f);
				if (OutPositions.ContainsByPredicate([&](const FVector& Existing)
				{
					return FVector::DistSquared(Existing, Position)
						< FMath::Square(MinimumSpacing);
				}))
				{
					continue;
				}
				OutPositions.Add(Position);
				bResolved = true;
				break;
			}
			if (!bResolved)
			{
				OutPositions.Reset();
				return false;
			}
		}
		OutFailedMemberIndex = INDEX_NONE;
		OutFailureStatus = EGuLiFlightNavSegmentStatus::Valid;
		return OutPositions.Num() == GULI_WINGMAN_GROUP_SIZE;
	}

	FGuLiWingmanCandidateSample BuildCandidateSample(const AGuLiWingmanPawn& Pawn)
	{
		const FGuLiWingmanRuntimeState& State = Pawn.GetRuntimeState();
		const FTransform Transform = Pawn.GetActorTransform();
		const FRotator Rotation = Transform.Rotator().GetNormalized();
		FGuLiWingmanCandidateSample Sample;
		Sample.Wingman = State.Identity.Handle;
		Sample.PositionCentimeters =
			GuLiWingmanProtocol::QuantizePositionCentimeters(Transform.GetLocation());
		Sample.VelocityCentimetersPerSecond = FIntVector(
			QuantizeToInt32(State.Dynamics.Velocity.X),
			QuantizeToInt32(State.Dynamics.Velocity.Y),
			QuantizeToInt32(State.Dynamics.Velocity.Z));
		Sample.RotationCentiDegrees = FIntVector(
			QuantizeToInt32(Rotation.Pitch * 100.0),
			QuantizeToInt32(Rotation.Yaw * 100.0),
			QuantizeToInt32(Rotation.Roll * 100.0));
		Sample.FlightMode = static_cast<uint8>(State.Dynamics.Mode);
		return Sample;
	}
}

UGuLiWingmanSimulationSubsystem::UGuLiWingmanSimulationSubsystem()
{
	MemberBehaviorStateTree = TSoftObjectPtr<UStateTree>(FSoftObjectPath(
		TEXT("/Game/GuLiStrike/Wingman/ST_WingmanMemberBehavior.ST_WingmanMemberBehavior")));
	DefaultWingmanMesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(
		TEXT("/Game/GuLiStrike/Wingman/SM_Wingman_Mass.SM_Wingman_Mass")));
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
}

void UGuLiWingmanSimulationSubsystem::Deinitialize()
{
	DestroyAllOwnedGroups();
	Super::Deinitialize();
}

bool UGuLiWingmanSimulationSubsystem::CanOwnSimulation() const
{
	const UWorld* World = GetWorld();
	return World && World->IsGameWorld()
		&& IsOwnerSimulationNetMode(World->GetNetMode());
}

bool UGuLiWingmanSimulationSubsystem::IsOwnerSimulationNetMode(const ENetMode NetMode)
{
	return NetMode == NM_Client || NetMode == NM_Standalone || NetMode == NM_ListenServer;
}

UStateTree* UGuLiWingmanSimulationSubsystem::ResolveMemberBehaviorStateTree() const
{
	return MemberBehaviorStateTree.IsNull() ? nullptr : MemberBehaviorStateTree.LoadSynchronous();
}

UStaticMesh* UGuLiWingmanSimulationSubsystem::ResolveWingmanMesh() const
{
	return DefaultWingmanMesh.IsNull() ? nullptr : DefaultWingmanMesh.LoadSynchronous();
}

bool UGuLiWingmanSimulationSubsystem::CreateOrResetOwnedGroup(
	const FGuLiWingmanGroupHandle& Group,
	const FGuLiGroupAbilityConfigSnapshot& AbilityConfig,
	const FTransform& CarrierTransform,
	const FVector& CarrierVelocity,
	const FGuLiCarrierSourceRef& CarrierSource,
	AActor* CarrierActor)
{
	if (!CanOwnSimulation() || !Group.IsValid()
		|| !AbilityConfig.IsUsableByLeaseOwner() || !ConfigMatchesGroup(AbilityConfig, Group)
		|| CarrierTransform.ContainsNaN() || CarrierVelocity.ContainsNaN()
		|| !CarrierSource.IsValid())
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
	if (!BuildInitialFormationPositions(Navigation, !bBypassNavigation,
		CarrierTransform, AbilityConfig.FormationRuntime, InitialPositions,
		FailedMemberIndex, FailedPosition, FailureStatus))
	{
		if (!InitialFormationNavigationFailuresLogged.Contains(Group))
		{
			InitialFormationNavigationFailuresLogged.Add(Group);
			UE_LOG(LogTemp, Warning,
				TEXT("[GULI_WINGMAN_INITIAL_NAV_GATE] Ship=%s ShipGen=%u GroupGen=%u "
					"FailedMember=%d Status=%s(%d) Position=%s"),
				*Group.ShipInstanceId.ToString(), Group.ShipGeneration,
				Group.GroupGeneration, FailedMemberIndex,
				GetFlightNavSegmentStatusName(FailureStatus),
				static_cast<int32>(FailureStatus), *FailedPosition.ToCompactString());
		}
		return false;
	}
	InitialFormationNavigationFailuresLogged.Remove(Group);

	if (FGuLiWingmanLocalGroupRuntime* Existing = OwnedGroups.Find(Group))
	{
		if (Existing->Pawns.Num() == GULI_WINGMAN_GROUP_SIZE
			&& Existing->AbilityConfig.HasSameVersion(AbilityConfig))
		{
			return UpdateOwnedGroupCarrier(
				Group, CarrierTransform, CarrierVelocity, CarrierSource, CarrierActor);
		}
		DestroyOwnedGroup(Group);
	}

	UStateTree* StateTree = ResolveMemberBehaviorStateTree();
	UStaticMesh* Mesh = ResolveWingmanMesh();
#if WITH_DEV_AUTOMATION_TESTS
	const bool bCanBypassBehaviorAsset = bAllowBehaviorAssetBypassForTests;
#else
	const bool bCanBypassBehaviorAsset = false;
#endif
	if (!StateTree && !bCanBypassBehaviorAsset)
	{
		UE_LOG(LogTemp, Error, TEXT("Wingman native member StateTree failed to load: %s"),
			*MemberBehaviorStateTree.ToSoftObjectPath().ToString());
		return false;
	}

	FGuLiWingmanLocalGroupRuntime NewRuntime;
	NewRuntime.AbilityConfig = AbilityConfig;
	NewRuntime.bCombatAuthorizationValid = true;
	NewRuntime.CarrierActor = CarrierActor;
	NewRuntime.Pawns.Reserve(GULI_WINGMAN_GROUP_SIZE);
	NewRuntime.FlightNavigation.Reserve(GULI_WINGMAN_FLIGHT_COUNT);
	for (uint8 FlightIndex = 0u; FlightIndex < GULI_WINGMAN_FLIGHT_COUNT; ++FlightIndex)
	{
		NewRuntime.FlightNavigation.Add(MakeShared<FGuLiWingmanFlightNavigationRuntime>());
	}

	UWorld* World = GetWorld();
	for (int32 StableSlot = 0; StableSlot < GULI_WINGMAN_GROUP_SIZE; ++StableSlot)
	{
		FActorSpawnParameters SpawnParameters;
		SpawnParameters.ObjectFlags |= RF_Transient;
		SpawnParameters.SpawnCollisionHandlingOverride =
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		SpawnParameters.Name = MakeUniqueObjectName(World->PersistentLevel,
			AGuLiWingmanPawn::StaticClass(), TEXT("WingmanPawn"));
		AGuLiWingmanPawn* Pawn = World->SpawnActor<AGuLiWingmanPawn>(
			AGuLiWingmanPawn::StaticClass(),
			FTransform(FRotator::ZeroRotator, InitialPositions[StableSlot]),
			SpawnParameters);
		if (!Pawn || !InitializeOwnedPawn(*Pawn, Group, StableSlot, AbilityConfig,
			InitialPositions[StableSlot], CarrierTransform, CarrierVelocity, CarrierSource,
			CarrierActor))
		{
			if (Pawn)
			{
				Pawn->Destroy();
			}
			for (const TWeakObjectPtr<AGuLiWingmanPawn>& Spawned : NewRuntime.Pawns)
			{
				if (AGuLiWingmanPawn* SpawnedPawn = Spawned.Get())
				{
					SpawnedPawn->Destroy();
				}
			}
			return false;
		}
		Pawn->ConfigureMesh(Mesh);
		NewRuntime.Pawns.Add(Pawn);
	}
	OwnedGroups.Add(Group, MoveTemp(NewRuntime));
	return true;
}

bool UGuLiWingmanSimulationSubsystem::InitializeOwnedPawn(
	AGuLiWingmanPawn& Pawn,
	const FGuLiWingmanGroupHandle& Group,
	const int32 StableSlot,
	const FGuLiGroupAbilityConfigSnapshot& AbilityConfig,
	const FVector& InitialPosition,
	const FTransform& CarrierTransform,
	const FVector& CarrierVelocity,
	const FGuLiCarrierSourceRef& CarrierSource,
	AActor* CarrierActor)
{
	if (StableSlot < 0 || StableSlot >= GULI_WINGMAN_GROUP_SIZE)
	{
		return false;
	}
	FGuLiWingmanRuntimeState State;
	State.Identity.Handle.Flight.Group = Group;
	State.Identity.Handle.Flight.FlightIndex = static_cast<uint8>(
		StableSlot / GULI_WINGMAN_MEMBERS_PER_FLIGHT);
	State.Identity.Handle.MemberIndex = static_cast<uint8>(
		StableSlot % GULI_WINGMAN_MEMBERS_PER_FLIGHT);
	State.Identity.Handle.EntityGeneration = 1u;
	State.Identity.WingmanTypeId = AbilityConfig.WingmanTypeId;
	State.Ability.AbilitySetRevision = AbilityConfig.AbilitySetRevision;
	State.Ability.LoadoutRevision = AbilityConfig.LoadoutRevision;
	State.Ability.FormationCommandRevision = AbilityConfig.FormationCommandRevision;
	State.Ability.FormationDefinitionChecksum = AbilityConfig.FormationDefinitionChecksum;
	State.Tuning.Formation = AbilityConfig.FormationRuntime;
	State.Tuning.BasicWeapon = AbilityConfig.BasicWeaponRuntime;
	State.Carrier.Transform = CarrierTransform;
	State.Carrier.Velocity = CarrierVelocity;
	State.Carrier.Source = CarrierSource;
	GuLiWingmanSwarmFlow::InitializeAgent(
		AbilityConfig.FormationRuntime, State.Identity.Handle,
		FMath::Max(1u, AbilityConfig.EffectiveClientSimTick), State.SwarmAgent);

	FVector InitialVelocity = FVector::ZeroVector;
	FVector Forward = FVector::ForwardVector;
	if (AbilityConfig.FormationRuntime.Model == EGuLiWingmanFormationModel::DoubleRingLegacy)
	{
		const int32 InnerCount = static_cast<int32>(AbilityConfig.FormationRuntime.InnerRingSlots);
		const bool bInner = StableSlot < InnerCount;
		const int32 RingIndex = bInner ? StableSlot : StableSlot - InnerCount;
		const int32 RingCount = bInner ? InnerCount
			: static_cast<int32>(AbilityConfig.FormationRuntime.OuterRingSlots);
		State.FormationSlot.RadiusCentimeters = bInner
			? AbilityConfig.FormationRuntime.InnerRingRadiusCentimeters
			: AbilityConfig.FormationRuntime.OuterRingRadiusCentimeters;
		State.FormationSlot.HeightCentimeters = bInner
			? AbilityConfig.FormationRuntime.InnerRingHeightCentimeters
			: AbilityConfig.FormationRuntime.OuterRingHeightCentimeters;
		State.FormationSlot.PhaseRadians = static_cast<float>(RingIndex) * UE_TWO_PI
			/ static_cast<float>(FMath::Max(1, RingCount));
		State.FormationSlot.AngularSpeedRadiansPerSecond = bInner
			? AbilityConfig.FormationRuntime.InnerAngularSpeedRadiansPerSecond
			: AbilityConfig.FormationRuntime.OuterAngularSpeedRadiansPerSecond;
		State.FormationSlot.bClockwise = bInner;
		const float DirectionSign = bInner ? -1.0f : 1.0f;
		Forward = (DirectionSign * FVector(
			-FMath::Sin(State.FormationSlot.PhaseRadians),
			FMath::Cos(State.FormationSlot.PhaseRadians), 0.0f)).GetSafeNormal();
		InitialVelocity = Forward
			* AbilityConfig.FormationRuntime.CruiseSpeedCentimetersPerSecond;
	}
	else
	{
		InitialVelocity = GuLiWingmanSwarmFlow::BuildPreferredVelocity(
			InitialPosition, CarrierVelocity, CarrierTransform.GetLocation(),
			CarrierVelocity, State.SwarmAgent, AbilityConfig.FormationRuntime,
			EGuLiWingmanFlightMode::Orbit);
		Forward = InitialVelocity.GetSafeNormal();
	}
	if (Forward.IsNearlyZero())
	{
		Forward = FVector::ForwardVector;
	}
	State.Guidance.bUsesVelocityField =
		AbilityConfig.FormationRuntime.Model == EGuLiWingmanFormationModel::SwarmOrbit;
	State.Guidance.PreferredVelocity = InitialVelocity;
	State.Guidance.DesiredPosition = InitialPosition;
	State.Guidance.DesiredForward = Forward;
	State.Guidance.DesiredSpeedCentimetersPerSecond = FMath::Clamp(
		static_cast<float>(InitialVelocity.Size()),
		AbilityConfig.FormationRuntime.MinimumSpeedCentimetersPerSecond,
		AbilityConfig.FormationRuntime.CruiseSpeedCentimetersPerSecond);
	State.Dynamics.Velocity = InitialVelocity;
	State.Dynamics.CaptureSimulationTick = FMath::Max(
		1u, AbilityConfig.EffectiveClientSimTick);
	State.Dynamics.Mode = EGuLiWingmanFlightMode::Orbit;
	State.Dynamics.bAlive = true;
	State.Attack.EntityGeneration = 1u;
	State.Avoidance.LastVerifiedSafePoint = InitialPosition;
	State.Avoidance.bHasVerifiedSafePoint = true;

	Pawn.SetActorTransform(FTransform(Forward.Rotation(), InitialPosition),
		false, nullptr, ETeleportType::TeleportPhysics);
	const bool bInitialized = Pawn.InitializeOwnerSimulation(
		State, ResolveMemberBehaviorStateTree(), ResolveWingmanMesh());
	if (bInitialized && Pawn.GetFlightMovement())
	{
		Pawn.GetFlightMovement()->SetCarrierActor(CarrierActor);
	}
	return bInitialized;
}

bool UGuLiWingmanSimulationSubsystem::UpdateOwnedGroupCarrier(
	const FGuLiWingmanGroupHandle& Group,
	const FTransform& CarrierTransform,
	const FVector& CarrierVelocity,
	const FGuLiCarrierSourceRef& CarrierSource,
	AActor* CarrierActor)
{
	FGuLiWingmanLocalGroupRuntime* Runtime = OwnedGroups.Find(Group);
	if (!Runtime || CarrierTransform.ContainsNaN() || CarrierVelocity.ContainsNaN())
	{
		return false;
	}
	Runtime->CarrierActor = CarrierActor;
	for (const TWeakObjectPtr<AGuLiWingmanPawn>& Entry : Runtime->Pawns)
	{
		AGuLiWingmanPawn* Pawn = Entry.Get();
		if (!Pawn)
		{
			return false;
		}
		FGuLiWingmanCarrierState& Carrier = Pawn->GetMutableRuntimeState().Carrier;
		Carrier.Transform = CarrierTransform;
		Carrier.Velocity = CarrierVelocity;
		// The local Ship transform is available even during a transient canonical
		// source gap or a network disconnect. Keep the last publishable source for
		// later relay recovery, while flight follows the live local Ship every frame.
		if (CarrierSource.IsValid())
		{
			Carrier.Source = CarrierSource;
		}
		if (Pawn->GetFlightMovement())
		{
			Pawn->GetFlightMovement()->SetCarrierActor(CarrierActor);
		}
	}
	return true;
}

bool UGuLiWingmanSimulationSubsystem::ApplyCommittedAbilityConfig(
	const FGuLiWingmanGroupHandle& Group,
	const FGuLiGroupAbilityConfigSnapshot& AbilityConfig)
{
	FGuLiWingmanLocalGroupRuntime* Runtime = OwnedGroups.Find(Group);
	if (!Runtime || !AbilityConfig.IsUsableByLeaseOwner()
		|| !ConfigMatchesGroup(AbilityConfig, Group)
		|| (Runtime->AbilityConfig.IsUsableByLeaseOwner()
			&& Runtime->AbilityConfig.WingmanTypeId != AbilityConfig.WingmanTypeId))
	{
		return false;
	}
	if (Runtime->AbilityConfig.HasSameVersion(AbilityConfig))
	{
		Runtime->bCombatAuthorizationValid = true;
		return true;
	}

	const FGuLiGroupAbilityConfigSnapshot PreviousConfig = Runtime->AbilityConfig;
	const bool bFormationChanged =
		FormationProjectionHash(PreviousConfig) != FormationProjectionHash(AbilityConfig);
	if (bFormationChanged)
	{
		CancelNavigationForRuntime(*Runtime, true);
	}
	const double NowSeconds = GetWorld()
		? static_cast<double>(GetWorld()->GetTimeSeconds()) : 0.0;
	for (const TWeakObjectPtr<AGuLiWingmanPawn>& Entry : Runtime->Pawns)
	{
		AGuLiWingmanPawn* Pawn = Entry.Get();
		if (!Pawn)
		{
			return false;
		}
		FGuLiWingmanRuntimeState& State = Pawn->GetMutableRuntimeState();
		State.Ability.AbilitySetRevision = AbilityConfig.AbilitySetRevision;
		State.Ability.LoadoutRevision = AbilityConfig.LoadoutRevision;
		State.Ability.FormationCommandRevision = AbilityConfig.FormationCommandRevision;
		State.Ability.FormationDefinitionChecksum = AbilityConfig.FormationDefinitionChecksum;
		State.Tuning.Formation = AbilityConfig.FormationRuntime;
		State.Tuning.BasicWeapon = AbilityConfig.BasicWeaponRuntime;
		State.Identity.WingmanTypeId = AbilityConfig.WingmanTypeId;
		if (bFormationChanged)
		{
			GuLiWingmanSwarmFlow::InitializeAgent(
				AbilityConfig.FormationRuntime, State.Identity.Handle,
				FMath::Max(1u, State.Dynamics.CaptureSimulationTick), State.SwarmAgent);
			const int32 StableSlot = State.Identity.Handle.GetGroupMemberIndex();
			const int32 InnerCount = static_cast<int32>(
				AbilityConfig.FormationRuntime.InnerRingSlots);
			const bool bInner = StableSlot < InnerCount;
			State.FormationSlot.RadiusCentimeters = bInner
				? AbilityConfig.FormationRuntime.InnerRingRadiusCentimeters
				: AbilityConfig.FormationRuntime.OuterRingRadiusCentimeters;
			State.FormationSlot.HeightCentimeters = bInner
				? AbilityConfig.FormationRuntime.InnerRingHeightCentimeters
				: AbilityConfig.FormationRuntime.OuterRingHeightCentimeters;
			State.FormationSlot.AngularSpeedRadiansPerSecond = bInner
				? AbilityConfig.FormationRuntime.InnerAngularSpeedRadiansPerSecond
				: AbilityConfig.FormationRuntime.OuterAngularSpeedRadiansPerSecond;
			State.FormationSlot.bClockwise = bInner;
			State.Weapon.SourceAcceptedState = FGuLiAcceptedStateRef{};
			State.Weapon.SourceAcceptedAbilitySetRevision = 0u;
			State.Weapon.SourceAcceptedFormationCommandRevision = 0u;
			State.Weapon.SourceAcceptedFormationDefinitionChecksum = 0u;
		}

		for (const FGuLiWingmanWeaponChannelConfig& Channel : AbilityConfig.WeaponChannels)
		{
			if (!Channel.bEnabled || Channel.Kind != EGuLiWingmanWeaponKind::BasicAutomatic)
			{
				continue;
			}
			const FGuLiWingmanWeaponChannelConfig* Previous =
				PreviousConfig.FindWeaponChannel(Channel.Binding);
			double* ExistingNext = State.Weapon.FindNextFireSeconds(Channel.Binding.SlotId);
			if (!Previous || !Previous->bEnabled)
			{
				State.Weapon.SetNextFireSeconds(
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
					(*ExistingNext - NowSeconds)
						/ Previous->Runtime.CooldownSeconds, 0.0, 1.0);
				*ExistingNext = NowSeconds
					+ RemainingRatio * Channel.Runtime.CooldownSeconds;
			}
		}
	}
	Runtime->AbilityConfig = AbilityConfig;
	Runtime->bCombatAuthorizationValid = true;
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
	if (!Runtime || Roster.Num() != GULI_WINGMAN_GROUP_SIZE
		|| Runtime->Pawns.Num() != GULI_WINGMAN_GROUP_SIZE)
	{
		return false;
	}
	TStaticArray<const FGuLiWingmanRosterEntry*, GULI_WINGMAN_GROUP_SIZE> BySlot{};
	for (const FGuLiWingmanRosterEntry& Entry : Roster)
	{
		const int32 StableSlot = Entry.Wingman.GetGroupMemberIndex();
		if (!Entry.IsWellFormed(Group)
			|| Entry.WingmanTypeId != Runtime->AbilityConfig.WingmanTypeId
			|| StableSlot < 0 || StableSlot >= GULI_WINGMAN_GROUP_SIZE
			|| BySlot[StableSlot] != nullptr)
		{
			return false;
		}
		BySlot[StableSlot] = &Entry;
	}

	for (int32 StableSlot = 0; StableSlot < GULI_WINGMAN_GROUP_SIZE; ++StableSlot)
	{
		AGuLiWingmanPawn* Pawn = Runtime->Pawns[StableSlot].Get();
		const FGuLiWingmanRosterEntry* RosterEntry = BySlot[StableSlot];
		if (!Pawn || !RosterEntry)
		{
			return false;
		}
		FGuLiWingmanRuntimeState& State = Pawn->GetMutableRuntimeState();
		if (State.Identity.Handle != RosterEntry->Wingman)
		{
			const FTransform PreservedTransform = Pawn->GetActorTransform();
			const FVector PreservedVelocity = State.Dynamics.Velocity;
			const uint32 PreservedTick = State.Dynamics.CaptureSimulationTick;
			State.Identity.Handle = RosterEntry->Wingman;
			State.Identity.WingmanTypeId = RosterEntry->WingmanTypeId;
			State.Attack = FGuLiWingmanAttackRunState{};
			State.Attack.EntityGeneration = RosterEntry->Wingman.EntityGeneration;
			State.Weapon = FGuLiWingmanWeaponState{};
			// A replacement generation owns a fresh fire-sequence and cooldown domain.
			// Keep the stable-slot pose, but never let the new member inherit the dead
			// member's readiness or fire immediately before one complete channel interval.
			const double NowSeconds = GetWorld()
				? static_cast<double>(GetWorld()->GetTimeSeconds()) : 0.0;
			for (const FGuLiWingmanWeaponChannelConfig& Channel : Runtime->AbilityConfig.WeaponChannels)
			{
				if (Channel.bEnabled && Channel.Kind == EGuLiWingmanWeaponKind::BasicAutomatic)
				{
					State.Weapon.SetNextFireSeconds(
						Channel.Binding.SlotId, NowSeconds + Channel.Runtime.CooldownSeconds);
				}
			}
			State.Avoidance = FGuLiWingmanAvoidanceState{};
			State.Avoidance.LastVerifiedSafePoint = PreservedTransform.GetLocation();
			State.Avoidance.bHasVerifiedSafePoint = true;
			State.Dynamics.CaptureSimulationTick = PreservedTick;
			State.Dynamics.Velocity = PreservedVelocity;
			GuLiWingmanSwarmFlow::InitializeAgent(
				Runtime->AbilityConfig.FormationRuntime, RosterEntry->Wingman,
				PreservedTick, State.SwarmAgent);
			Runtime->PendingAttackShots.RemoveAll([&](const auto& Pair)
			{
				return Pair.Key == RosterEntry->Wingman.Flight.FlightIndex
					&& Pair.Value.MemberIndex == RosterEntry->Wingman.MemberIndex;
			});
		}
		Pawn->SetAlive(!RosterEntry->bDead);
	}
	return true;
}

bool UGuLiWingmanSimulationSubsystem::InvalidateOwnedGroupAbilities(
	const FGuLiWingmanGroupHandle& Group)
{
	FGuLiWingmanLocalGroupRuntime* Runtime = OwnedGroups.Find(Group);
	if (!Runtime)
	{
		return false;
	}
	// Revoking combat authorization must not revoke the owning client's flight
	// controller. Preserve the last known-good formation and navigation tuning so
	// all live Pawns keep moving while reliable combat state catches up.
	Runtime->bCombatAuthorizationValid = false;
	Runtime->PendingAttackShots.Reset();
	for (const TWeakObjectPtr<AGuLiWingmanPawn>& Entry : Runtime->Pawns)
	{
		if (AGuLiWingmanPawn* Pawn = Entry.Get())
		{
			Pawn->GetMutableRuntimeState().Attack = FGuLiWingmanAttackRunState{};
			Pawn->GetMutableRuntimeState().Weapon = FGuLiWingmanWeaponState{};
		}
	}
	return true;
}

bool UGuLiWingmanSimulationSubsystem::DestroyOwnedGroup(
	const FGuLiWingmanGroupHandle& Group)
{
	FGuLiWingmanLocalGroupRuntime* Existing = OwnedGroups.Find(Group);
	if (!Existing)
	{
		return false;
	}
	CancelNavigationForRuntime(*Existing, true);
	FGuLiWingmanLocalGroupRuntime Runtime;
	if (!OwnedGroups.RemoveAndCopyValue(Group, Runtime))
	{
		return false;
	}
	for (const TWeakObjectPtr<AGuLiWingmanPawn>& Entry : Runtime.Pawns)
	{
		if (AGuLiWingmanPawn* Pawn = Entry.Get())
		{
			Pawn->Destroy();
		}
	}
	return true;
}

void UGuLiWingmanSimulationSubsystem::DestroyAllOwnedGroups()
{
	TArray<FGuLiWingmanGroupHandle> Groups;
	OwnedGroups.GetKeys(Groups);
	for (const FGuLiWingmanGroupHandle& Group : Groups)
	{
		DestroyOwnedGroup(Group);
	}
	OwnedGroups.Reset();
}

bool UGuLiWingmanSimulationSubsystem::AdvanceOwnedGroup(
	const FGuLiWingmanGroupHandle& Group,
	const float DeltaSeconds)
{
	FGuLiWingmanLocalGroupRuntime* Runtime = OwnedGroups.Find(Group);
	if (!Runtime || !CanOwnSimulation()
		|| !Runtime->AbilityConfig.IsUsableByLeaseOwner()
		|| !FMath::IsFinite(DeltaSeconds) || DeltaSeconds < 0.0f)
	{
		return false;
	}
	TickNavigationBehavior(Group, DeltaSeconds);

	TArray<AGuLiWingmanPawn*, TInlineAllocator<GULI_WINGMAN_GROUP_SIZE>> SortedPawns;
	for (const TWeakObjectPtr<AGuLiWingmanPawn>& Entry : Runtime->Pawns)
	{
		AGuLiWingmanPawn* Pawn = Entry.Get();
		if (!Pawn)
		{
			return false;
		}
		SortedPawns.Add(Pawn);
	}
	SortedPawns.Sort([](const AGuLiWingmanPawn& Lhs, const AGuLiWingmanPawn& Rhs)
	{
		return WingmanLess(Lhs, Rhs);
	});
	bool bBypassNavigation = false;
#if WITH_DEV_AUTOMATION_TESTS
	bBypassNavigation = bBypassNavigationRequirementForTests;
#endif
	for (AGuLiWingmanPawn* Pawn : SortedPawns)
	{
		Pawn->UpdateBehaviorObservation(DeltaSeconds);
		if (UGuLiWingmanFlightMovementComponent* Movement = Pawn->GetFlightMovement())
		{
			Movement->AdvanceFixedSteps(DeltaSeconds, bBypassNavigation);
		}
	}
	return true;
}

AGuLiWingmanPawn* UGuLiWingmanSimulationSubsystem::FindOwnedPawn(
	const FGuLiWingmanLocalGroupRuntime& Runtime,
	const FGuLiWingmanHandle& Wingman) const
{
	if (!Wingman.IsValid())
	{
		return nullptr;
	}
	for (const TWeakObjectPtr<AGuLiWingmanPawn>& Entry : Runtime.Pawns)
	{
		if (AGuLiWingmanPawn* Pawn = Entry.Get();
			Pawn && Pawn->GetWingmanHandle() == Wingman)
		{
			return Pawn;
		}
	}
	return nullptr;
}

AGuLiWingmanPawn* UGuLiWingmanSimulationSubsystem::FindOwnedPawn(
	const FGuLiWingmanHandle& Wingman) const
{
	const FGuLiWingmanLocalGroupRuntime* Runtime = OwnedGroups.Find(Wingman.Flight.Group);
	return Runtime ? FindOwnedPawn(*Runtime, Wingman) : nullptr;
}

int32 UGuLiWingmanSimulationSubsystem::GetOwnedPawnCount(
	const FGuLiWingmanGroupHandle& Group) const
{
	const FGuLiWingmanLocalGroupRuntime* Runtime = OwnedGroups.Find(Group);
	if (!Runtime)
	{
		return 0;
	}
	int32 Count = 0;
	for (const TWeakObjectPtr<AGuLiWingmanPawn>& Pawn : Runtime->Pawns)
	{
		Count += Pawn.IsValid() ? 1 : 0;
	}
	return Count;
}

int32 UGuLiWingmanSimulationSubsystem::GetTotalOwnedPawnCount() const
{
	int32 Count = 0;
	for (const TPair<FGuLiWingmanGroupHandle, FGuLiWingmanLocalGroupRuntime>& Pair : OwnedGroups)
	{
		for (const TWeakObjectPtr<AGuLiWingmanPawn>& Pawn : Pair.Value.Pawns)
		{
			Count += Pawn.IsValid() ? 1 : 0;
		}
	}
	return Count;
}

bool UGuLiWingmanSimulationSubsystem::IsOwnedGroupCombatAuthorizationValid(
	const FGuLiWingmanGroupHandle& Group) const
{
	const FGuLiWingmanLocalGroupRuntime* Runtime = OwnedGroups.Find(Group);
	return Runtime && Runtime->bCombatAuthorizationValid;
}

bool UGuLiWingmanSimulationSubsystem::BuildCandidate(
	const FGuLiWingmanGroupHandle& Group,
	const uint32 MatchEpoch,
	const uint32 LeaseEpoch,
	const uint32 CandidateSequence,
	const uint32 ClientSimTick,
	FGuLiWingmanCandidateBatch& OutCandidate) const
{
	OutCandidate = FGuLiWingmanCandidateBatch{};
	const FGuLiWingmanLocalGroupRuntime* Runtime = OwnedGroups.Find(Group);
	if (!Runtime || !Runtime->AbilityConfig.IsUsableByLeaseOwner()
		|| MatchEpoch == 0u || LeaseEpoch == 0u
		|| CandidateSequence == 0u || ClientSimTick == 0u)
	{
		return false;
	}
	OutCandidate.MatchEpoch = MatchEpoch;
	OutCandidate.Group = Group;
	OutCandidate.LeaseEpoch = LeaseEpoch;
	OutCandidate.CandidateSequence = CandidateSequence;
	OutCandidate.ClientSimTick = ClientSimTick;
	OutCandidate.AbilitySetRevision = Runtime->AbilityConfig.AbilitySetRevision;
	OutCandidate.FormationCommandRevision = Runtime->AbilityConfig.FormationCommandRevision;
	OutCandidate.FormationDefinitionChecksum = Runtime->AbilityConfig.FormationDefinitionChecksum;
	for (const TWeakObjectPtr<AGuLiWingmanPawn>& Entry : Runtime->Pawns)
	{
		const AGuLiWingmanPawn* Pawn = Entry.Get();
		if (!Pawn)
		{
			return false;
		}
		const FGuLiWingmanRuntimeState& State = Pawn->GetRuntimeState();
		if (!State.Dynamics.bAlive)
		{
			continue;
		}
		if (!OutCandidate.CarrierSource.IsValid())
		{
			OutCandidate.CarrierSource = State.Carrier.Source;
		}
		if (OutCandidate.CarrierSource.CanonicalEpoch != State.Carrier.Source.CanonicalEpoch
			|| OutCandidate.CarrierSource.MoveRevision != State.Carrier.Source.MoveRevision)
		{
			return false;
		}
		OutCandidate.Samples.Add(BuildCandidateSample(*Pawn));
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
	if (!Runtime || !Runtime->AbilityConfig.IsUsableByLeaseOwner()
		|| MatchEpoch == 0u || LeaseEpoch == 0u || ConnectionGeneration == 0u
		|| RosterRevision == 0u || FlightIndex >= GULI_WINGMAN_FLIGHT_COUNT
		|| RequiredMemberMask == 0u
		|| RequiredMemberMask >= (1u << GULI_WINGMAN_MEMBERS_PER_FLIGHT)
		|| ObservedGrantRevision == 0u || CandidateSequence == 0u
		|| FrameSequence == 0u || ClientSimTick == 0u
		|| !FMath::IsFinite(CaptureEstimatedServerTimeSeconds)
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

	for (const TWeakObjectPtr<AGuLiWingmanPawn>& Entry : Runtime->Pawns)
	{
		const AGuLiWingmanPawn* Pawn = Entry.Get();
		if (!Pawn)
		{
			return false;
		}
		const FGuLiWingmanRuntimeState& State = Pawn->GetRuntimeState();
		const FGuLiWingmanHandle& Handle = State.Identity.Handle;
		if (Handle.Flight.FlightIndex != FlightIndex
			|| (RequiredMemberMask & (1u << Handle.MemberIndex)) == 0u
			|| !State.Dynamics.bAlive)
		{
			continue;
		}
		if (!OutCandidate.CarrierSource.IsValid())
		{
			OutCandidate.CarrierSource = State.Carrier.Source;
		}
		if (OutCandidate.CarrierSource.CanonicalEpoch != State.Carrier.Source.CanonicalEpoch
			|| OutCandidate.CarrierSource.MoveRevision != State.Carrier.Source.MoveRevision)
		{
			return false;
		}
		OutCandidate.Samples.Add(BuildCandidateSample(*Pawn));
	}
	return OutCandidate.IsWellFormed();
}

bool UGuLiWingmanSimulationSubsystem::ApplyAcceptedBatch(
	const FGuLiWingmanAcceptedBatch& AcceptedBatch)
{
	FGuLiWingmanLocalGroupRuntime* Runtime = OwnedGroups.Find(AcceptedBatch.Group);
	if (!Runtime || !AcceptedBatch.IsWellFormed()
		|| !Runtime->AbilityConfig.IsUsableByLeaseOwner()
		|| AcceptedBatch.AbilitySetRevision != Runtime->AbilityConfig.AbilitySetRevision
		|| AcceptedBatch.FormationCommandRevision
			!= Runtime->AbilityConfig.FormationCommandRevision
		|| AcceptedBatch.FormationDefinitionChecksum
			!= Runtime->AbilityConfig.FormationDefinitionChecksum)
	{
		return false;
	}

	TArray<AGuLiWingmanPawn*, TInlineAllocator<GULI_WINGMAN_MEMBERS_PER_FLIGHT>> Pawns;
	for (const FGuLiWingmanCandidateSample& Sample : AcceptedBatch.Samples)
	{
		AGuLiWingmanPawn* Pawn = FindOwnedPawn(*Runtime, Sample.Wingman);
		if (!Pawn)
		{
			return false;
		}
		Pawns.Add(Pawn);
	}
	for (int32 Index = 0; Index < AcceptedBatch.Samples.Num(); ++Index)
	{
		AGuLiWingmanPawn* Pawn = Pawns[Index];
		FGuLiWingmanRuntimeState& State = Pawn->GetMutableRuntimeState();
		if (!State.Weapon.SourceAcceptedState.IsValid()
			|| State.Weapon.SourceAcceptedState.MatchEpoch
				!= AcceptedBatch.StateRef.MatchEpoch
			|| State.Weapon.SourceAcceptedState.AcceptedSequence
				< AcceptedBatch.StateRef.AcceptedSequence)
		{
			State.Weapon.SourceAcceptedState = AcceptedBatch.StateRef;
			State.Weapon.SourceAcceptedAbilitySetRevision = AcceptedBatch.AbilitySetRevision;
			State.Weapon.SourceAcceptedFormationCommandRevision =
				AcceptedBatch.FormationCommandRevision;
			State.Weapon.SourceAcceptedFormationDefinitionChecksum =
				AcceptedBatch.FormationDefinitionChecksum;
		}
		const FGuLiWingmanCandidateSample& Sample = AcceptedBatch.Samples[Index];
		if ((AcceptedBatch.RebasedMemberMask & (1u << Sample.Wingman.MemberIndex)) != 0u)
		{
			const FVector Position(
				Sample.PositionCentimeters.X,
				Sample.PositionCentimeters.Y,
				Sample.PositionCentimeters.Z);
			const FVector Velocity(
				Sample.VelocityCentimetersPerSecond.X,
				Sample.VelocityCentimetersPerSecond.Y,
				Sample.VelocityCentimetersPerSecond.Z);
			const FRotator Rotation(
				Sample.RotationCentiDegrees.X * 0.01,
				Sample.RotationCentiDegrees.Y * 0.01,
				Sample.RotationCentiDegrees.Z * 0.01);
			Pawn->ApplyAuthorityRebase(FTransform(Rotation, Position), Velocity);
		}
	}
	return true;
}

bool UGuLiWingmanSimulationSubsystem::TryBuildWeaponFireIntent(
	const FGuLiWingmanGroupHandle& Group,
	const FGuLiWingmanHandle& Emitter,
	const uint32 MatchEpoch,
	const uint32 LeaseEpoch,
	const FGuLiWingmanWeaponChannelConfig& Channel,
	const FGuLiWingmanAttackTarget& AssignedTarget,
	const double NowSeconds,
	const uint32 ClientFireTick,
	const bool bClientPredictedLineOfSight,
	FGuLiWingmanFireIntent& OutIntent)
{
	OutIntent = FGuLiWingmanFireIntent{};
	FGuLiWingmanLocalGroupRuntime* Runtime = OwnedGroups.Find(Group);
	const FGuLiWingmanWeaponChannelConfig* CurrentChannel = Runtime
		? Runtime->AbilityConfig.FindWeaponChannel(Channel.Binding) : nullptr;
	if (!Runtime || !Runtime->bCombatAuthorizationValid
		|| !Runtime->AbilityConfig.IsUsableByLeaseOwner()
		|| !Emitter.IsValid() || Emitter.Flight.Group != Group
		|| !Channel.IsWellFormed() || !Channel.bEnabled
		|| Channel.Kind != EGuLiWingmanWeaponKind::BasicAutomatic
		|| Channel.Binding.MatchEpoch != MatchEpoch || !CurrentChannel
		|| CurrentChannel->SkillId != Channel.SkillId
		|| CurrentChannel->AbilityId != Channel.AbilityId
		|| CurrentChannel->ProfileRevision != Channel.ProfileRevision
		|| CurrentChannel->DefinitionRevision != Channel.DefinitionRevision
		|| CurrentChannel->DefinitionChecksum != Channel.DefinitionChecksum
		|| MatchEpoch == 0u || LeaseEpoch == 0u || !AssignedTarget.IsValid()
		|| !FMath::IsFinite(AssignedTarget.ServerTime)
		|| !FMath::IsFinite(AssignedTarget.Radius)
		|| ClientFireTick == 0u || !FMath::IsFinite(NowSeconds)
		|| Runtime->AbilityConfig.MatchEpoch != MatchEpoch)
	{
		return false;
	}
	AGuLiWingmanPawn* Pawn = FindOwnedPawn(*Runtime, Emitter);
	if (!Pawn)
	{
		return false;
	}
	FGuLiWingmanRuntimeState& State = Pawn->GetMutableRuntimeState();
	FGuLiWingmanWeaponState& Weapon = State.Weapon;
	if (!State.Dynamics.bAlive
		|| State.Ability.AbilitySetRevision != Runtime->AbilityConfig.AbilitySetRevision
		|| State.Ability.LoadoutRevision != Runtime->AbilityConfig.LoadoutRevision
		|| State.Ability.FormationCommandRevision
			!= Runtime->AbilityConfig.FormationCommandRevision
		|| State.Ability.FormationDefinitionChecksum
			!= Runtime->AbilityConfig.FormationDefinitionChecksum
		|| !Weapon.SourceAcceptedState.IsValid()
		|| Weapon.SourceAcceptedState.MatchEpoch != MatchEpoch
		|| Weapon.SourceAcceptedState.GroupGeneration != Group.GroupGeneration
		|| Weapon.SourceAcceptedAbilitySetRevision
			!= Runtime->AbilityConfig.AbilitySetRevision
		|| Weapon.SourceAcceptedFormationCommandRevision
			!= Runtime->AbilityConfig.FormationCommandRevision
		|| Weapon.SourceAcceptedFormationDefinitionChecksum
			!= Runtime->AbilityConfig.FormationDefinitionChecksum
		|| NowSeconds < Weapon.GetNextFireSeconds(Channel.Binding.SlotId))
	{
		return false;
	}
	const FVector AimDirection =
		(AssignedTarget.Location - Pawn->GetActorLocation()).GetSafeNormal();
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
	Candidate.Target = AssignedTarget.Target;
	Candidate.TargetAssignmentRevision = AssignedTarget.Revision;
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
	Weapon.SetNextFireSeconds(Channel.Binding.SlotId,
		NowSeconds + Channel.Runtime.CooldownSeconds);
	if (Channel.Binding.SlotId
		== GuLiGetDefaultWeaponSlotId(EGuLiShipAbilitySlot::BasicWeapon))
	{
		Weapon.NextBasicFireSeconds = Weapon.GetNextFireSeconds(Channel.Binding.SlotId);
	}
	Weapon.Target = AssignedTarget.Target;
	OutIntent = MoveTemp(Candidate);
	return true;
}

bool UGuLiWingmanSimulationSubsystem::TryBuildBasicFireIntent(
	const FGuLiWingmanGroupHandle& Group,
	const FGuLiWingmanHandle& Emitter,
	const uint32 MatchEpoch,
	const uint32 LeaseEpoch,
	const FGuLiWingmanAttackTarget& AssignedTarget,
	const double NowSeconds,
	const double CooldownSeconds,
	const uint32 ClientFireTick,
	const bool bClientPredictedLineOfSight,
	FGuLiWingmanFireIntent& OutIntent)
{
	(void)CooldownSeconds;
	const FGuLiWingmanLocalGroupRuntime* Runtime = OwnedGroups.Find(Group);
	const FGuLiWingmanWeaponChannelConfig* Channel = Runtime
		? Runtime->AbilityConfig.FindFirstWeaponChannel(
			EGuLiWingmanWeaponKind::BasicAutomatic) : nullptr;
	return Channel && TryBuildWeaponFireIntent(Group, Emitter, MatchEpoch,
		LeaseEpoch, *Channel, AssignedTarget, NowSeconds, ClientFireTick,
		bClientPredictedLineOfSight, OutIntent);
}

void UGuLiWingmanSimulationSubsystem::ClearFlightNavigationGuidance(
	FGuLiWingmanLocalGroupRuntime& Runtime,
	const uint8 FlightIndex,
	const bool bUsingSafeFallback)
{
	for (const TWeakObjectPtr<AGuLiWingmanPawn>& Entry : Runtime.Pawns)
	{
		AGuLiWingmanPawn* Pawn = Entry.Get();
		if (!Pawn || Pawn->GetWingmanHandle().Flight.FlightIndex != FlightIndex)
		{
			continue;
		}
		FGuLiWingmanNavigationGuidanceState& Guidance =
			Pawn->GetMutableRuntimeState().Navigation;
		Guidance = FGuLiWingmanNavigationGuidanceState{};
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
	for (const TWeakObjectPtr<AGuLiWingmanPawn>& Entry : Runtime.Pawns)
	{
		AGuLiWingmanPawn* Pawn = Entry.Get();
		if (!Pawn || Pawn->GetWingmanHandle().Flight.FlightIndex != FlightIndex)
		{
			continue;
		}
		FGuLiWingmanNavigationGuidanceState& Guidance =
			Pawn->GetMutableRuntimeState().Navigation;
		Guidance.Waypoint = Waypoint;
		Guidance.PathGoal = PathGoal;
		Guidance.AbilitySetRevision = Runtime.AbilityConfig.AbilitySetRevision;
		Guidance.FormationCommandRevision =
			Runtime.AbilityConfig.FormationCommandRevision;
		Guidance.RequestSerial = RequestSerial;
		Guidance.PathPointIndex = PathPointIndex;
		Guidance.bHasPath = true;
		Guidance.bUsingSafeFallback = false;
	}
}

void UGuLiWingmanSimulationSubsystem::CancelNavigationForRuntime(
	FGuLiWingmanLocalGroupRuntime& Runtime,
	const bool bClearPawnGuidance)
{
	for (int32 FlightIndex = 0; FlightIndex < Runtime.FlightNavigation.Num(); ++FlightIndex)
	{
		TSharedPtr<FGuLiWingmanFlightNavigationRuntime>& State =
			Runtime.FlightNavigation[FlightIndex];
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
		*State = FGuLiWingmanFlightNavigationRuntime{};
		if (bClearPawnGuidance && FlightIndex < GULI_WINGMAN_FLIGHT_COUNT)
		{
			ClearFlightNavigationGuidance(
				Runtime, static_cast<uint8>(FlightIndex), false);
		}
	}
	Runtime.NavigationEvaluationAccumulator = 0.0f;
}

void UGuLiWingmanSimulationSubsystem::TickNavigationBehavior(
	const FGuLiWingmanGroupHandle& Group,
	const float DeltaSeconds)
{
	if (const UWorld* World = GetWorld();
		World && World->GetNetMode() == NM_DedicatedServer)
	{
		FGuLiWingmanQAInvariantRegistry::Add(TEXT("SERVER_WINGMAN_PATHFINDING_EXECUTED"));
	}
	FGuLiWingmanLocalGroupRuntime* Runtime = OwnedGroups.Find(Group);
	if (!Runtime || !CanOwnSimulation()
		|| !Runtime->AbilityConfig.IsUsableByLeaseOwner())
	{
		return;
	}
	Runtime->NavigationEvaluationAccumulator +=
		FMath::Clamp(DeltaSeconds, 0.0f, 0.25f);
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
		for (uint8 Index = 0u; Index < GULI_WINGMAN_FLIGHT_COUNT; ++Index)
		{
			Runtime->FlightNavigation.Add(
				MakeShared<FGuLiWingmanFlightNavigationRuntime>());
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
	for (const TWeakObjectPtr<AGuLiWingmanPawn>& Entry : Runtime->Pawns)
	{
		const AGuLiWingmanPawn* Pawn = Entry.Get();
		if (!Pawn)
		{
			continue;
		}
		const FGuLiWingmanRuntimeState& State = Pawn->GetRuntimeState();
		const uint8 FlightIndex = State.Identity.Handle.Flight.FlightIndex;
		if (FlightIndex >= GULI_WINGMAN_FLIGHT_COUNT || !State.Dynamics.bAlive)
		{
			continue;
		}
		FFlightObservation& Observation = Observations[FlightIndex];
		FVector FormationOffset = FVector::ZeroVector;
		if (State.Tuning.Formation.Model == EGuLiWingmanFormationModel::SwarmOrbit)
		{
			FormationOffset = GuLiWingmanSwarmFlow::BuildFlightRecoveryOffset(
				State.Tuning.Formation, FlightIndex);
		}
		else
		{
			FormationOffset = FVector(
				State.FormationSlot.RadiusCentimeters
					* FMath::Cos(State.FormationSlot.PhaseRadians),
				State.FormationSlot.RadiusCentimeters
					* FMath::Sin(State.FormationSlot.PhaseRadians),
				State.FormationSlot.HeightCentimeters);
		}
		Observation.PositionSum += Pawn->GetActorLocation();
		Observation.FormationGoalSum +=
			State.Carrier.Transform.GetLocation() + FormationOffset;
		Observation.AgentRadiusCentimeters = FMath::Max(
			Observation.AgentRadiusCentimeters,
			State.Tuning.Formation.AgentRadiusCentimeters);
		++Observation.AliveMemberCount;
		Observation.bNeedsNavigationPath |=
			State.Dynamics.Mode == EGuLiWingmanFlightMode::CatchUp
			|| State.Dynamics.Mode == EGuLiWingmanFlightMode::Recover;
	}

	const double NowSeconds = GetWorld()
		? static_cast<double>(GetWorld()->GetTimeSeconds()) : 0.0;
	const UGuLiFlightNavigationSubsystem* Navigation = GetWorld()
		? GetWorld()->GetSubsystem<UGuLiFlightNavigationSubsystem>() : nullptr;
	bool bBypassNavigation = false;
#if WITH_DEV_AUTOMATION_TESTS
	bBypassNavigation = bBypassNavigationRequirementForTests;
#endif
	for (uint8 FlightIndex = 0u; FlightIndex < GULI_WINGMAN_FLIGHT_COUNT; ++FlightIndex)
	{
		FFlightObservation& Observation = Observations[FlightIndex];
		TSharedPtr<FGuLiWingmanFlightNavigationRuntime>& StatePtr =
			Runtime->FlightNavigation[FlightIndex];
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
			State = FGuLiWingmanFlightNavigationRuntime{};
			ClearFlightNavigationGuidance(*Runtime, FlightIndex, false);
			continue;
		}
		const FVector Start = Observation.PositionSum
			/ static_cast<float>(Observation.AliveMemberCount);
		const FVector Goal = Observation.FormationGoalSum
			/ static_cast<float>(Observation.AliveMemberCount);
		if (Start.ContainsNaN() || Goal.ContainsNaN())
		{
			State.PathPoints.Reset();
			State.NextPathPointIndex = INDEX_NONE;
			State.bUsingSafeFallback = true;
			State.NextRequestSeconds = NowSeconds + FailedPathRetrySeconds;
			ClearFlightNavigationGuidance(*Runtime, FlightIndex, true);
			continue;
		}
		if (bBypassNavigation)
		{
			if (++State.RequestSerial == 0u)
			{
				State.RequestSerial = 1u;
			}
			State.PathPoints = {Start, Goal};
			State.ActivePathGoal = Goal;
			State.NextPathPointIndex = 1;
			PublishFlightNavigationGuidance(
				*Runtime, FlightIndex, Goal, Goal, State.RequestSerial, 1u);
			continue;
		}

		const float RepathDistance = FMath::Max(
			MinimumGoalDriftForRepathCentimeters,
			Observation.AgentRadiusCentimeters * 4.0f);
		const float WaypointReachDistance = FMath::Max(
			MinimumWaypointReachDistanceCentimeters,
			Observation.AgentRadiusCentimeters * 2.0f);
		if (State.bRequestPending
			&& FVector::DistSquared(Goal, State.RequestedGoal)
				> FMath::Square(RepathDistance)
			&& NowSeconds >= State.NextRequestSeconds)
		{
			if (State.CancellationToken.IsValid()
				&& !State.CancellationToken->IsCancelled())
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
			const uint32 CompletedSerial = State.PendingRequestSerial;
			FGuLiFlightNavPathResult Result = State.PendingResult->Get();
			State.PendingResult.Reset();
			State.CancellationToken.Reset();
			State.bRequestPending = false;
			const bool bMatchingProjection = CompletedSerial != 0u
				&& CompletedSerial == State.RequestSerial
				&& State.RequestedAbilitySetRevision
					== Runtime->AbilityConfig.AbilitySetRevision
				&& State.RequestedFormationCommandRevision
					== Runtime->AbilityConfig.FormationCommandRevision;
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
				&& FVector::DistSquared(Start,
					State.PathPoints[State.NextPathPointIndex])
					<= FMath::Square(WaypointReachDistance))
			{
				++State.NextPathPointIndex;
			}
			PublishFlightNavigationGuidance(*Runtime, FlightIndex,
				State.PathPoints[State.NextPathPointIndex], State.ActivePathGoal,
				State.RequestSerial, static_cast<uint16>(
					FMath::Min(State.NextPathPointIndex, 65535)));
		}
		else if (!State.bRequestPending)
		{
			ClearFlightNavigationGuidance(
				*Runtime, FlightIndex, State.bUsingSafeFallback);
		}
		const bool bHasActivePath =
			State.PathPoints.IsValidIndex(State.NextPathPointIndex);
		const bool bGoalDrifted = bHasActivePath
			&& FVector::DistSquared(Goal, State.ActivePathGoal)
				> FMath::Square(RepathDistance);
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
		uint32 NextSerial = State.RequestSerial + 1u;
		if (NextSerial == 0u)
		{
			NextSerial = 1u;
		}
		State.RequestSerial = NextSerial;
		State.PendingRequestSerial = NextSerial;
		State.RequestedStart = Start;
		State.RequestedGoal = Goal;
		State.RequestedAbilitySetRevision = Runtime->AbilityConfig.AbilitySetRevision;
		State.RequestedFormationCommandRevision =
			Runtime->AbilityConfig.FormationCommandRevision;
		State.CancellationToken =
			MakeShared<FGuLiFlightNavCancellationToken, ESPMode::ThreadSafe>();
		FGuLiFlightNavPathQueryOptions Options;
		Options.AgentRadius = FMath::Max(0.0f, Observation.AgentRadiusCentimeters);
		Options.MaximumExpandedNodes = MaximumNavigationExpandedNodes;
		Options.bSmoothPath = true;
		TFuture<FGuLiFlightNavPathResult> Future = Navigation->FindPathAsync(
			Start, Goal, Options, State.CancellationToken);
		State.PendingResult =
			MakeShared<TFuture<FGuLiFlightNavPathResult>>(MoveTemp(Future));
		State.bRequestPending = true;
		State.bUsingSafeFallback = false;
		State.NextRequestSeconds = NowSeconds + MinimumRepathIntervalSeconds;
		++NavigationAsyncRequestsIssued;
	}
}

bool UGuLiWingmanSimulationSubsystem::GetNavigationDiagnostics(
	const FGuLiWingmanGroupHandle& Group,
	FGuLiWingmanNavigationDiagnostics& OutDiagnostics) const
{
	OutDiagnostics = FGuLiWingmanNavigationDiagnostics{};
	OutDiagnostics.AsyncRequestsIssued = NavigationAsyncRequestsIssued;
	OutDiagnostics.ResultsAccepted = NavigationResultsAccepted;
	OutDiagnostics.SafeFallbackResults = NavigationSafeFallbackResults;
	OutDiagnostics.PendingRequestsCancelled = NavigationPendingRequestsCancelled;
	const FGuLiWingmanLocalGroupRuntime* Runtime = OwnedGroups.Find(Group);
	if (!Runtime)
	{
		return false;
	}
	for (const TSharedPtr<FGuLiWingmanFlightNavigationRuntime>& State :
		Runtime->FlightNavigation)
	{
		if (!State.IsValid())
		{
			continue;
		}
		OutDiagnostics.PendingFlightRequests += State->bRequestPending ? 1 : 0;
		OutDiagnostics.ActiveFlightPaths +=
			State->PathPoints.IsValidIndex(State->NextPathPointIndex) ? 1 : 0;
	}
	for (const TWeakObjectPtr<AGuLiWingmanPawn>& Entry : Runtime->Pawns)
	{
		const AGuLiWingmanPawn* Pawn = Entry.Get();
		if (Pawn && Pawn->GetRuntimeState().Navigation.bHasPath)
		{
			++OutDiagnostics.NavigationGuidedEntities;
		}
	}
	return true;
}

bool UGuLiWingmanSimulationSubsystem::GetAvoidanceDiagnostics(
	const FGuLiWingmanGroupHandle& Group,
	FGuLiWingmanAvoidanceDiagnostics& OutDiagnostics) const
{
	OutDiagnostics = FGuLiWingmanAvoidanceDiagnostics{};
	const FGuLiWingmanLocalGroupRuntime* Runtime = OwnedGroups.Find(Group);
	if (!Runtime)
	{
		return false;
	}
	for (const TWeakObjectPtr<AGuLiWingmanPawn>& Entry : Runtime->Pawns)
	{
		const AGuLiWingmanPawn* Pawn = Entry.Get();
		if (!Pawn)
		{
			continue;
		}
		const FGuLiWingmanAvoidanceState& State =
			Pawn->GetRuntimeState().Avoidance;
		++OutDiagnostics.EvaluatedEntities;
		OutDiagnostics.FullLookAheadBlockedEntities +=
			State.ConsecutiveBlockedSeconds > 0.0f ? 1 : 0;
		OutDiagnostics.SafeHeadingEntities +=
			State.bHasNextStepSafeDirection ? 1 : 0;
		OutDiagnostics.ControlledRecoveryEntities +=
			State.bControlledRecovery ? 1 : 0;
		OutDiagnostics.VerifiedRecoveryPointEntities +=
			State.bHasVerifiedSafePoint ? 1 : 0;
		OutDiagnostics.FlightNavBoundaryThreatEntities +=
			State.bDetectedFlightNavBoundary ? 1 : 0;
		OutDiagnostics.WorldStaticThreatEntities +=
			State.bDetectedWorldStatic ? 1 : 0;
		OutDiagnostics.WorldDynamicThreatEntities +=
			State.bDetectedWorldDynamic ? 1 : 0;
		OutDiagnostics.HeadingProbes += State.HeadingProbeCount;
	}
	return true;
}

bool UGuLiWingmanSimulationSubsystem::GetMotionDiagnostics(
	FGuLiWingmanMotionDiagnostics& OutDiagnostics) const
{
	OutDiagnostics = FGuLiWingmanMotionDiagnostics{};
	if (OwnedGroups.IsEmpty())
	{
		return false;
	}
	double CarrierDistanceSum = 0.0;
	double SpeedSum = 0.0;
	float MinDistance = BIG_NUMBER;
	float MaxDistance = 0.0f;
	for (const TPair<FGuLiWingmanGroupHandle, FGuLiWingmanLocalGroupRuntime>& Pair : OwnedGroups)
	{
		for (const TWeakObjectPtr<AGuLiWingmanPawn>& Entry : Pair.Value.Pawns)
		{
			const AGuLiWingmanPawn* Pawn = Entry.Get();
			if (!Pawn)
			{
				continue;
			}
			const FGuLiWingmanRuntimeState& State = Pawn->GetRuntimeState();
			++OutDiagnostics.EvaluatedEntities;
			if (!State.Dynamics.bAlive)
			{
				continue;
			}
			++OutDiagnostics.AliveEntities;
			switch (State.Dynamics.Mode)
			{
			case EGuLiWingmanFlightMode::Orbit: ++OutDiagnostics.OrbitEntities; break;
			case EGuLiWingmanFlightMode::Follow: ++OutDiagnostics.FollowEntities; break;
			case EGuLiWingmanFlightMode::CatchUp: ++OutDiagnostics.CatchUpEntities; break;
			case EGuLiWingmanFlightMode::Recover: ++OutDiagnostics.RecoverEntities; break;
			case EGuLiWingmanFlightMode::Stale: ++OutDiagnostics.StaleEntities; break;
			default: break;
			}
			const float Distance = static_cast<float>(FVector::Distance(
				Pawn->GetActorLocation(), State.Carrier.Transform.GetLocation()));
			CarrierDistanceSum += Distance;
			SpeedSum += State.Dynamics.Velocity.Size();
			MinDistance = FMath::Min(MinDistance, Distance);
			MaxDistance = FMath::Max(MaxDistance, Distance);
			if (OutDiagnostics.AliveEntities == 1)
			{
				OutDiagnostics.FirstAliveLocation = Pawn->GetActorLocation();
			}
		}
	}
	if (OutDiagnostics.AliveEntities > 0)
	{
		const double Inverse = 1.0 / static_cast<double>(OutDiagnostics.AliveEntities);
		OutDiagnostics.MinimumCarrierDistanceCentimeters = MinDistance;
		OutDiagnostics.MeanCarrierDistanceCentimeters =
			static_cast<float>(CarrierDistanceSum * Inverse);
		OutDiagnostics.MaximumCarrierDistanceCentimeters = MaxDistance;
		OutDiagnostics.MeanSpeedCentimetersPerSecond =
			static_cast<float>(SpeedSum * Inverse);
	}
	return OutDiagnostics.EvaluatedEntities > 0;
}

void UGuLiWingmanSimulationSubsystem::DrainEmergencyRebaseRequests(
	const FGuLiWingmanGroupHandle& Group,
	TArray<FGuLiWingmanLocalRebaseRequest>& OutRequests)
{
	OutRequests.Reset();
	FGuLiWingmanLocalGroupRuntime* Runtime = OwnedGroups.Find(Group);
	if (!Runtime)
	{
		return;
	}
	for (const TWeakObjectPtr<AGuLiWingmanPawn>& Entry : Runtime->Pawns)
	{
		AGuLiWingmanPawn* Pawn = Entry.Get();
		if (!Pawn)
		{
			continue;
		}
		EGuLiWingmanEmergencyRebaseReason Reason;
		if (Pawn->ConsumeEmergencyRebaseRequest(Reason))
		{
			FGuLiWingmanLocalRebaseRequest& Request = OutRequests.AddDefaulted_GetRef();
			Request.Wingman = Pawn->GetWingmanHandle();
			Request.Reason = Reason;
			Runtime->PendingAttackShots.RemoveAll([&](const auto& Pair)
			{
				return Pair.Key == Request.Wingman.Flight.FlightIndex
					&& Pair.Value.MemberIndex == Request.Wingman.MemberIndex;
			});
		}
	}
}

void UGuLiWingmanSimulationSubsystem::QueueStaleEmergencyRebaseRetries(
	const FGuLiWingmanGroupHandle& Group,
	const double EstimatedServerTimeSeconds)
{
	FGuLiWingmanLocalGroupRuntime* Runtime = OwnedGroups.Find(Group);
	if (!Runtime || !FMath::IsFinite(EstimatedServerTimeSeconds))
	{
		return;
	}
	for (const TWeakObjectPtr<AGuLiWingmanPawn>& Entry : Runtime->Pawns)
	{
		if (AGuLiWingmanPawn* Pawn = Entry.Get())
		{
			Pawn->QueueStaleRebaseRetry(EstimatedServerTimeSeconds);
		}
	}
}

bool UGuLiWingmanSimulationSubsystem::ApplyEmergencyRebaseResponse(
	const FGuLiWingmanEmergencyRebaseResponse& Response,
	const FGuLiWingmanAcceptedBatch* AcceptedBatch)
{
	if (!Response.IsWellFormed())
	{
		return false;
	}
	AGuLiWingmanPawn* Pawn = FindOwnedPawn(Response.Wingman);
	if (!Pawn)
	{
		return false;
	}
	if (Response.Result == EGuLiWingmanEmergencyRebaseResult::Accepted)
	{
		return AcceptedBatch && AcceptedBatch->IsWellFormed()
			&& AcceptedBatch->StateRef.AcceptedSequence == Response.AcceptedSequence
			&& ApplyAcceptedBatch(*AcceptedBatch);
	}
	Pawn->MarkRebaseRejected(
		Response.Result == EGuLiWingmanEmergencyRebaseResult::NoSafePoint,
		Response.RetryAfterServerTimeSeconds);
	return true;
}

bool UGuLiWingmanSimulationSubsystem::HasOwnedGroup(
	const FGuLiWingmanGroupHandle& Group) const
{
	return OwnedGroups.Contains(Group);
}

#if WITH_DEV_AUTOMATION_TESTS
void UGuLiWingmanSimulationSubsystem::SetMemberBehaviorStateTreeForTests(UStateTree* StateTree)
{
	MemberBehaviorStateTree = StateTree;
	bAllowBehaviorAssetBypassForTests = StateTree == nullptr;
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
	TSharedPtr<FGuLiWingmanFlightNavigationRuntime>& StatePtr =
		Runtime->FlightNavigation[FlightIndex];
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
	State = FGuLiWingmanFlightNavigationRuntime{};
	State.RequestSerial = NextSerial;
	State.PendingRequestSerial = bPendingRequest ? NextSerial : 0u;
	State.RequestedAbilitySetRevision = Runtime->AbilityConfig.AbilitySetRevision;
	State.RequestedFormationCommandRevision =
		Runtime->AbilityConfig.FormationCommandRevision;
	State.PathPoints = PathPoints;
	if (!PathPoints.IsEmpty())
	{
		State.RequestedStart = PathPoints[0];
		State.RequestedGoal = PathPoints.Last();
		State.ActivePathGoal = PathPoints.Last();
		State.NextPathPointIndex = 1;
		PublishFlightNavigationGuidance(*Runtime, FlightIndex,
			PathPoints[1], PathPoints.Last(), State.RequestSerial, 1u);
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
