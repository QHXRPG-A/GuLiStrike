// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Battle/Network/GuLiBattleTypes.h"
#include "Engine/NetSerialization.h"
#include "GuLiBuildingTypes.generated.h"

class UStaticMesh;

/** Stable match-local building identifiers. Numeric values are part of the placement wire contract. */
UENUM(BlueprintType)
enum class EGuLiBuildingType : uint8
{
	Invalid = 0,
	MissileTurret = 1,
	SentryTurret = 2,
	Outpost = 3
};

/** One authoritative reason for a placement result. None means the server accepted the request. */
UENUM(BlueprintType)
enum class EGuLiBuildingPlacementRejectReason : uint8
{
	None = 0,
	NotReady,
	UnauthorizedRole,
	AssetUnavailable,
	InvalidRequest,
	NoGround,
	OutOfRange,
	NoLineOfSight,
	SlopeTooSteep,
	Blocked,
	BuilderLimitReached,
	WorldLimitReached,
	RateLimited,
	Duplicate,
	SpawnFailed
};

/** Catalog row shared by preview, placement validation and the replicated building actor. */
USTRUCT(BlueprintType)
struct GULISTRIKE_API FGuLiBuildingDefinition
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Building")
	EGuLiBuildingType Type = EGuLiBuildingType::MissileTurret;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Building")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Building")
	TObjectPtr<UStaticMesh> Mesh;

	/** Half extent of the baked Scale-1 visual bounds in centimeters. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Building", meta = (ClampMin = "1.0"))
	FVector CollisionExtent = FVector(100.0f);

	/** Moves the baked mesh so the actor origin is the footprint center at ground height. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Building")
	FVector VisualOffset = FVector::ZeroVector;

	bool IsUsable() const;
};

/** Client intent only. The server canonicalizes ground height and validates every gameplay rule. */
USTRUCT()
struct GULISTRIKE_API FGuLiBuildingPlacementRequest
{
	GENERATED_BODY()

	UPROPERTY()
	uint32 ClientRequestId = 0u;

	UPROPERTY()
	EGuLiBuildingType Type = EGuLiBuildingType::MissileTurret;

	UPROPERTY()
	FVector_NetQuantize10 DesiredGroundLocation = FVector::ZeroVector;

	UPROPERTY()
	uint16 CompressedYaw = 0u;

	bool IsWellFormed() const;
	float GetYawDegrees() const;
};

USTRUCT()
struct GULISTRIKE_API FGuLiBuildingPlacementResult
{
	GENERATED_BODY()

	UPROPERTY()
	uint32 ClientRequestId = 0u;

	UPROPERTY()
	EGuLiBuildingPlacementRejectReason RejectReason = EGuLiBuildingPlacementRejectReason::InvalidRequest;

	bool WasAccepted() const { return RejectReason == EGuLiBuildingPlacementRejectReason::None; }
};

/** Native-only presentation tone. It never crosses the network or owns gameplay state. */
enum class EGuLiBuildingFeedbackTone : uint8
{
	Info,
	Success,
	Error
};

namespace GuLiBuildingPlacementPolicy
{
	inline constexpr float GroundMaximumRangeCentimeters = 10000.0f;
	inline constexpr float MaximumSlopeDegrees = 15.0f;
	inline constexpr float PlacementClearanceCentimeters = 100.0f;
	inline constexpr int32 MaximumBuildingsPerBuilder = 6;
	inline constexpr int32 MaximumBuildingsPerWorld = 24;
	inline constexpr int32 MaximumRequestsPerSecond = 10;

	struct FNumberKeyDecision
	{
		bool bHandled = false;
		bool bArmCommanderMove = false;
		EGuLiBuildingType SelectedType = EGuLiBuildingType::MissileTurret;
	};

	GULISTRIKE_API bool IsBuildingRole(EGuLiCommanderRole Role);
	GULISTRIKE_API bool IsKnownBuildingType(EGuLiBuildingType Type);
	GULISTRIKE_API bool IsSlopeAllowed(const FVector& SurfaceNormal, float MaximumDegrees = MaximumSlopeDegrees);
	GULISTRIKE_API bool IsGroundPlacementInRange(
		const FVector& PawnLocation,
		const FVector& GroundLocation,
		float MaximumRange = GroundMaximumRangeCentimeters);
	GULISTRIKE_API EGuLiBuildingPlacementRejectReason ValidateCapacity(
		int32 BuilderCount,
		int32 WorldCount,
		int32 BuilderLimit = MaximumBuildingsPerBuilder,
		int32 WorldLimit = MaximumBuildingsPerWorld);
	GULISTRIKE_API bool IsNewerRequestId(uint32 Candidate, uint32 Previous);
	GULISTRIKE_API bool AreSameRequest(
		const FGuLiBuildingPlacementRequest& Lhs,
		const FGuLiBuildingPlacementRequest& Rhs);
	GULISTRIKE_API FNumberKeyDecision ResolveNumberKey(
		int32 Number,
		bool bBuildModeActive,
		EGuLiCommanderRole Role);
}

GULISTRIKE_API FText GetGuLiBuildingPlacementReasonText(EGuLiBuildingPlacementRejectReason Reason);
GULISTRIKE_API FText GetGuLiBuildingFallbackDisplayName(EGuLiBuildingType Type);
