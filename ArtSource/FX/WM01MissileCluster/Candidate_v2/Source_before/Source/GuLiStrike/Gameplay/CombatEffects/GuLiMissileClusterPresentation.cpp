#include "Gameplay/CombatEffects/GuLiMissileClusterPresentation.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Gameplay/Vfx/GuLiVfxRegistrySubsystem.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "NiagaraComponent.h"
#include "NiagaraDataInterfaceArrayFunctionLibrary.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "ProfilingDebugging/CsvProfiler.h"
#include "SceneView.h"

CSV_DEFINE_CATEGORY(GuLiMissileCluster, true);
namespace
{
	TAutoConsoleVariable<int32> FullTrailBudget(TEXT("gs.MissileCluster.FullTrails"), 512, TEXT("Full trails, including fading slots and quality handoffs."));
	TAutoConsoleVariable<int32> ParticleBudget(TEXT("gs.MissileCluster.Particles"), 65536, TEXT("GPU capacity budget; bodies/flames are the minimum. Lower budgets drain existing history before reclaiming it."));
	TAutoConsoleVariable<float> MaximumDistance(TEXT("gs.MissileCluster.Distance"), 20000.f, TEXT("Visual distance in cm; culling includes 1.2 seconds of historical smoke."));
	TAutoConsoleVariable<float> MinimumScreenFraction(TEXT("gs.MissileCluster.MinScreenFraction"), .001f, TEXT("Cull below this projected history-bound fraction."));
	constexpr float CellSize = 4000.f;
	constexpr int32 Lanes[] = {0, 8, 24};
	constexpr int32 Ids[] = {GuLiVfxIds::WM01MissileClusterMinimal, GuLiVfxIds::WM01MissileClusterLite, GuLiVfxIds::WM01MissileClusterFull};
	struct FView { FVector Position; FConvexVolume Frustum; float ProjectionScale = 1; };
}

UWorld* UGuLiMissileClusterPresentation::GetWorld() const { return GetOuter() ? GetOuter()->GetWorld() : nullptr; }

void UGuLiMissileClusterPresentation::ReleaseComponent(TWeakObjectPtr<UNiagaraComponent>& Weak)
{
	if (auto* Component = Weak.Get()) { Component->DeactivateImmediate(); Component->ReleaseToPool(); }
	Weak.Reset();
}

void UGuLiMissileClusterPresentation::Release(FBatch& Batch)
{
	ReleaseComponent(Batch.Component); ReleaseComponent(Batch.Retiring.Component);
	Batch.Retiring = {}; Batch.Quality = -1;
}

void UGuLiMissileClusterPresentation::Reset()
{
	for (auto& Batch : Batches) Release(Batch);
	Batches.Reset(); SlotById.Reset(); SystemCount = ParticleCapacity = ActiveMissiles = FullTrails = 0;
	UpdateMilliseconds = 0;
}

void UGuLiMissileClusterPresentation::BeginFrame(double Now, bool bEnabled)
{
	if (Now < FrameTime) { Reset(); NextResourceCheck = 0; }
	FrameTime = Now;
	bFrameEnabled = bEnabled && GetWorld() && GetWorld()->GetNetMode() != NM_DedicatedServer;
	if (!bFrameEnabled) { Reset(); return; }
	if (!bResourcesReady && Now >= NextResourceCheck)
	{
		NextResourceCheck = Now + 1; Systems.SetNum(3); bResourcesReady = true;
		for (int32 Q = 0; Q < 3; ++Q)
		{
			Systems[Q] = GuLiVfx::Load<UNiagaraSystem>(this, Ids[Q]);
			const FNiagaraVariable Contract(FNiagaraTypeDefinition::GetIntDef(), TEXT("User.MissileContractVersion"));
			bResourcesReady &= Systems[Q] && Systems[Q]->HasAnyGPUEmitters() && Systems[Q]->IsReadyToRun()
				&& Systems[Q]->GetExposedParameters().GetParameterValueOrDefault<int32>(Contract, 0) == 2;
		}
	}
	for (auto& Batch : Batches) for (auto& Slot : Batch.Slots) Slot.bSubmitted = false;
}

int32 UGuLiMissileClusterPresentation::Acquire(const FGuid& Id, const FVector& Position)
{
	if (const auto* Found = SlotById.Find(Id)) return *Found;
	const FIntVector Cell(FMath::FloorToInt(Position.X / CellSize), FMath::FloorToInt(Position.Y / CellSize), FMath::FloorToInt(Position.Z / CellSize));
	int32 BatchIndex = INDEX_NONE, SlotIndex = INDEX_NONE, EmptyBatch = INDEX_NONE;
	for (int32 B = 0; B < Batches.Num(); ++B)
	{
		const auto& Batch = Batches[B];
		if (!Batch.Slots.ContainsByPredicate([](const FSlot& S) { return S.bAllocated; }) && !Batch.Retiring.Component.IsValid())
		{ EmptyBatch = B; continue; }
		if (Batch.Cell != Cell) continue;
		const int32 Limit = Batch.Capacity > 0 ? Batch.Capacity : MissilesPerBatch;
		for (int32 I = 0; I < Limit; ++I) if (!Batch.Slots[I].bAllocated) { SlotIndex = I; break; }
		if (SlotIndex != INDEX_NONE) { BatchIndex = B; break; }
	}
	if (BatchIndex == INDEX_NONE)
	{
		BatchIndex = EmptyBatch != INDEX_NONE ? EmptyBatch : Batches.AddDefaulted();
		auto& Batch = Batches[BatchIndex]; Release(Batch); Batch.Cell = Cell; Batch.RecentBounds.Reset();
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

bool UGuLiMissileClusterPresentation::Submit(const FGuid& Id, const FVector& Position, const FVector& Direction)
{
	if (!bFrameEnabled || !IsReady() || !Id.IsValid() || Position.ContainsNaN() || Direction.ContainsNaN()) return false;
	const int32 Index = Acquire(Id, Position); auto& Batch = Batches[Index / MissilesPerBatch];
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
	// Preserve the existing mesh's scale; smoke width remains independent.
	const float VisualScale = GuLiVfx::Scale(this, GuLiVfxIds::MissileFlight).X;
	for (int32 I = 0; I < MissilesPerBatch; ++I)
	{
		const auto& Slot = Batch.Slots[I]; Batch.Positions[I] = Slot.Position; Batch.Directions[I] = Slot.Direction;
		Batch.Sizes[I] = FVector2D(35 * VisualScale, 325 * VisualScale);
		const float State = !Slot.bAllocated ? 0.f : Slot.FinishedAt < 0 ? 1.f : 2.f;
		Batch.Meta[I] = FLinearColor(float(Slot.Generation), State, float(Slot.FinishedAt), TrailLifetime);
	}
	using Arrays = UNiagaraDataInterfaceArrayFunctionLibrary;
	Component->SetVariableFloat(TEXT("User.MissileTime"), float(FrameTime));
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
	if (!bFrameEnabled || !IsReady()) return;
	TArray<FView> Views;
	for (auto It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		const auto* Controller = It->Get(); const auto* Player = Controller ? Controller->GetLocalPlayer() : nullptr;
		FSceneViewProjectionData Projection;
		if (!Player || !Player->ViewportClient || !Player->ViewportClient->Viewport
			|| !Player->GetProjectionData(Player->ViewportClient->Viewport, Projection)) continue;
		auto& View = Views.AddDefaulted_GetRef(); View.Position = Projection.ViewOrigin;
		View.ProjectionScale = FMath::Max(float(Projection.ProjectionMatrix.M[0][0]), float(Projection.ProjectionMatrix.M[1][1]));
		GetViewFrustumBounds(View.Frustum, Projection.ComputeViewProjectionMatrix(), true);
	}
	struct FCandidate { int32 Index; double Distance; float Screen; };
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
		// Includes the body, exhaust offset, <=18 cm drift and <=22 cm smoke width.
		if (Batch.Bounds.IsValid) Batch.Bounds = Batch.Bounds.ExpandBy(140);
		if (Batch.Retiring.Component.IsValid()) Batch.Bounds += Batch.Retiring.Bounds;
		if ((!Batch.Occupied && !Batch.Retiring.Component.IsValid()) || !Batch.Bounds.IsValid) { Release(Batch); continue; }
		double Closest = TNumericLimits<double>::Max(); float Screen = 0;
		for (const auto& View : Views)
		{
			if (!View.Frustum.IntersectBox(Batch.Bounds.GetCenter(), Batch.Bounds.GetExtent())) continue;
			const double Distance = FMath::Sqrt(Batch.Bounds.ComputeSquaredDistanceToPoint(View.Position));
			Closest = FMath::Min(Closest, Distance);
			Screen = FMath::Max(Screen, float(Batch.Bounds.GetExtent().Size() / FMath::Max(1.0, Distance)) * View.ProjectionScale);
		}
		if (Closest > FMath::Max(1.f, MaximumDistance.GetValueOnGameThread()) || Screen < FMath::Max(0.f, MinimumScreenFraction.GetValueOnGameThread()))
		{ Release(Batch); continue; }
		if (!Batch.Capacity) for (int32 I = 0; I < Batch.Slots.Num(); ++I) if (Batch.Slots[I].bAllocated) Batch.Capacity = I + 1;
		Visible.Add({B, Closest, Screen}); Minimum += Batch.Capacity * 2;
		Used += Batch.Capacity * (2 + (Batch.Component.IsValid() ? Lanes[Batch.Quality] : 0));
		UsedFull += Batch.Quality == 2 ? Batch.Occupied : 0;
		if (Batch.Retiring.Component.IsValid()) { Used += Batch.Retiring.Capacity; UsedFull += Batch.Retiring.FullTrails; }
	}
	Visible.Sort([](const FCandidate& A, const FCandidate& B) { return A.Distance < B.Distance; });
	// Reserve a minimal replacement per batch so downgrades can preserve history.
	const int32 Limit = FMath::Max(ParticleBudget.GetValueOnGameThread(), Minimum * 2);
	const int32 FullLimit = FMath::Max(0, FullTrailBudget.GetValueOnGameThread());
	for (const auto& Candidate : Visible)
	{
		auto& Batch = Batches[Candidate.Index];
		const bool bFullDistance = Candidate.Distance < (Batch.Quality == 2 ? 8800 : 7200) && Candidate.Screen > (Batch.Quality == 2 ? .008f : .012f);
		const bool bLiteDistance = Candidate.Distance < (Batch.Quality >= 1 ? 16000 : 14000) && Candidate.Screen > (Batch.Quality >= 1 ? .002f : .003f);
		int32 Desired = bFullDistance ? 2 : bLiteDistance ? 1 : 0;
		if (Used > Limit - Minimum || UsedFull > FullLimit) Desired = 0;
		if (!Batch.Component.IsValid())
		{
			while (Desired > 0 && (Used + Batch.Capacity * Lanes[Desired] > Limit - Minimum || (Desired == 2 && UsedFull + Batch.Occupied > FullLimit))) --Desired;
			if (StartBatch(Batch, Desired)) { Used += Batch.Capacity * Lanes[Desired]; if (Desired == 2) UsedFull += Batch.Occupied; }
		}
		else if (Desired != Batch.Quality && !Batch.Retiring.Component.IsValid() && FrameTime - Batch.QualitySince >= .35)
		{
			const int32 OldQuality = Batch.Quality;
			while (Desired > OldQuality && (Used + Batch.Capacity * (2 + Lanes[Desired])
				- (OldQuality == 0 ? Batch.Capacity * 2 : 0) > Limit - Minimum
				|| (Desired == 2 && UsedFull + Batch.Occupied > FullLimit))) --Desired;
			const int32 Extra = Batch.Capacity * (2 + Lanes[Desired]) - (OldQuality == 0 ? Batch.Capacity * 2 : 0);
			const int32 ExtraFull = Desired == 2 ? Batch.Occupied : 0;
			// Runtime budget reductions drain existing smoke for <=1.25 s.
			if (Desired != OldQuality && (Used + Extra <= Limit - (Desired ? Minimum : 0) || Desired == 0)
				&& (Desired != 2 || UsedFull + ExtraFull <= FullLimit))
			{
				RetireHistory(Batch);
				if (StartBatch(Batch, Desired)) { Used += Extra; UsedFull += ExtraFull; }
			}
		}
		Upload(Batch);
		if (Batch.Component.IsValid())
		{ ++SystemCount; ParticleCapacity += Batch.Capacity * (2 + Lanes[Batch.Quality]); if (Batch.Quality == 2) FullTrails += Batch.Occupied; }
		if (auto* Retiring = Batch.Retiring.Component.Get())
		{
			Retiring->SetVariableFloat(TEXT("User.MissileTime"), float(FrameTime));
			++SystemCount; ParticleCapacity += Batch.Retiring.Capacity; FullTrails += Batch.Retiring.FullTrails;
		}
	}
	UpdateMilliseconds = (FPlatformTime::Seconds() - Started) * 1000;
	CSV_CUSTOM_STAT(GuLiMissileCluster, Systems, SystemCount, ECsvCustomStatOp::Set);
	CSV_CUSTOM_STAT(GuLiMissileCluster, ParticleCapacity, ParticleCapacity, ECsvCustomStatOp::Set);
	CSV_CUSTOM_STAT(GuLiMissileCluster, Missiles, ActiveMissiles, ECsvCustomStatOp::Set);
	CSV_CUSTOM_STAT(GuLiMissileCluster, FullTrails, FullTrails, ECsvCustomStatOp::Set);
	CSV_CUSTOM_STAT(GuLiMissileCluster, UpdateMs, UpdateMilliseconds, ECsvCustomStatOp::Set);
}
