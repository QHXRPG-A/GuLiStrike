// Copyright Epic Games, Inc. All Rights Reserved.

#include "Commander/Mass/GuLiCommanderSelectionQuery.h"
#include "Commander/Orders/GuLiSpecialTaskCatalog.h"

namespace GuLiCommanderSelectionQuery
{
	namespace Private
	{
		constexpr double MaximumRayDistanceCentimeters = 600000.0;
		// Only synthetic/test candidates without a model use these scaled fallback dimensions.
		constexpr double PickBodyHalfHeightCentimeters = 100.0;
		constexpr double PickBodyHorizontalRadiusCentimeters = 150.0;
		constexpr double PickPresentationToleranceSeconds = 0.35;
		constexpr double MaximumPickPresentationErrorCentimeters = 500.0;

		bool IsEligible(const FCandidate& Candidate, const EGuLiTeam Team)
		{
			return Candidate.SoldierId.IsValid() && Candidate.bAlive && Candidate.Team == Team
				&& Candidate.UnitTypeId != 0u && !Candidate.Location.ContainsNaN()
				&& !Candidate.Velocity.ContainsNaN();
		}

		bool IsInsidePickRay(const FGuLiSelectionRequest& Request, const FCandidate& Candidate)
		{
			const FVector Direction = FVector(Request.RayDirection).GetSafeNormal();
			// A bounded sphere encloses that body so a stationary model's top/side is not
			// rejected merely because the cursor ray does not pass near its foot point.
			const FVector BodyCenter = Candidate.WorldBounds.IsValid ? Candidate.WorldBounds.GetCenter()
				: Candidate.Location + FVector(0.0, 0.0, PickBodyHalfHeightCentimeters);
			const FVector Offset = BodyCenter - FVector(Request.RayOrigin);
			const double AlongRay = FVector::DotProduct(Offset, Direction);
			if (AlongRay <= 0.0 || AlongRay > MaximumRayDistanceCentimeters)
			{
				return false;
			}
			// The ID is a hint for the presented soldier, not permission to select arbitrary map units.
			const double PresentationAllowance = FMath::Min(
				Candidate.Velocity.Size() * PickPresentationToleranceSeconds,
				MaximumPickPresentationErrorCentimeters);
			const double BodyRadius = Candidate.WorldBounds.IsValid ? Candidate.WorldBounds.GetExtent().Size()
				: FMath::Sqrt(FMath::Square(PickBodyHalfHeightCentimeters)
				+ FMath::Square(PickBodyHorizontalRadiusCentimeters));
			const double Radius = BodyRadius + PresentationAllowance
				+ AlongRay * FMath::Tan(static_cast<double>(Request.PickHalfAngleRadians));
			return (Offset - Direction * AlongRay).SizeSquared() <= FMath::Square(Radius);
		}

		bool IsInsideBox(const FGuLiSelectionRequest& Request, const FVector& FootLocation)
		{
			const FVector Rays[] = {
				Request.BoxTopLeftRay, Request.BoxTopRightRay,
				Request.BoxBottomRightRay, Request.BoxBottomLeftRay
			};
			const FVector CenterRay = (Rays[0] + Rays[1] + Rays[2] + Rays[3]).GetSafeNormal();
			const FVector Offset = FootLocation - FVector(Request.RayOrigin);
			if (FVector::DotProduct(Offset, CenterRay) <= 0.0
				|| Offset.SizeSquared() > FMath::Square(MaximumRayDistanceCentimeters))
			{
				return false;
			}
			for (int32 Index = 0; Index < 4; ++Index)
			{
				FVector InwardNormal = FVector::CrossProduct(Rays[Index], Rays[(Index + 1) % 4]).GetSafeNormal();
				if (FVector::DotProduct(InwardNormal, CenterRay) < 0.0)
				{
					InwardNormal *= -1.0;
				}
				if (FVector::DotProduct(InwardNormal, Offset) < -1.0)
				{
					return false;
				}
			}
			return true;
		}
	}

	bool ResolveCandidates(
		const FGuLiSelectionRequest& Request,
		const EGuLiTeam Team,
		const TConstArrayView<FCandidate> Population,
		TArray<FGuLiSoldierId>& OutIds)
	{
		OutIds.Reset();
		if (!GuLiCommanderProtocol::IsPlayableTeam(Team) || !Request.IsWellFormed())
		{
			return false;
		}
		if (Request.Modifier == EGuLiSelectionModifier::Clear)
		{
			return true;
		}

		const FCandidate* Seed = nullptr;
		if (Request.Kind == EGuLiSelectionKind::Point || Request.Kind == EGuLiSelectionKind::SameType)
		{
			// Stable Actors are resolved by the resource subsystem. Mass contributes no hit for
			// this point request, but the unified selection transaction remains valid.
			if (Request.SeedActorId.IsValid())
			{
				return true;
			}
			for (const FCandidate& Candidate : Population)
			{
				if (Candidate.SoldierId == Request.SeedSoldierId)
				{
					Seed = &Candidate;
					break;
				}
			}
			if (!Seed || !Private::IsEligible(*Seed, Team) || !Private::IsInsidePickRay(Request, *Seed))
			{
				return false;
			}
			if (Request.Kind == EGuLiSelectionKind::Point)
			{
				OutIds.Add(Seed->SoldierId);
				return true;
			}
		}

		const FVector Center = FVector(Request.Center);
		const double RadiusSquared = FMath::Square(static_cast<double>(
			GuLiCommanderProtocol::GetSelectionRadiusCentimeters(Request.RadiusPreset)));
		TArray<const FCandidate*> Hits;
		TSet<FGuLiSoldierId> SeenIds;
		for (const FCandidate& Candidate : Population)
		{
			if (!Private::IsEligible(Candidate, Team) || SeenIds.Contains(Candidate.SoldierId))
			{
				continue;
			}
			const bool bHit = Request.Kind == EGuLiSelectionKind::SameType
				? Candidate.UnitTypeId == Seed->UnitTypeId && Private::IsInsideBox(Request, Candidate.Location)
					&& FVector::DistSquared2D(Candidate.Location, Center) <= FMath::Square(GetDefault<UGuLiUnitTaskSettings>()->SameTypeRadiusCentimeters)
				: (Request.Kind == EGuLiSelectionKind::Box
					? Private::IsInsideBox(Request, Candidate.Location)
					: FVector::DistSquared2D(Candidate.Location, Center) <= RadiusSquared);
			if (bHit)
			{
				SeenIds.Add(Candidate.SoldierId);
				Hits.Add(&Candidate);
			}
		}
		Hits.Sort([Center](const FCandidate& Lhs, const FCandidate& Rhs)
		{
			const double LhsDistance = FVector::DistSquared2D(Lhs.Location, Center);
			const double RhsDistance = FVector::DistSquared2D(Rhs.Location, Center);
			return LhsDistance != RhsDistance ? LhsDistance < RhsDistance : Lhs.SoldierId < Rhs.SoldierId;
		});
		const int32 Limit = static_cast<int32>(GULI_MAX_CONTROL_COHORTS * GULI_CONTROL_COHORT_TARGET_SIZE);
		if (Hits.Num() > Limit) return false;
		const int32 Count = FMath::Min(Hits.Num(), Limit);
		OutIds.Reserve(Count);
		for (int32 Index = 0; Index < Count; ++Index)
		{
			OutIds.Add(Hits[Index]->SoldierId);
		}
		return true;
	}

	void CombineMembership(
		const TConstArrayView<FGuLiSoldierId> ExistingIds,
		const TConstArrayView<FGuLiSoldierId> HitIds,
		const EGuLiSelectionModifier Modifier,
		TArray<FGuLiSoldierId>& OutIds)
	{
		OutIds.Reset();
		if (Modifier == EGuLiSelectionModifier::Clear)
		{
			return;
		}
		TSet<FGuLiSoldierId> Members;
		if (Modifier == EGuLiSelectionModifier::Add || Modifier == EGuLiSelectionModifier::Toggle)
		{
			for (const FGuLiSoldierId Id : ExistingIds)
			{
				if (Id.IsValid())
				{
					Members.Add(Id);
				}
			}
		}
		TSet<FGuLiSoldierId> SeenHits;
		for (const FGuLiSoldierId Id : HitIds)
		{
			if (!Id.IsValid() || SeenHits.Contains(Id))
			{
				continue;
			}
			SeenHits.Add(Id);
			if (Modifier == EGuLiSelectionModifier::Toggle && Members.Contains(Id))
			{
				Members.Remove(Id);
			}
			else
			{
				Members.Add(Id);
			}
		}
		OutIds = Members.Array();
		OutIds.Sort();
	}
}
