#include "Gameplay/Data/GuLiObjectScale.h"
#include "Components/MeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"

void GuLiObjectScale::ApplyOutlineScale(UMeshComponent* Component)
{
	static const FName AppliedTag(TEXT("GuLi.Scale020.OutlineApplied"));
	if (!Component || Component->ComponentHasTag(AppliedTag)) return;
	TMap<UMaterialInterface*, UMaterialInterface*> Resolved;
	auto ScaleMaterial = [&](UMaterialInterface* Source) -> UMaterialInterface*
	{
		if (!Source) return nullptr;
		if (auto* Existing = Resolved.Find(Source)) return *Existing;
		float Width = 0.0f;
		if (!Source->GetScalarParameterValue(FMaterialParameterInfo(TEXT("OutlineWidthCm")), Width)) return Source;
		auto* Dynamic = UMaterialInstanceDynamic::Create(Source, Component);
		Dynamic->SetScalarParameterValue(TEXT("OutlineWidthCm"), Width * GameplayScale);
		Resolved.Add(Source, Dynamic);
		return Dynamic;
	};
	Component->SetOverlayMaterial(ScaleMaterial(Component->GetOverlayMaterial()));
	TArray<TObjectPtr<UMaterialInterface>> Slots;
	Component->GetMaterialSlotsOverlayMaterial(Slots);
	for (auto& Material : Slots) Material = ScaleMaterial(Material);
	Component->MaterialSlotsOverlayMaterial = MoveTemp(Slots);
	Component->ComponentTags.Add(AppliedTag);
	Component->MarkRenderStateDirty();
}
