#pragma once
#include "CoreMinimal.h"
#include "Gameplay/CombatEffects/GuLiCombatEffectTypes.h"
#include "GuLiFlightEvent.generated.h"

struct FGuLiLogicalMissileState;

/** Immutable launch recipe or authoritative terminal. No damage and no per-frame update payload. */
USTRUCT()
struct GULISTRIKE_API FGuLiFlightEvent
{
	GENERATED_BODY()
	UPROPERTY() FGuLiCombatEffectState State;
	UPROPERTY() FGuLiCombatShotCue Muzzle;
	UPROPERTY() bool bBootstrap = false;
	UPROPERTY() bool bHasMuzzle = false;
	UPROPERTY() int32 ImpactVfxId = 0;
	UPROPERTY() bool bUseCatalogImpact = false;
	UPROPERTY() FVector_NetQuantizeNormal ImpactNormal = FVector::UpVector;
	bool Serialize(FArchive& Ar);
};

namespace GuLiFlightWire
{
	inline constexpr int32 MaximumRecords = 16;
	inline constexpr int32 MaximumBytes = 1000;
	/** Exact bit encoding includes record lengths and batch header in the 1000-byte limit. */
	GULISTRIKE_API bool EncodeRecord(const FGuLiFlightEvent& Event, TArray<uint8>& Bytes, uint16& Bits);
	GULISTRIKE_API int32 EncodeBatch(TConstArrayView<FGuLiFlightEvent> Events, TArray<uint8>& Payload);
	GULISTRIKE_API bool DecodeBatch(const TArray<uint8>& Payload, TArray<FGuLiFlightEvent>& Events);
	GULISTRIKE_API FGuLiCombatEffectState FromLogicalMissile(const FGuLiLogicalMissileState& Missile);
	inline bool IsFlight(EGuLiCombatEffectKind Kind)
	{ return Kind == EGuLiCombatEffectKind::Projectile || Kind == EGuLiCombatEffectKind::LinearProjectile; }
}
