#include "Gameplay/Vfx/GuLiVfxBindingComponent.h"
#include "Gameplay/Vfx/GuLiVfxRegistrySubsystem.h"
#include "GameFramework/Actor.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"

void UGuLiVfxBindingComponent::BeginPlay()
{
	Super::BeginPlay();
	if (GetNetMode() == NM_DedicatedServer || !GetOwner()) return;
	TInlineComponentArray<UNiagaraComponent*> Components(GetOwner());
	for (const auto& Binding : Bindings)
	{
		auto* Found = Components.FindByPredicate([&](const UNiagaraComponent* Component) { return Component->GetFName() == Binding.ComponentName; });
		if (!Found) { UE_LOG(LogTemp, Error, TEXT("VFX binding component %s not found on %s"), *Binding.ComponentName.ToString(), *GetOwner()->GetName()); continue; }
		auto* Component = *Found;
		Component->SetAsset(GuLiVfx::Load<UNiagaraSystem>(this, Binding.VfxId));
#if WITH_EDITORONLY_DATA
		// Deferred editor compilation belongs to resource preparation, before the
		// preserved Blueprint first activates a mining/construction beam in combat.
		if (auto* System=Component->GetAsset()) System->PollForCompilationComplete();
#endif
		Component->SetRelativeScale3D(GuLiVfx::Scale(this, Binding.VfxId, Component->GetRelativeScale3D()));
	}
}
