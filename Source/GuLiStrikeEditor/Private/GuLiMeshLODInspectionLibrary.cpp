#include "GuLiMeshLODInspectionLibrary.h"

#include "Engine/StaticMesh.h"
#include "StaticMeshAttributes.h"
#include "StaticMeshResources.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
TArray<TSharedPtr<FJsonValue>> ColorSummary(const TMap<uint32, int32>& Counts)
{
	TArray<TPair<uint32, int32>> Sorted;
	for (const auto& Item : Counts) Sorted.Emplace(Item.Key, Item.Value);
	Sorted.Sort([](const auto& A, const auto& B) { return A.Value > B.Value; });
	TArray<TSharedPtr<FJsonValue>> Result;
	for (int32 Index = 0; Index < FMath::Min(16, Sorted.Num()); ++Index)
	{
		auto Row = MakeShared<FJsonObject>();
		Row->SetStringField(TEXT("rgba"), FColor(Sorted[Index].Key).ToHex());
		Row->SetNumberField(TEXT("count"), Sorted[Index].Value);
		Result.Add(MakeShared<FJsonValueObject>(Row));
	}
	return Result;
}
}

FString UGuLiMeshLODInspectionLibrary::InspectStaticMeshLODInputs(UStaticMesh* Mesh)
{
	auto Root = MakeShared<FJsonObject>();
	Root->SetBoolField(TEXT("success"), Mesh != nullptr);
	if (Mesh)
	{
		Root->SetStringField(TEXT("mesh"), Mesh->GetPathName());
		TArray<TSharedPtr<FJsonValue>> Rows;
		for (int32 LOD = 0; LOD < Mesh->GetNumLODs(); ++LOD)
		{
			auto Row = MakeShared<FJsonObject>();
			Row->SetNumberField(TEXT("lod"), LOD);
			if (const FMeshDescription* Description = Mesh->GetMeshDescription(LOD))
			{
				const FStaticMeshConstAttributes Attributes(*Description);
				const auto Colors = Attributes.GetVertexInstanceColors();
				const auto Names = Attributes.GetPolygonGroupMaterialSlotNames();
				const auto Positions = Attributes.GetVertexPositions();
				const auto Normals = Attributes.GetVertexInstanceNormals();
				TMap<uint32, int32> Counts;
				for (const FVertexInstanceID Vertex : Description->VertexInstances().GetElementIDs())
				{
					const FVector4f V = Colors[Vertex];
					++Counts.FindOrAdd(FLinearColor(V.X, V.Y, V.Z, V.W).ToFColor(false).DWColor());
				}
				Row->SetNumberField(TEXT("source_unique_colors"), Counts.Num());
				Row->SetArrayField(TEXT("source_colors"), ColorSummary(Counts));
				TArray<TSharedPtr<FJsonValue>> Groups;
				for (const FPolygonGroupID Group : Description->PolygonGroups().GetElementIDs())
				{
					auto G = MakeShared<FJsonObject>();
					G->SetStringField(TEXT("slot"), Names[Group].ToString());
					G->SetNumberField(TEXT("triangles"), Description->GetNumPolygonGroupTriangles(Group));
					double Volume = 0;
					int32 AlignedNormals = 0;
					for (const FTriangleID Triangle : Description->GetPolygonGroupTriangles(Group))
					{
						const auto Vertices = Description->GetTriangleVertices(Triangle);
						const auto Instances = Description->GetTriangleVertexInstances(Triangle);
						const FVector A(Positions[Vertices[0]]);
						const FVector B(Positions[Vertices[1]]);
						const FVector C(Positions[Vertices[2]]);
						Volume += FVector::DotProduct(A, FVector::CrossProduct(B, C)) / 6.0;
						const FVector FaceNormal = FVector::CrossProduct(B - A, C - A);
						if (FVector::DotProduct(FaceNormal, FVector(Normals[Instances[0]])) > 0)
							++AlignedNormals;
					}
					G->SetNumberField(TEXT("signed_volume"), Volume);
					G->SetNumberField(TEXT("normal_aligned_triangles"), AlignedNormals);
					Groups.Add(MakeShared<FJsonValueObject>(G));
				}
				Row->SetArrayField(TEXT("source_groups"), Groups);
			}
			if (const FStaticMeshRenderData* RenderData = Mesh->GetRenderData())
			{
				const auto& Resource = RenderData->LODResources[LOD];
				const auto& Buffer = Resource.VertexBuffers.ColorVertexBuffer;
				TMap<uint32, int32> Counts;
				for (uint32 Vertex = 0; Vertex < Buffer.GetNumVertices(); ++Vertex)
					++Counts.FindOrAdd(Buffer.VertexColor(Vertex).DWColor());
				Row->SetNumberField(TEXT("render_unique_colors"), Counts.Num());
				Row->SetArrayField(TEXT("render_colors"), ColorSummary(Counts));
				TArray<TSharedPtr<FJsonValue>> Sections;
				for (const auto& Section : Resource.Sections)
				{
					auto S = MakeShared<FJsonObject>();
					S->SetNumberField(TEXT("material"), Section.MaterialIndex);
					S->SetNumberField(TEXT("triangles"), Section.NumTriangles);
					Sections.Add(MakeShared<FJsonValueObject>(S));
				}
				Row->SetArrayField(TEXT("render_sections"), Sections);
			}
			Rows.Add(MakeShared<FJsonValueObject>(Row));
		}
		Root->SetArrayField(TEXT("lods"), Rows);
	}
	FString Result;
	FJsonSerializer::Serialize(Root, TJsonWriterFactory<>::Create(&Result));
	return Result;
}
