// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Battle/Network/GuLiBattleTypes.h"
#include "Engine/NetSerialization.h"
#include "Gameplay/Economy/GuLiEconomyTypes.h"
#include "GuLiResourceTypes.generated.h"

inline constexpr int32 GULI_RESOURCE_BOARD_DIMENSION = 9;
inline constexpr int32 GULI_RESOURCE_BOARD_CENTER = (GULI_RESOURCE_BOARD_DIMENSION + 1) / 2;
inline constexpr int32 GULI_RESOURCE_TERRITORY_COUNT = GULI_RESOURCE_BOARD_DIMENSION * GULI_RESOURCE_BOARD_DIMENSION;
inline constexpr int32 GULI_RESOURCE_BLUE_CLUSTER_COUNT = 200;
inline constexpr int32 GULI_RESOURCE_RED_CLUSTER_COUNT = 40;
inline constexpr int32 GULI_RESOURCE_CLUSTER_COUNT = 240;
inline constexpr int32 GULI_RESOURCE_NODES_PER_CLUSTER = 26;
inline constexpr int32 GULI_RESOURCE_NODE_COUNT = 6240;
inline constexpr int32 GULI_RESOURCE_RAW_PER_CLUSTER = 40;
inline constexpr int32 GULI_RESOURCE_LAYOUT_VERSION = 4;
// Source meshes and the deterministic authored pattern remain in their original units.
inline constexpr float GULI_RESOURCE_OBJECT_SCALE = 0.2f;
inline constexpr int32 GULI_RESOURCE_BAKE_SEED = 20260911;
inline constexpr float GULI_RESOURCE_PLAYABLE_HALF_EXTENT_CM = 135000.0f;
inline constexpr float GULI_RESOURCE_TERRITORY_SIZE_CM =
	2.0f * GULI_RESOURCE_PLAYABLE_HALF_EXTENT_CM / GULI_RESOURCE_BOARD_DIMENSION;
inline constexpr float GULI_RESOURCE_TERRITORY_HALF_EXTENT_CM = GULI_RESOURCE_TERRITORY_SIZE_CM * 0.5f;
inline constexpr float GULI_RESOURCE_FACTORY_ANCHOR_Y_CM =
	GULI_RESOURCE_PLAYABLE_HALF_EXTENT_CM - GULI_RESOURCE_TERRITORY_SIZE_CM * 0.25f;
inline constexpr float GULI_RESOURCE_ASSEMBLY_ANCHOR_Y_CM =
	GULI_RESOURCE_PLAYABLE_HALF_EXTENT_CM - GULI_RESOURCE_TERRITORY_SIZE_CM * 0.75f;
inline constexpr float GULI_RESOURCE_CLUSTER_OBSTACLE_RADIUS_CM = 520.0f;
inline constexpr float GULI_RESOURCE_FACTORY_OBSTACLE_HALF_EXTENT_CM = 500.0f;
inline constexpr float GULI_RESOURCE_MINING_VEHICLE_NAV_RADIUS_CM = 150.0f;
inline constexpr float GULI_RESOURCE_FACTORY_DOCK_MIN_OFFSET_CM =
	GULI_RESOURCE_FACTORY_OBSTACLE_HALF_EXTENT_CM + GULI_RESOURCE_MINING_VEHICLE_NAV_RADIUS_CM;

UENUM(BlueprintType)
enum class EGuLiOreVisualStage : uint8
{
	Hidden = 0,
	Remnant,
	Partial,
	Full
};

UENUM(BlueprintType)
enum class EGuLiMiningControlMode : uint8
{
	Auto = 0,
	PlayerOrder,
	Grace
};

UENUM(BlueprintType)
enum class EGuLiMiningTaskState : uint8
{
	Idle = 0,
	MovingToCluster,
	Mining,
	ReturningToFactory,
	Docking,
	PlayerMoving,
	Blocked,
	WaitingForFactoryDoor UMETA(Hidden), // Legacy ordinal, no longer emitted: doors are presentation only.
	EnteringFactory UMETA(Hidden), // Legacy ordinal; vehicles now navigate directly to an unload point.
	TurningInFactory UMETA(Hidden), // Legacy ordinal, turn at unload is immediate.
	ExitingFactory UMETA(Hidden)
};

/** Public cosmetic state; cargo and player orders remain team-private. */
USTRUCT()
struct FGuLiMiningVisualState
{
	GENERATED_BODY()
	UPROPERTY() bool bActive = false;
	UPROPERTY() uint32 NodeId = 0;
	UPROPERTY() FVector_NetQuantize10 Target = FVector::ZeroVector;
};

USTRUCT()
struct FGuLiFactoryDoorState
{
	GENERATED_BODY()
	UPROPERTY() bool bOpen = false;
	UPROPERTY() float StartAlpha = 0.0f;
	UPROPERTY() float StartServerTime = 0.0f;
	UPROPERTY() float Duration = 3.0f;
};

UENUM(BlueprintType)
enum class EGuLiMiningOrderType : uint8
{
	Move = 0,
	MineCluster,
	ReturnToFactory,
	Cancel
};

/** Stable match-local identity for an Actor that participates in Commander selection. */
USTRUCT()
struct GULISTRIKE_API FGuLiControllableActorId
{
	GENERATED_BODY()

	FGuLiControllableActorId() = default;
	explicit FGuLiControllableActorId(const uint32 InValue) : Value(InValue) {}

	UPROPERTY(EditAnywhere, Category = "Commander|Selection")
	uint32 Value = 0u;

	bool IsValid() const { return Value != 0u; }
	void Reset() { Value = 0u; }
	bool NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess);

	friend bool operator==(const FGuLiControllableActorId& A, const FGuLiControllableActorId& B)
	{
		return A.Value == B.Value;
	}
	friend bool operator!=(const FGuLiControllableActorId& A, const FGuLiControllableActorId& B)
	{
		return !(A == B);
	}
	friend bool operator<(const FGuLiControllableActorId& A, const FGuLiControllableActorId& B)
	{
		return A.Value < B.Value;
	}
	friend uint32 GetTypeHash(const FGuLiControllableActorId& Id) { return ::GetTypeHash(Id.Value); }
};

template <>
struct TStructOpsTypeTraits<FGuLiControllableActorId>
	: public TStructOpsTypeTraitsBase2<FGuLiControllableActorId>
{
	enum { WithNetSerializer = true, WithIdenticalViaEquality = true };
};

USTRUCT(BlueprintType)
struct GULISTRIKE_API FGuLiTerritoryDefinition
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Resources|Board")
	FName TerritoryId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Resources|Board", meta = (ClampMin = "1", ClampMax = "9"))
	uint8 BoardRow = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Resources|Board", meta = (ClampMin = "1", ClampMax = "9"))
	uint8 BoardColumn = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Resources|Board")
	EGuLiTeam InitialOwner = EGuLiTeam::Unassigned;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Resources|Board")
	uint8 BlueClusterBudget = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Resources|Board")
	uint8 RedClusterBudget = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Resources|Board")
	FVector Center = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Resources|Board")
	TArray<FVector2D> LocalPolygon;

	bool IsWellFormed(FString* OutError = nullptr) const;
};

USTRUCT(BlueprintType)
struct GULISTRIKE_API FGuLiResourceClusterDefinition
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Resources|Ore")
	uint16 ClusterId = 0u;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Resources|Ore")
	uint8 TerritoryIndex = 0u;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Resources|Ore")
	EGuLiResourceType ResourceType = EGuLiResourceType::Blue;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Resources|Ore")
	FVector Center = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Resources|Ore")
	float ObstacleRadiusCentimeters = GULI_RESOURCE_CLUSTER_OBSTACLE_RADIUS_CM;

	UPROPERTY(EditAnywhere, Category = "Resources|Ore")
	uint32 FirstNodeIndex = 0u;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Resources|Ore")
	uint8 NodeCount = GULI_RESOURCE_NODES_PER_CLUSTER;

	bool IsWellFormed(int32 TotalNodeCount, FString* OutError = nullptr) const;
};

USTRUCT(BlueprintType)
struct GULISTRIKE_API FGuLiResourceNodeDefinition
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Resources|Ore")
	uint32 NodeId = 0u;

	UPROPERTY(EditAnywhere, Category = "Resources|Ore")
	uint16 ClusterId = 0u;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Resources|Ore")
	EGuLiResourceType ResourceType = EGuLiResourceType::Blue;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Resources|Ore", meta = (ClampMin = "0", ClampMax = "3"))
	uint8 FamilyIndex = 0u;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Resources|Ore", meta = (ClampMin = "1", ClampMax = "3"))
	uint8 InitialAmount = 1u;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Resources|Ore")
	FTransform WorldTransform = FTransform::Identity;

	EGuLiOreVisualStage GetInitialVisualStage() const;
	bool IsWellFormed(int32 TotalClusterCount, FString* OutError = nullptr) const;
};

USTRUCT(BlueprintType)
struct GULISTRIKE_API FGuLiResourceSpawnAnchors
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Resources|Spawn")
	FVector RedFactory = FVector(0.0, GULI_RESOURCE_FACTORY_ANCHOR_Y_CM, 0.0);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Resources|Spawn")
	FVector RedAssembly = FVector(0.0, GULI_RESOURCE_ASSEMBLY_ANCHOR_Y_CM, 0.0);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Resources|Spawn")
	FVector BlueFactory = FVector(0.0, -GULI_RESOURCE_FACTORY_ANCHOR_Y_CM, 0.0);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Resources|Spawn")
	FVector BlueAssembly = FVector(0.0, -GULI_RESOURCE_ASSEMBLY_ANCHOR_Y_CM, 0.0);

	bool IsWellFormed() const;
};

USTRUCT()
struct GULISTRIKE_API FGuLiMiningCommand
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Resources|Mining")
	uint32 RequestId = 0u;

	UPROPERTY(EditAnywhere, Category = "Resources|Mining")
	EGuLiMiningOrderType Type = EGuLiMiningOrderType::Move;

	UPROPERTY(EditAnywhere, Category = "Resources|Mining")
	FVector_NetQuantize10 Target = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, Category = "Resources|Mining")
	uint16 ClusterId = 0u;

	UPROPERTY(EditAnywhere, Category = "Resources|Mining")
	uint32 SelectionRevision = 0u;

	bool IsWellFormed() const;
};

/** Server-authored private state for one factory, mirrored only to players on its team. */
USTRUCT()
struct GULISTRIKE_API FGuLiResourceFactoryPrivateState
{
	GENERATED_BODY()

	UPROPERTY()
	FGuLiControllableActorId StableActorId;

	UPROPERTY()
	int32 QueuedRawAmount = 0;

	friend bool operator==(
		const FGuLiResourceFactoryPrivateState& A,
		const FGuLiResourceFactoryPrivateState& B)
	{
		return A.StableActorId == B.StableActorId
			&& A.QueuedRawAmount == B.QueuedRawAmount;
	}
};

/** Server-authored private state for one mining vehicle. Public transform/team/id stay on the Actor. */
USTRUCT()
struct GULISTRIKE_API FGuLiMiningVehiclePrivateState
{
	GENERATED_BODY()

	UPROPERTY()
	FGuLiControllableActorId StableActorId;

	UPROPERTY()
	FGuLiResourceAmounts Cargo;

	UPROPERTY()
	EGuLiMiningControlMode ControlMode = EGuLiMiningControlMode::Auto;

	UPROPERTY()
	EGuLiMiningTaskState TaskState = EGuLiMiningTaskState::Idle;

	UPROPERTY()
	uint16 TargetClusterId = 0u;

	/** Absolute authoritative world time; clients subtract GameState server time for the HUD countdown. */
	UPROPERTY()
	float GraceEndServerTime = 0.0f;

	UPROPERTY()
	uint32 ActivePlayerRequestId = 0u;

	friend bool operator==(
		const FGuLiMiningVehiclePrivateState& A,
		const FGuLiMiningVehiclePrivateState& B)
	{
		return A.StableActorId == B.StableActorId
			&& A.Cargo.Blue == B.Cargo.Blue
			&& A.Cargo.Red == B.Cargo.Red
			&& A.Cargo.Revision == B.Cargo.Revision
			&& A.ControlMode == B.ControlMode
			&& A.TaskState == B.TaskState
			&& A.TargetClusterId == B.TargetClusterId
			&& A.GraceEndServerTime == B.GraceEndServerTime
			&& A.ActivePlayerRequestId == B.ActivePlayerRequestId;
	}
};

/**
 * One connection-owned mirror of authoritative team resource facts.
 * A copy lives on every PlayerState, but COND_OwnerOnly sends it only to that connection.
 */
USTRUCT()
struct GULISTRIKE_API FGuLiTeamResourcePrivateState
{
	GENERATED_BODY()

	UPROPERTY()
	FGuLiResourceAmounts Inventory;

	UPROPERTY()
	TArray<FGuLiResourceFactoryPrivateState> Factories;

	UPROPERTY()
	TArray<FGuLiMiningVehiclePrivateState> MiningVehicles;

	UPROPERTY()
	uint32 Revision = 0u;

	void SortByStableId();
	bool HasSamePayload(const FGuLiTeamResourcePrivateState& Other) const;
	const FGuLiMiningVehiclePrivateState* FindMiningVehicle(FGuLiControllableActorId Id) const;
};

namespace GuLiResources
{
	GULISTRIKE_API FName MakeTerritoryId(int32 Row, int32 Column);
	GULISTRIKE_API int32 ToTerritoryIndex(int32 Row, int32 Column);
	GULISTRIKE_API FVector GetTerritoryCenter(int32 Row, int32 Column);
	GULISTRIKE_API EGuLiTeam GetInitialTerritoryOwner(int32 Row, int32 Column);
	GULISTRIKE_API EGuLiOreVisualStage AmountToVisualStage(uint8 RemainingAmount);
	/** Shared initial engineering layout for runtime spawning and resource exclusion. */
	GULISTRIKE_API FVector InitialEngineeringVehicleOffset(EGuLiTeam Team, int32 Index, bool bConstruction);
	GULISTRIKE_API uint8 GetBlueClusterBudget(int32 Row, int32 Column);
	GULISTRIKE_API uint8 GetRedClusterBudget(int32 Row, int32 Column);
	GULISTRIKE_API bool IsPlayableTeam(EGuLiTeam Team);
}
