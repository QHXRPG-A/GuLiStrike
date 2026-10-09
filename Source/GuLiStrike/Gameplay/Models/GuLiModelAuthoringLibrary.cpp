#include "Gameplay/Models/GuLiModelAuthoringLibrary.h"
#if WITH_EDITOR
#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"
#include "MeshDescription.h"
#include "StaticMeshAttributes.h"
#include "StaticMeshResources.h"
#include "Rendering/SkeletalMeshModel.h"
#include "Rendering/SkeletalMeshLODModel.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "RenderingThread.h"
#include "Components/SkinnedMeshComponent.h"
#include "Gameplay/Models/GuLiModelRegistrySubsystem.h"
#include "Gameplay/Models/GuLiLocalTeamColorSubsystem.h"
#include "Gameplay/Presentation/GuLiTeamOutlineComponent.h"
#include "Engine/World.h"
#include "Engine/StaticMeshActor.h"
#include "Animation/SkeletalMeshActor.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Gameplay/Ship/GuLiStrikeShip.h"
#include "Gameplay/GroundMech/GuLiGroundMechCharacter.h"
#include "Serialization/MemoryWriter.h"
#include "Misc/SecureHash.h"
#include "UObject/UnrealType.h"
#include "UObject/Package.h"
#include "UObject/MetaData.h"
#include "Materials/MaterialInterface.h"
#include "Misc/FileHelper.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Engine/LocalPlayer.h"
#include "Engine/GameViewportClient.h"
#include "GameFramework/PlayerController.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SViewport.h"
#include "ImageUtils.h"
#endif

namespace
{
#if WITH_EDITOR
FString GuLiPaintHash(const TArray<uint8>& Bytes)
{
	FSHAHash Hash; FSHA1::HashBuffer(Bytes.GetData(),Bytes.Num(),Hash.Hash); return Hash.ToString();
}

template<typename TSet>
void GuLiAttributeHashes(const TSet& Attributes,const FString& Prefix,TSharedRef<FJsonObject> Result)
{
	Attributes.ForEach([&](FName Name,const auto& Values)
	{
		if (Name==MeshAttribute::VertexInstance::Color) return;
		TArray<uint8> Bytes; FMemoryWriter Writer(Bytes,true);
		int32 Channels=Values.GetNumChannels(); Writer<<Channels;
		for (int32 C=0;C<Channels;++C) for (int32 I=0;I<Attributes.GetNumElements();++I)
		{
			const auto View=Values.GetArrayView(I,C); int32 Size=View.Num(); Writer<<Size;
			for (const auto& Value : View) { auto Copy=Value; if constexpr (std::is_same_v<std::decay_t<decltype(Copy)>,FName>) {auto Label=Copy.ToString(); Writer<<Label;} else {Writer<<Copy;} }
		}
		Result->SetStringField(Prefix+Name.ToString(),GuLiPaintHash(Bytes));
	});
}

// MeshDescription polygon groups use imported slot names. Gameplay interfaces
// use the display slot name; both aliases refer to the same original material.
int32 GuLiPaintSlot(const UObject* Mesh,FName Name,int32 LOD=0,int32 Group=INDEX_NONE)
{
	if (const auto* Static=Cast<UStaticMesh>(Mesh))
	{
		const auto& Slots=Static->GetStaticMaterials();
		for (int32 I=0;I<Slots.Num();++I) if (Slots[I].MaterialSlotName==Name) return I;
		if (Group!=INDEX_NONE) return Static->GetSectionInfoMap().Get(LOD,Group).MaterialIndex;
		for (int32 I=0;I<Slots.Num();++I) if (!Name.IsNone() && Slots[I].ImportedMaterialSlotName==Name) return I;
	}
	if (const auto* Skeletal=Cast<USkeletalMesh>(Mesh))
	{
		const auto& Slots=Skeletal->GetMaterials();
		for (int32 I=0;I<Slots.Num();++I) if (Slots[I].MaterialSlotName==Name) return I;
		if (Group!=INDEX_NONE)
		{
			const auto* Info=Skeletal->GetLODInfo(LOD);
			return Info && Info->LODMaterialMap.IsValidIndex(Group) && Info->LODMaterialMap[Group]!=INDEX_NONE ? Info->LODMaterialMap[Group] : Group;
		}
		for (int32 I=0;I<Slots.Num();++I) if (!Name.IsNone() && Slots[I].ImportedMaterialSlotName==Name) return I;
	}
	return INDEX_NONE;
}

bool GuLiPaintableSlot(const UObject* Mesh,int32 Index)
{
	FName Name; const UMaterialInterface* Material=nullptr;
	if (const auto* Static=Cast<UStaticMesh>(Mesh); Static && Static->GetStaticMaterials().IsValidIndex(Index))
	{ const auto& S=Static->GetStaticMaterials()[Index]; Name=S.MaterialSlotName; Material=S.MaterialInterface; }
	else if (const auto* Skeletal=Cast<USkeletalMesh>(Mesh); Skeletal && Skeletal->GetMaterials().IsValidIndex(Index))
	{ const auto& S=Skeletal->GetMaterials()[Index]; Name=S.MaterialSlotName; Material=S.MaterialInterface; }
	const FString Label=Name.ToString().ToLower();
	return Material && Material->GetBlendMode()==BLEND_Opaque && !Label.Contains(TEXT("outline")) && !Label.Contains(TEXT("contour")) && !Label.Contains(TEXT("display")) && !Label.Contains(TEXT("glass"));
}

bool GuLiRefreshDisplayBounds(UStaticMesh* Mesh)
{
	if (!Mesh || !Mesh->GetRenderData()) return false;
	// CommitMeshDescription caches unscaled source bounds in UE5.7. Meshes
	// with a non-unit import build scale must use their built render bounds.
	FBoxSphereBounds Bounds = Mesh->GetRenderData()->Bounds;
	const FVector Min = Bounds.Origin - Bounds.BoxExtent - Mesh->GetNegativeBoundsExtension();
	const FVector Max = Bounds.Origin + Bounds.BoxExtent + Mesh->GetPositiveBoundsExtension();
	Bounds.Origin = (Min + Max) * .5;
	Bounds.BoxExtent = (Max - Min) * .5;
	if (!Mesh->GetNegativeBoundsExtension().IsZero() || !Mesh->GetPositiveBoundsExtension().IsZero())
		Bounds.SphereRadius = Bounds.BoxExtent.Size();
	Mesh->SetExtendedBounds(Bounds);
	return true;
}
#endif
FString GuLiAuthoringResult(bool Success, const FString& Error)
{
#if WITH_EDITOR
	auto Json=MakeShared<FJsonObject>(); Json->SetBoolField(TEXT("success"),Success); Json->SetStringField(TEXT("error"),Error);
	FString Text; FJsonSerializer::Serialize(Json,TJsonWriterFactory<>::Create(&Text)); return Text;
#else
	return TEXT("{\"success\":false,\"error\":\"Editor authoring only\"}");
#endif
}
}

FString UGuLiModelAuthoringLibrary::EncodeCandidateMeshReport(UObject* Mesh,int32 LOD,const FString& File)
{ FString Error; const bool Success=EncodeCandidateMesh(Mesh,LOD,File,Error); return GuLiAuthoringResult(Success,Error); }
FString UGuLiModelAuthoringLibrary::EncodeUniformCandidateMeshReport(UObject* Mesh,int32 LOD,int32 Role,FLinearColor Color)
{ FString Error; const bool Success=EncodeUniformCandidateMesh(Mesh,LOD,Role,Color,Error); return GuLiAuthoringResult(Success,Error); }

FString UGuLiModelAuthoringLibrary::SetCompatibilityModelId(UObject* Target,FName PropertyName,int32 ModelId)
{
#if WITH_EDITOR
	if (!Target || !Target->HasAnyFlags(RF_ClassDefaultObject) || !Target->GetOutermost()->GetName().StartsWith(TEXT("/Game/GuLiStrike/")))
		return GuLiAuthoringResult(false,TEXT("Only existing project Blueprint CDOs can be migrated."));
	const bool Ship=Target->IsA<AGuLiStrikeShip>() && PropertyName==TEXT("HullModelId");
	const bool Ground=Target->IsA<AGuLiGroundMechCharacter>() && PropertyName==TEXT("ModelId") && ModelId==GuLiModelIds::GroundMech;
	FGuLiStrikeModelsModelsRow Definition;
	if ((!Ship && !Ground) || !UGuLiModelRegistrySubsystem::Query(Target,ModelId,Definition)
		|| (Ground && Definition.ResourceType!=TEXT("SkeletalMesh"))
		|| (Ship && Definition.ResourceType!=TEXT("StaticMesh")))
		return GuLiAuthoringResult(false,TEXT("Invalid model or unsupported compatibility cache."));
	auto* Property=FindFProperty<FIntProperty>(Target->GetClass(),PropertyName);
	if (!Property) return GuLiAuthoringResult(false,TEXT("Compatibility field missing."));
	Target->Modify(); Property->SetPropertyValue_InContainer(Target,ModelId); Target->MarkPackageDirty();
	return GuLiAuthoringResult(true,TEXT(""));
#else
	return GuLiAuthoringResult(false,TEXT("Editor migration only."));
#endif
}

FString UGuLiModelAuthoringLibrary::GetMeshInvariantSnapshot(UObject* Mesh)
{
#if WITH_EDITOR
	auto* Static=Cast<UStaticMesh>(Mesh); auto* Skeletal=Cast<USkeletalMesh>(Mesh);
	if (!Static && !Skeletal) return GuLiAuthoringResult(false,TEXT("Expected a mesh."));
	auto Json=MakeShared<FJsonObject>(); Json->SetBoolField(TEXT("success"),true);
	TArray<TSharedPtr<FJsonValue>> LODs;
	const int32 Count=Static ? Static->GetNumSourceModels() : Skeletal->GetLODNum();
	for (int32 LOD=0;LOD<Count;++LOD)
	{
		const FMeshDescription* Source=Static ? Static->GetMeshDescription(LOD) : Skeletal->GetMeshDescription(LOD);
		if (!Source)
		{
			auto Item=MakeShared<FJsonObject>(); Item->SetNumberField(TEXT("lod"),LOD);
			Item->SetBoolField(TEXT("generated_from_source_lod"),true); LODs.Add(MakeShared<FJsonValueObject>(Item)); continue;
		}
		auto Hashes=MakeShared<FJsonObject>();
		GuLiAttributeHashes(Source->VertexAttributes(),TEXT("vertex/"),Hashes);
		GuLiAttributeHashes(Source->VertexInstanceAttributes(),TEXT("corner/"),Hashes);
		GuLiAttributeHashes(Source->EdgeAttributes(),TEXT("edge/"),Hashes);
		GuLiAttributeHashes(Source->TriangleAttributes(),TEXT("triangle/"),Hashes);
		GuLiAttributeHashes(Source->PolygonAttributes(),TEXT("polygon/"),Hashes);
		GuLiAttributeHashes(Source->PolygonGroupAttributes(),TEXT("slot/"),Hashes);
		auto Item=MakeShared<FJsonObject>(); Item->SetNumberField(TEXT("lod"),LOD); Item->SetObjectField(TEXT("attributes"),Hashes);
		Item->SetNumberField(TEXT("vertices"),Source->Vertices().Num()); Item->SetNumberField(TEXT("triangles"),Source->Triangles().Num());
		// A paint seam may duplicate a corner, never a geometric vertex. Compare
		// every original non-color corner attribute by triangle, independently
		// of corner storage indices. Vertex/skin and skeleton hashes stay exact.
		TArray<FString> CornerHashes; CornerHashes.SetNum(Source->VertexInstances().GetArraySize());
		for (const auto VI : Source->VertexInstances().GetElementIDs())
		{
			TArray<uint8> Bytes; FMemoryWriter Writer(Bytes,true);
			Source->VertexInstanceAttributes().ForEach([&](FName Name,const auto& Values)
			{
				if (Name==MeshAttribute::VertexInstance::Color) return;
				auto Label=Name.ToString(); Writer<<Label; int32 Channels=Values.GetNumChannels(); Writer<<Channels;
				for (int32 C=0;C<Channels;++C) {const auto View=Values.GetArrayView(VI.GetValue(),C); int32 Size=View.Num(); Writer<<Size; for (const auto& Value : View) {auto Copy=Value; if constexpr (std::is_same_v<std::decay_t<decltype(Copy)>,FName>) {auto N=Copy.ToString(); Writer<<N;} else Writer<<Copy;}}
			});
			CornerHashes[VI.GetValue()]=GuLiPaintHash(Bytes);
		}
		TArray<FString> Triangles;
		for (const auto T : Source->Triangles().GetElementIDs())
		{
			TArray<FString,TInlineAllocator<3>> Corners; for (const auto VI : Source->GetTriangleVertexInstances(T)) Corners.Add(CornerHashes[VI.GetValue()]); Corners.Sort();
			Triangles.Add(FString::FromInt(Source->GetTrianglePolygonGroup(T).GetValue())+TEXT(":")+Corners[0]+Corners[1]+Corners[2]);
		}
		Triangles.Sort(); TArray<uint8> Bytes; FMemoryWriter Writer(Bytes,true); Writer<<Triangles;
		Item->SetStringField(TEXT("triangle_attributes_sha1"),GuLiPaintHash(Bytes));
		LODs.Add(MakeShared<FJsonValueObject>(Item));
	}
	Json->SetArrayField(TEXT("lods"),LODs);
	// Verify the actual display LODs as well as the editor source. Reduced LODs
	// have no separate source description on some legacy assets.
	TArray<TSharedPtr<FJsonValue>> RenderLODs;
	if (Static && Static->GetRenderData()) for (int32 LOD=0;LOD<Static->GetRenderData()->LODResources.Num();++LOD)
	{
		const auto& R=Static->GetRenderData()->LODResources[LOD]; const auto& P=R.VertexBuffers.PositionVertexBuffer; const auto& V=R.VertexBuffers.StaticMeshVertexBuffer;
		TArray<uint8> Bytes; FMemoryWriter Writer(Bytes,true);
		TArray<FString> VertexHashes, ContentVertexHashes;
		for (uint32 I=0;I<P.GetNumVertices();++I)
		{
			TArray<uint8> VertexBytes; FMemoryWriter VertexWriter(VertexBytes,true);
			auto Position=P.VertexPosition(I); auto X=V.VertexTangentX(I); auto Z=V.VertexTangentZ(I); Writer<<Position<<X<<Z;
			VertexWriter<<Position<<X<<Z;
			for (uint32 C=0;C<V.GetNumTexCoords();++C) {auto UV=V.GetVertexUV(I,C); Writer<<UV; VertexWriter<<UV;}
			VertexHashes.Add(GuLiPaintHash(VertexBytes));
			TArray<uint8> ContentBytes; FMemoryWriter ContentWriter(ContentBytes,true);
			for (int32 A=0;A<3;++A) {int64 Q=FMath::RoundToInt64(double(Position[A])*1000.); ContentWriter<<Q;}
			for (uint32 C=0;C<V.GetNumTexCoords();++C) {const auto UV=V.GetVertexUV(I,C); for (int32 A=0;A<2;++A) {int64 Q=FMath::RoundToInt64(double(UV[A])*100000.); ContentWriter<<Q;}}
			ContentVertexHashes.Add(GuLiPaintHash(ContentBytes));
		}
		for (int32 I=0;I<R.IndexBuffer.GetNumIndices();++I) {uint32 Index=R.IndexBuffer.GetIndex(I); Writer<<Index;}
		auto Item=MakeShared<FJsonObject>(); Item->SetNumberField(TEXT("lod"),LOD); Item->SetNumberField(TEXT("vertices"),P.GetNumVertices()); Item->SetStringField(TEXT("geometry_uv_normal_index_sha1"),GuLiPaintHash(Bytes));
		TArray<FString> TriangleHashes;
		for (int32 I=0;I<R.IndexBuffer.GetNumIndices();I+=3)
		{
			TArray<FString,TInlineAllocator<3>> H{VertexHashes[R.IndexBuffer.GetIndex(I)],VertexHashes[R.IndexBuffer.GetIndex(I+1)],VertexHashes[R.IndexBuffer.GetIndex(I+2)]}; H.Sort();
			TriangleHashes.Add(H[0]+H[1]+H[2]);
		}
		TriangleHashes.Sort(); TArray<uint8> TriangleBytes; FMemoryWriter TriangleWriter(TriangleBytes,true); TriangleWriter<<TriangleHashes;
		Item->SetNumberField(TEXT("triangles"),R.IndexBuffer.GetNumIndices()/3); Item->SetStringField(TEXT("triangle_surface_sha1"),GuLiPaintHash(TriangleBytes));
		TriangleHashes.Reset();
		for (int32 I=0;I<R.IndexBuffer.GetNumIndices();I+=3) {TArray<FString,TInlineAllocator<3>> H{ContentVertexHashes[R.IndexBuffer.GetIndex(I)],ContentVertexHashes[R.IndexBuffer.GetIndex(I+1)],ContentVertexHashes[R.IndexBuffer.GetIndex(I+2)]}; H.Sort(); TriangleHashes.Add(H[0]+H[1]+H[2]);}
		TriangleHashes.Sort(); TriangleBytes.Reset(); FMemoryWriter ContentWriter(TriangleBytes,true); ContentWriter<<TriangleHashes;
		Item->SetStringField(TEXT("triangle_geometry_uv_skin_sha1"),GuLiPaintHash(TriangleBytes));
		RenderLODs.Add(MakeShared<FJsonValueObject>(Item));
	}
	if (Skeletal && Skeletal->GetImportedModel()) for (int32 LOD=0;LOD<Skeletal->GetImportedModel()->LODModels.Num();++LOD)
	{
		const auto& R=Skeletal->GetImportedModel()->LODModels[LOD]; TArray<uint8> Bytes; FMemoryWriter Writer(Bytes,true);
		TArray<FString> VertexHashes, ContentVertexHashes;
		for (const auto& S : R.Sections)
		{
			for (const auto Bone : S.BoneMap) {auto Value=Bone; Writer<<Value;}
			for (const auto& V : S.SoftVertices)
			{
				TArray<uint8> VertexBytes; FMemoryWriter VertexWriter(VertexBytes,true);
				auto Position=V.Position; auto X=V.TangentX; auto Y=V.TangentY; auto Z=V.TangentZ;
				Writer<<Position<<X<<Y<<Z; VertexWriter<<Position<<X<<Y<<Z;
				for (uint32 C=0;C<R.NumTexCoords;++C) {auto Copy=V.UVs[C]; Writer<<Copy; VertexWriter<<Copy;}
				for (const auto Bone : V.InfluenceBones) {auto Copy=Bone; Writer<<Copy;}
				for (const auto Weight : V.InfluenceWeights) {auto Copy=Weight; Writer<<Copy;}
				for (int32 I=0;I<MAX_TOTAL_INFLUENCES;++I) if (V.InfluenceWeights[I]>0)
				{ int32 Bone=S.BoneMap[V.InfluenceBones[I]]; uint16 Weight=V.InfluenceWeights[I]; VertexWriter<<Bone<<Weight; }
				VertexHashes.Add(GuLiPaintHash(VertexBytes));
				TArray<uint8> ContentBytes; FMemoryWriter ContentWriter(ContentBytes,true);
				for (int32 A=0;A<3;++A) {int64 Q=FMath::RoundToInt64(double(Position[A])*1000.); ContentWriter<<Q;}
				for (uint32 C=0;C<R.NumTexCoords;++C) for (int32 A=0;A<2;++A) {int64 Q=FMath::RoundToInt64(double(V.UVs[C][A])*100000.); ContentWriter<<Q;}
				TArray<FString> Influences;
				for (int32 I=0;I<MAX_TOTAL_INFLUENCES;++I) if (V.InfluenceWeights[I]>0) Influences.Add(FString::Printf(TEXT("%d:%d"),S.BoneMap[V.InfluenceBones[I]],V.InfluenceWeights[I]));
				Influences.Sort(); ContentWriter<<Influences; ContentVertexHashes.Add(GuLiPaintHash(ContentBytes));
			}
		}
		for (const auto Index : R.IndexBuffer) {auto Value=Index; Writer<<Value;}
		auto Item=MakeShared<FJsonObject>(); Item->SetNumberField(TEXT("lod"),LOD); Item->SetNumberField(TEXT("vertices"),R.NumVertices); Item->SetStringField(TEXT("geometry_uv_skin_index_sha1"),GuLiPaintHash(Bytes));
		TArray<FString> TriangleHashes;
		for (int32 I=0;I<R.IndexBuffer.Num();I+=3)
		{
			TArray<FString,TInlineAllocator<3>> H{VertexHashes[R.IndexBuffer[I]],VertexHashes[R.IndexBuffer[I+1]],VertexHashes[R.IndexBuffer[I+2]]}; H.Sort(); TriangleHashes.Add(H[0]+H[1]+H[2]);
		}
		TriangleHashes.Sort(); TArray<uint8> TriangleBytes; FMemoryWriter TriangleWriter(TriangleBytes,true); TriangleWriter<<TriangleHashes;
		Item->SetNumberField(TEXT("triangles"),R.IndexBuffer.Num()/3); Item->SetStringField(TEXT("triangle_surface_sha1"),GuLiPaintHash(TriangleBytes));
		TriangleHashes.Reset();
		for (int32 I=0;I<R.IndexBuffer.Num();I+=3) {TArray<FString,TInlineAllocator<3>> H{ContentVertexHashes[R.IndexBuffer[I]],ContentVertexHashes[R.IndexBuffer[I+1]],ContentVertexHashes[R.IndexBuffer[I+2]]}; H.Sort(); TriangleHashes.Add(H[0]+H[1]+H[2]);}
		TriangleHashes.Sort(); TriangleBytes.Reset(); FMemoryWriter ContentWriter(TriangleBytes,true); ContentWriter<<TriangleHashes;
		Item->SetStringField(TEXT("triangle_geometry_uv_skin_sha1"),GuLiPaintHash(TriangleBytes));
		RenderLODs.Add(MakeShared<FJsonValueObject>(Item));
	}
	Json->SetArrayField(TEXT("render_lods"),RenderLODs);
	if (Skeletal)
	{
		TArray<TSharedPtr<FJsonValue>> Bones; const auto& Ref=Skeletal->GetRefSkeleton();
		for (int32 I=0;I<Ref.GetNum();++I)
		{
			auto Bone=MakeShared<FJsonObject>(); Bone->SetStringField(TEXT("name"),Ref.GetBoneName(I).ToString());
			Bone->SetNumberField(TEXT("parent"),Ref.GetParentIndex(I)); Bone->SetStringField(TEXT("pose"),Ref.GetRefBonePose()[I].ToString());
			Bones.Add(MakeShared<FJsonValueObject>(Bone));
		}
		Json->SetArrayField(TEXT("reference_bones"),Bones);
	}
	FString Text; FJsonSerializer::Serialize(Json,TJsonWriterFactory<>::Create(&Text)); return Text;
#else
	return GuLiAuthoringResult(false,TEXT("Editor snapshot only."));
#endif
}

FString UGuLiModelAuthoringLibrary::WriteMeshPaintGeometry(UObject* Mesh,int32 LOD,const FString& File,bool Rendered)
{
#if WITH_EDITOR
	auto* Static=Cast<UStaticMesh>(Mesh); auto* Skeletal=Cast<USkeletalMesh>(Mesh);
	if ((!Static && !Skeletal) || LOD<0 || !FPaths::ConvertRelativePathToFull(File).StartsWith(FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()/TEXT("ArtSource/ModelInterface_B_20261008/"))))
		return GuLiAuthoringResult(false,TEXT("Invalid mesh or geometry-export destination."));
	TArray<FName> Slots;
	if (Static) for (const auto& S : Static->GetStaticMaterials()) Slots.Add(S.MaterialSlotName);
	else for (const auto& S : Skeletal->GetMaterials()) Slots.Add(S.MaterialSlotName);
	TArray<TSharedPtr<FJsonValue>> Triangles, TriangleSlots, SlotNames, TriangleIDs, VertexIDs;
	for (const auto& Name : Slots) SlotNames.Add(MakeShared<FJsonValueString>(Name.ToString()));
	auto Add=[&](const FVector3f& A,const FVector3f& B,const FVector3f& C,int32 Slot,int32 TriangleID,FIntVector IDs)
	{
		const FString Label=Slots.IsValidIndex(Slot) ? Slots[Slot].ToString().ToLower() : FString();
		if (Label.Contains(TEXT("outline")) || Label.Contains(TEXT("contour"))) return;
		TArray<TSharedPtr<FJsonValue>> Values;
		for (const auto& V : {A,B,C}) for (int32 Axis=0;Axis<3;++Axis) Values.Add(MakeShared<FJsonValueNumber>(V[Axis]));
		for (int32 I=0;I<4;++I) Values.Add(MakeShared<FJsonValueNumber>(0));
		Triangles.Add(MakeShared<FJsonValueArray>(Values)); TriangleSlots.Add(MakeShared<FJsonValueNumber>(Slot)); TriangleIDs.Add(MakeShared<FJsonValueNumber>(TriangleID));
		VertexIDs.Add(MakeShared<FJsonValueArray>(TArray<TSharedPtr<FJsonValue>>{MakeShared<FJsonValueNumber>(IDs.X),MakeShared<FJsonValueNumber>(IDs.Y),MakeShared<FJsonValueNumber>(IDs.Z)}));
	};
	if (Rendered)
	{
		if (Static && Static->GetRenderData() && Static->GetRenderData()->LODResources.IsValidIndex(LOD))
		{
			const auto& R=Static->GetRenderData()->LODResources[LOD]; const auto& P=R.VertexBuffers.PositionVertexBuffer;
			for (const auto& Section : R.Sections) for (uint32 T=0;T<Section.NumTriangles;++T)
			{
				const uint32 I=Section.FirstIndex+T*3; const FIntVector IDs(R.IndexBuffer.GetIndex(I),R.IndexBuffer.GetIndex(I+1),R.IndexBuffer.GetIndex(I+2));
				Add(P.VertexPosition(IDs.X),P.VertexPosition(IDs.Y),P.VertexPosition(IDs.Z),Section.MaterialIndex,I/3,IDs);
			}
		}
		else if (Skeletal && Skeletal->GetImportedModel() && Skeletal->GetImportedModel()->LODModels.IsValidIndex(LOD))
		{
			const auto& R=Skeletal->GetImportedModel()->LODModels[LOD]; TArray<FSoftSkinVertex> Vertices; R.GetVertices(Vertices);
			for (const auto& Section : R.Sections) for (uint32 T=0;T<Section.NumTriangles;++T)
			{
				const uint32 I=Section.BaseIndex+T*3; const FIntVector IDs(R.IndexBuffer[I],R.IndexBuffer[I+1],R.IndexBuffer[I+2]);
				Add(Vertices[IDs.X].Position,Vertices[IDs.Y].Position,Vertices[IDs.Z].Position,Section.MaterialIndex,I/3,IDs);
			}
		}
		else return GuLiAuthoringResult(false,TEXT("Rendered LOD unavailable."));
	}
	else
	{
		const FMeshDescription* D=Static ? Static->GetMeshDescription(LOD) : Skeletal->GetMeshDescription(LOD);
		if (!D) return GuLiAuthoringResult(false,TEXT("Source mesh description unavailable."));
		FStaticMeshConstAttributes A(*D); const auto Positions=A.GetVertexPositions(); const auto Names=A.GetPolygonGroupMaterialSlotNames();
		for (const auto T : D->Triangles().GetElementIDs())
		{
			const auto V=D->GetTriangleVertices(T); Add(Positions[V[0]],Positions[V[1]],Positions[V[2]],GuLiPaintSlot(Mesh,Names[D->GetTrianglePolygonGroup(T)],LOD,D->GetTrianglePolygonGroup(T).GetValue()),T.GetValue(),FIntVector(V[0].GetValue(),V[1].GetValue(),V[2].GetValue()));
		}
	}
	auto Json=MakeShared<FJsonObject>(); Json->SetArrayField(TEXT("triangles"),Triangles); Json->SetArrayField(TEXT("triangle_slots"),TriangleSlots); Json->SetArrayField(TEXT("material_slots"),SlotNames);
	Json->SetArrayField(TEXT("ue_axes"),{MakeShared<FJsonValueNumber>(1),MakeShared<FJsonValueNumber>(2),MakeShared<FJsonValueNumber>(3)});
	Json->SetStringField(TEXT("resource"),Mesh->GetPathName()); Json->SetNumberField(TEXT("lod"),LOD); Json->SetBoolField(TEXT("rendered"),Rendered);
	Json->SetArrayField(TEXT("triangle_ids"),TriangleIDs); Json->SetArrayField(TEXT("vertex_ids"),VertexIDs);
	FString Text; FJsonSerializer::Serialize(Json,TJsonWriterFactory<TCHAR,TCondensedJsonPrintPolicy<TCHAR>>::Create(&Text));
	return GuLiAuthoringResult(FFileHelper::SaveStringToFile(Text,*File),TEXT(""));
#else
	return GuLiAuthoringResult(false,TEXT("Editor export only."));
#endif
}

FString UGuLiModelAuthoringLibrary::PreserveDisplayLODForPaint(UObject* Object,int32 LOD)
{
#if WITH_EDITOR
	if (!Object || !Object->GetPathName().StartsWith(TEXT("/Game/GuLiStrike/Review/ModelInterface/")) || LOD<0) return GuLiAuthoringResult(false,TEXT("Independent review display LOD required."));
	auto* Static=Cast<UStaticMesh>(Object); auto* Skeletal=Cast<USkeletalMesh>(Object); FMeshDescription D;
	if (Static && Static->GetRenderData() && Static->GetRenderData()->LODResources.IsValidIndex(LOD))
	{
		const auto& R=Static->GetRenderData()->LODResources[LOD]; const auto& P=R.VertexBuffers.PositionVertexBuffer; const auto& V=R.VertexBuffers.StaticMeshVertexBuffer;
		FStaticMeshAttributes A(D); A.Register(); auto Positions=A.GetVertexPositions(); auto Normals=A.GetVertexInstanceNormals(); auto Tangents=A.GetVertexInstanceTangents(); auto Signs=A.GetVertexInstanceBinormalSigns(); auto UVs=A.GetVertexInstanceUVs(); auto Colors=A.GetVertexInstanceColors(); auto Names=A.GetPolygonGroupMaterialSlotNames();
		UVs.SetNumChannels(V.GetNumTexCoords()); TArray<FVertexID> IDs;
		for (uint32 I=0;I<P.GetNumVertices();++I) {const auto ID=D.CreateVertex(); Positions[ID]=P.VertexPosition(I); IDs.Add(ID);}
		for (const auto& S : R.Sections)
		{
			const auto Group=D.CreatePolygonGroup(); Names[Group]=Static->GetStaticMaterials()[S.MaterialIndex].MaterialSlotName;
			for (uint32 T=0;T<S.NumTriangles;++T)
			{
				TArray<FVertexInstanceID,TInlineAllocator<3>> Corners;
				for (int32 K=0;K<3;++K)
				{
					const uint32 I=R.IndexBuffer.GetIndex(S.FirstIndex+T*3+K); const auto VI=D.CreateVertexInstance(IDs[I]);
					Normals[VI]=FVector3f(V.VertexTangentZ(I)); Tangents[VI]=V.VertexTangentX(I);
					Signs[VI]=FMath::Sign(FVector3f::DotProduct(FVector3f::CrossProduct(Normals[VI],Tangents[VI]),V.VertexTangentY(I)));
					for (uint32 C=0;C<V.GetNumTexCoords();++C) UVs.Set(VI,C,V.GetVertexUV(I,C));
					const FColor Color=R.VertexBuffers.ColorVertexBuffer.GetNumVertices()>I ? R.VertexBuffers.ColorVertexBuffer.VertexColor(I) : FColor::White;
					Colors[VI]=FVector4f(FLinearColor(Color)); Corners.Add(VI);
				}
				D.CreateTriangle(Group,Corners);
			}
		}
		Object->Modify(); *Static->CreateMeshDescription(LOD)=MoveTemp(D); Static->CommitMeshDescription(LOD);
		auto& Source=Static->GetSourceModel(LOD); Source.ReductionSettings.PercentTriangles=1.f; Source.ReductionSettings.PercentVertices=1.f;
		Source.BuildSettings.bRecomputeNormals=false; Source.BuildSettings.bRecomputeTangents=false; Source.BuildSettings.bRemoveDegenerates=false; Source.BuildSettings.bGenerateLightmapUVs=false;
		Source.BuildSettings.bUseFullPrecisionUVs=true;
	}
	else if (Skeletal && Skeletal->GetImportedModel() && Skeletal->GetImportedModel()->LODModels.IsValidIndex(LOD))
	{
		Skeletal->GetImportedModel()->LODModels[LOD].GetMeshDescription(Skeletal,LOD,D);
		Object->Modify(); Skeletal->CreateMeshDescription(LOD,MoveTemp(D)); Skeletal->CommitMeshDescription(LOD);
		auto* Info=Skeletal->GetLODInfo(LOD); Info->bHasBeenSimplified=false; Info->ReductionSettings.NumOfTrianglesPercentage=1.f; Info->ReductionSettings.NumOfVertPercentage=1.f;
		Info->BuildSettings.bRecomputeNormals=false; Info->BuildSettings.bRecomputeTangents=false; Info->BuildSettings.bRemoveDegenerates=false; Info->BuildSettings.bUseFullPrecisionUVs=true;
	}
	else return GuLiAuthoringResult(false,TEXT("Existing display LOD unavailable."));
	Object->MarkPackageDirty(); return GuLiAuthoringResult(true,TEXT(""));
#else
	return GuLiAuthoringResult(false,TEXT("Editor paint only."));
#endif
}

FString UGuLiModelAuthoringLibrary::RefreshPaintDisplayBounds(UObject* Object)
{
#if WITH_EDITOR
	auto* Mesh = Cast<UStaticMesh>(Object);
	if (!Mesh || (!Mesh->GetPathName().StartsWith(TEXT("/Game/GuLiStrike/Review/ModelInterface/"))
		&& Mesh->GetOutermost()->GetMetaData().GetValue(Mesh, TEXT("GuLi.ModelApproved")) != FString(TEXT("1"))))
		return GuLiAuthoringResult(false, TEXT("Only a paint candidate or an authorized formal paint mesh is supported."));
	Mesh->Modify();
	if (!GuLiRefreshDisplayBounds(Mesh)) return GuLiAuthoringResult(false, TEXT("Built render geometry is not available."));
	Mesh->MarkPackageDirty();
	return GuLiAuthoringResult(true, TEXT(""));
#else
	return GuLiAuthoringResult(false, TEXT("Editor paint bounds only."));
#endif
}

FString UGuLiModelAuthoringLibrary::EncodeIndexedMeshPaint(UObject* Object,int32 LOD,const FString& File)
{
#if WITH_EDITOR
	if (!Object || !Object->GetPathName().StartsWith(TEXT("/Game/GuLiStrike/Review/ModelInterface/")) || LOD<0)
		return GuLiAuthoringResult(false,TEXT("Only independent review meshes accept paint encoding."));
	auto* Static=Cast<UStaticMesh>(Object); auto* Skeletal=Cast<USkeletalMesh>(Object);
	FMeshDescription* D=Static ? Static->GetMeshDescription(LOD) : (Skeletal ? Skeletal->GetMeshDescription(LOD) : nullptr);
	FString Text; TSharedPtr<FJsonObject> Json;
	if (!D || !FFileHelper::LoadFileToString(Text,*File) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Json)) return GuLiAuthoringResult(false,TEXT("Mesh description or paint sidecar unavailable."));
	const TArray<TSharedPtr<FJsonValue>>* Triangles=nullptr; const TArray<TSharedPtr<FJsonValue>>* IDs=nullptr;
	if (!Json->TryGetArrayField(TEXT("triangles"),Triangles) || !Json->TryGetArrayField(TEXT("triangle_ids"),IDs) || IDs->Num()!=Triangles->Num()) return GuLiAuthoringResult(false,TEXT("Indexed paint requires exact triangle IDs."));
	FStaticMeshAttributes A(*D); auto Positions=A.GetVertexPositions(); auto Colors=A.GetVertexInstanceColors(); const auto Names=A.GetPolygonGroupMaterialSlotNames();
	if (!Colors.IsValid()) return GuLiAuthoringResult(false,TEXT("Missing existing vertex color attribute."));
	TSet<FTriangleID> Covered; TMap<FVertexInstanceID,FVector4f> Pending;
	struct FSeam { FTriangleID Triangle; int32 Corner; FVertexInstanceID Original; FVector4f Color; };
	TArray<FSeam> Seams;
	for (int32 I=0;I<IDs->Num();++I)
	{
		const FTriangleID T(FMath::RoundToInt((*IDs)[I]->AsNumber())); const auto& Paint=(*Triangles)[I]->AsArray();
		if (!D->Triangles().IsValid(T) || Paint.Num()!=13 || Covered.Contains(T)) return GuLiAuthoringResult(false,TEXT("Invalid or repeated triangle ID."));
		if (!GuLiPaintableSlot(Object,GuLiPaintSlot(Object,Names[D->GetTrianglePolygonGroup(T)],LOD,D->GetTrianglePolygonGroup(T).GetValue()))) continue;
		const auto V=D->GetTriangleVertices(T);
		for (int32 K=0;K<3;++K)
		{
			const FVector3f Expected(Paint[K*3]->AsNumber(),Paint[K*3+1]->AsNumber(),Paint[K*3+2]->AsNumber());
			if (!Positions[V[K]].Equals(Expected,1.e-4f)) return GuLiAuthoringResult(false,TEXT("Sidecar positions differ from the unchanged original triangle."));
		}
		const int32 Role=FMath::RoundToInt(Paint[9]->AsNumber());
		if (Role<0 || Role>7) return GuLiAuthoringResult(false,TEXT("Invalid paint role."));
		const FVector4f Color(Paint[10]->AsNumber(),Paint[11]->AsNumber(),Paint[12]->AsNumber(),float(Role)/255.f);
		const auto Corners=D->GetTriangleVertexInstances(T);
		for (int32 K=0;K<Corners.Num();++K)
		{
			const auto VI=Corners[K];
			if (const auto* Previous=Pending.Find(VI); Previous && !Previous->Equals(Color,1.e-5f))
			{
				if (D->GetPolygonTriangles(D->GetTrianglePolygon(T)).Num()!=1) return GuLiAuthoringResult(false,TEXT("Paint seam inside an existing triangulated polygon requires a whole-polygon region."));
				Seams.Add({T,K,VI,Color}); continue;
			}
			Pending.Add(VI,Color);
		}
		Covered.Add(T);
	}
	for (const auto T : D->Triangles().GetElementIDs()) if (GuLiPaintableSlot(Object,GuLiPaintSlot(Object,Names[D->GetTrianglePolygonGroup(T)],LOD,D->GetTrianglePolygonGroup(T).GetValue())) && !Covered.Contains(T)) return GuLiAuthoringResult(false,TEXT("Paint does not cover every original opaque body triangle."));
	for (const auto& Pair : Pending) for (const auto T : D->GetVertexInstanceConnectedTriangleIDs(Pair.Key)) if (!GuLiPaintableSlot(Object,GuLiPaintSlot(Object,Names[D->GetTrianglePolygonGroup(T)],LOD,D->GetTrianglePolygonGroup(T).GetValue()))) return GuLiAuthoringResult(false,TEXT("Protected and body material share a source corner."));
	if (Pending.IsEmpty()) return GuLiAuthoringResult(false,TEXT("No opaque body paint."));
	Object->Modify();
	for (const auto& Seam : Seams)
	{
		const auto NewVI=D->CreateVertexInstance(D->GetVertexInstanceVertex(Seam.Original));
		D->VertexInstanceAttributes().ForEach([&](FName Name,auto Values)
		{
			for (int32 C=0;C<Values.GetNumChannels();++C)
			{
				const auto From=Values.GetArrayView(Seam.Original.GetValue(),C); auto To=Values.GetArrayView(NewVI.GetValue(),C);
				for (int32 I=0;I<From.Num();++I) To[I]=From[I];
			}
		});
		D->SetPolygonVertexInstance(D->GetTrianglePolygon(Seam.Triangle),Seam.Corner,NewVI);
		Pending.Add(NewVI,Seam.Color);
	}
	Colors=FStaticMeshAttributes(*D).GetVertexInstanceColors();
	for (const auto& Pair : Pending) Colors[Pair.Key]=Pair.Value;
	if (Static) {Static->CommitMeshDescription(LOD); Static->PostEditChange(); GuLiRefreshDisplayBounds(Static);}
	else {Skeletal->SetHasVertexColors(true); Skeletal->CommitMeshDescription(LOD); Skeletal->PostEditChange();}
	Object->MarkPackageDirty(); return GuLiAuthoringResult(true,TEXT(""));
#else
	return GuLiAuthoringResult(false,TEXT("Editor paint only."));
#endif
}

FString UGuLiModelAuthoringLibrary::EncodeSkeletalRenderPaint(UObject* Object,int32 LOD,const FString& File)
{
#if WITH_EDITOR
	auto* Mesh=Cast<USkeletalMesh>(Object);
	if (!Mesh || !Mesh->GetPathName().StartsWith(TEXT("/Game/GuLiStrike/Review/ModelInterface/")) || !Mesh->GetImportedModel() || !Mesh->GetImportedModel()->LODModels.IsValidIndex(LOD) || !Mesh->GetResourceForRendering())
		return GuLiAuthoringResult(false,TEXT("Existing review skeletal display LOD required."));
	FString Text; TSharedPtr<FJsonObject> Json; const TArray<TSharedPtr<FJsonValue>>* Input=nullptr;
	if (!FFileHelper::LoadFileToString(Text,*File) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Json) || !Json->TryGetArrayField(TEXT("render_vertex_colors"),Input)) return GuLiAuthoringResult(false,TEXT("Missing skeletal display vertex paint."));
	auto& Model=Mesh->GetImportedModel()->LODModels[LOD]; auto& Render=Mesh->GetResourceForRendering()->LODRenderData[LOD]; TMap<int32,FColor> Pending;
	for (const auto& Value : *Input)
	{
		const auto& Row=Value->AsArray(); if (Row.Num()!=8) return GuLiAuthoringResult(false,TEXT("Expected vertex ID, original position, role and linear RGB."));
		const int32 Index=FMath::RoundToInt(Row[0]->AsNumber()), Role=FMath::RoundToInt(Row[4]->AsNumber()); int32 Section=INDEX_NONE, Local=INDEX_NONE;
		if (Index<0 || uint32(Index)>=Model.NumVertices || Role<0 || Role>7 || Pending.Contains(Index)) return GuLiAuthoringResult(false,TEXT("Invalid skeletal paint vertex."));
		Model.GetSectionFromVertexIndex(Index,Section,Local);
		if (!GuLiPaintableSlot(Mesh,Model.Sections[Section].MaterialIndex)) return GuLiAuthoringResult(false,TEXT("Skeletal paint includes a protected slot."));
		const FVector3f P(Row[1]->AsNumber(),Row[2]->AsNumber(),Row[3]->AsNumber());
		if (!Model.Sections[Section].SoftVertices[Local].Position.Equals(P,1.e-4f)) return GuLiAuthoringResult(false,TEXT("Skeletal display vertex moved."));
		FColor Color=FLinearColor(Row[5]->AsNumber(),Row[6]->AsNumber(),Row[7]->AsNumber()).ToFColor(true); Color.A=uint8(Role); Pending.Add(Index,Color);
	}
	for (const auto& S : Model.Sections) if (GuLiPaintableSlot(Mesh,S.MaterialIndex)) for (int32 I=0;I<S.SoftVertices.Num();++I) if (!Pending.Contains(S.BaseVertexIndex+I)) return GuLiAuthoringResult(false,TEXT("Skeletal paint does not cover every body vertex."));
	FlushRenderingCommands(); Mesh->Modify(); Mesh->SetHasVertexColors(true); Mesh->SetVertexColorGuid(FGuid::NewGuid());
	Mesh->ReleaseResources(); Mesh->ReleaseResourcesFence.Wait();
	TUniquePtr<FSkinnedMeshComponentRecreateRenderStateContext> Context=MakeUnique<FSkinnedMeshComponentRecreateRenderStateContext>(Mesh);
	if (Render.StaticVertexBuffers.ColorVertexBuffer.GetNumVertices()==0) Render.StaticVertexBuffers.ColorVertexBuffer.InitFromSingleColor(FColor::White,Render.GetNumVertices());
	for (const auto& Pair : Pending)
	{
		int32 Section=INDEX_NONE,Local=INDEX_NONE; Model.GetSectionFromVertexIndex(Pair.Key,Section,Local);
		Model.Sections[Section].SoftVertices[Local].Color=Pair.Value;
		Render.StaticVertexBuffers.ColorVertexBuffer.VertexColor(Pair.Key)=Pair.Value;
	}
	Mesh->GetLODInfo(LOD)->bHasPerLODVertexColors=true; Mesh->InitResources(); Mesh->GetOnMeshChanged().Broadcast(); Mesh->MarkPackageDirty();
	return GuLiAuthoringResult(true,TEXT(""));
#else
	return GuLiAuthoringResult(false,TEXT("Editor paint only."));
#endif
}

bool UGuLiModelAuthoringLibrary::EncodeCandidateMesh(UObject* Object, int32 LOD, const FString& File, FString& Error)
{
	Error.Reset();
#if WITH_EDITOR
	if (!Object || !Object->GetPathName().StartsWith(TEXT("/Game/GuLiStrike/Review/ModelInterface/")))
	{ Error=TEXT("Paint encoding accepts independent Review/ModelInterface assets only; formal assets are protected."); return false; }
	auto* Static=Cast<UStaticMesh>(Object); auto* Skeletal=Cast<USkeletalMesh>(Object);
	if ((!Static && !Skeletal) || LOD<0) { Error=TEXT("Expected StaticMesh or SkeletalMesh and valid LOD."); return false; }
	FMeshDescription* Description=Static ? Static->GetMeshDescription(LOD) : Skeletal->GetMeshDescription(LOD);
	if (!Description) { Error=TEXT("Source LOD has no editable mesh description; do not reconstruct/reorder its geometry."); return false; }
	FString Text; TSharedPtr<FJsonObject> Json;
	if (!FFileHelper::LoadFileToString(Text,*File) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Json))
	{ Error=TEXT("Missing/invalid paint region sidecar."); return false; }
	const TArray<TSharedPtr<FJsonValue>>* Input=nullptr;
	if (!Json->TryGetArrayField(TEXT("triangles"),Input) || Input->IsEmpty()) { Error=TEXT("Empty paint region sidecar."); return false; }
	struct FPaint { FVector3f Points[3]; FVector3f RGB; uint8 Role=0; };
	TArray<FPaint> Paint; FBox3f SourceBox(ForceInit);
	for (const auto& Value : *Input)
	{
		const auto& A=Value->AsArray(); if (A.Num()!=13) { Error=TEXT("Expected nine coordinates, role and RGB."); return false; }
		FPaint P;
		for (int32 I=0; I<3; ++I) { P.Points[I]=FVector3f(A[I*3]->AsNumber(),A[I*3+1]->AsNumber(),A[I*3+2]->AsNumber()); SourceBox+=P.Points[I]; }
		const int32 Role=FMath::RoundToInt(A[9]->AsNumber()); if (Role<0 || Role>7) { Error=TEXT("Invalid paint role."); return false; }
		P.Role=uint8(Role); P.RGB=FVector3f(A[10]->AsNumber(),A[11]->AsNumber(),A[12]->AsNumber()); Paint.Add(P);
	}
	FStaticMeshAttributes Attributes(*Description);
	// The source description already owns these attributes. Register() would
	// reset the existing UV channel count and corrupt animation/lightmap UVs.
	auto Positions=Attributes.GetVertexPositions(); auto Colors=Attributes.GetVertexInstanceColors();
	const auto SlotNames=Attributes.GetPolygonGroupMaterialSlotNames();
	TSet<FName> OpaqueSlots;
	auto AddOpaqueSlot=[&OpaqueSlots](FName Name, const UMaterialInterface* Material)
	{
		const FString Label=Name.ToString().ToLower();
		if (Material && Material->GetBlendMode()==BLEND_Opaque && !Label.Contains(TEXT("outline"))
			&& !Label.Contains(TEXT("contour")) && !Label.Contains(TEXT("display")) && !Label.Contains(TEXT("glass"))) OpaqueSlots.Add(Name);
	};
	if (Static) for (const auto& Slot : Static->GetStaticMaterials()) AddOpaqueSlot(Slot.MaterialSlotName,Slot.MaterialInterface);
	else for (const auto& Slot : Skeletal->GetMaterials()) AddOpaqueSlot(Slot.MaterialSlotName,Slot.MaterialInterface);
	// Separate outline shells expand the bounds and are absent from the approved
	// body sidecar. They must neither participate in correspondence nor be painted.
	FBox3f TargetBox(ForceInit);
	for (const auto Triangle : Description->Triangles().GetElementIDs())
	{
		const auto Label=SlotNames[Description->GetTrianglePolygonGroup(Triangle)].ToString().ToLower();
		if (Label.Contains(TEXT("outline")) || Label.Contains(TEXT("contour"))) continue;
		for (const auto V : Description->GetTriangleVertices(Triangle)) TargetBox+=Positions[V];
	}
	const FVector3f SourceSize=SourceBox.GetSize(), TargetSize=TargetBox.GetSize();
	if (SourceSize.GetMin()<UE_SMALL_NUMBER || TargetSize.GetMin()<UE_SMALL_NUMBER) { Error=TEXT("Degenerate paint/source bounds."); return false; }
	// Normalize bounds only for correspondence. All original vertices/normals/UVs/weights stay untouched.
	auto Key=[](const FVector3f& A,const FVector3f& B,const FVector3f& C)
	{
		TArray<FString,TInlineAllocator<3>> Points;
		for (const auto& V : {A,B,C}) Points.Add(FString::Printf(TEXT("%d,%d,%d"),FMath::RoundToInt(V.X*100000),FMath::RoundToInt(V.Y*100000),FMath::RoundToInt(V.Z*100000)));
		Points.Sort(); return Points[0]+TEXT("/")+Points[1]+TEXT("/")+Points[2];
	};
	TMap<FTriangleID,FString> TargetKeys;
	for (const auto Triangle : Description->Triangles().GetElementIDs())
	{
		if (!OpaqueSlots.Contains(SlotNames[Description->GetTrianglePolygonGroup(Triangle)])) continue;
		const auto Vertices=Description->GetTriangleVertices(Triangle);
		TargetKeys.Add(Triangle,Key((Positions[Vertices[0]]-TargetBox.Min)/TargetSize,(Positions[Vertices[1]]-TargetBox.Min)/TargetSize,(Positions[Vertices[2]]-TargetBox.Min)/TargetSize));
	}
	const int32 Permutations[6][3]={{0,1,2},{0,2,1},{1,0,2},{1,2,0},{2,0,1},{2,1,0}};
	const TArray<TSharedPtr<FJsonValue>>* ExplicitAxes=nullptr;
	int32 RequestedAxis[3]={0,1,2}, RequestedSigns=0;
	if (Json->TryGetArrayField(TEXT("ue_axes"),ExplicitAxes))
	{
		if (ExplicitAxes->Num()!=3) { Error=TEXT("ue_axes requires three signed source axes, for example [1,-2,3]."); return false; }
		TSet<int32> Used;
		for (int32 A=0; A<3; ++A)
		{
			const int32 Value=FMath::RoundToInt((*ExplicitAxes)[A]->AsNumber());
			if (Value==0 || FMath::Abs(Value)>3 || Used.Contains(FMath::Abs(Value))) { Error=TEXT("Invalid/duplicate ue_axes source axis."); return false; }
			Used.Add(FMath::Abs(Value)); RequestedAxis[A]=FMath::Abs(Value)-1;
			if (Value<0) RequestedSigns|=1<<A;
		}
	}
	// FBX unit conversion introduces sub-pixel float differences. Snap only to
	// existing target positions within 0.002% of normalized bounds; never move a vertex.
	TMap<FIntVector,TArray<FVector3f>> TargetPoints;
	auto Cell=[](const FVector3f& V){return FIntVector(FMath::FloorToInt(V.X*50000),FMath::FloorToInt(V.Y*50000),FMath::FloorToInt(V.Z*50000));};
	for (const auto& Pair : TargetKeys) for (const auto V : Description->GetTriangleVertices(Pair.Key))
	{
		const FVector3f N=(Positions[V]-TargetBox.Min)/TargetSize; TargetPoints.FindOrAdd(Cell(N)).AddUnique(N);
	}
	auto Snap=[&](const FVector3f& N)
	{
		const FIntVector C=Cell(N); FVector3f Best=N; float Distance=4.e-10f;
		for (int32 X=-1;X<=1;++X) for (int32 Y=-1;Y<=1;++Y) for (int32 Z=-1;Z<=1;++Z)
			if (const auto* Points=TargetPoints.Find(C+FIntVector(X,Y,Z))) for (const auto& P : *Points)
			{
				const float D=(P-N).SizeSquared(); if (D<Distance) { Distance=D; Best=P; }
			}
		return Best;
	};
	TMap<FString,FPaint> Best; int32 BestMatches=0;
	bool AmbiguousPaint = false;
	for (const auto& Axis : Permutations) for (int32 Signs=0; Signs<8; ++Signs)
	{
		if (ExplicitAxes && (Signs!=RequestedSigns || Axis[0]!=RequestedAxis[0] || Axis[1]!=RequestedAxis[1] || Axis[2]!=RequestedAxis[2])) continue;
		TMap<FString,FPaint> Lookup; bool Conflict=false;
		TMap<FVector3f,FVector3f> SnappedPoints;
		for (const auto& P : Paint)
		{
			FVector3f N[3];
			for (int32 I=0; I<3; ++I)
			{
				const auto V=(P.Points[I]-SourceBox.Min)/SourceSize;
				for (int32 A=0; A<3; ++A) N[I][A]=(Signs&(1<<A)) ? 1.f-V[Axis[A]] : V[Axis[A]];
				if (const auto* Existing=SnappedPoints.Find(N[I])) N[I]=*Existing;
				else { const FVector3f InputPoint=N[I]; N[I]=Snap(InputPoint); SnappedPoints.Add(InputPoint,N[I]); }
			}
			const auto K=Key(N[0],N[1],N[2]);
			if (const auto* Old=Lookup.Find(K); Old && (Old->Role!=P.Role || !Old->RGB.Equals(P.RGB,1.e-5f))) { Conflict=true; break; }
			Lookup.Add(K,P);
		}
		if (Conflict) continue;
		int32 Matches=0; for (const auto& Pair : TargetKeys) if (Lookup.Contains(Pair.Value)) ++Matches;
		if (Matches>BestMatches) { BestMatches=Matches; Best=MoveTemp(Lookup); AmbiguousPaint=false; }
		else if (Matches==TargetKeys.Num() && BestMatches==Matches)
		{
			// Symmetric meshes can match several orientations. Do not guess which faces
			// are front/back when those orientations would assign different paint.
			for (const auto& Pair : TargetKeys)
			{
				const auto& First=Best.FindChecked(Pair.Value);
				const auto& Other=Lookup.FindChecked(Pair.Value);
				if (First.Role!=Other.Role || !First.RGB.Equals(Other.RGB,1.e-5f)) { AmbiguousPaint=true; break; }
			}
		}
	}
	// Refuse partial paint. A failed topology/LOD match must not silently mislabel fixed faces.
	if (BestMatches!=TargetKeys.Num())
	{ Error=FString::Printf(TEXT("LOD %d correspondence incomplete: %d/%d triangles. Preserve original mesh; supply its exact matching B LOD."),LOD,BestMatches,TargetKeys.Num()); return false; }
	if (AmbiguousPaint)
	{ Error=TEXT("Symmetric mesh has ambiguous paint orientation; preserve its geometry and supply an explicitly oriented region sidecar."); return false; }
	TMap<FVertexInstanceID,FVector4f> PendingColors;
	for (const auto& Pair : TargetKeys)
	{
		if (!OpaqueSlots.Contains(SlotNames[Description->GetTrianglePolygonGroup(Pair.Key)])) continue;
		const auto& P=Best.FindChecked(Pair.Value);
		const FVector4f Color(P.RGB.X,P.RGB.Y,P.RGB.Z,float(P.Role)/255.f);
		for (const auto VI : Description->GetTriangleVertexInstances(Pair.Key))
		{
			if (const auto* Old=PendingColors.Find(VI); Old && !Old->Equals(Color,1.e-5f))
			{ Error=TEXT("Existing shared vertex instance crosses different paint roles; encoding refused without changing topology."); return false; }
			PendingColors.Add(VI,Color);
		}
	}
	if (PendingColors.IsEmpty()) { Error=TEXT("No opaque body slots; transparency, linework and fixed displays are preserved."); return false; }
	for (const auto& Pair : PendingColors)
		for (const auto Triangle : Description->GetVertexInstanceConnectedTriangleIDs(Pair.Key))
			if (!OpaqueSlots.Contains(SlotNames[Description->GetTrianglePolygonGroup(Triangle)]))
			{ Error=TEXT("Body vertex instance is shared with a protected material slot; encoding refused to preserve transparency/linework."); return false; }
	Object->Modify();
	for (const auto& Pair : PendingColors) Colors[Pair.Key]=Pair.Value;
	if (Static) { Static->CommitMeshDescription(LOD); Static->PostEditChange(); GuLiRefreshDisplayBounds(Static); }
	else { Skeletal->SetHasVertexColors(true); Skeletal->CommitMeshDescription(LOD); Skeletal->PostEditChange(); }
	Object->MarkPackageDirty();
	return true;
#else
	Error=TEXT("Model paint encoding is editor-only."); return false;
#endif
}

bool UGuLiModelAuthoringLibrary::EncodeUniformCandidateMesh(UObject* Object, int32 LOD, int32 Role, FLinearColor Color, FString& Error)
{
	Error.Reset();
#if WITH_EDITOR
	if (!Object || !Object->GetPathName().StartsWith(TEXT("/Game/GuLiStrike/Review/ModelInterface/")) || LOD<0 || Role<0 || Role>7
		|| !FMath::IsFinite(Color.R) || !FMath::IsFinite(Color.G) || !FMath::IsFinite(Color.B))
	{ Error=TEXT("Uniform paint requires an independent review mesh, valid role/LOD and finite linear RGB."); return false; }
	auto* Static=Cast<UStaticMesh>(Object); auto* Skeletal=Cast<USkeletalMesh>(Object);
	FMeshDescription* Description=Static ? Static->GetMeshDescription(LOD) : (Skeletal ? Skeletal->GetMeshDescription(LOD) : nullptr);
	if (!Description) { Error=TEXT("Original source LOD has no editable mesh description."); return false; }
	FStaticMeshAttributes Attributes(*Description);
	const auto SlotNames=Attributes.GetPolygonGroupMaterialSlotNames();
	TSet<FName> Opaque;
	auto AddSlot=[&](FName Name,const UMaterialInterface* Material)
	{
		const FString Label=Name.ToString().ToLower();
		if (Material && Material->GetBlendMode()==BLEND_Opaque && !Label.Contains(TEXT("outline")) && !Label.Contains(TEXT("contour"))
			&& !Label.Contains(TEXT("display")) && !Label.Contains(TEXT("glass"))) Opaque.Add(Name);
	};
	if (Static) for (const auto& S : Static->GetStaticMaterials()) AddSlot(S.MaterialSlotName,S.MaterialInterface);
	else for (const auto& S : Skeletal->GetMaterials()) AddSlot(S.MaterialSlotName,S.MaterialInterface);
	TSet<FVertexInstanceID> Pending;
	for (const auto T : Description->Triangles().GetElementIDs()) if (Opaque.Contains(SlotNames[Description->GetTrianglePolygonGroup(T)]))
		for (const auto VI : Description->GetTriangleVertexInstances(T)) Pending.Add(VI);
	if (Pending.IsEmpty()) { Error=TEXT("No opaque body faces."); return false; }
	for (const auto VI : Pending) for (const auto T : Description->GetVertexInstanceConnectedTriangleIDs(VI))
		if (!Opaque.Contains(SlotNames[Description->GetTrianglePolygonGroup(T)]))
		{ Error=TEXT("Uniform paint would affect a protected shared vertex instance."); return false; }
	Object->Modify(); auto Colors=Attributes.GetVertexInstanceColors();
	for (const auto VI : Pending) Colors[VI]=FVector4f(Color.R,Color.G,Color.B,float(Role)/255.f);
	if (Static) { Static->CommitMeshDescription(LOD); Static->PostEditChange(); GuLiRefreshDisplayBounds(Static); }
	else { Skeletal->SetHasVertexColors(true); Skeletal->CommitMeshDescription(LOD); Skeletal->PostEditChange(); }
	Object->MarkPackageDirty(); return true;
#else
	Error=TEXT("Model paint encoding is editor-only."); return false;
#endif
}

AActor* UGuLiModelAuthoringLibrary::SpawnRuntimePaintSample(UObject* Context, int32 Id, EGuLiTeam Team, FVector Location, float Scale)
{
#if WITH_EDITOR
	auto* World=Context ? Context->GetWorld() : nullptr;
	if (!World || !World->IsPlayInEditor() || !World->IsGameWorld() || !FMath::IsFinite(Scale) || Scale<=0 || Location.ContainsNaN()) return nullptr;
	auto* Registry=World->GetSubsystem<UGuLiModelRegistrySubsystem>();
	FGuLiStrikeModelsModelsRow Definition;
	if (!Registry || !Registry->GetModelDefinition(Id,Definition)) return nullptr;
	FActorSpawnParameters Params; Params.ObjectFlags|=RF_Transient;
	Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AActor* Actor=nullptr;
	if (auto* StaticResource=Registry->LoadStaticModel(Id))
	{
		auto* Sample=World->SpawnActor<AStaticMeshActor>(Location,FRotator::ZeroRotator,Params);
		if (Sample) { Sample->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable); Sample->GetStaticMeshComponent()->SetStaticMesh(StaticResource); }
		Actor=Sample;
	}
	else if (auto* SkeletalResource=Registry->LoadSkeletalModel(Id))
	{
		auto* Sample=World->SpawnActor<ASkeletalMeshActor>(Location,FRotator::ZeroRotator,Params);
		if (Sample) Sample->GetSkeletalMeshComponent()->SetSkeletalMeshAsset(SkeletalResource);
		Actor=Sample;
	}
	else if (UClass* Class=Registry->LoadPresentationClass(Id)) Actor=World->SpawnActor<AActor>(Class,Location,FRotator::ZeroRotator,Params);
	if (!Actor) return nullptr;
	Actor->SetActorScale3D(FVector(Scale)); Actor->SetActorEnableCollision(false);
	Actor->Tags.Add(TEXT("GuLi.ModelRuntimePaintSample"));
	auto* Identity=NewObject<UGuLiTeamOutlineComponent>(Actor);
	Actor->AddInstanceComponent(Identity); Identity->RegisterComponent(); Identity->SetOutlineTeam(Team);
	const auto Parts=Registry->GetModelParts(Id);
	TArray<UMeshComponent*> Components; Actor->GetComponents(Components);
	for (auto* Component : Components)
	{
		Component->SetCanEverAffectNavigation(false);
		FString Part=TEXT("Root");
		for (const auto& Binding : Parts) if (UGuLiModelRegistrySubsystem::FindPart(Actor,Binding.ComponentPath)==Component) { Part=Binding.PartKey; break; }
		// A transient assembly may carry an uninitialized gameplay lifecycle.
		// Its review team is explicit; normal gameplay registrations use ownership.
		UGuLiLocalTeamColorSubsystem::Register(Actor,Component,Id,Part,Team);
	}
	return Actor;
#else
	return nullptr;
#endif
}

bool UGuLiModelAuthoringLibrary::CaptureRuntimeViewport(APlayerController* Controller, const FString& File)
{
#if WITH_EDITOR
	if (!Controller || !Controller->IsLocalController() || !Controller->GetWorld()->IsPlayInEditor()
		|| !FSlateApplication::IsInitialized()) return false;
	FString Path=FPaths::ConvertRelativePathToFull(File);
	FPaths::NormalizeFilename(Path); FPaths::CollapseRelativeDirectories(Path);
	FString Root=FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()/TEXT("Artifacts/"));
	FPaths::NormalizeFilename(Root);
	if (!Path.StartsWith(Root) || !Path.EndsWith(TEXT(".png"))) return false;
	const auto* Player=Controller->GetLocalPlayer();
	const auto Widget=Player && Player->ViewportClient ? Player->ViewportClient->GetGameViewportWidget() : nullptr;
	if (!Widget.IsValid()) return false;
	TArray<FColor> Pixels; FIntVector Size;
	if (!FSlateApplication::Get().TakeScreenshot(Widget.ToSharedRef(),Pixels,Size) || Size.X<=0 || Size.Y<=0) return false;
	TArray64<uint8> PNG;
	FImageUtils::PNGCompressImageArray(Size.X,Size.Y,MakeArrayView(Pixels),PNG);
	return FFileHelper::SaveArrayToFile(PNG,*Path);
#else
	return false;
#endif
}
