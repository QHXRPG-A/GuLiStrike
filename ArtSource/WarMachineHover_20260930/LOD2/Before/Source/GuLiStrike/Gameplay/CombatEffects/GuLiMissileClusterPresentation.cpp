#include "Gameplay/CombatEffects/GuLiMissileClusterPresentation.h"
#include "Commander/Presentation/GuLiCommanderLODSubsystem.h"
#include "Commander/Presentation/GuLiCommanderOverviewSubsystem.h"
#include "Engine/World.h"
#include "Gameplay/Vfx/GuLiVfxRegistrySubsystem.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "NiagaraComponent.h"
#include "NiagaraDataInterfaceArrayFunctionLibrary.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "ProfilingDebugging/CsvProfiler.h"

CSV_DEFINE_CATEGORY(GuLiMissileCluster, true);
namespace
{
	TAutoConsoleVariable<int32> FullTrailBudget(TEXT("gs.MissileCluster.FullTrails"), 512, TEXT("Full trails, including fading slots and quality handoffs."));
	TAutoConsoleVariable<int32> ParticleBudget(TEXT("gs.MissileCluster.Particles"), 65536, TEXT("GPU capacity budget; bodies/flames are the minimum. Lower budgets drain existing history before reclaiming it."));
	TAutoConsoleVariable<float> MaximumDistance(TEXT("gs.MissileCluster.Distance"), 20000.f, TEXT("Visual distance in cm; culling includes 2.4 seconds of historical smoke."));
	TAutoConsoleVariable<float> MinimumScreenFraction(TEXT("gs.MissileCluster.MinScreenFraction"), .001f, TEXT("Cull below this projected history-bound fraction."));
	constexpr float MissileClusterCellSize = 4000.f;
	constexpr int32 Lanes[] = {0, 8, 24};
	constexpr int32 Ids[] = {GuLiVfxIds::WM01MissileClusterMinimal, GuLiVfxIds::WM01MissileClusterLite, GuLiVfxIds::WM01MissileClusterFull};
	const FName LODConsumer(TEXT("WM01MissileBatches"));
	// The legacy Niagara quality array runs in the opposite direction to public LOD0/1/2.
	int32 ToNiagaraQuality(const EGuLiCommanderLODLevel Level)
	{
		switch (Level)
		{
		case EGuLiCommanderLODLevel::Full: return 2;
		case EGuLiCommanderLODLevel::Reduced: return 1;
		default: return 0;
		}
	}
	EGuLiCommanderLODLevel ToCommanderLevel(const int32 Quality)
	{
		switch (Quality)
		{
		case 2: return EGuLiCommanderLODLevel::Full;
		case 1: return EGuLiCommanderLODLevel::Reduced;
		default: return EGuLiCommanderLODLevel::Minimal;
		}
	}
}

UWorld* UGuLiMissileClusterPresentation::GetWorld() const { return GetOuter() ? GetOuter()->GetWorld() : nullptr; }
bool UGuLiMissileClusterPresentation::IsReady() const { return bResourcesReady && CommanderLOD.IsValid(); }

void UGuLiMissileClusterPresentation::ReleaseComponent(TWeakObjectPtr<UNiagaraComponent>& Weak)
{
	if (auto* Component = Weak.Get()) { Component->DeactivateImmediate(); UGuLiCommanderOverviewSubsystem::ForgetVisual(Component); Component->ReleaseToPool(); }
	Weak.Reset();
}

void UGuLiMissileClusterPresentation::Release(FBatch& Batch)
{
	ReleaseComponent(Batch.Component); ReleaseComponent(Batch.Retiring.Component);
	Batch.Retiring = {}; Batch.Quality = -1; Batch.QualitySince = -1000;
}

void UGuLiMissileClusterPresentation::Reset()
{
	for (auto& Batch : Batches) Release(Batch);
	Batches.Reset(); SlotById.Reset(); VisualProfiles.Reset(); SystemCount = ParticleCapacity = ActiveMissiles = FullTrails = 0;
	UpdateMilliseconds = 0;
	if (auto* LOD = CommanderLOD.Get()) LOD->ClearConsumer(LODConsumer);
}

void UGuLiMissileClusterPresentation::BeginFrame(double Now, bool bEnabled)
{
	if (Now < FrameTime) { Reset(); NextResourceCheck = 0; }
	FrameTime = Now;
	CommanderLOD = GetWorld() ? GetWorld()->GetSubsystem<UGuLiCommanderLODSubsystem>() : nullptr;
	bFrameEnabled = bEnabled && GetWorld() && GetWorld()->GetNetMode() != NM_DedicatedServer;
	if (!bFrameEnabled) { Reset(); return; }
	if (!bResourcesReady && Now >= NextResourceCheck)
	{
		NextResourceCheck = Now + 1; Systems.SetNum(3); bResourcesReady = true;
		for (int32 Q = 0; Q < 3; ++Q)
		{
			Systems[Q] = GuLiVfx::Load<UNiagaraSystem>(this, Ids[Q]);
#if WITH_EDITOR
			// UE can defer compilation until the first component activation. We gate
			// component creation on readiness, so service that request here instead
			// of waiting forever for an activation that cannot happen. Match the
			// Niagara component's non-blocking compile path; keep legacy visuals
			// while VM/GPU work is pending, without forcing a recompile every retry.
			if (Systems[Q] && Systems[Q]->HasOutstandingCompilationRequests(true))
				Systems[Q]->PollForCompilationComplete();
#endif
			const FNiagaraVariable Contract(FNiagaraTypeDefinition::GetIntDef(), TEXT("User.MissileContractVersion"));
			bResourcesReady &= Systems[Q] && Systems[Q]->HasAnyGPUEmitters() && Systems[Q]->IsReadyToRun()
				&& Systems[Q]->GetExposedParameters().GetParameterValueOrDefault<int32>(Contract, 0) == 2;
			// Older candidates have the same capacity contract but no independent visual inputs.
			if (Systems[Q]) for (const TCHAR* Name : {TEXT("User.MissileSmokeInitialWidth"), TEXT("User.MissileSmokeMaximumWidth"),
				TEXT("User.MissileFlameWidth"), TEXT("User.MissileFlameLength")})
			{
				const FNiagaraVariable Parameter(FNiagaraTypeDefinition::GetFloatDef(), Name);
				const float Value = Systems[Q]->GetExposedParameters().GetParameterValueOrDefault<float>(Parameter, 0);
				bResourcesReady &= FMath::IsFinite(Value) && Value > 0;
			}
		}
	}
	for (auto& Batch : Batches) for (auto& Slot : Batch.Slots) Slot.bSubmitted = false;
}

int32 UGuLiMissileClusterPresentation::Acquire(const FGuid& Id, const FVector& Position,
	const UGuLiProjectileEffectDefinition* Definition, const FGuLiProjectileVisualSettings& Visual)
{
	if (const auto* Found = SlotById.Find(Id)) return *Found;
	const FIntVector Cell(FMath::FloorToInt(Position.X / MissileClusterCellSize), FMath::FloorToInt(Position.Y / MissileClusterCellSize), FMath::FloorToInt(Position.Z / MissileClusterCellSize));
	int32 BatchIndex = INDEX_NONE, SlotIndex = INDEX_NONE, EmptyBatch = INDEX_NONE;
	for (int32 B = 0; B < Batches.Num(); ++B)
	{
		const auto& Batch = Batches[B];
		if (!Batch.Slots.ContainsByPredicate([](const FSlot& S) { return S.bAllocated; }) && !Batch.Retiring.Component.IsValid())
		{ EmptyBatch = B; continue; }
		if (Batch.Cell != Cell || Batch.Definition.Get() != Definition) continue;
		const int32 Limit = Batch.Capacity > 0 ? Batch.Capacity : MissilesPerBatch;
		for (int32 I = 0; I < Limit; ++I) if (!Batch.Slots[I].bAllocated) { SlotIndex = I; break; }
		if (SlotIndex != INDEX_NONE) { BatchIndex = B; break; }
	}
	if (BatchIndex == INDEX_NONE)
	{
		BatchIndex = EmptyBatch != INDEX_NONE ? EmptyBatch : Batches.AddDefaulted();
		auto& Batch = Batches[BatchIndex]; Release(Batch); Batch.Cell = Cell; Batch.RecentBounds.Reset();
		Batch.Definition = Definition; Batch.Visual = Visual;
		Batch.Capacity = 0; Batch.RetryAt = 0; Batch.Slots.SetNum(MissilesPerBatch);
		Batch.Positions.Init(FVector::ZeroVector, MissilesPerBatch); Batch.Directions.Init(FVector::ForwardVector, MissilesPerBatch);
		Batch.Sizes.Init(FVector2D::ZeroVector, MissilesPerBatch); Batch.Meta.Init(FLinearColor::Transparent, MissilesPerBatch);
		SlotIndex = 0;
	}
	auto& Slot = Batches[BatchIndex].Slots[SlotIndex];
	const uint32 Generation = (Slot.Generation % 1000000) + 1;
	Slot = {}; Slot.Id = Id; Slot.Generation = Generation; Slot.bAllocated = true;
	Slot.Position = Position; Slot.LastSeen = FrameTime;
	const int32 Index = BatchIndex * MissilesPerBatch + SlotIndex;
	SlotById.Add(Id, Index); return Index;
}

bool UGuLiMissileClusterPresentation::Submit(const FGuid& Id, const FVector& Position, const FVector& Direction,
	const UGuLiProjectileEffectDefinition* Definition)
{
	if (!bFrameEnabled || !IsReady() || !Id.IsValid() || Position.ContainsNaN() || Direction.ContainsNaN()) return false;
	if (!IsValid(Definition)) return false;
	const TWeakObjectPtr<const UGuLiProjectileEffectDefinition> Key(Definition);
	const auto* Visual = VisualProfiles.Find(Key);
	if (!Visual)
	{
		FGuLiProjectileVisualSettings Resolved;
		if (!Definition->ResolveVisualSettings(Resolved)) return false;
		Visual = &VisualProfiles.Add(Key, Resolved);
	}
	const int32 Index = Acquire(Id, Position, Definition, *Visual); auto& Batch = Batches[Index / MissilesPerBatch];
	auto& Slot = Batch.Slots[Index % MissilesPerBatch];
	if (Slot.FinishedAt >= 0) return false;
	if (FrameTime - Slot.LastSeen > .20 || FVector::DistSquared(Slot.Position, Position) > FMath::Square(600.0))
		Slot.Generation = (Slot.Generation % 1000000) + 1;
	Slot.Position = Position; Slot.Direction = Direction.GetSafeNormal(UE_SMALL_NUMBER, Slot.Direction);
	Slot.LastSeen = FrameTime; Slot.bSubmitted = true;
	return FrameTime >= Batch.RetryAt;
}

void UGuLiMissileClusterPresentation::Finish(const FGuid& Id, const FVector& ImpactPosition, bool bImmediate)
{
	const auto* Index = SlotById.Find(Id); if (!Index) return;
	auto& Slot = Batches[*Index / MissilesPerBatch].Slots[*Index % MissilesPerBatch];
	if (bImmediate)
	{ Slot.bAllocated = false; Slot.Generation = (Slot.Generation % 1000000) + 1; SlotById.Remove(Id); return; }
	if (Slot.FinishedAt < 0)
	{
		if (!ImpactPosition.ContainsNaN() && FVector::DistSquared(Slot.Position, ImpactPosition) <= FMath::Square(600.0)) Slot.Position = ImpactPosition;
		Slot.FinishedAt = GetWorld() ? GetWorld()->GetTimeSeconds() : FrameTime;
	}
}

void UGuLiMissileClusterPresentation::Upload(FBatch& Batch)
{
	auto* Component = Batch.Component.Get(); if (!Component) return;
	// Preserve the v2 body scale; exhaust dimensions are independently table-authored centimeters.
	const float VisualScale = GuLiVfx::Scale(this, GuLiVfxIds::MissileFlight).X * 1.5f;
	for (int32 I = 0; I < MissilesPerBatch; ++I)
	{
		const auto& Slot = Batch.Slots[I]; Batch.Positions[I] = Slot.Position; Batch.Directions[I] = Slot.Direction;
		Batch.Sizes[I] = FVector2D(35 * VisualScale, 325 * VisualScale);
		const float State = !Slot.bAllocated ? 0.f : Slot.FinishedAt < 0 ? 1.f : 2.f;
		Batch.Meta[I] = FLinearColor(float(Slot.Generation), State, float(Slot.FinishedAt), TrailLifetime);
	}
	using Arrays = UNiagaraDataInterfaceArrayFunctionLibrary;
	Component->SetVariableFloat(TEXT("User.MissileTime"), float(FrameTime));
	Component->SetVariableFloat(TEXT("User.MissileSmokeInitialWidth"), Batch.Visual.SmokeInitialWidthCentimeters);
	Component->SetVariableFloat(TEXT("User.MissileSmokeMaximumWidth"), Batch.Visual.SmokeMaximumWidthCentimeters);
	Component->SetVariableFloat(TEXT("User.MissileFlameWidth"), Batch.Visual.FlameWidthCentimeters);
	Component->SetVariableFloat(TEXT("User.MissileFlameLength"), Batch.Visual.FlameLengthCentimeters);
	Component->SetVariableFloat(TEXT("User.MissileTrailWeight"), FMath::Clamp(float((FrameTime - Batch.QualitySince) / HandoffSeconds), 0.f, 1.f));
	Arrays::SetNiagaraArrayPosition(Component, TEXT("User.LaserPositions"), Batch.Positions);
	Arrays::SetNiagaraArrayVector(Component, TEXT("User.LaserDirections"), Batch.Directions);
	Arrays::SetNiagaraArrayVector2D(Component, TEXT("User.LaserSizes"), Batch.Sizes);
	Arrays::SetNiagaraArrayColor(Component, TEXT("User.LaserColors"), Batch.Meta);
	Component->SetSystemFixedBounds(Batch.Bounds);
	Component->SetEmitterFixedBounds(TEXT("Body"), Batch.Bounds);
	Component->SetEmitterFixedBounds(TEXT("Flame"), Batch.Bounds);
	if (Batch.Quality > 0) Component->SetEmitterFixedBounds(TEXT("History"), Batch.Bounds);
}

bool UGuLiMissileClusterPresentation::StartBatch(FBatch& Batch, int32 Quality)
{
	if (!IsReady() || FrameTime < Batch.RetryAt) return false;
	Batch.Component = UNiagaraFunctionLibrary::SpawnSystemAtLocation(GetWorld(), Systems[Quality], FVector::ZeroVector,
		FRotator::ZeroRotator, FVector::OneVector, false, false, ENCPoolMethod::ManualRelease, false);
	auto* Component = Batch.Component.Get();
	if (!Component) { Batch.RetryAt = FrameTime + 1; return false; }
	if (auto* Overview = GetWorld()->GetSubsystem<UGuLiCommanderOverviewSubsystem>()) Overview->RegisterVisual(Component);
	Batch.Quality = Quality; Batch.QualitySince = FrameTime;
	Component->SetCastShadow(false);
	Component->SetVariableInt(TEXT("User.MissileSlotCount"), Batch.Capacity);
	Component->SetVariableInt(TEXT("User.MissileHistoryCount"), Batch.Capacity * Lanes[Quality]);
	Component->SetVariableFloat(TEXT("User.MissileHeads"), 1);
	Component->SetVariableFloat(TEXT("User.MissileEmitHistory"), 1);
	Upload(Batch); Component->Activate(true); return true;
}

void UGuLiMissileClusterPresentation::RetireHistory(FBatch& Batch)
{
	if (!Batch.Component.IsValid()) return;
	if (Batch.Quality == 0) { ReleaseComponent(Batch.Component); Batch.Quality = -1; return; }
	// Freeze these arrays: a replacement slot's generation can never erase this tail.
	Upload(Batch);
	auto* Component = Batch.Component.Get();
	Component->SetVariableFloat(TEXT("User.MissileHeads"), 0);
	Component->SetVariableFloat(TEXT("User.MissileEmitHistory"), 0);
	Component->SetVariableFloat(TEXT("User.MissileTrailWeight"), 1);
	Batch.Retiring = {Batch.Component, Batch.Bounds, FrameTime + TrailLifetime + .05,
		Batch.Capacity * (2 + Lanes[Batch.Quality]), Batch.Quality == 2 ? Batch.Occupied : 0};
	Batch.Component.Reset(); Batch.Quality = -1;
}

void UGuLiMissileClusterPresentation::EndFrame()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(GuLiMissileCluster);
	const double Started = FPlatformTime::Seconds();
	SystemCount = ParticleCapacity = ActiveMissiles = FullTrails = 0;
	auto* LOD = CommanderLOD.Get();
	if (!bFrameEnabled || !IsReady())
	{
		UpdateMilliseconds = 0;
		if (LOD) LOD->ClearConsumer(LODConsumer);
		return;
	}
	FGuLiCommanderLODConsumerStats LODStats;
	struct FCandidate { int32 Index; FGuLiCommanderLODDecision Decision; };
	TArray<FCandidate> Visible; int32 Used = 0, UsedFull = 0, Minimum = 0;
	for (int32 B = 0; B < Batches.Num(); ++B)
	{
		auto& Batch = Batches[B]; FBox Current(ForceInit); Batch.Occupied = 0;
		if (Batch.Retiring.Component.IsValid() && FrameTime >= Batch.Retiring.ReleaseTime)
		{ ReleaseComponent(Batch.Retiring.Component); Batch.Retiring = {}; }
		for (auto& Slot : Batch.Slots)
		{
			if (!Slot.bAllocated) continue;
			if (!Slot.bSubmitted && Slot.FinishedAt < 0) Slot.FinishedAt = FrameTime;
			if (Slot.FinishedAt >= 0 && FrameTime - Slot.FinishedAt > TrailLifetime + .05)
			{ SlotById.Remove(Slot.Id); Slot.bAllocated = false; continue; }
			++Batch.Occupied; Current += Slot.Position; if (Slot.FinishedAt < 0) ++ActiveMissiles;
		}
		Batch.RecentBounds.RemoveAll([this](const FBoundsSample& S) { return FrameTime - S.Time > TrailLifetime + .1; });
		if (Current.IsValid)
		{
			if (!Batch.RecentBounds.IsEmpty() && FrameTime - Batch.RecentBounds.Last().Time < .04) Batch.RecentBounds.Last().Bounds += Current;
			else Batch.RecentBounds.Add({Current, FrameTime});
		}
		Batch.Bounds = Current; for (const auto& Sample : Batch.RecentBounds) Batch.Bounds += Sample.Bounds;
		// Include exhaust offset, sprite half-width/overlap and <=27 cm drift around historical heads.
		const float BodyLength = 325.f * GuLiVfx::Scale(this, GuLiVfxIds::MissileFlight).X * 1.5f;
		const float Extent = FMath::Max(210.f, BodyLength * .5f + Batch.Visual.FlameLengthCentimeters
			+ FMath::Max(Batch.Visual.FlameWidthCentimeters, Batch.Visual.SmokeMaximumWidthCentimeters) * .62f + 27.f);
		if (Batch.Bounds.IsValid) Batch.Bounds = Batch.Bounds.ExpandBy(Extent);
		if (Batch.Retiring.Component.IsValid()) Batch.Bounds += Batch.Retiring.Bounds;
		if (!Batch.Occupied && !Batch.Retiring.Component.IsValid()) { Release(Batch); continue; }
		++LODStats.Objects;
		FGuLiCommanderLODQuery Query;
		Query.Bounds = Batch.Bounds;
		if (Batch.Component.IsValid() && Batch.Quality >= 0) Query.CurrentLevel = ToCommanderLevel(Batch.Quality);
		Query.LastChangeWorldSeconds = Batch.QualitySince;
		const auto Decision = LOD->Evaluate(Query);
		if (!Decision.bVisible)
		{
			if (Decision.Reason == EGuLiCommanderLODReason::NoLocalView) ++LODStats.NoView;
			else if (Decision.Reason == EGuLiCommanderLODReason::InvalidBounds) ++LODStats.InvalidBounds;
			else ++LODStats.FrustumCulled;
			Release(Batch); continue;
		}
		// Missile-only visibility limits: never install these as global unit culling rules.
		if (Decision.bOverviewOnly) { Release(Batch); continue; }
		if (!Decision.bCommanderView && Decision.DistanceCentimeters > FMath::Max(1.f, MaximumDistance.GetValueOnGameThread()))
		{ ++LODStats.DistanceCulled; Release(Batch); continue; }
		if (!Decision.bCommanderView && Decision.ScreenFraction < FMath::Max(0.f, MinimumScreenFraction.GetValueOnGameThread()))
		{ ++LODStats.ScreenCulled; Release(Batch); continue; }
		++LODStats.Visible;
		++LODStats.Target[static_cast<int32>(Decision.TargetLevel)];
		if (!Batch.Capacity) for (int32 I = 0; I < Batch.Slots.Num(); ++I) if (Batch.Slots[I].bAllocated) Batch.Capacity = I + 1;
		Visible.Add({B, Decision}); Minimum += Batch.Capacity * 2;
		Used += Batch.Capacity * (2 + (Batch.Component.IsValid() ? Lanes[Batch.Quality] : 0));
		UsedFull += Batch.Quality == 2 ? Batch.Occupied : 0;
		if (Batch.Retiring.Component.IsValid()) { Used += Batch.Retiring.Capacity; UsedFull += Batch.Retiring.FullTrails; }
	}
	Visible.Sort([](const FCandidate& A, const FCandidate& B) { return A.Decision.DistanceCentimeters < B.Decision.DistanceCentimeters; });
	// Reserve a minimal replacement per batch so downgrades can preserve history.
	const int32 Limit = FMath::Max(ParticleBudget.GetValueOnGameThread(), Minimum * 2);
	const int32 FullLimit = FMath::Max(0, FullTrailBudget.GetValueOnGameThread());
	for (const auto& Candidate : Visible)
	{
		auto& Batch = Batches[Candidate.Index];
		const int32 TargetQuality = ToNiagaraQuality(Candidate.Decision.TargetLevel);
		int32 Desired = TargetQuality;
		if (Used > Limit - Minimum || UsedFull > FullLimit) Desired = 0;
		if (!Batch.Component.IsValid())
		{
			while (Desired > 0 && (Used + Batch.Capacity * Lanes[Desired] > Limit - Minimum || (Desired == 2 && UsedFull + Batch.Occupied > FullLimit))) --Desired;
			if (StartBatch(Batch, Desired)) { Used += Batch.Capacity * Lanes[Desired]; if (Desired == 2) UsedFull += Batch.Occupied; }
		}
		else if (Desired != Batch.Quality && !Batch.Retiring.Component.IsValid() && Candidate.Decision.bCanTransition)
		{
			const int32 OldQuality = Batch.Quality;
			while (Desired > OldQuality && (Used + Batch.Capacity * (2 + Lanes[Desired])
				- (OldQuality == 0 ? Batch.Capacity * 2 : 0) > Limit - Minimum
				|| (Desired == 2 && UsedFull + Batch.Occupied > FullLimit))) --Desired;
			const int32 Extra = Batch.Capacity * (2 + Lanes[Desired]) - (OldQuality == 0 ? Batch.Capacity * 2 : 0);
			const int32 ExtraFull = Desired == 2 ? Batch.Occupied : 0;
			// Runtime budget reductions drain existing smoke for <=2.45 s.
			if (Desired != OldQuality && (Used + Extra <= Limit - (Desired ? Minimum : 0) || Desired == 0)
				&& (Desired != 2 || UsedFull + ExtraFull <= FullLimit))
			{
				RetireHistory(Batch);
				if (StartBatch(Batch, Desired)) { Used += Extra; UsedFull += ExtraFull; }
			}
		}
		Upload(Batch);
		if (Desired < TargetQuality) ++LODStats.BudgetDowngraded;
		if (Batch.Component.IsValid())
		{
			++LODStats.Applied[static_cast<int32>(ToCommanderLevel(Batch.Quality))];
			if (Batch.Quality != Desired) ++LODStats.TransitionPending;
			++SystemCount; ParticleCapacity += Batch.Capacity * (2 + Lanes[Batch.Quality]); if (Batch.Quality == 2) FullTrails += Batch.Occupied;
		}
		else ++LODStats.ResourcesUnavailable;
		if (auto* Retiring = Batch.Retiring.Component.Get())
		{
			Retiring->SetVariableFloat(TEXT("User.MissileTime"), float(FrameTime));
			++SystemCount; ParticleCapacity += Batch.Retiring.Capacity; FullTrails += Batch.Retiring.FullTrails;
		}
	}
	UpdateMilliseconds = (FPlatformTime::Seconds() - Started) * 1000;
	LOD->ReportConsumer(LODConsumer, LODStats);
	CSV_CUSTOM_STAT(GuLiMissileCluster, Systems, SystemCount, ECsvCustomStatOp::Set);
	CSV_CUSTOM_STAT(GuLiMissileCluster, ParticleCapacity, ParticleCapacity, ECsvCustomStatOp::Set);
	CSV_CUSTOM_STAT(GuLiMissileCluster, Missiles, ActiveMissiles, ECsvCustomStatOp::Set);
	CSV_CUSTOM_STAT(GuLiMissileCluster, FullTrails, FullTrails, ECsvCustomStatOp::Set);
	CSV_CUSTOM_STAT(GuLiMissileCluster, UpdateMs, UpdateMilliseconds, ECsvCustomStatOp::Set);
}
