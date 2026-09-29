#include "Gameplay/Presentation/GuLiWarMachineHoverComponent.h"

#include "Engine/World.h"
#include "Gameplay/Vfx/GuLiVfxRegistrySubsystem.h"
#include "HAL/IConsoleManager.h"
#include "NiagaraComponent.h"
#include "NiagaraDataInterfaceArrayFunctionLibrary.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "ProfilingDebugging/CsvProfiler.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

CSV_DEFINE_CATEGORY(GuLiHover, true);
namespace
{
	TAutoConsoleVariable<int32> EnableHoverFX(TEXT("gs.Commander.HoverFX"), 1,
		TEXT("WarMachine exhaust only: 0=off, 1=on. Does not change unit visibility or animation."));
	TAutoConsoleVariable<int32> LogHoverStats(TEXT("gs.Commander.HoverFX.LogStats"), 0,
		TEXT("Log hover Niagara system/nozzle/trail counts once per second for performance capture."));
	void Release(TWeakObjectPtr<UNiagaraComponent>& Component)
	{
		if (auto* C = Component.Get()) { C->DeactivateImmediate(); C->ReleaseToPool(); }
		Component.Reset();
	}
}

UGuLiWarMachineHoverComponent::UGuLiWarMachineHoverComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(false);
}

int32 UGuLiWarMachineHoverComponent::Acquire(uint64 Key)
{
	if (const auto* Existing = SlotByKey.Find(Key)) return *Existing;
	if (FreeSlots.IsEmpty())
	{
		const int32 First = Slots.Num();
		Slots.AddDefaulted(NozzlesPerBatch);
		auto& B = Batches.AddDefaulted_GetRef();
		B.Positions.Init(FVector::ZeroVector, NozzlesPerBatch);
		B.Directions.Init(-FVector::UpVector, NozzlesPerBatch);
		B.Sizes.Init(FVector2D::ZeroVector, NozzlesPerBatch);
		B.Colors.Init(FLinearColor::Transparent, NozzlesPerBatch);
		B.TrailPositions.Init(FVector::ZeroVector, NozzlesPerBatch);
		B.TrailSizes.Init(FVector2D::ZeroVector, B.TrailPositions.Num());
		B.TrailColors.Init(FLinearColor::Transparent, B.TrailPositions.Num());
		for (int32 I = First + NozzlesPerBatch - 1; I >= First; --I) FreeSlots.Add(I);
	}
	const int32 I = FreeSlots.Pop(EAllowShrinking::No);
	auto& Slot = Slots[I];
	const uint32 NextGeneration = Slot.Generation + 1;
	Slot = {}; Slot.Key = Key; Slot.Generation = NextGeneration; Slot.bAllocated = true;
	SlotByKey.Add(Key, I);
	return I;
}

float UGuLiWarMachineHoverComponent::FadeAt(const FVector& Position) const
{
	return FMath::Clamp((20000.0f - float(FVector::Distance(CameraPosition, Position))) / 2000.0f, 0.0f, 1.0f);
}

void UGuLiWarMachineHoverComponent::BeginFrame(double Now, const FVector& Camera)
{
	if (Now < FrameTime) Reset();
	FrameTime = Now; CameraPosition = Camera;
	ActiveNozzles = MovingNozzles = 0;
	bFrameEnabled = GetNetMode() != NM_DedicatedServer && EnableHoverFX.GetValueOnGameThread() != 0;
	if (!bFrameEnabled) { if (!Batches.IsEmpty()) Reset(); return; }
	for (auto& Slot : Slots) Slot.bSubmitted = false;
	for (auto& B : Batches)
	{
		B.Colors.Init(FLinearColor::Transparent, NozzlesPerBatch);
		B.Sizes.Init(FVector2D::ZeroVector, NozzlesPerBatch);
		B.TrailColors.Init(FLinearColor::Transparent, NozzlesPerBatch);
		B.TrailSizes.Init(FVector2D::ZeroVector, NozzlesPerBatch);
		B.Bounds = FBox(ForceInit);
	}
}

void UGuLiWarMachineHoverComponent::Submit(const FGuLiHoverNozzleSource& Source)
{
	if (!bFrameEnabled || Source.Disc >= 4 || Source.DiscDiameter <= 0 || Source.Position.ContainsNaN() || Source.Direction.ContainsNaN()) return;
	const float Fade = FadeAt(Source.Position);
	if (Fade <= 0) return;
	const int32 Index = Acquire((uint64(Source.UnitId) << 2) | Source.Disc);
	auto& Slot = Slots[Index];
	auto& B = Batches[Index / NozzlesPerBatch]; const int32 I = Index % NozzlesPerBatch;
	Slot.bSubmitted = true;
	if (Source.bReset || FrameTime - Slot.LastSeenTime > TrailLifetime)
	{
		++Slot.Generation;
		// GPU lanes retain old world snapshots and suppress emission on a generation change.
	}
	Slot.LastSeenTime = FrameTime;
	const FVector Down = Source.Direction.GetSafeNormal();
	const float Length = FMath::Lerp(60.0f, 90.0f, FMath::Clamp(Source.HorizontalSpeed / 1440.0f, 0.0f, 1.0f));
	const float Width = Source.DiscDiameter * 1.10f;
	B.Positions[I] = Source.Position + Down * (Length * .5f);
	B.Directions[I] = Down;
	B.Sizes[I] = FVector2D(Width, Length);
	B.Colors[I] = FLinearColor(.04f, .28f, 1.0f, Fade * .90f);
	B.TrailPositions[I] = Source.Position + Down * (Length * .65f);
	B.TrailSizes[I] = FVector2D(Width, Source.bReset ? 0 : Source.HorizontalSpeed);
	B.TrailColors[I] = FLinearColor(float(Slot.Generation), 1, 0, 0);
	B.Bounds += FBox(Source.Position - FVector(Width), Source.Position + FVector(Width));
	B.Bounds += Source.Position + Down * Length;
	++ActiveNozzles;
	if (!Source.bReset && Source.HorizontalSpeed > 5.0f) ++MovingNozzles;
}

void UGuLiWarMachineHoverComponent::EndFrame()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(GuLiWarMachineHover);
	if (!bFrameEnabled)
	{
		CSV_CUSTOM_STAT(GuLiHover, Nozzles, 0, ECsvCustomStatOp::Set);
		CSV_CUSTOM_STAT(GuLiHover, MovingNozzles, 0, ECsvCustomStatOp::Set);
		CSV_CUSTOM_STAT(GuLiHover, NiagaraSystems, 0, ECsvCustomStatOp::Set);
		CSV_CUSTOM_STAT(GuLiHover, NiagaraParticleCapacity, 0, ECsvCustomStatOp::Set);
		return;
	}
	for (int32 Index = 0; Index < Slots.Num(); ++Index)
	{
		auto& Slot = Slots[Index]; if (!Slot.bAllocated) continue;
		// Keep the retired slot until its complete world-space tail has expired.
		if (!Slot.bSubmitted && FrameTime - Slot.LastSeenTime >= TrailLifetime)
		{ SlotByKey.Remove(Slot.Key); Slot.bAllocated = false; FreeSlots.Add(Index); }
	}
	for (auto& B : Batches)
	{
		B.RecentBounds.RemoveAll([this](const auto& Sample) { return FrameTime - Sample.Time > TrailLifetime + .025; });
		if (B.Bounds.IsValid)
		{
			// One conservative history envelope per batch, not per-particle CPU simulation.
			if (!B.RecentBounds.IsEmpty() && FrameTime - B.RecentBounds.Last().Time < .025)
				B.RecentBounds.Last().Bounds += B.Bounds;
			else B.RecentBounds.Add({B.Bounds, FrameTime});
		}
		for (const auto& Sample : B.RecentBounds) B.Bounds += Sample.Bounds;
		if (!B.Bounds.IsValid) { Release(B.Component); continue; }
		bool bActivate = false;
		if (!B.Component.IsValid())
		{
			auto* System = GuLiVfx::Load<UNiagaraSystem>(this, GuLiVfxIds::WarMachineHover);
			if (!System) continue;
			if (!System->HasAnyGPUEmitters())
			{
				if (!bReportedIncompatibleAsset) UE_LOG(LogTemp, Warning, TEXT("WarMachineHover asset is still a CPU prototype; run build_warmachine_hover_fx.py with the new editor module before gameplay validation."));
				bReportedIncompatibleAsset = true; continue;
			}
			B.Component = UNiagaraFunctionLibrary::SpawnSystemAtLocation(GetWorld(), System, FVector::ZeroVector,
				FRotator::ZeroRotator, FVector::OneVector, false, false, ENCPoolMethod::ManualRelease, false);
			if (!B.Component.IsValid()) continue;
			B.Component->SetCastShadow(false); bActivate = true;
		}
		using Arrays = UNiagaraDataInterfaceArrayFunctionLibrary;
		auto* Component = B.Component.Get();
		Component->SetVariableFloat(TEXT("User.HoverTime"), float(FrameTime));
		Component->SetVariablePosition(TEXT("User.HoverCamera"), CameraPosition);
		Arrays::SetNiagaraArrayPosition(Component, TEXT("User.LaserPositions"), B.Positions);
		Arrays::SetNiagaraArrayVector(Component, TEXT("User.LaserDirections"), B.Directions);
		Arrays::SetNiagaraArrayVector2D(Component, TEXT("User.LaserSizes"), B.Sizes);
		Arrays::SetNiagaraArrayColor(Component, TEXT("User.LaserColors"), B.Colors);
		Arrays::SetNiagaraArrayPosition(Component, TEXT("User.MuzzlePositions"), B.TrailPositions);
		Arrays::SetNiagaraArrayVector2D(Component, TEXT("User.MuzzleSizes"), B.TrailSizes);
		Arrays::SetNiagaraArrayColor(Component, TEXT("User.MuzzleColors"), B.TrailColors);
		Component->SetSystemFixedBounds(B.Bounds.ExpandBy(80 * TrailLifetime + 50));
		if (bActivate) Component->Activate(true);
	}
	CSV_CUSTOM_STAT(GuLiHover, Nozzles, ActiveNozzles, ECsvCustomStatOp::Set);
	CSV_CUSTOM_STAT(GuLiHover, MovingNozzles, MovingNozzles, ECsvCustomStatOp::Set);
	CSV_CUSTOM_STAT(GuLiHover, NiagaraSystems, GetActiveSystems(), ECsvCustomStatOp::Set);
	CSV_CUSTOM_STAT(GuLiHover, NiagaraParticleCapacity, GetActiveSystems() * NozzlesPerBatch * (1 + TrailSamplesPerNozzle), ECsvCustomStatOp::Set);
	if (LogHoverStats.GetValueOnGameThread() && FMath::FloorToInt(FrameTime) != FMath::FloorToInt(FrameTime - GetWorld()->GetDeltaSeconds()))
		UE_LOG(LogTemp, Display, TEXT("HoverFX GPU systems=%d nozzles=%d moving_nozzles=%d particle_capacity=%d (capacity, not live count)"),
			GetActiveSystems(), ActiveNozzles, MovingNozzles, GetActiveSystems() * NozzlesPerBatch * (1 + TrailSamplesPerNozzle));
}

int32 UGuLiWarMachineHoverComponent::GetActiveSystems() const
{
	int32 Count = 0; for (const auto& B : Batches) if (B.Component.IsValid()) ++Count; return Count;
}

void UGuLiWarMachineHoverComponent::Reset()
{
	for (auto& B : Batches) Release(B.Component);
	Batches.Reset(); Slots.Reset(); FreeSlots.Reset(); SlotByKey.Reset();
	ActiveNozzles = MovingNozzles = 0;
}

void UGuLiWarMachineHoverComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	Reset(); Super::EndPlay(Reason);
}
