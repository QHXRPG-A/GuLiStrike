// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "GuLiCommanderLandscapeQuerySubsystem.generated.h"

class ALandscapeProxy;

/**
 * Shared, read-only Landscape query used by commander presentation systems.
 *
 * Only ALandscapeProxy height data is accepted. Props, soldiers and buildings
 * can therefore never become an accidental camera/minimap ground source.
 */
UCLASS()
class GULISTRIKE_API UGuLiCommanderLandscapeQuerySubsystem final : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** Returns the union of the live Landscape component bounds in world XY. */
	bool TryGetBounds(FBox2D& OutBounds) const;

	/** Returns a strict Landscape height at world XY; never falls back to another actor. */
	bool TryGetLandscapeHeight(const FVector2D& WorldXY, float& OutHeight) const;

	/** Changes only when the live proxy set or its component bounds change. */
	uint32 GetCacheRevision() const;

	/** Explicitly invalidates the cache after a known landscape/level change. */
	void InvalidateCache();

private:
	struct FCachedLandscape
	{
		TWeakObjectPtr<ALandscapeProxy> Proxy;
		FBox2D Bounds = FBox2D(ForceInit);
	};

	void EnsureCache() const;
	void RefreshCache() const;

	mutable TArray<FCachedLandscape> CachedLandscapes;
	mutable FBox2D CachedBounds = FBox2D(ForceInit);
	mutable double LastRefreshWorldSeconds = -1.0;
	mutable uint32 CacheSignature = 0u;
	mutable uint32 CacheRevision = 0u;
	mutable int32 LastHeightProxyIndex = INDEX_NONE;
	mutable bool bCacheInitialized = false;
};
