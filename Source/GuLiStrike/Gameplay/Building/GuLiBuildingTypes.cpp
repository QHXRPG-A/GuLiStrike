// Copyright Epic Games, Inc. All Rights Reserved.

#include "Gameplay/Building/GuLiBuildingTypes.h"
#include "Gameplay/Data/GuLiGameText.h"

#include "Engine/StaticMesh.h"
#include "Gameplay/Building/GuLiBuildingCatalog.h"

bool FGuLiBuildingDefinition::IsUsable() const
{
	return (GuLiBuildingPlacementPolicy::IsKnownBuildingType(Type) || Category == EGuLiBuildingCategory::Stronghold)
		&& Mesh != nullptr
		&& !DisplayName.IsEmpty()
		&& FMath::IsFinite(CollisionExtent.X)
		&& FMath::IsFinite(CollisionExtent.Y)
		&& FMath::IsFinite(CollisionExtent.Z)
		&& CollisionExtent.GetMin() > 0.0f
		&& !VisualOffset.ContainsNaN();
}

bool FGuLiBuildingPlacementRequest::IsWellFormed() const
{
	const FVector Location = DesiredGroundLocation;
	return ClientRequestId != 0u
		&& GuLiBuildingPlacementPolicy::IsKnownBuildingType(Type)
		&& FMath::IsFinite(Location.X)
		&& FMath::IsFinite(Location.Y)
		&& FMath::IsFinite(Location.Z);
}

float FGuLiBuildingPlacementRequest::GetYawDegrees() const
{
	return FRotator::DecompressAxisFromShort(CompressedYaw);
}

bool GuLiBuildingPlacementPolicy::IsBuildingRole(const EGuLiCommanderRole Role)
{
	return Role == EGuLiCommanderRole::Commander || Role == EGuLiCommanderRole::Ground;
}

bool GuLiBuildingPlacementPolicy::IsKnownBuildingType(const EGuLiBuildingType Type)
{
	return Type == EGuLiBuildingType::MissileTurret
		|| Type == EGuLiBuildingType::SentryTurret
		|| Type == EGuLiBuildingType::Outpost
		|| Type == EGuLiBuildingType::Barracks
		|| Type == EGuLiBuildingType::ShieldGenerator
		|| Type == EGuLiBuildingType::Factory;
}

FGuLiResourceAmounts GuLiBuildingPlacementPolicy::GetEconomyCost(const EGuLiBuildingType Type)
{
	const auto* Definition = UGuLiBuildingCatalog::LoadDefaultCatalog()->FindDefinition(Type);
	check(Definition);
	return Definition->Cost;
}

bool GuLiBuildingPlacementPolicy::IsSlopeAllowed(
	const FVector& SurfaceNormal,
	const float MaximumDegrees)
{
	if (SurfaceNormal.ContainsNaN() || !FMath::IsFinite(MaximumDegrees)
		|| MaximumDegrees < 0.0f || MaximumDegrees >= 90.0f)
	{
		return false;
	}
	const FVector Normalized = SurfaceNormal.GetSafeNormal();
	return !Normalized.IsNearlyZero()
		&& FVector::DotProduct(Normalized, FVector::UpVector)
			>= FMath::Cos(FMath::DegreesToRadians(MaximumDegrees));
}

bool GuLiBuildingPlacementPolicy::IsGroundPlacementInRange(
	const FVector& PawnLocation,
	const FVector& GroundLocation,
	const float MaximumRange)
{
	return !PawnLocation.ContainsNaN() && !GroundLocation.ContainsNaN()
		&& FMath::IsFinite(MaximumRange) && MaximumRange >= 0.0f
		&& FVector::DistSquared(PawnLocation, GroundLocation) <= FMath::Square(MaximumRange);
}

EGuLiBuildingPlacementRejectReason GuLiBuildingPlacementPolicy::ValidateCapacity(
	const int32 BuilderCount,
	const int32 WorldCount,
	const int32 BuilderLimit,
	const int32 WorldLimit)
{
	if (WorldCount >= WorldLimit)
	{
		return EGuLiBuildingPlacementRejectReason::WorldLimitReached;
	}
	if (BuilderCount >= BuilderLimit)
	{
		return EGuLiBuildingPlacementRejectReason::BuilderLimitReached;
	}
	return EGuLiBuildingPlacementRejectReason::None;
}

bool GuLiBuildingPlacementPolicy::IsNewerRequestId(const uint32 Candidate, const uint32 Previous)
{
	return Candidate != 0u && static_cast<int32>(Candidate - Previous) > 0;
}

bool GuLiBuildingPlacementPolicy::AreSameRequest(
	const FGuLiBuildingPlacementRequest& Lhs,
	const FGuLiBuildingPlacementRequest& Rhs)
{
	return Lhs.ClientRequestId == Rhs.ClientRequestId
		&& Lhs.Type == Rhs.Type
		&& FVector(Lhs.DesiredGroundLocation).Equals(FVector(Rhs.DesiredGroundLocation), 0.1f)
		&& Lhs.CompressedYaw == Rhs.CompressedYaw;
}

GuLiBuildingPlacementPolicy::FNumberKeyDecision GuLiBuildingPlacementPolicy::ResolveNumberKey(
	const int32 Number,
	const bool bBuildModeActive,
	const EGuLiCommanderRole Role)
{
	FNumberKeyDecision Decision;
	if (bBuildModeActive && IsBuildingRole(Role) && Number >= 1 && Number <= 6)
	{
		Decision.bHandled = true;
		Decision.SelectedType = static_cast<EGuLiBuildingType>(Number);
		return Decision;
	}
	if (!bBuildModeActive && Role == EGuLiCommanderRole::Commander && Number == 1)
	{
		Decision.bArmCommanderMove = true;
	}
	return Decision;
}

FText GetGuLiBuildingPlacementReasonText(const EGuLiBuildingPlacementRejectReason Reason)
{
	switch (Reason)
	{
	case EGuLiBuildingPlacementRejectReason::None:
		return GuLiGameText::Get(TEXT("UI.BuildingTypes.139"));
	case EGuLiBuildingPlacementRejectReason::NotReady:
		return GuLiGameText::Get(TEXT("UI.BuildingTypes.140"));
	case EGuLiBuildingPlacementRejectReason::UnauthorizedRole:
		return GuLiGameText::Get(TEXT("UI.BuildingTypes.141"));
	case EGuLiBuildingPlacementRejectReason::AssetUnavailable:
		return GuLiGameText::Get(TEXT("UI.BuildingTypes.142"));
	case EGuLiBuildingPlacementRejectReason::InvalidRequest:
		return GuLiGameText::Get(TEXT("UI.BuildingTypes.143"));
	case EGuLiBuildingPlacementRejectReason::NoGround:
		return GuLiGameText::Get(TEXT("UI.BuildingTypes.144"));
	case EGuLiBuildingPlacementRejectReason::OutOfRange:
		return GuLiGameText::Get(TEXT("UI.BuildingTypes.145"));
	case EGuLiBuildingPlacementRejectReason::NoLineOfSight:
		return GuLiGameText::Get(TEXT("UI.BuildingTypes.146"));
	case EGuLiBuildingPlacementRejectReason::SlopeTooSteep:
		return GuLiGameText::Get(TEXT("UI.BuildingTypes.147"));
	case EGuLiBuildingPlacementRejectReason::Blocked:
		return GuLiGameText::Get(TEXT("UI.BuildingTypes.148"));
	case EGuLiBuildingPlacementRejectReason::BuilderLimitReached:
		return GuLiGameText::Get(TEXT("UI.BuildingTypes.149"));
	case EGuLiBuildingPlacementRejectReason::WorldLimitReached:
		return GuLiGameText::Get(TEXT("UI.BuildingTypes.150"));
	case EGuLiBuildingPlacementRejectReason::RateLimited:
		return GuLiGameText::Get(TEXT("UI.BuildingTypes.151"));
	case EGuLiBuildingPlacementRejectReason::Duplicate:
		return GuLiGameText::Get(TEXT("UI.BuildingTypes.152"));
	case EGuLiBuildingPlacementRejectReason::InsufficientResources:
		return GuLiGameText::Get(TEXT("UI.BuildingTypes.153"));
	case EGuLiBuildingPlacementRejectReason::SpawnFailed:
		return GuLiGameText::Get(TEXT("UI.BuildingTypes.154"));
	default:
		return GuLiGameText::Get(TEXT("UI.BuildingTypes.155"));
	}
}

FText GetGuLiBuildingFallbackDisplayName(const EGuLiBuildingType Type)
{
	switch (Type)
	{
	case EGuLiBuildingType::MissileTurret:
		return GuLiGameText::Get(TEXT("UI.BuildingTypes.156"));
	case EGuLiBuildingType::SentryTurret:
		return GuLiGameText::Get(TEXT("UI.BuildingTypes.157"));
	case EGuLiBuildingType::Outpost:
		return GuLiGameText::Get(TEXT("UI.BuildingTypes.158"));
	case EGuLiBuildingType::Barracks:
		return GuLiGameText::Get(TEXT("UI.BuildingTypes.159"));
	case EGuLiBuildingType::ShieldGenerator:
		return GuLiGameText::Get(TEXT("UI.BuildingTypes.160"));
	case EGuLiBuildingType::Factory:
		return GuLiGameText::Get(TEXT("UI.BuildingTypes.161"));
	default:
		return GuLiGameText::Get(TEXT("UI.BuildingTypes.162"));
	}
}
