// Copyright Epic Games, Inc. All Rights Reserved.

#include "Gameplay/Building/GuLiBuildingAssetBakingLibrary.h"

#include "Engine/StaticMesh.h"
#include "StaticMeshAttributes.h"

#if WITH_EDITOR
#include "StaticMeshCompiler.h"
#endif

bool UGuLiBuildingAssetBakingLibrary::BakeRenderGeometryScale(
	UStaticMesh* SourceMesh,
	UStaticMesh* TargetMesh,
	const FVector BuildScale,
	const bool bEnableNanite)
{
#if WITH_EDITOR
	if (!IsValid(SourceMesh) || !IsValid(TargetMesh))
	{
		UE_LOG(LogTemp, Error, TEXT("Building bake requires valid source and target meshes."));
		return false;
	}

	if (BuildScale.ContainsNaN() || BuildScale.GetMin() <= UE_SMALL_NUMBER)
	{
		UE_LOG(LogTemp, Error, TEXT("Building bake scale must contain finite positive values."));
		return false;
	}

	TArray<UStaticMesh*> MeshesToFinish { SourceMesh, TargetMesh };
	FStaticMeshCompilingManager::Get().FinishCompilation(MeshesToFinish);

	const FStaticMeshRenderData* SourceRenderData = SourceMesh->GetRenderData();
	if (SourceRenderData == nullptr || SourceRenderData->LODResources.IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("Building bake source has no compiled render LODs."));
		return false;
	}

	TargetMesh->Modify();
	while (TargetMesh->GetNumSourceModels() < SourceRenderData->LODResources.Num())
	{
		TargetMesh->AddSourceModel();
	}
	while (TargetMesh->GetNumSourceModels() > SourceRenderData->LODResources.Num())
	{
		TargetMesh->RemoveSourceModel(TargetMesh->GetNumSourceModels() - 1);
	}

	for (int32 LODIndex = 0; LODIndex < SourceRenderData->LODResources.Num(); ++LODIndex)
	{
		FMeshDescription ExportedDescription;
		SourceMesh->ExportStaticMeshLOD(SourceRenderData->LODResources[LODIndex], ExportedDescription);
		if (ExportedDescription.IsEmpty())
		{
			UE_LOG(LogTemp, Error, TEXT("Building bake source render LOD %d is empty."), LODIndex);
			return false;
		}

		// ExportStaticMeshLOD preserves every position-buffer entry, including
		// unreferenced vertices. Imported Fab meshes can contain outlying unused
		// entries that inflate bounds after a rebuild, so strip only true orphans.
		TArray<FVertexID> OrphanVertices;
		for (const FVertexID VertexID : ExportedDescription.Vertices().GetElementIDs())
		{
			if (ExportedDescription.GetVertexVertexInstanceIDs(VertexID).IsEmpty())
			{
				OrphanVertices.Add(VertexID);
			}
		}
		for (const FVertexID VertexID : OrphanVertices)
		{
			ExportedDescription.DeleteVertex(VertexID);
		}

		// Some imported Fab meshes have render-buffer positions whose extrema no
		// longer agree with the authoritative render bounds stored on the source.
		// Normalize those positions back to the source bounds before the requested
		// uniform BuildScale is applied, keeping culling, collision, and visuals in
		// the same coordinate contract.
		const FBoxSphereBounds ExportedBounds = ExportedDescription.GetBounds();
		const FBoxSphereBounds DesiredBounds = SourceRenderData->Bounds;
		if (ExportedBounds.BoxExtent.GetMin() <= UE_SMALL_NUMBER
			|| DesiredBounds.BoxExtent.GetMin() <= UE_SMALL_NUMBER)
		{
			UE_LOG(LogTemp, Error, TEXT("Building bake encountered degenerate bounds."));
			return false;
		}

		const FVector BoundsScale = DesiredBounds.BoxExtent / ExportedBounds.BoxExtent;
		FStaticMeshAttributes Attributes(ExportedDescription);
		TVertexAttributesRef<FVector3f> Positions = Attributes.GetVertexPositions();
		for (const FVertexID VertexID : ExportedDescription.Vertices().GetElementIDs())
		{
			const FVector Position(Positions[VertexID]);
			Positions[VertexID] = FVector3f(
				DesiredBounds.Origin + (Position - ExportedBounds.Origin) * BoundsScale);
		}

		FMeshDescription* TargetDescription = TargetMesh->GetMeshDescription(LODIndex);
		if (TargetDescription == nullptr)
		{
			TargetDescription = TargetMesh->CreateMeshDescription(LODIndex);
		}
		if (TargetDescription == nullptr)
		{
			UE_LOG(LogTemp, Error, TEXT("Unable to create building target source LOD %d."), LODIndex);
			return false;
		}

		*TargetDescription = MoveTemp(ExportedDescription);
		TargetMesh->CommitMeshDescription(LODIndex);
		TargetMesh->GetSourceModel(LODIndex).BuildSettings.BuildScale3D = BuildScale;
	}

	FMeshNaniteSettings NaniteSettings = TargetMesh->GetNaniteSettings();
	NaniteSettings.bEnabled = bEnableNanite;
	TargetMesh->SetNaniteSettings(NaniteSettings);
	TargetMesh->Build(true);

	TArray<UStaticMesh*> TargetToFinish { TargetMesh };
	FStaticMeshCompilingManager::Get().FinishCompilation(TargetToFinish);
	const FVector ScaledOrigin = SourceRenderData->Bounds.Origin * BuildScale;
	const FVector ScaledExtent = SourceRenderData->Bounds.BoxExtent * BuildScale.GetAbs();
	TargetMesh->SetExtendedBounds(
		FBoxSphereBounds(ScaledOrigin, ScaledExtent, ScaledExtent.Size()));
	TargetMesh->MarkPackageDirty();
	return true;
#else
	UE_LOG(LogTemp, Error, TEXT("Building asset baking is editor-only."));
	return false;
#endif
}
