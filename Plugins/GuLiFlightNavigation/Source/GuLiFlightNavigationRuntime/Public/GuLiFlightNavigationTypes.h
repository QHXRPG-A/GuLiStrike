#pragma once

#include "CoreMinimal.h"
#include "GuLiFlightNavigationTypes.generated.h"

namespace GuLiFlightNavigation
{
	inline constexpr uint32 CurrentDataFormatVersion = 2;
	inline constexpr int32 InvalidCell = INDEX_NONE;
	inline constexpr int32 InvalidPortal = INDEX_NONE;
}

/**
 * Authoring settings that define both the baked topology and the static-collision
 * source signature. The volume persists the settings used by the last successful
 * bake so commandlets and cook can detect setting changes without rebuilding.
 */
USTRUCT(BlueprintType)
struct GULIFLIGHTNAVIGATIONRUNTIME_API FGuLiFlightNavBakeSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flight Navigation", meta = (ClampMin = "10.0"))
	float MinimumCellSize = 2000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flight Navigation", meta = (ClampMin = "0.0"))
	float AgentRadius = 1500.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flight Navigation", meta = (ClampMin = "0", ClampMax = "16"))
	int32 MaximumDepth = 8;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flight Navigation", meta = (ClampMin = "9"))
	int32 MaximumNodes = 200000;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flight Navigation", meta = (ClampMin = "1"))
	int32 MaximumCells = 50000;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flight Navigation", meta = (ClampMin = "0.01"))
	float FaceCoordinateTolerance = 0.1f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flight Navigation", meta = (ClampMin = "0.0"))
	float MinimumPortalSpan = 10.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collision")
	TEnumAsByte<ECollisionChannel> CollisionChannel = ECC_WorldStatic;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collision")
	bool bTraceComplex = false;
};

UENUM(BlueprintType)
enum class EGuLiFlightNavPathStatus : uint8
{
	Success,
	InvalidData,
	StartOutsideNavigation,
	GoalOutsideNavigation,
	InsufficientClearance,
	Disconnected,
	NodeLimitExceeded,
	Cancelled,
	NoPath
};

USTRUCT()
struct GULIFLIGHTNAVIGATIONRUNTIME_API FGuLiFlightNavBakeMetadata
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, Category = "Flight Navigation")
	uint32 FormatVersion = GuLiFlightNavigation::CurrentDataFormatVersion;

	UPROPERTY(VisibleAnywhere, Category = "Flight Navigation")
	uint32 DefinitionRevision = 0;

	UPROPERTY(VisibleAnywhere, Category = "Flight Navigation")
	FGuid BakeId;

	UPROPERTY(VisibleAnywhere, Category = "Flight Navigation")
	FBox Bounds = FBox(EForceInit::ForceInit);

	UPROPERTY(VisibleAnywhere, Category = "Flight Navigation", meta = (ClampMin = "1.0"))
	float MinimumCellSize = 2000.0f;

	UPROPERTY(VisibleAnywhere, Category = "Flight Navigation", meta = (ClampMin = "0.0"))
	float BakedAgentRadius = 1500.0f;

	UPROPERTY(VisibleAnywhere, Category = "Flight Navigation")
	FName SourceWorldPackage;

	UPROPERTY(VisibleAnywhere, Category = "Flight Navigation")
	FString SourceVolumePath;

	UPROPERTY(VisibleAnywhere, Category = "Flight Navigation")
	uint64 GeometrySignature = 0;

	UPROPERTY(VisibleAnywhere, Category = "Flight Navigation")
	uint64 SettingsHash = 0;

	UPROPERTY(VisibleAnywhere, Category = "Flight Navigation")
	uint64 ContentChecksum = 0;
};

USTRUCT()
struct GULIFLIGHTNAVIGATIONRUNTIME_API FGuLiFlightNavOctreeNode
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, Category = "Flight Navigation")
	FVector Center = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, Category = "Flight Navigation")
	FVector Extent = FVector::ZeroVector;

	/** First child in a dense run ordered by XYZ octant bits. */
	UPROPERTY(VisibleAnywhere, Category = "Flight Navigation")
	int32 FirstChild = INDEX_NONE;

	/** A leaf with INDEX_NONE is blocked; otherwise it references Cells. */
	UPROPERTY(VisibleAnywhere, Category = "Flight Navigation")
	int32 LeafCellIndex = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, Category = "Flight Navigation")
	uint8 ChildMask = 0;

	bool IsLeaf() const
	{
		return ChildMask == 0;
	}
};

USTRUCT()
struct GULIFLIGHTNAVIGATIONRUNTIME_API FGuLiFlightNavCell
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, Category = "Flight Navigation")
	FVector Center = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, Category = "Flight Navigation")
	FVector Extent = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, Category = "Flight Navigation")
	float Clearance = 0.0f;

	UPROPERTY(VisibleAnywhere, Category = "Flight Navigation")
	int32 ComponentId = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, Category = "Flight Navigation")
	int32 FirstLink = 0;

	UPROPERTY(VisibleAnywhere, Category = "Flight Navigation")
	int32 LinkCount = 0;

	UPROPERTY(VisibleAnywhere, Category = "Flight Navigation")
	uint64 StableId = 0;

	FBox GetBounds() const
	{
		return FBox::BuildAABB(Center, Extent);
	}
};

USTRUCT()
struct GULIFLIGHTNAVIGATIONRUNTIME_API FGuLiFlightNavPortal
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, Category = "Flight Navigation")
	int32 CellA = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, Category = "Flight Navigation")
	int32 CellB = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, Category = "Flight Navigation")
	FVector Center = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, Category = "Flight Navigation")
	FVector Normal = FVector::ForwardVector;

	/** Half-size of the overlap rectangle; the normal-axis component is zero. */
	UPROPERTY(VisibleAnywhere, Category = "Flight Navigation")
	FVector Extent = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, Category = "Flight Navigation")
	float Clearance = 0.0f;

	UPROPERTY(VisibleAnywhere, Category = "Flight Navigation")
	uint64 StableId = 0;
};

USTRUCT()
struct GULIFLIGHTNAVIGATIONRUNTIME_API FGuLiFlightNavLink
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, Category = "Flight Navigation")
	int32 ToCell = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, Category = "Flight Navigation")
	int32 PortalIndex = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, Category = "Flight Navigation")
	float Cost = 0.0f;
};

USTRUCT(BlueprintType)
struct GULIFLIGHTNAVIGATIONRUNTIME_API FGuLiFlightNavPathQueryOptions
{
	GENERATED_BODY()

	/** Zero selects the radius used for the bake. Larger-than-baked radii are rejected. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flight Navigation", meta = (ClampMin = "0.0"))
	float AgentRadius = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flight Navigation", meta = (ClampMin = "1", ClampMax = "1000000"))
	int32 MaximumExpandedNodes = 8192;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flight Navigation")
	bool bSmoothPath = true;
};

USTRUCT(BlueprintType)
struct GULIFLIGHTNAVIGATIONRUNTIME_API FGuLiFlightNavPathResult
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Flight Navigation")
	EGuLiFlightNavPathStatus Status = EGuLiFlightNavPathStatus::InvalidData;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Flight Navigation")
	TArray<FVector> Points;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Flight Navigation")
	TArray<int32> CellPath;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Flight Navigation")
	int32 ExpandedNodes = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Flight Navigation")
	float TotalCost = 0.0f;

	bool IsSuccess() const
	{
		return Status == EGuLiFlightNavPathStatus::Success;
	}
};
