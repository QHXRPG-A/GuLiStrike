#include "Battle/Combat/GuLiWingmanWeaponPolicy.h"

namespace
{
	bool IsInsideRange(const FVector& Source, const FGuLiCombatTargetSnapshot& Target, const float Range)
	{
		const double EffectiveRange = static_cast<double>(Range) + FMath::Max(0.0f, Target.CollisionRadius);
		return FVector::DistSquared(Source, Target.Location) <= FMath::Square(EffectiveRange);
	}

	bool IsStableWingmanTargetLess(const FGuLiTargetHandle& Lhs, const FGuLiTargetHandle& Rhs)
	{
		if (Lhs.Kind != Rhs.Kind)
		{
			return static_cast<uint8>(Lhs.Kind) < static_cast<uint8>(Rhs.Kind);
		}
		if (Lhs.AuthorityId != Rhs.AuthorityId)
		{
			return Lhs.AuthorityId < Rhs.AuthorityId;
		}
		if (Lhs.Generation != Rhs.Generation)
		{
			return Lhs.Generation < Rhs.Generation;
		}
		return Lhs.LocalId < Rhs.LocalId;
	}
}

bool GuLiWingmanMissileAim::Quantize(
	const FVector& Direction,
	FIntVector& OutDirectionMilli)
{
	OutDirectionMilli = FIntVector::ZeroValue;
	if (Direction.ContainsNaN())
	{
		return false;
	}
	const FVector Unit = Direction.GetSafeNormal();
	if (Unit.IsNearlyZero())
	{
		return false;
	}
	OutDirectionMilli = FIntVector(
		FMath::RoundToInt(Unit.X * QuantizedUnit),
		FMath::RoundToInt(Unit.Y * QuantizedUnit),
		FMath::RoundToInt(Unit.Z * QuantizedUnit));
	return OutDirectionMilli != FIntVector::ZeroValue;
}

bool GuLiWingmanMissileAim::Decode(
	const FIntVector& DirectionMilli,
	FVector& OutDirection)
{
	OutDirection = FVector::ZeroVector;
	if (FMath::Abs(DirectionMilli.X) > QuantizedUnit
		|| FMath::Abs(DirectionMilli.Y) > QuantizedUnit
		|| FMath::Abs(DirectionMilli.Z) > QuantizedUnit)
	{
		return false;
	}
	const int64 LengthSquared = static_cast<int64>(DirectionMilli.X) * DirectionMilli.X
		+ static_cast<int64>(DirectionMilli.Y) * DirectionMilli.Y
		+ static_cast<int64>(DirectionMilli.Z) * DirectionMilli.Z;
	constexpr int64 MinimumLengthSquared = 990ll * 990ll;
	constexpr int64 MaximumLengthSquared = 1010ll * 1010ll;
	if (LengthSquared < MinimumLengthSquared || LengthSquared > MaximumLengthSquared)
	{
		return false;
	}
	OutDirection = FVector(
		static_cast<double>(DirectionMilli.X),
		static_cast<double>(DirectionMilli.Y),
		static_cast<double>(DirectionMilli.Z)).GetSafeNormal();
	return !OutDirection.IsNearlyZero() && OutDirection.IsNormalized();
}

bool FGuLiWingmanMissileSalvoRequest::IsWellFormed() const
{
	FVector DecodedAim;
	return ActivationId.IsValid() && Binding.IsWellFormed()
		&& Binding.Domain == EGuLiWeaponDomain::Wingman
		&& !SkillId.IsNone() && MissileAbilityId.IsValid()
		&& AbilitySetRevision != 0u && LoadoutRevision != 0u
		&& ProfileRevision != 0u && MissileDefinitionRevision != 0u
		&& GuLiWingmanMissileAim::Decode(AimDirectionMilli, DecodedAim);
}

EGuLiWingmanRejectReason GuLiWingmanMissilePolicy::SelectTarget(
	const FVector& AuthorityOrigin,const FVector& AimForward,const FGuLiWingmanWeaponRuntimeConfig& Runtime,
	const FGuLiTargetHandle& Source,EGuLiTeam Team,TConstArrayView<FGuLiCombatTargetSnapshot> Targets,
	TFunctionRef<bool(const FVector&,const FGuLiCombatTargetSnapshot&)> Visible,FGuLiCombatTargetSnapshot& OutTarget)
{
	OutTarget = FGuLiCombatTargetSnapshot{};
	const FVector SafeForward = AimForward.GetSafeNormal();
	if (AuthorityOrigin.ContainsNaN() || SafeForward.IsNearlyZero())
	{
		return EGuLiWingmanRejectReason::InvalidTarget;
	}

	const double MinimumDot = FMath::Cos(FMath::DegreesToRadians(
		FMath::Clamp(static_cast<double>(Runtime.TargetConeHalfAngleDegrees), 0.0, 180.0)));
	double BestDistanceSquared = TNumericLimits<double>::Max();
	bool bFriendlyInReticle = false;
	bool bEnemyInReticleOutOfRange = false;
	bool bEnemyInReticleBlocked = false;
	for (const FGuLiCombatTargetSnapshot& Candidate : Targets)
	{
		if (!Candidate.Handle.IsValid() || Candidate.Handle == Source
			|| !Candidate.bAlive || Candidate.Location.ContainsNaN()
			|| Candidate.Team == EGuLiTeam::Unassigned)
		{
			continue;
		}
		const FVector ToTarget = Candidate.Location - AuthorityOrigin;
		const double DistanceSquared = ToTarget.SizeSquared();
		if (!FMath::IsFinite(DistanceSquared) || DistanceSquared <= UE_DOUBLE_SMALL_NUMBER)
		{
			continue;
		}
		const bool bInsideCone = FVector::DotProduct(
			SafeForward, ToTarget / FMath::Sqrt(DistanceSquared)) >= MinimumDot;
		if (!bInsideCone)
		{
			continue;
		}
		const bool bInsideRange = IsInsideRange(AuthorityOrigin, Candidate, Runtime.RangeCentimeters);
		if (Candidate.Team == Team)
		{
			bFriendlyInReticle |= bInsideRange;
			continue;
		}
		if (!bInsideRange)
		{
			bEnemyInReticleOutOfRange = true;
			continue;
		}
		if (Runtime.bRequiresLineOfSight && !Visible(AuthorityOrigin, Candidate))
		{
			bEnemyInReticleBlocked = true;
			continue;
		}

		const bool bDistanceTie = FMath::IsNearlyEqual(
			DistanceSquared, BestDistanceSquared, 1.0);
		if (!OutTarget.Handle.IsValid()
			|| (DistanceSquared < BestDistanceSquared && !bDistanceTie)
			|| (bDistanceTie && IsStableWingmanTargetLess(Candidate.Handle, OutTarget.Handle)))
		{
			OutTarget = Candidate;
			BestDistanceSquared = DistanceSquared;
		}
	}

	if (OutTarget.Handle.IsValid())
	{
		return EGuLiWingmanRejectReason::None;
	}
	if (bEnemyInReticleBlocked)
	{
		return EGuLiWingmanRejectReason::NoLineOfSight;
	}
	if (bEnemyInReticleOutOfRange)
	{
		return EGuLiWingmanRejectReason::OutOfRange;
	}
	return bFriendlyInReticle
		? EGuLiWingmanRejectReason::FriendlyTarget
		: EGuLiWingmanRejectReason::InvalidTarget;
}

bool GuLiWingmanAttackAuthority::IsAirBurstStartEligible(const FVector& SourceLocation,
	const FGuLiCombatTargetSnapshot& LiveTarget, const float StartDistanceCentimeters)
{
	return LiveTarget.Handle.IsValid() && LiveTarget.bAlive
		&& !SourceLocation.ContainsNaN() && !LiveTarget.Location.ContainsNaN()
		&& FMath::IsFinite(StartDistanceCentimeters) && StartDistanceCentimeters > 0.0f
		&& FVector::Distance(SourceLocation, LiveTarget.Location) >= StartDistanceCentimeters;
}
