#pragma once

#include "CoreMinimal.h"
#include "MassEntityTypes.h"
#include "MassEntityHandle.h"
#include "Commander/Mass/GuLiBattleAuthoritySubsystem.h"
#include "Commander/Mass/Navigation/GuLiSharedMoveRoutes.h"
#include "Commander/Mass/Navigation/GuLiNavigationDependency.h"
#include "Commander/Network/GuLiMoveLatency.h"
#include "Gameplay/Navigation/GuLiCircleAvoidance.h"
#include "Gameplay/Navigation/Grid/GuLiGridMotion.h"
#include "Gameplay/CommanderSkills/GuLiCommanderSkillTypes.h"
#include "Gameplay/Data/GuLiCommanderSoldierDefinition.h"
#include "Gameplay/Presentation/GuLiVATAnimation.h"
#include "Gameplay/Skills/GuLiSkillTypes.h"
#include "GuLiCommanderMassRuntime.generated.h"

struct FGuLiSoldierWeaponRuntime
{
	FName SlotId;
	int32 ProfileIndex = INDEX_NONE;
	FGuLiSoldierAttackState Attack;
};

/** Sole authoritative unit state. Navigation and steering update this Mass-owned record directly. */
USTRUCT()
struct GULISTRIKE_API FGuLiMassSoldierRuntimeFragment : public FMassFragment
{
	GENERATED_BODY()
	// Local directory slot; rebound after archetype compaction, never serialized.
	int32 DirectoryIndex = INDEX_NONE;
	// Entity 是当前 EntityManager 的访问键；SoldierId 不随选择组、移动编队或 Archetype 改变。
	FMassEntityHandle Entity;
	FGuLiSoldierId SoldierId;
        FGuLiHybridObjectKey HybridKey;
        bool bLocalSimulation=true;
	EGuLiTeam Team = EGuLiTeam::Unassigned;
	uint16 UnitTypeId = GULI_DEFAULT_SOLDIER_UNIT_TYPE_ID;
	float AvoidanceRadiusCentimeters = 150.0f;
	bool bMovementEvenArchetype = true;
	FVector Location = FVector::ZeroVector;
	EGuLiUnitMovementLayer MovementLayer = EGuLiUnitMovementLayer::Ground;
	float AltitudeCentimeters = 0.f;
	FVector GroundLocation() const { return GuLiUnitMovement::GroundPosition(Location, AltitudeCentimeters); }
	FVector WorldFromGround(const FVector& P) const { return GuLiUnitMovement::WorldPosition(P, AltitudeCentimeters); }
	FVector Velocity = FVector::ZeroVector;
	double LastMovementUpdateSimulationSeconds = 0.0;
	float FacingYawDegrees = 0.0f;
	float FixedSpawnYawDegrees = 0.0f;
	EGuLiSoldierFacingPolicy FacingPolicy = EGuLiSoldierFacingPolicy::FaceVelocity;
	FGuLiMechanicalAnimationState MechanicalPose;
	FGuLiVATPlayback VATPlayback;
	uint8 NextMechanicalMuzzle = 0;
	float Health = 100.0f;
	float MaxHealth = 100.0f;
	float Defense = 0.0f;
	TSharedPtr<const TArray<FGuLiResolvedSkillProfile>> WeaponProfiles;
	TArray<FGuLiSoldierWeaponRuntime> Weapons;
	bool bAllowAutomaticFire = true;
	// Unique active skill is independent of automatic weapon slots and advances on SimulationSeconds.
	FGuLiActiveSkillRuntime ActiveSkill;
	// StateRevision 标识离散状态变化；ActiveOrderId 指向当前批次，0 表示无活动指令。
	uint32 StateRevision = 1u;
	uint32 ActiveOrderId = 0u;
	uint64 TaskGeneration = 0;
	TSharedPtr<FGuLiSharedMoveIntent> MoveIntent;
        FGuLiGridMotion GridMotion;
        uint32 GridOrderId=0;
        TWeakObjectPtr<ACharacter> EngineeringCarrier;
        bool bEngineering=false;
        uint32 EngineeringDisplacementRevision=0;
        float ArrivalAcceptance=20.f;
	TSharedPtr<FGuLiSharedRouteGoal> RouteGoal;
	TSharedPtr<FGuLiSharedMoveRoute> SharedRoute;
	FGuLiDirectMoveCheck DirectDock;
	int32 RouteCursor = 0;
	bool bHasDockTarget = false;
	GuLiMoveLatency::FContext MoveTrace;
	double CommandAcceptedAt = 0, DirectionAppliedAt = 0, FirstDisplacementAt = 0;
	bool bAutomaticAdvance = false;
	bool bAttackMoveHolding = false;
	FNavLocation LastValidNavLocation;
	FNavLocation FinalDestination;
	FVector CommandStartLocation = FVector::ZeroVector;
	FVector CurrentNavigationWaypoint = FVector::ZeroVector;
	EGuLiSoldierNavigationState NavigationState = EGuLiSoldierNavigationState::Idle;
	EGuLiSoldierNavigationFailure NavigationFailure = EGuLiSoldierNavigationFailure::None;
	uint32 LastCompletedOrderId = 0u;
	uint32 LastFailedOrderId = 0u;
	uint32 FinalDestinationNavigationGeneration = 0u;
	TSharedPtr<FGuLiNavigationDependency> ActiveNavigation;
	bool bNavigationBudgetPending = false;
	float NoProgressSeconds = 0.0f;
	double FailureSimulationSeconds = 0.0;
	float BestWaypointDistanceCentimeters = TNumericLimits<float>::Max();
	int32 LastProgressPathPointIndex = INDEX_NONE;
	int32 ConsecutiveSurfaceFailures = 0;
	int32 TotalSurfaceFailures = 0;
	TArray<FVector> PersonalPathPoints;
	int32 PersonalPathPointIndex = 0;
	int32 PersonalPathRetries = 0;
	double NextPersonalPathQuerySeconds = 0;
	bool bHasFinalDestination = false;
	bool bForceMovementUpdate = false;
	FVector LastCapturedPoseLocation = FVector::ZeroVector;
	uint32 LastCapturedPoseFrameSequence = 0u;
	double LastCapturedPoseSimulationSeconds = 0.0;
	float LastCapturedMovementSpeed = 0.0f;
	// The wreck window retains the identity; expiration retires both entity and replicated record.
	double DeathSimulationSeconds = -1.0;
	bool bWreckExpired = false;
	FGuid ExternalControlToken;
	bool bPhased = false;
	bool bExternalActionsLocked = false;
	GuLiCircleAvoidance::FState CircleState;
    uint64 BirthBuildingSource=0;
    FVector BirthExit=FVector::ZeroVector;
        FGuid MovementCommandIdentity; // Retained on arrival; not a destination or active-order number.
        FGuid YieldCommandIdentity; // Original request's command, unchanged through neighbor relays.
        FVector YieldDirection=FVector::ZeroVector;
        float YieldRemainingSeconds=0;
        int32 GridReferenceFootprint=1;
	uint32 DisplacementFrameFloor = 0;
	FVector DisplacementLocation = FVector::ZeroVector;
	double DisplacementSimulationTime = 0;
	double ExternalLockSimulationTime = 0;
	bool CanAct() const { return IsAlive() && !bExternalActionsLocked; }
        bool CanSimulate() const { return CanAct() && (!HybridKey.IsValid() || bLocalSimulation); }
	bool IsPresent() const { return IsAlive() && !bPhased; }

	bool IsAlive() const
	{
		return FMath::IsFinite(Health) && Health > 0.0f;
	}
};

// Mass relocates fragments without destroying the source, as the former TArray did.
// Keep the existing owning arrays and shared route references in this record.
template<>
struct TMassFragmentTraits<FGuLiMassSoldierRuntimeFragment>
{
	enum { AuthorAcceptsItsNotTriviallyCopyable = true };
};
