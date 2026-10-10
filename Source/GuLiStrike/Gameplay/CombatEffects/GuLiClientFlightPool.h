#pragma once
#include "CoreMinimal.h"
#include "Gameplay/CombatEffects/GuLiFlightEvent.h"

/** Process-local only. A stale handle can never address a reused slot or another match. */
struct FGuLiClientFlightHandle
{
	int32 Index = INDEX_NONE;
	uint32 Generation = 0, Epoch = 0;
};

enum class EGuLiClientFlightRenderKind : uint8 { Laser, Niagara, Count };

/** No UObject ownership: the presentation subsystem retains every cold launch asset. */
class FGuLiClientFlightPool
{
public:
	struct FSlot
	{
		FGuLiCombatEffectState Prediction;
		FGuLiProjectileCurveCoefficients CurveCoefficients;
		FVector DisplayLocation = FVector::ZeroVector;
		double PredictionTime = 0;
		uint32 Generation = 1;
		int32 ActiveIndex = INDEX_NONE;
		EGuLiClientFlightRenderKind Kind = EGuLiClientFlightRenderKind::Laser;
	};
	void BeginEpoch(uint32 NewEpoch);
	FGuLiClientFlightHandle Acquire(const FGuLiFlightEvent& Recipe, EGuLiClientFlightRenderKind Kind, int32 Growth);
	void Release(FGuLiClientFlightHandle Handle);
	FSlot* Find(FGuLiClientFlightHandle Handle);
	const FSlot* Find(FGuLiClientFlightHandle Handle) const;
	void Advance(FGuLiClientFlightHandle Handle, const FGuLiFlightEvent& Recipe,
		float ServerTime, const FVector* TargetPosition);
	int32 GetCapacity() const { return Slots.Num(); }
	int32 GetActiveCount() const { return Slots.Num() - Free.Num(); }
	uint32 GetEpoch() const { return Epoch; }
	uint64 GetReusedAcquisitions() const { return ReusedAcquisitions; }
	uint64 GetReleases() const { return Releases; }
	TConstArrayView<int32> GetActive(EGuLiClientFlightRenderKind Kind) const { return Active[uint8(Kind)]; }
private:
	TArray<FSlot> Slots;
	TArray<int32> Free;
	TArray<int32> Active[uint8(EGuLiClientFlightRenderKind::Count)];
	uint32 Epoch = 0;
	uint64 ReusedAcquisitions = 0, Releases = 0;
};

/** Shared predictor used by the data pool and the legacy Actor pool. */
namespace GuLiClientFlight
{
	void Advance(FGuLiCombatEffectState& Prediction, double& PredictionTime,
		const FGuLiFlightEvent& Recipe,
		float ServerTime, const FVector* TargetPosition, const FGuLiProjectileCurveCoefficients* Coefficients = nullptr);
	FVector DisplayLocation(const FGuLiCombatEffectState& Prediction, double PredictionTime,
		float ServerTime, const FGuLiProjectileCurveCoefficients* Coefficients = nullptr);
}
