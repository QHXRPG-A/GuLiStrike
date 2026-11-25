// Copyright Epic Games, Inc. All Rights Reserved.

#include "Gameplay/Wingman/GuLiWingmanSimulationSubsystem.h"
#include "Gameplay/Models/GuLiModelRegistrySubsystem.h"
#include "Gameplay/CombatEffects/GuLiUnitFeedbackSubsystem.h"

#include "Battle/Relay/GuLiWingmanRelayTypes.h"
#include "Development/GuLiWingmanQAEvidence.h"
#include "Engine/World.h"
#include "Misc/App.h"
#include "Network/GuLiNetworkSettings.h"
#include "EngineUtils.h"
#include "Gameplay/Ship/GuLiStrikeShip.h"
#include "Engine/StaticMesh.h"
#include "Gameplay/Wingman/GuLiWingmanPawn.h"
#include "Gameplay/Wingman/Movement/GuLiWingmanFlightMovementComponent.h"
#include "Gameplay/Wingman/Movement/GuLiWingmanSteering.h"
#include "Gameplay/Wingman/Movement/GuLiWingmanSwarmFlow.h"
#include "Gameplay/Wingman/Movement/GuLiWingmanFlightCollision.h"
#include "StateTree.h"
#include "Subsystems/SubsystemCollection.h"

namespace
{
	constexpr uint32 MaximumSpawnCandidatesPerMember = 64u;

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

	FGuLiWingmanFormationSlotState MakeDoubleRingSlot(const FGuLiWingmanFormationRuntimeConfig& Formation, int32 Slot)
	{
		const bool bInner = Slot < Formation.InnerRingSlots;
		const int32 Index = bInner ? Slot : Slot - Formation.InnerRingSlots;
		const int32 Count = bInner ? Formation.InnerRingSlots : Formation.OuterRingSlots;
		FGuLiWingmanFormationSlotState Result;
		Result.RadiusCentimeters = bInner ? Formation.InnerRingRadiusCentimeters : Formation.OuterRingRadiusCentimeters;
		Result.HeightCentimeters = bInner ? Formation.InnerRingHeightCentimeters : Formation.OuterRingHeightCentimeters;
		Result.AngularSpeedRadiansPerSecond = bInner ? Formation.InnerAngularSpeedRadiansPerSecond : Formation.OuterAngularSpeedRadiansPerSecond;
		Result.PhaseRadians = float(Index) * UE_TWO_PI / FMath::Max(1, Count);
		Result.bClockwise = bInner;
		return Result;
	}

    bool BuildInitialFormationPositions(UWorld* World, const FTransform& Carrier,
        const FGuLiWingmanFormationRuntimeConfig& Formation, TArray<FVector>& Positions,
        int32& FailedSlot, TConstArrayView<int32> Slots = {})
    {
        using namespace GuLiWingmanFlightCollision;
        FailedSlot = INDEX_NONE;
        if (!World || !Formation.IsWellFormed()) return false;
        TArray<FHull> Hulls;
        SampleHulls(*World, GuLiWingmanSteering::FixedStepSeconds, Hulls, true);
        Positions.Init(FVector(MAX_flt), GULI_WINGMAN_GROUP_SIZE);
        const float ClearanceRadius = Formation.AgentRadiusCentimeters + Formation.SafetyMarginCentimeters();
        const bool bSwarm = Formation.Model == EGuLiWingmanFormationModel::SwarmOrbit;
        for (int32 Slot = 0; Slot < GULI_WINGMAN_GROUP_SIZE; ++Slot)
        {
            if (!Slots.IsEmpty() && !Slots.Contains(Slot)) continue;
            FGuLiWingmanHandle Handle;
            Handle.Flight.FlightIndex = Slot / GULI_WINGMAN_MEMBERS_PER_FLIGHT;
            Handle.MemberIndex = Slot % GULI_WINGMAN_MEMBERS_PER_FLIGHT;
            Handle.EntityGeneration = 1;
            FGuLiWingmanSwarmAgentState Agent;
            GuLiWingmanSwarmFlow::InitializeAgent(Formation, Handle, 1, Agent);
            const FVector RingOffset = bSwarm ? FVector::ZeroVector : MakeDoubleRingSlot(Formation, Slot).Offset();
            bool bFound = false;
            for (uint32 Attempt = 0; Attempt < MaximumSpawnCandidatesPerMember; ++Attempt)
            {
                const FVector Offset = bSwarm
                    ? GuLiWingmanSwarmFlow::BuildInitialOffset(Formation, Handle, Agent, Attempt)
                    : RingOffset;
                const FVector Anchor = Carrier.GetLocation() + Offset;
                FVector Point = Anchor;
                // A double-ring slot has a fixed XY. Try successively higher positions
                // only if its first exterior position is still occupied by a ship.
                Point.Z += ClearanceRadius * (bSwarm ? Attempt / 8 : Attempt);
                if (Point.Z - Anchor.Z > Formation.CatchUpDistanceCentimeters) break;
                FHitResult Surface;
                // A sweep from above can find the top even when the nominal slot is
                // underneath a single-sided Landscape. Location is the sphere center.
                if (SweepTerrain(*World, Point + FVector(0, 0, 1000000), Point, ClearanceRadius, Surface))
                {
                    if (Surface.bStartPenetrating) continue;
                    Point.Z = FMath::Max(Point.Z, Surface.Location.Z + 2.0);
                }
                if (Point.Z - Anchor.Z > Formation.CatchUpDistanceCentimeters
                    || !ClearPosition(*World, Hulls, Point, ClearanceRadius))
                {
                    continue;
                }
                Positions[Slot] = Point;
                bFound = true;
                break;
            }
            if (!bFound)
            {
                FailedSlot = Slot;
                return false;
            }
        }
        return true;
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
	DefaultModelId = GuLiModelIds::Wingman;
}

bool UGuLiWingmanSimulationSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	return Super::ShouldCreateSubsystem(Outer) && World && World->IsGameWorld()
		&& IsOwnerSimulationNetMode(World->GetNetMode());
}

TConstArrayView<TWeakObjectPtr<AGuLiStrikeShip>> UGuLiWingmanSimulationSubsystem::GetFlightObstacleShips(bool bRefresh)
{
    if (bRefresh || FlightObstacleFrame != GFrameCounter)
    {
        FlightObstacleShips.Reset();
        for (TActorIterator<AGuLiStrikeShip> Ship(GetWorld()); Ship; ++Ship)
        {
            FlightObstacleShips.Add(*Ship);
        }
        FlightObstacleFrame = GFrameCounter;
    }
    return FlightObstacleShips;
}

void UGuLiWingmanSimulationSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
    if (FApp::CanEverRender() && !UGuLiNetworkSettings::IsLogicOnly() && UGuLiNetworkSettings::IsHybrid(GetWorld()))
        HybridPresentationTick = FWorldDelegates::OnWorldTickEnd.AddUObject(this, &ThisClass::RenderHybridMembers);
}

void UGuLiWingmanSimulationSubsystem::RenderHybridMembers(UWorld* World, ELevelTick TickType, float DeltaSeconds)
{
    if (World != GetWorld() || TickType == LEVELTICK_ViewportsOnly || World->IsPaused() || DeltaSeconds <= 0) return;
    const double Now = World->GetTimeSeconds();
    for (const auto& Entry : IndependentPawns)
        if (auto* Pawn = Entry.Value.Get()) Pawn->RenderHybridPresentation(Now, DeltaSeconds);
}

void UGuLiWingmanSimulationSubsystem::Deinitialize()
{
    FWorldDelegates::OnWorldTickEnd.Remove(HybridPresentationTick);
	TArray<TSharedPtr<FGuLiWingmanLocalGroupRuntime>> Independent;
	for (const auto& Pair:IndependentRuntimes) if (auto Runtime=Pair.Value.Pin()) Independent.Add(MoveTemp(Runtime));
	for (const auto& Runtime:Independent) ReleaseIndependentMember(*Runtime);
	IndependentRuntimes.Reset();IndependentPawns.Reset();
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
	return GuLiModels::Load<UStaticMesh>(this, DefaultModelId);
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
    TArray<FVector> InitialPositions;
    int32 FailedSlot;
    if (!BuildInitialFormationPositions(GetWorld(),CarrierTransform,AbilityConfig.FormationRuntime,InitialPositions,FailedSlot)) return false;

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
		State.FormationSlot = MakeDoubleRingSlot(AbilityConfig.FormationRuntime, StableSlot);
		Forward = FVector::CrossProduct(FVector::UpVector, State.FormationSlot.Offset()).GetSafeNormal();
		if (State.FormationSlot.bClockwise) Forward *= -1.0;
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
	return Pawn.InitializeOwnerSimulation(
		State, ResolveMemberBehaviorStateTree(), ResolveWingmanMesh());
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
			State.FlightPlan = {};
			GuLiWingmanSwarmFlow::InitializeAgent(
				AbilityConfig.FormationRuntime, State.Identity.Handle,
				FMath::Max(1u, State.Dynamics.CaptureSimulationTick), State.SwarmAgent);
			State.FormationSlot = MakeDoubleRingSlot(AbilityConfig.FormationRuntime, State.Identity.Handle.GetGroupMemberIndex());
			State.Guidance.bUsesVelocityField = AbilityConfig.FormationRuntime.Model == EGuLiWingmanFormationModel::SwarmOrbit;
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
		// Copy the old generation and its flight velocity before the reliable roster hides/reuses it.
		if (State.Dynamics.bAlive && (RosterEntry->bDead || State.Identity.Handle != RosterEntry->Wingman))
			if (auto* Feedback = GetWorld()->GetSubsystem<UGuLiUnitFeedbackSubsystem>())
				Feedback->PlayWingmanDestruction(State.Identity.Handle, Pawn->GetPresentationTransform().GetLocation());
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

bool UGuLiWingmanSimulationSubsystem::GetClearFormationPositions(UObject* Context, const FTransform& Carrier,
    const FGuLiWingmanFormationRuntimeConfig& Formation, TArray<FVector>& OutPositions)
{
    return PlanExternalFormationPositions(Context ? Context->GetWorld() : nullptr,Carrier,Formation,OutPositions);
}

bool UGuLiWingmanSimulationSubsystem::PlanExternalFormationPositions(UWorld* World, const FTransform& Carrier,
	const FGuLiWingmanFormationRuntimeConfig& Formation, TArray<FVector>& OutPositions, TConstArrayView<int32> LivingSlots,FString* Failure)
{
    int32 Failed=INDEX_NONE;
    const bool Ready=BuildInitialFormationPositions(World,Carrier,Formation,OutPositions,Failed,LivingSlots);
    if (Failure) *Failure=Ready ? FString{} : FString::Printf(TEXT("No clear spawn for wingman slot %d"),Failed);
    return Ready;
}

void UGuLiWingmanSimulationSubsystem::SetGroupExternalControlState(const FGuLiWingmanGroupHandle& Group, bool bPhased, bool bLocked)
{
	if (auto* Runtime = OwnedGroups.Find(Group))
	{
		if (bLocked && !Runtime->bExternalActionsLocked)
		{

			Runtime->PendingAttackShots.Reset();
		}
		Runtime->bExternalActionsLocked = bLocked;
		for (auto& WeakPawn : Runtime->Pawns)
		{
			if (auto* Pawn = WeakPawn.Get(); Pawn && Pawn->GetRuntimeState().Dynamics.bAlive)
			{
				Pawn->SetPhaseAppearance(bPhased);
			}
		}
	}
}

bool UGuLiWingmanSimulationSubsystem::AdvanceOwnedGroup(
	const FGuLiWingmanGroupHandle& Group,
	const float DeltaSeconds)
{
	FGuLiWingmanLocalGroupRuntime* Runtime = OwnedGroups.Find(Group);
    return Runtime && AdvanceRuntime(*Runtime,DeltaSeconds,GetWorld()->GetTimeSeconds(),false);
}
bool UGuLiWingmanSimulationSubsystem::AdvanceRuntime(FGuLiWingmanLocalGroupRuntime& Value,float DeltaSeconds,double NowSeconds,bool LogicalBehavior)
{
    auto* Runtime=&Value;
	if (Runtime && Runtime->bExternalActionsLocked) { return true; }
	if (!Runtime || !CanOwnSimulation()
		|| !Runtime->AbilityConfig.IsUsableByLeaseOwner()
		|| !FMath::IsFinite(DeltaSeconds) || DeltaSeconds < 0.0f)
	{
		return false;
	}

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
	for (AGuLiWingmanPawn* Pawn : SortedPawns)
	{
		Pawn->UpdateBehaviorObservation(DeltaSeconds);
        if (LogicalBehavior) Pawn->AdvanceLogicalBehavior(DeltaSeconds);
		if (UGuLiWingmanFlightMovementComponent* Movement = Pawn->GetFlightMovement())
		{
			Movement->AdvanceFixedSteps(DeltaSeconds);
		}
		if (!Pawn->UsesHybridPresentation()) Pawn->UpdateFlightTrail();
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
    if (const auto* Pawn=IndependentPawns.Find(Wingman)) return Pawn->Get();
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

#endif
