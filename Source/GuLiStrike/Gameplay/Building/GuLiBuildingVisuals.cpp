#include "Gameplay/Building/GuLiBuildingVisuals.h"
#include "Gameplay/Building/GuLiBuildingTypes.h"
#include "Gameplay/Resources/GuLiResourceMapDefinition.h"
#include "Gameplay/Resources/GuLiResourceWorldSubsystem.h"
#include "Gameplay/Presentation/GuLiUnitRenderPolicy.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Gameplay/Models/GuLiModelRegistrySubsystem.h"

UClass* GuLiBuildingVisuals::ResolveFactoryPresentation(const UObject* Context)
{
	const UWorld* World = Context ? Context->GetWorld() : nullptr;
	const auto* Resources = World ? World->GetSubsystem<UGuLiResourceWorldSubsystem>() : nullptr;
	const auto* Config = Resources ? Resources->GetEconomyConfig() : nullptr;
	return GuLiModels::LoadClass<AActor>(Context, Config ? Config->FactoryModelId : GuLiModelIds::ResourceFactory);
}

UMeshComponent* GuLiBuildingVisuals::CopyMesh(AActor& Owner, USceneComponent& Parent,
	UMeshComponent& Source, const FTransform& RelativeTransform, UMaterialInterface* Override, bool bFollowPose)
{
	UMeshComponent* Copy = nullptr;
	if (auto* Static = Cast<UStaticMeshComponent>(&Source); Static && Static->GetStaticMesh())
	{
		auto* Mesh = NewObject<UStaticMeshComponent>(&Owner, NAME_None, RF_Transient);
		Mesh->SetStaticMesh(Static->GetStaticMesh());
		// Translucent holograms/glow require the fallback renderer, not Nanite.
		Mesh->bDisallowNanite = Override != nullptr || Static->bDisallowNanite;
		Copy = Mesh;
	}
	else if (auto* Skeletal = Cast<USkeletalMeshComponent>(&Source); Skeletal && Skeletal->GetSkeletalMeshAsset())
	{
		auto* Mesh = NewObject<USkeletalMeshComponent>(&Owner, NAME_None, RF_Transient);
		Mesh->SetSkeletalMeshAsset(Skeletal->GetSkeletalMeshAsset());
		if (bFollowPose) Mesh->SetLeaderPoseComponent(Skeletal);
		else Mesh->OverrideAnimationData(Skeletal->AnimationData.AnimToPlay, false, false, 0.0f);
		Copy = Mesh;
	}
	if (!Copy) return nullptr;
	Copy->ComponentTags.Add(TEXT("GuLiConstructionProxy"));
	Copy->SetMobility(EComponentMobility::Movable);
	Copy->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Copy->SetGenerateOverlapEvents(false);
	Copy->SetCanEverAffectNavigation(false);
	Copy->SetReceivesDecals(false);
	Copy->SetCastShadow(Override == nullptr && Source.CastShadow);
	if (Override) GuLiUnitRenderPolicy::ApplyReflectionExclusions(*Copy);
	Copy->SetupAttachment(&Parent);
	Copy->SetRelativeTransform(RelativeTransform);
	for (int32 Index = 0; Index < Copy->GetNumMaterials(); ++Index)
		Copy->SetMaterial(Index, Override ? Override : Source.GetMaterial(Index));
	Owner.AddInstanceComponent(Copy);
	Copy->RegisterComponent();
	return Copy;
}

bool GuLiBuildingVisuals::CreatePreviewMeshes(AActor& Owner, USceneComponent& Parent,
	const FGuLiBuildingDefinition& Definition, UMaterialInterface* Material,
	TArray<TObjectPtr<UMeshComponent>>& OutMeshes)
{
	if (Definition.Category != EGuLiBuildingCategory::Factory)
	{
		auto* Template = NewObject<UStaticMeshComponent>(&Owner, NAME_None, RF_Transient);
		Template->SetStaticMesh(Definition.Mesh);
		if (auto* Mesh = CopyMesh(Owner, Parent, *Template,
			FTransform(FQuat::Identity, Definition.VisualOffset, Definition.MeshScale), Material)) OutMeshes.Add(Mesh);
		return !OutMeshes.IsEmpty();
	}
	auto* Class = Cast<UBlueprintGeneratedClass>(Definition.PresentationClass.Get());
	if (!Class || !Class->SimpleConstructionScript) return false;
	TFunction<void(USCS_Node*, const FTransform&)> Visit = [&](USCS_Node* Node, const FTransform& ParentTransform)
	{
		auto* Scene = Cast<USceneComponent>(Node->GetActualComponentTemplate(Class));
		const FTransform Transform = Scene ? Scene->GetRelativeTransform() * ParentTransform : ParentTransform;
		if (auto* Source = Cast<UMeshComponent>(Scene))
			if (auto* Copy = CopyMesh(Owner, Parent, *Source, Transform, Material)) OutMeshes.Add(Copy);
		for (USCS_Node* Child : Node->GetChildNodes()) Visit(Child, Transform);
	};
	const FTransform Scale(FQuat::Identity, FVector::ZeroVector, FVector(FactoryScale));
	for (USCS_Node* Root : Class->SimpleConstructionScript->GetRootNodes()) Visit(Root, Scale);
	return !OutMeshes.IsEmpty();
}
