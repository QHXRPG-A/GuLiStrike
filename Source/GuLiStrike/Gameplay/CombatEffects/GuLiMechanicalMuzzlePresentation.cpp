#include "Gameplay/CombatEffects/GuLiCombatEffectPresentationSubsystem.h"

#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "Gameplay/Vfx/GuLiVfxRegistrySubsystem.h"
#include "Gameplay/Data/Generated/GuLiVfxIds.h"

void UGuLiCombatEffectPresentationSubsystem::ResetMechanicalMuzzles()
{
	for (auto& Item : MechanicalMuzzles)
		if (IsValid(Item.Component)) { Item.Component->DeactivateImmediate(); Item.Component->ReleaseToPool(); }
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
			if (IsValid(Item.Component)) { Item.Component->DeactivateImmediate(); Item.Component->ReleaseToPool(); }
			MechanicalMuzzles.RemoveAtSwap(Index, 1, EAllowShrinking::No);
			continue;
		}
		if (Age < 0.0f) continue;
		if (!Item.bStarted)
		{
			// Flash is a burst. Missed presentation frames must not replay an old shot.
			if (Age > 0.20f) { MechanicalMuzzles.RemoveAtSwap(Index, 1, EAllowShrinking::No); continue; }
			// The existing registry entry resolves exactly to the requested NS_Flash_1.
			if (!LoadedMechanicalMuzzleSystem) LoadedMechanicalMuzzleSystem = GuLiVfx::Load<UNiagaraSystem>(this, GuLiVfxIds::MachineGunImpact);
			Item.Component = LoadedMechanicalMuzzleSystem ? UNiagaraFunctionLibrary::SpawnSystemAtLocation(GetWorld(),
				LoadedMechanicalMuzzleSystem, Muzzle.GetLocation(), Muzzle.Rotator(), GuLiVfx::Scale(this, GuLiVfxIds::MachineGunImpact),
				false, false, ENCPoolMethod::ManualRelease, false) : nullptr;
			Item.bStarted = true;
			if (Item.Component) { Item.Component->SetCastShadow(false); Item.Component->Activate(true); ++Counters.BurstsPlayed; }
		}
		if (IsValid(Item.Component))
		{
			Item.Component->SetWorldLocationAndRotation(Muzzle.GetLocation(), Muzzle.GetRotation());
			if (Age > 0.24f) Item.Component->Deactivate();
		}
	}
}
