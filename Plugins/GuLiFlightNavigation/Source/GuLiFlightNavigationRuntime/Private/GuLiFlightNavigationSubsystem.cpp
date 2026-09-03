#include "GuLiFlightNavigationSubsystem.h"

#include "GuLiFlightNavigationData.h"
#include "GuLiFlightNavigationVolume.h"

namespace
{
	bool ContainsAgentCenterInCurrentVolumeBounds(
		const FBox& Bounds,
		const FVector& Point,
		const float AgentRadius)
	{
		if (Bounds.IsValid == 0
			|| Point.ContainsNaN()
			|| !FMath::IsFinite(AgentRadius)
			|| AgentRadius < 0.0f)
		{
			return false;
		}

		const FVector RadiusInset(AgentRadius);
		const FVector InsetMin = Bounds.Min + RadiusInset;
		const FVector InsetMax = Bounds.Max - RadiusInset;
		return InsetMin.X <= InsetMax.X
			&& InsetMin.Y <= InsetMax.Y
			&& InsetMin.Z <= InsetMax.Z
			&& FBox(InsetMin, InsetMax).IsInsideOrOn(Point);
	}
}

void UGuLiFlightNavigationSubsystem::Deinitialize()
{
	RegisteredVolumes.Reset();
	RuntimeGraphCache.Reset();
	Super::Deinitialize();
}

void UGuLiFlightNavigationSubsystem::RegisterVolume(AGuLiFlightNavigationVolume* Volume)
{
	if (IsValid(Volume))
	{
		RegisteredVolumes.AddUnique(Volume);
	}
}

void UGuLiFlightNavigationSubsystem::UnregisterVolume(AGuLiFlightNavigationVolume* Volume)
{
	RegisteredVolumes.Remove(Volume);
}

bool UGuLiFlightNavigationSubsystem::HasUsableNavigationAt(
	const FVector& Point, FString& OutError) const
{
	OutError.Reset();
	if (Point.ContainsNaN())
	{
		OutError = TEXT("The navigation point is not finite.");
		return false;
	}
	const AGuLiFlightNavigationVolume* Volume = ResolveVolume(Point, Point);
	if (!Volume || !Volume->NavigationData)
	{
		OutError = TEXT("No enabled baked Flight Navigation volume contains the point.");
		return false;
	}
	if (!Volume->NavigationData->ValidateData(OutError))
	{
		if (OutError.IsEmpty())
		{
			OutError = TEXT("Flight Navigation data failed validation.");
		}
		return false;
	}
	return true;
}

const AGuLiFlightNavigationVolume* UGuLiFlightNavigationSubsystem::ResolveVolume(
	const FVector& Start,
	const FVector& Goal) const
{
	const AGuLiFlightNavigationVolume* BestVolume = nullptr;
	for (const TWeakObjectPtr<AGuLiFlightNavigationVolume>& VolumePtr : RegisteredVolumes)
	{
		const AGuLiFlightNavigationVolume* Volume = VolumePtr.Get();
		if (!IsValid(Volume)
			|| !Volume->ContainsNavigationPoint(Start)
			|| !Volume->ContainsNavigationPoint(Goal)
			|| Volume->NavigationData == nullptr
			|| !Volume->NavigationData->HasBakedData())
		{
			continue;
		}

		if (BestVolume == nullptr
			|| Volume->QueryPriority > BestVolume->QueryPriority
			|| (Volume->QueryPriority == BestVolume->QueryPriority
				&& Volume->GetPathName().Compare(BestVolume->GetPathName(), ESearchCase::CaseSensitive) < 0))
		{
			BestVolume = Volume;
		}
	}
	return BestVolume;
}

FGuLiFlightNavigationQuery UGuLiFlightNavigationSubsystem::CreateQuery(
	const FVector& Start,
	const FVector& Goal) const
{
	const AGuLiFlightNavigationVolume* Volume = ResolveVolume(Start, Goal);
	if (Volume == nullptr || Volume->NavigationData == nullptr)
	{
		return FGuLiFlightNavigationQuery();
	}

	UGuLiFlightNavigationData* Data = Volume->NavigationData;
	RuntimeGraphCache.RemoveAllSwap([](const FRuntimeGraphCacheEntry& Entry)
	{
		return !Entry.Data.IsValid() || !Entry.Graph.IsValid();
	}, EAllowShrinking::No);
	for (const FRuntimeGraphCacheEntry& Entry : RuntimeGraphCache)
	{
		if (Entry.Data.Get() == Data
			&& Entry.ContentChecksum == Data->Metadata.ContentChecksum
			&& Entry.DefinitionRevision == Data->Metadata.DefinitionRevision)
		{
			return FGuLiFlightNavigationQuery(Entry.Graph);
		}
	}

	TSharedPtr<const FGuLiFlightNavRuntimeGraph, ESPMode::ThreadSafe> RuntimeGraph = Data->CreateRuntimeGraph();
	if (!RuntimeGraph.IsValid())
	{
		return FGuLiFlightNavigationQuery();
	}

	FRuntimeGraphCacheEntry& NewEntry = RuntimeGraphCache.AddDefaulted_GetRef();
	NewEntry.Data = Data;
	NewEntry.ContentChecksum = Data->Metadata.ContentChecksum;
	NewEntry.DefinitionRevision = Data->Metadata.DefinitionRevision;
	NewEntry.Graph = RuntimeGraph;
	return FGuLiFlightNavigationQuery(MoveTemp(RuntimeGraph));
}

EGuLiFlightNavSegmentStatus UGuLiFlightNavigationSubsystem::ValidateAuthoritativeSegment(
	const FVector& Start,
	const FVector& End,
	const float AgentRadius,
	int32* OutStartCell,
	int32* OutEndCell) const
{
	if (OutStartCell)
	{
		*OutStartCell = INDEX_NONE;
	}
	if (OutEndCell)
	{
		*OutEndCell = INDEX_NONE;
	}

	// Select from the authoritative previous position only. Requiring End during volume
	// resolution would turn a legitimate boundary violation into InvalidData before the immutable
	// query has a chance to classify it as EndpointOutsideNavigation.
	const AGuLiFlightNavigationVolume* Volume = ResolveVolume(Start, Start);
	const FGuLiFlightNavigationQuery Query = CreateQuery(Start, Start);
	if (!Volume || !Volume->NavigationData || !Query.IsValid())
	{
		return EGuLiFlightNavSegmentStatus::InvalidData;
	}

	const float EffectiveRadius = AgentRadius > 0.0f
		? AgentRadius
		: Volume->NavigationData->Metadata.BakedAgentRadius;
	if (EffectiveRadius < 0.0f
		|| EffectiveRadius > Volume->NavigationData->Metadata.BakedAgentRadius + UE_KINDA_SMALL_NUMBER)
	{
		// Delegate the classification so this facade cannot diverge from query-level radius policy.
		return Query.ValidateAuthoritativeSegment(
			Start, End, AgentRadius, OutStartCell, OutEndCell);
	}

	const FBox CurrentVolumeBounds = Volume->GetBounds().GetBox();
	if (!ContainsAgentCenterInCurrentVolumeBounds(CurrentVolumeBounds, Start, EffectiveRadius)
		|| !ContainsAgentCenterInCurrentVolumeBounds(CurrentVolumeBounds, End, EffectiveRadius))
	{
		return EGuLiFlightNavSegmentStatus::EndpointOutsideNavigation;
	}

	return Query.ValidateAuthoritativeSegment(
		Start,
		End,
		AgentRadius,
		OutStartCell,
		OutEndCell);
}

EGuLiFlightNavSegmentStatus UGuLiFlightNavigationSubsystem::ValidateEndpointsInSameComponent(
	const FVector& Start,
	const FVector& End,
	const float AgentRadius,
	int32* OutStartCell,
	int32* OutEndCell) const
{
	if (OutStartCell)
	{
		*OutStartCell = INDEX_NONE;
	}
	if (OutEndCell)
	{
		*OutEndCell = INDEX_NONE;
	}

	const AGuLiFlightNavigationVolume* Volume = ResolveVolume(Start, Start);
	const FGuLiFlightNavigationQuery Query = CreateQuery(Start, Start);
	if (!Volume || !Volume->NavigationData || !Query.IsValid())
	{
		return EGuLiFlightNavSegmentStatus::InvalidData;
	}

	const float EffectiveRadius = AgentRadius > 0.0f
		? AgentRadius
		: Volume->NavigationData->Metadata.BakedAgentRadius;
	if (EffectiveRadius < 0.0f
		|| EffectiveRadius > Volume->NavigationData->Metadata.BakedAgentRadius + UE_KINDA_SMALL_NUMBER)
	{
		return Query.ValidateEndpointsInSameComponent(
			Start, End, AgentRadius, OutStartCell, OutEndCell);
	}

	const FBox CurrentVolumeBounds = Volume->GetBounds().GetBox();
	if (!ContainsAgentCenterInCurrentVolumeBounds(CurrentVolumeBounds, Start, EffectiveRadius)
		|| !ContainsAgentCenterInCurrentVolumeBounds(CurrentVolumeBounds, End, EffectiveRadius))
	{
		return EGuLiFlightNavSegmentStatus::EndpointOutsideNavigation;
	}

	return Query.ValidateEndpointsInSameComponent(
		Start, End, AgentRadius, OutStartCell, OutEndCell);
}

FString UGuLiFlightNavigationSubsystem::DescribeNavigationAt(const FVector& Point) const
{
	FString Description = FString::Printf(
		TEXT("Point=%s RegisteredVolumes=%d"),
		*Point.ToCompactString(),
		RegisteredVolumes.Num());
	for (int32 Index = 0; Index < RegisteredVolumes.Num(); ++Index)
	{
		const AGuLiFlightNavigationVolume* Volume = RegisteredVolumes[Index].Get();
		if (!IsValid(Volume))
		{
			Description += FString::Printf(TEXT(" | [%d] InvalidVolume"), Index);
			continue;
		}

		const UGuLiFlightNavigationData* Data = Volume->NavigationData;
		FString ValidationError = TEXT("MissingData");
		const bool bDataValid = Data && Data->ValidateData(ValidationError);
		const FBox CurrentBounds = Volume->GetBounds().GetBox();
		const FBox BakedBounds = Data ? Data->Metadata.Bounds : FBox(ForceInit);
		Description += FString::Printf(
			TEXT(" | [%d] Path=%s Enabled=%d Contains=%d HasBaked=%d DataValid=%d "
				"CurrentBounds=%s BakedBounds=%s Error=%s"),
			Index,
			*Volume->GetPathName(),
			Volume->bNavigationEnabled ? 1 : 0,
			Volume->ContainsNavigationPoint(Point) ? 1 : 0,
			Data && Data->HasBakedData() ? 1 : 0,
			bDataValid ? 1 : 0,
			*CurrentBounds.ToString(),
			*BakedBounds.ToString(),
			ValidationError.IsEmpty() ? TEXT("None") : *ValidationError);
	}
	return Description;
}

FGuLiFlightNavPathResult UGuLiFlightNavigationSubsystem::FindPath(
	const FVector& Start,
	const FVector& Goal,
	const FGuLiFlightNavPathQueryOptions& Options)
{
	return CreateQuery(Start, Goal).FindPath(Start, Goal, Options);
}

TFuture<FGuLiFlightNavPathResult> UGuLiFlightNavigationSubsystem::FindPathAsync(
	const FVector& Start,
	const FVector& Goal,
	const FGuLiFlightNavPathQueryOptions& Options,
	const TSharedPtr<FGuLiFlightNavCancellationToken, ESPMode::ThreadSafe>& CancellationToken) const
{
	return CreateQuery(Start, Goal).FindPathAsync(Start, Goal, Options, CancellationToken);
}
