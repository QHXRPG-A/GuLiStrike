#include "Gameplay/CombatEffects/GuLiCombatEffectPresentationSubsystem.h"
#include "Commander/Presentation/GuLiCommanderOverviewSubsystem.h"
#include "Engine/World.h"

#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "Gameplay/Vfx/GuLiVfxRegistrySubsystem.h"
#include "Gameplay/Data/Generated/GuLiVfxIds.h"

FVector UGuLiCombatEffectPresentationSubsystem::EvaluateLaunchVisualOffset(
	FGuLiLocalCombatEffect& Visual, FName Slot, float RenderTime)
{
	constexpr float ConvergenceSeconds = .12f;
	const float Age = RenderTime - Visual.State.StartTime;
	if (Age < 0) return FVector::ZeroVector;
	if (!Visual.bLaunchVisualOffsetResolved)
	{
		Visual.bLaunchVisualOffsetResolved = true;
		if (Age < ConvergenceSeconds && Visual.State.Source.Kind == EGuLiTargetKind::CommanderSoldier)
			if (const auto* Provider = MuzzleProviders.Find(Visual.State.Source.Kind);
				Provider && Provider->Owner.IsValid() && Provider->LaunchOffset)
				Provider->LaunchOffset(Visual.State.Source, Slot, Visual.State.LaunchLocation, Visual.LaunchVisualOffset);
	}
	// A terminal network state can arrive ahead of the buffered presentation clock.
	// The caller handles the actual displayed impact; do not drop the offset early.
	if (Age >= ConvergenceSeconds) return FVector::ZeroVector;
	const float T = FMath::Clamp(Age / ConvergenceSeconds, 0.0f, 1.0f);
	return Visual.LaunchVisualOffset * (1 - T*T*(3-2*T));
}

void UGuLiCombatEffectPresentationSubsystem::ResetMechanicalMuzzles()
{
	for (auto& Item : MechanicalMuzzles)
		if (IsValid(Item.Component)) { Item.Component->DeactivateImmediate(); UGuLiCommanderOverviewSubsystem::ForgetVisual(Item.Component); Item.Component->ReleaseToPool(); }
	MechanicalMuzzles.Reset();
	LoadedMechanicalMuzzleSystem = nullptr;
}

void UGuLiCombatEffectPresentationSubsystem::UpdateMechanicalMuzzles(float Now, bool bEnabled)
{
	for (int32 Index = MechanicalMuzzles.Num()-1; Index >= 0; --Index)
	{
		auto& Item = MechanicalMuzzles[Index];
		FTransform Muzzle;
		float RenderTime = Now;
		const bool bResolved = ResolveMuzzleTransform(Item.Cue, Muzzle, RenderTime);
		const float Age = RenderTime-Item.Cue.MechanicalPoseTimeSeconds;
		if (!bEnabled || !bResolved || !IsVisibleLocation(Muzzle.GetLocation()) || Age > 1.0f
			|| Now-Item.Cue.ServerTime > 2.0f || (Item.bStarted && (!IsValid(Item.Component) || Item.Component->IsComplete())))
		{
			if (IsValid(Item.Component)) { Item.Component->DeactivateImmediate(); UGuLiCommanderOverviewSubsystem::ForgetVisual(Item.Component); Item.Component->ReleaseToPool(); }
			MechanicalMuzzles.RemoveAtSwap(Index, 1, EAllowShrinking::No);
			continue;
		}
		if (Age < 0.0f) continue;
		if (!Item.bStarted)
		{
			// Flash is a burst. Missed presentation frames must not replay an old shot.
			if (Age > 0.20f) { MechanicalMuzzles.RemoveAtSwap(Index, 1, EAllowShrinking::No); continue; }
			// Muzzle and hit sizes are independent even though they reuse the same flash asset.
			if (!LoadedMechanicalMuzzleSystem) LoadedMechanicalMuzzleSystem = GuLiVfx::Load<UNiagaraSystem>(this, GuLiVfxIds::GroundMachineGunMuzzle);
			const float MuzzleScale = Item.Cue.Source.Kind == EGuLiTargetKind::CommanderSoldier && Item.Cue.UnitTypeId == 2 ? 2.0f : 1.0f;
			Item.Component = LoadedMechanicalMuzzleSystem ? UNiagaraFunctionLibrary::SpawnSystemAtLocation(GetWorld(),
				LoadedMechanicalMuzzleSystem, Muzzle.GetLocation(), Muzzle.Rotator(), GuLiVfx::Scale(this, GuLiVfxIds::GroundMachineGunMuzzle, FVector(MuzzleScale)),
				false, false, ENCPoolMethod::ManualRelease, false) : nullptr;
			Item.bStarted = true;
			if (auto* Overview = GetWorld()->GetSubsystem<UGuLiCommanderOverviewSubsystem>()) Overview->RegisterVisual(Item.Component);
			if (Item.Component) { Item.Component->SetCastShadow(false); Item.Component->Activate(true); ++Counters.BurstsPlayed; }
		}
		if (IsValid(Item.Component))
		{
			Item.Component->SetWorldLocationAndRotation(Muzzle.GetLocation(), Muzzle.GetRotation());
			if (Age > 0.24f) Item.Component->Deactivate();
		}
	}
}
