#include "Gameplay/CombatEffects/GuLiClientFlightPool.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

void FGuLiClientFlightPool::BeginEpoch(const uint32 NewEpoch)
{
	if (Epoch == NewEpoch) return;
	Epoch = NewEpoch;
	Free.Reset();
	for (auto& List : Active) List.Reset();
	for (int32 Index = Slots.Num() - 1; Index >= 0; --Index)
	{
		auto& Slot = Slots[Index];
		if (++Slot.Generation == 0) ++Slot.Generation;
		Slot.ActiveIndex = INDEX_NONE;
		Slot.Prediction = {};
		Free.Add(Index);
	}
}

FGuLiClientFlightHandle FGuLiClientFlightPool::Acquire(const FGuLiFlightEvent& Recipe,
	const EGuLiClientFlightRenderKind Kind, const int32 Growth)
{
	BeginEpoch(Recipe.State.MatchEpoch);
	if (Free.IsEmpty())
	{
		const int32 First = Slots.Num();
		Slots.SetNum(First + FMath::Max(1, Growth), EAllowShrinking::No);
		for (int32 Index = Slots.Num() - 1; Index >= First; --Index) Free.Add(Index);
	}
	const int32 Index = Free.Pop(EAllowShrinking::No);
	auto& Slot = Slots[Index];
	if (Slot.Generation > 1) ++ReusedAcquisitions;
	Slot.Prediction = Recipe.State; Slot.PredictionTime = Recipe.State.SampleTime;
	Slot.CurveCoefficients.Initialize(Recipe.State);
	Slot.DisplayLocation = Recipe.State.Location; Slot.Kind = Kind;
	Slot.ActiveIndex = Active[uint8(Kind)].Add(Index);
	return {Index, Slot.Generation, Epoch};
}

FGuLiClientFlightPool::FSlot* FGuLiClientFlightPool::Find(const FGuLiClientFlightHandle Handle)
{
	if (Handle.Epoch != Epoch || !Slots.IsValidIndex(Handle.Index)) return nullptr;
	auto& Slot = Slots[Handle.Index];
	return Slot.ActiveIndex != INDEX_NONE && Slot.Generation == Handle.Generation ? &Slot : nullptr;
}
const FGuLiClientFlightPool::FSlot* FGuLiClientFlightPool::Find(const FGuLiClientFlightHandle Handle) const
{ return const_cast<FGuLiClientFlightPool*>(this)->Find(Handle); }

void FGuLiClientFlightPool::Release(const FGuLiClientFlightHandle Handle)
{
	if (auto* Slot = Find(Handle))
	{
		++Releases;
		auto& List = Active[uint8(Slot->Kind)];
		const int32 Removed = Slot->ActiveIndex;
		List.RemoveAtSwap(Removed, 1, EAllowShrinking::No);
		if (List.IsValidIndex(Removed)) Slots[List[Removed]].ActiveIndex = Removed;
		Slot->ActiveIndex = INDEX_NONE; Slot->Prediction = {};
		if (++Slot->Generation == 0) ++Slot->Generation;
		Free.Add(Handle.Index);
	}
}

void FGuLiClientFlightPool::Advance(const FGuLiClientFlightHandle Handle, const FGuLiFlightEvent& Recipe,
	const float ServerTime, const FVector* TargetPosition)
{
	if (auto* Slot = Find(Handle))
	{
		const auto* Coefficients = GuLiCombatEffects::ShouldPrecomputeCurves() ? &Slot->CurveCoefficients : nullptr;
		GuLiClientFlight::Advance(Slot->Prediction, Slot->PredictionTime, Recipe, ServerTime, TargetPosition, Coefficients);
		Slot->DisplayLocation = GuLiClientFlight::DisplayLocation(Slot->Prediction, Slot->PredictionTime,
			ServerTime, Coefficients);
	}
}

void GuLiClientFlight::Advance(FGuLiCombatEffectState& Prediction, double& PredictionTime,
	const FGuLiFlightEvent& Recipe, const float ServerTime, const FVector* TargetPosition,
	const FGuLiProjectileCurveCoefficients* Coefficients)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(GuLiClientFlightPrediction);
	if (!Recipe.State.EffectId.IsValid()) return;
	const double Until = FMath::Min(double(ServerTime), double(Recipe.State.EndTime));
	if (Prediction.Kind == EGuLiCombatEffectKind::LinearProjectile)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(GuLiClientFlightPureLinear);
		Prediction.Location = FVector(Recipe.State.LaunchLocation) + FVector(Recipe.State.Velocity)
			* FMath::Max(0.0, Until - Recipe.State.StartTime);
		PredictionTime = Until;
	}
	else
	{
		if (TargetPosition && !Prediction.bFixedPoint) Prediction.LastTargetLocation = *TargetPosition;
		while (PredictionTime + 1.0 / 30.0 <= Until + UE_SMALL_NUMBER)
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(GuLiClientFlightPureHoming);
			const float Step = 1.f / 30.f;
			PredictionTime += Step;
			GuLiCombatEffects::AdvanceProjectile(Prediction, float(PredictionTime - Prediction.StartTime), Step, Coefficients);
		}
	}
	Prediction.SampleTime = PredictionTime;
}

FVector GuLiClientFlight::DisplayLocation(const FGuLiCombatEffectState& Prediction, const double PredictionTime,
	const float ServerTime, const FGuLiProjectileCurveCoefficients* Coefficients)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(GuLiClientFlightPureDisplay);
	if (Prediction.Kind == EGuLiCombatEffectKind::LinearProjectile) return Prediction.Location;
	const double Residual = FMath::Clamp(double(ServerTime) - PredictionTime, 0.0, 1.0 / 30.0);
	if (ServerTime - Prediction.StartTime <= Prediction.Motion.LiftSeconds)
		return GuLiCombatEffects::LiftPosition(Prediction, FMath::Max(0.f, ServerTime - Prediction.StartTime), Coefficients);
	return FVector(Prediction.Location) + FVector(Prediction.Velocity) * Residual;
}
