#include "GuLiConstructionShapeLibrary.h"
#include "NiagaraSystem.h"
#include "NiagaraEmitter.h"
#include "NiagaraMeshRendererProperties.h"
#include "NiagaraScriptSource.h"
#include "NiagaraGraph.h"
#include "NiagaraNodeFunctionCall.h"
#include "ViewModels/Stack/NiagaraParameterHandle.h"
#include "ViewModels/Stack/NiagaraStackGraphUtilities.h"
#include "Engine/StaticMesh.h"

namespace
{
	bool Owned(const UNiagaraSystem* System)
	{
		return System && System->GetPathName().StartsWith(TEXT("/Game/GuLiStrike/Buildings/Construction/"));
	}
}

bool UGuLiConstructionShapeLibrary::BindConstructionMeshRenderer(UNiagaraSystem* System, FName EmitterName,
	FName Parameter, UStaticMesh* PreviewMesh, UMaterialInterface* Material)
{
	if (!Owned(System) || !PreviewMesh || !Material || !Parameter.ToString().StartsWith(TEXT("User."))) return false;
	for (const auto& Handle : System->GetEmitterHandles())
	{
		if (Handle.GetName() != EmitterName) continue;
		auto* Data = Handle.GetEmitterData(); if (!Data) return false;
		UNiagaraMeshRendererProperties* Renderer = nullptr;
		for (auto* Existing : Data->GetRenderers()) if (auto* Mesh = Cast<UNiagaraMeshRendererProperties>(Existing)) { Renderer = Mesh; break; }
		if (!Renderer) return false; // Authoring script creates the renderer explicitly first.
		System->Modify(); Renderer->Modify();
		// The completion mesh and its companion sprite emitters share the building's yaw/scale.
		for (const auto& SpaceHandle : System->GetEmitterHandles())
			if (auto* SpaceData = SpaceHandle.GetEmitterData()) SpaceData->bLocalSpace = true;
		const FNiagaraVariable Variable(FNiagaraTypeDefinition(UStaticMesh::StaticClass()), Parameter);
		System->GetExposedParameters().AddParameter(Variable);
		System->GetExposedParameters().SetUObject(PreviewMesh, Variable);
		Renderer->Meshes.SetNum(1); Renderer->Meshes[0].Mesh = PreviewMesh;
		Renderer->Meshes[0].MeshParameterBinding.ResolvedParameter = Variable;
		Renderer->Meshes[0].MeshParameterBinding.AliasedParameter = Variable;
		Renderer->bOverrideMaterials = true; Renderer->OverrideMaterials.SetNum(1);
		Renderer->OverrideMaterials[0].ExplicitMat = Material;
		System->RequestCompile(false); System->MarkPackageDirty(); return true;
	}
	return false;
}

bool UGuLiConstructionShapeLibrary::BindConstructionSpawnCount(UNiagaraSystem* System, FName EmitterName)
{
	if (!Owned(System)) return false;
	for (const auto& Handle : System->GetEmitterHandles())
	{
		if (Handle.GetName() != EmitterName) continue;
		auto* Data = Handle.GetEmitterData();
		auto* Source = Data ? Cast<UNiagaraScriptSource>(Data->GraphSource) : nullptr;
		if (!Source || !Source->NodeGraph) return false;
		TArray<UNiagaraNodeFunctionCall*> Calls; Source->NodeGraph->GetNodesOfClass(Calls);
		for (auto* Call : Calls)
		{
			if (!Call->GetFunctionName().Contains(TEXT("SpawnBurst_Instantaneous"))) continue;
			System->Modify(); Source->NodeGraph->Modify();
			const FNiagaraParameterHandle Input(FName(*(Call->GetFunctionName() + TEXT(".Spawn Count"))));
			auto& Pin = FNiagaraStackGraphUtilities::GetOrCreateStackFunctionInputOverridePin(*Call, Input,
				FNiagaraTypeDefinition::GetIntDef(), FGuid(), FGuid());
			const FNiagaraVariableBase Count(FNiagaraTypeDefinition::GetIntDef(), TEXT("User.RoofPointCount"));
			TSet<FNiagaraVariableBase> Known; Known.Add(Count);
			FNiagaraStackGraphUtilities::SetLinkedParameterValueForFunctionInput(Pin, Count, Known);
			System->RequestCompile(false); System->MarkPackageDirty(); return true;
		}
	}
	return false;
}
