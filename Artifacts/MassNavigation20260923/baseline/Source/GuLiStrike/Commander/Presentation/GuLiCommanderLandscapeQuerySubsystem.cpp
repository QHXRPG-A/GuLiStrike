// Copyright Epic Games, Inc. All Rights Reserved.

#include "Commander/Presentation/GuLiCommanderLandscapeQuerySubsystem.h"

#include "Engine/World.h"
#include "EngineUtils.h"
#include "LandscapeProxy.h"

namespace GuLiCommanderLandscapeQuery
{
	constexpr double CacheRefreshIntervalSeconds = 1.0;
}

void UGuLiCommanderLandscapeQuerySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	InvalidateCache();
}

void UGuLiCommanderLandscapeQuerySubsystem::Deinitialize()
{
	CachedLandscapes.Reset();
	CachedBounds = FBox2D(ForceInit);
	bCacheInitialized = false;
	LastRefreshWorldSeconds = -1.0;
	CacheSignature = 0u;
	CacheRevision = 0u;
	LastHeightProxyIndex = INDEX_NONE;
	Super::Deinitialize();
}

bool UGuLiCommanderLandscapeQuerySubsystem::TryGetBounds(FBox2D& OutBounds) const
{
	EnsureCache();
	OutBounds = CachedBounds;
	return OutBounds.bIsValid
		&& OutBounds.GetSize().X > UE_DOUBLE_SMALL_NUMBER
		&& OutBounds.GetSize().Y > UE_DOUBLE_SMALL_NUMBER;
}

bool UGuLiCommanderLandscapeQuerySubsystem::TryGetLandscapeHeight(
	const FVector2D& WorldXY,
	float& OutHeight) const
{
	if (WorldXY.ContainsNaN())
	{
		return false;
	}

	EnsureCache();
	const auto TryEntry = [&WorldXY, &OutHeight](const FCachedLandscape& Entry)
	{
		ALandscapeProxy* Proxy = Entry.Proxy.Get();
		if (!Proxy || !Entry.Bounds.IsInsideOrOn(WorldXY))
		{
			return false;
		}

		const TOptional<float> Height = Proxy->GetHeightAtLocation(
			FVector(WorldXY.X, WorldXY.Y, 0.0));
		if (Height.IsSet() && FMath::IsFinite(Height.GetValue()))
		{
			OutHeight = Height.GetValue();
			return true;
		}
		return false;
	};
	if (CachedLandscapes.IsValidIndex(LastHeightProxyIndex)
		&& TryEntry(CachedLandscapes[LastHeightProxyIndex]))
	{
		return true;
	}
	for (int32 ProxyIndex = 0; ProxyIndex < CachedLandscapes.Num(); ++ProxyIndex)
	{
		if (ProxyIndex != LastHeightProxyIndex && TryEntry(CachedLandscapes[ProxyIndex]))
		{
			LastHeightProxyIndex = ProxyIndex;
			return true;
		}
	}
	return false;
}

uint32 UGuLiCommanderLandscapeQuerySubsystem::GetCacheRevision() const
{
	EnsureCache();
	return CacheRevision;
}

void UGuLiCommanderLandscapeQuerySubsystem::InvalidateCache()
{
	CachedLandscapes.Reset();
	CachedBounds = FBox2D(ForceInit);
	bCacheInitialized = false;
	LastRefreshWorldSeconds = -1.0;
	LastHeightProxyIndex = INDEX_NONE;
}

void UGuLiCommanderLandscapeQuerySubsystem::EnsureCache() const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const double Now = static_cast<double>(World->GetTimeSeconds());
	if (!bCacheInitialized
		|| Now < LastRefreshWorldSeconds
		|| Now - LastRefreshWorldSeconds >= GuLiCommanderLandscapeQuery::CacheRefreshIntervalSeconds)
	{
		RefreshCache();
	}
}

void UGuLiCommanderLandscapeQuerySubsystem::RefreshCache() const
{
	const bool bWasInitialized = bCacheInitialized;
	const uint32 PreviousSignature = CacheSignature;
	CachedLandscapes.Reset();
	CachedBounds = FBox2D(ForceInit);
	CacheSignature = 0u;
	LastHeightProxyIndex = INDEX_NONE;
	bCacheInitialized = true;
	const UWorld* World = GetWorld();
	LastRefreshWorldSeconds = World ? static_cast<double>(World->GetTimeSeconds()) : -1.0;
	if (!World)
	{
		return;
	}

	for (TActorIterator<ALandscapeProxy> It(World); It; ++It)
	{
		const FBox ComponentBounds = It->GetComponentsBoundingBox(true);
		if (!ComponentBounds.IsValid)
		{
			continue;
		}

		const FBox2D Bounds(
			FVector2D(ComponentBounds.Min.X, ComponentBounds.Min.Y),
			FVector2D(ComponentBounds.Max.X, ComponentBounds.Max.Y));
		if (!Bounds.bIsValid || Bounds.GetSize().GetMin() <= 1.0)
		{
			continue;
		}

		FCachedLandscape& Entry = CachedLandscapes.AddDefaulted_GetRef();
		Entry.Proxy = *It;
		Entry.Bounds = Bounds;
		CachedBounds += Bounds.Min;
		CachedBounds += Bounds.Max;
		CacheSignature = HashCombineFast(CacheSignature, GetTypeHash(Entry.Proxy.Get()));
		CacheSignature = HashCombineFast(CacheSignature, GetTypeHash(Bounds.Min.X));
		CacheSignature = HashCombineFast(CacheSignature, GetTypeHash(Bounds.Min.Y));
		CacheSignature = HashCombineFast(CacheSignature, GetTypeHash(Bounds.Max.X));
		CacheSignature = HashCombineFast(CacheSignature, GetTypeHash(Bounds.Max.Y));
	}

	if (!bWasInitialized || PreviousSignature != CacheSignature)
	{
		++CacheRevision;
		if (CacheRevision == 0u)
		{
			CacheRevision = 1u;
		}
	}
}
