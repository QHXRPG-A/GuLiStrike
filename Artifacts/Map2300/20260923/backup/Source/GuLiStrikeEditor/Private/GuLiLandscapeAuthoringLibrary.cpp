#include "GuLiLandscapeAuthoringLibrary.h"

#include "Editor.h"
#include "Landscape.h"
#include "LandscapeEdit.h"
#include "LandscapeEditLayer.h"
#include "LandscapeInfo.h"
#include "LandscapeLayerInfoObject.h"
#include "Misc/FileHelper.h"
#include "ScopedTransaction.h"

bool UGuLiLandscapeAuthoringLibrary::RegisterTargetLayers(
	ALandscape* Landscape, const TArray<ULandscapeLayerInfoObject*>& LayerInfos)
{
	if (!GEditor || GEditor->PlayWorld || !IsValid(Landscape) || LayerInfos.IsEmpty()
		|| Landscape->GetWorld() != GEditor->GetEditorWorldContext().World()) return false;
	TSet<FName> Names;
	for (const auto* Layer : LayerInfos)
	{
		if (!IsValid(Layer) || Layer->GetLayerName().IsNone() || Names.Contains(Layer->GetLayerName())) return false;
		Names.Add(Layer->GetLayerName());
	}
	const FScopedTransaction Transaction(NSLOCTEXT("GuLiLandscape", "RegisterLayers", "Register persistent Landscape target layers"));
	Landscape->Modify();
	for (auto* Layer : LayerInfos)
	{
		const FLandscapeTargetLayerSettings Settings(Layer);
		if (Landscape->HasTargetLayer(Layer->GetLayerName()))
			Landscape->UpdateTargetLayer(Layer->GetLayerName(), Settings);
		else
			Landscape->AddTargetLayer(Layer->GetLayerName(), Settings);
	}
	return true;
}

bool UGuLiLandscapeAuthoringLibrary::ImportTargetLayerWeights(
	ALandscape* Landscape, const TArray<ULandscapeLayerInfoObject*>& LayerInfos, const FString& WeightFile)
{
	if (!GEditor || GEditor->PlayWorld || !IsValid(Landscape) || LayerInfos.IsEmpty()
		|| Landscape->GetWorld() != GEditor->GetEditorWorldContext().World()) return false;
	auto* Info = Landscape->GetLandscapeInfo();
	const auto EditLayers = Landscape->GetEditLayers();
	int32 MinX, MinY, MaxX, MaxY;
	if (!Info || Info->Layers.Num() != LayerInfos.Num() || EditLayers.IsEmpty() || !EditLayers[0]
		|| !Info->GetLandscapeExtent(MinX, MinY, MaxX, MaxY)) return false;
	TSet<ULandscapeLayerInfoObject*> DirtyLayers;
	TArray<int32> DestinationChannels;
	for (auto* Layer : LayerInfos)
	{
		if (!IsValid(Layer) || DirtyLayers.Contains(Layer)) return false;
		const int32 Channel = Info->GetLayerInfoIndex(Layer);
		if (Channel == INDEX_NONE) return false;
		DestinationChannels.Add(Channel);
		DirtyLayers.Add(Layer);
	}
	const int64 Samples = static_cast<int64>(MaxX - MinX + 1) * (MaxY - MinY + 1);
	const int64 Bytes = Samples * LayerInfos.Num();
	TArray<uint8> Source;
	if (Bytes <= 0 || Bytes > MAX_int32 || !FFileHelper::LoadFileToArray(Source, *WeightFile)
		|| Source.Num() != Bytes) return false;
	TArray<uint8> Packed;
	Packed.SetNumUninitialized(Source.Num());
	for (int64 Sample = 0; Sample < Samples; ++Sample)
		for (int32 Channel = 0; Channel < LayerInfos.Num(); ++Channel)
			Packed[Sample * LayerInfos.Num() + DestinationChannels[Channel]] = Source[Sample * LayerInfos.Num() + Channel];
	const FScopedTransaction Transaction(NSLOCTEXT("GuLiLandscape", "ImportWeights", "Import Landscape weights together"));
	Landscape->Modify();
	FScopedSetLandscapeEditingLayer Scope(Landscape, EditLayers[0]->GetGuid());
	{
		// Per-layer paint imports rebalance previously imported weight-blended layers.
		// The packed overload writes the original distribution in one operation.
		FLandscapeEditDataInterface Edit(Info);
		Edit.SetAlphaData(DirtyLayers, MinX, MinY, MaxX, MaxY, Packed.GetData(), 0);
		Edit.Flush();
	}
	Info->ForceLayersFullUpdate();
	return true;
}
