// Copyright Epic Games, Inc. All Rights Reserved.

#include "Gameplay/Building/GuLiBuildingTypes.h"

#include "Engine/StaticMesh.h"

bool FGuLiBuildingDefinition::IsUsable() const
{
	return GuLiBuildingPlacementPolicy::IsKnownBuildingType(Type)
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
		|| Type == EGuLiBuildingType::Outpost;
}

FGuLiResourceAmounts GuLiBuildingPlacementPolicy::GetEconomyCost(const EGuLiBuildingType Type)
{
	FGuLiResourceAmounts Cost;
	switch (Type)
	{
	case EGuLiBuildingType::MissileTurret: Cost.Blue = 20; break;
	case EGuLiBuildingType::SentryTurret: Cost.Blue = 10; break;
	case EGuLiBuildingType::Outpost: Cost.Blue = 40; break;
	default: checkNoEntry();
	}
	return Cost;
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
	if (bBuildModeActive && IsBuildingRole(Role) && Number >= 1 && Number <= 3)
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
		return NSLOCTEXT("GuLiBuilding", "Accepted", "已建造");
	case EGuLiBuildingPlacementRejectReason::NotReady:
		return NSLOCTEXT("GuLiBuilding", "NotReady", "战局或连接尚未就绪");
	case EGuLiBuildingPlacementRejectReason::UnauthorizedRole:
		return NSLOCTEXT("GuLiBuilding", "UnauthorizedRole", "当前角色无法建造");
	case EGuLiBuildingPlacementRejectReason::AssetUnavailable:
		return NSLOCTEXT("GuLiBuilding", "AssetUnavailable", "建造资源不可用");
	case EGuLiBuildingPlacementRejectReason::InvalidRequest:
		return NSLOCTEXT("GuLiBuilding", "InvalidRequest", "建造请求无效");
	case EGuLiBuildingPlacementRejectReason::NoGround:
		return NSLOCTEXT("GuLiBuilding", "NoGround", "未找到合法地面");
	case EGuLiBuildingPlacementRejectReason::OutOfRange:
		return NSLOCTEXT("GuLiBuilding", "OutOfRange", "超出 100 米建造距离");
	case EGuLiBuildingPlacementRejectReason::NoLineOfSight:
		return NSLOCTEXT("GuLiBuilding", "NoLineOfSight", "目标被遮挡");
	case EGuLiBuildingPlacementRejectReason::SlopeTooSteep:
		return NSLOCTEXT("GuLiBuilding", "SlopeTooSteep", "地面坡度超过 15 度");
	case EGuLiBuildingPlacementRejectReason::Blocked:
		return NSLOCTEXT("GuLiBuilding", "Blocked", "建造区域被占用");
	case EGuLiBuildingPlacementRejectReason::BuilderLimitReached:
		return NSLOCTEXT("GuLiBuilding", "BuilderLimitReached", "个人建筑已达 6 个");
	case EGuLiBuildingPlacementRejectReason::WorldLimitReached:
		return NSLOCTEXT("GuLiBuilding", "WorldLimitReached", "本局建筑已达 24 个");
	case EGuLiBuildingPlacementRejectReason::RateLimited:
		return NSLOCTEXT("GuLiBuilding", "RateLimited", "建造请求过快");
	case EGuLiBuildingPlacementRejectReason::Duplicate:
		return NSLOCTEXT("GuLiBuilding", "Duplicate", "建造请求已过期");
	case EGuLiBuildingPlacementRejectReason::InsufficientResources:
		return NSLOCTEXT("GuLiBuilding", "InsufficientResources", "蓝矿库存不足");
	case EGuLiBuildingPlacementRejectReason::SpawnFailed:
		return NSLOCTEXT("GuLiBuilding", "SpawnFailed", "建筑生成失败");
	default:
		return NSLOCTEXT("GuLiBuilding", "Unknown", "未知建造错误");
	}
}

FText GetGuLiBuildingFallbackDisplayName(const EGuLiBuildingType Type)
{
	switch (Type)
	{
	case EGuLiBuildingType::MissileTurret:
		return NSLOCTEXT("GuLiBuilding", "MissileTurret", "防空炮");
	case EGuLiBuildingType::SentryTurret:
		return NSLOCTEXT("GuLiBuilding", "SentryTurret", "哨戒炮");
	case EGuLiBuildingType::Outpost:
		return NSLOCTEXT("GuLiBuilding", "Outpost", "据点");
	default:
		return NSLOCTEXT("GuLiBuilding", "UnknownBuilding", "未知建筑");
	}
}
