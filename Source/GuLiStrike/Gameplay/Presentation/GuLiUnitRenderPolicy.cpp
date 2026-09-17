#include "Gameplay/Presentation/GuLiUnitRenderPolicy.h"

#include "Components/MeshComponent.h"
#include "GameFramework/Actor.h"
#include "GuLiStrike.h"
#include "Materials/MaterialInterface.h"

UMaterialInterface* UGuLiUnitRenderSettings::ResolveBodyMaterial(UMaterialInterface* Original)
{
	if (!Original) return nullptr;
	const FSoftObjectPath Path(Original);
	if (const auto* Cached = LoadedVariants.Find(Path)) return Cached->Get();
	const auto* Entry = MaterialVariants.FindByPredicate(
		[&Path](const FGuLiUnitMaterialVariant& Candidate)
		{ return Candidate.Original.ToSoftObjectPath() == Path; });
	// Already converted materials, effect materials and unrelated meshes are unchanged.
	if (!Entry) return Original;
	UMaterialInterface* Variant = Entry->Variant.LoadSynchronous();
	if (!Variant)
	{
		UE_LOG(LogGuLiStrike, Error, TEXT("Missing unit body material variant %s for %s"),
			*Entry->Variant.ToString(), *Path.ToString());
		Variant = Original;
	}
	LoadedVariants.Add(Path, Variant);
	return Variant;
}

void GuLiUnitRenderPolicy::ApplyReflectionExclusions(UPrimitiveComponent& Component)
{
	Component.SetVisibleInRayTracing(false);
	Component.SetAffectDistanceFieldLighting(false);
	Component.SetAffectDynamicIndirectLighting(false);
	// UE 5.7 has no setters for these two capture flags.
	if (Component.bVisibleInReflectionCaptures || Component.bVisibleInRealTimeSkyCaptures)
	{
		Component.bVisibleInReflectionCaptures = false;
		Component.bVisibleInRealTimeSkyCaptures = false;
		Component.MarkRenderStateDirty();
	}
}

void GuLiUnitRenderPolicy::ApplyBodyMaterials(UMeshComponent& Component)
{
	if (Component.GetNetMode() == NM_DedicatedServer) return;
	auto* Settings = GetMutableDefault<UGuLiUnitRenderSettings>();
	for (int32 Slot = 0; Slot < Component.GetNumMaterials(); ++Slot)
	{
		UMaterialInterface* Original = Component.GetMaterial(Slot);
		UMaterialInterface* Variant = Settings->ResolveBodyMaterial(Original);
		if (Variant != Original) Component.SetMaterial(Slot, Variant);
	}
}

void GuLiUnitRenderPolicy::Apply(UMeshComponent& Component)
{
	ApplyReflectionExclusions(Component);
	ApplyBodyMaterials(Component);
}

void GuLiUnitRenderPolicy::ApplyToActor(AActor* Actor)
{
	if (!Actor) return;
	TInlineComponentArray<UMeshComponent*> Meshes;
	Actor->GetComponents(Meshes, true);
	for (UMeshComponent* Mesh : Meshes) Apply(*Mesh);
}
