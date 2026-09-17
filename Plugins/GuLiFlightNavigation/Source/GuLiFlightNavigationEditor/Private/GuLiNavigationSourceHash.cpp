#include "GuLiNavigationSourceHash.h"

#include "AI/Navigation/NavCollisionBase.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "LandscapeHeightfieldCollisionComponent.h"
#include "LandscapeProxy.h"
#include "PhysicsEngine/BodySetup.h"
#include "UObject/UnrealType.h"

void FGuLiNavigationSourceHash::AddBytes(const void* Data, const int64 Size)
{
	const uint8* Bytes = static_cast<const uint8*>(Data);
	for (int64 Index = 0; Index < Size; ++Index)
	{
		State ^= Bytes[Index];
		State *= 1099511628211ull;
	}
}

void FGuLiNavigationSourceHash::AddUInt64(const uint64 Value)
{
	for (uint32 Shift = 0; Shift < 64; Shift += 8)
	{
		const uint8 Byte = static_cast<uint8>((Value >> Shift) & 0xff);
		AddBytes(&Byte, 1);
	}
}

void FGuLiNavigationSourceHash::AddString(const FString& Value)
{
	const FTCHARToUTF8 Utf8(*Value);
	AddUInt64(Utf8.Length());
	AddBytes(Utf8.Get(), Utf8.Length());
}

void FGuLiNavigationSourceHash::AddVector(const FVector& Value)
{
	// One hundredth of a centimetre; do not hash padded native structs.
	AddUInt64(static_cast<uint64>(FMath::RoundToInt64(Value.X * 100.0)));
	AddUInt64(static_cast<uint64>(FMath::RoundToInt64(Value.Y * 100.0)));
	AddUInt64(static_cast<uint64>(FMath::RoundToInt64(Value.Z * 100.0)));
}

void FGuLiNavigationSourceHash::AddTransform(const FTransform& Value)
{
	AddVector(Value.GetLocation());
	const FRotator Rotation = Value.Rotator().GetNormalized();
	AddVector(FVector(Rotation.Pitch, Rotation.Yaw, Rotation.Roll));
	AddVector(Value.GetScale3D());
}

void FGuLiNavigationSourceHash::AddProperty(const UObject* Object, const TCHAR* Name)
{
	if (!Object) return;
	if (const FProperty* Property = Object->GetClass()->FindPropertyByName(Name))
	{
		AddString(Name);
		AddUInt64(Property->ArrayDim);
		for (int32 Index = 0; Index < Property->ArrayDim; ++Index)
		{
			FString Value;
			Property->ExportText_InContainer(Index, Value, Object, nullptr, const_cast<UObject*>(Object), PPF_None);
			AddString(Value);
		}
	}
}

void FGuLiNavigationSourceHash::AddCollisionSource(const UPrimitiveComponent* Component)
{
	check(Component);
	AddString(Component->GetPathName());
	AddString(Component->GetClass()->GetPathName());
	AddTransform(Component->GetComponentTransform());
	AddVector(Component->Bounds.Origin);
	AddVector(Component->Bounds.BoxExtent);
	AddUInt64(Component->GetCollisionEnabled());
	AddUInt64(Component->GetCollisionObjectType());
	AddUInt64(Component->Mobility);
	for (int32 Channel = 0; Channel < ECC_MAX; ++Channel)
		AddUInt64(Component->GetCollisionResponseToChannel(static_cast<ECollisionChannel>(Channel)));
	for (const TCHAR* Name : { TEXT("BoxExtent"), TEXT("SphereRadius"), TEXT("CapsuleRadius"),
		TEXT("CapsuleHalfHeight"), TEXT("bFillCollisionUnderneathForNavmesh"), TEXT("bDynamicObstacle"),
		TEXT("bUseSystemDefaultObstacleAreaClass"), TEXT("AreaClassOverride") })
		AddProperty(Component, Name);
	if (const UBodySetup* Body = const_cast<UPrimitiveComponent*>(Component)->GetBodySetup())
	{
		// Shape-component body GUIDs are transient and change on reload. Geometry is the source.
		AddProperty(Body, TEXT("AggGeom"));
		AddProperty(Body, TEXT("CollisionTraceFlag"));
		AddProperty(Body, TEXT("BuildScale3D"));
		if (Cast<UStaticMesh>(Body->GetOuter())) AddString(Body->BodySetupGuid.ToString());
	}
	if (const UStaticMeshComponent* MeshComponent = Cast<UStaticMeshComponent>(Component))
	{
		const UStaticMesh* Mesh = MeshComponent->GetStaticMesh();
		AddString(GetPathNameSafe(Mesh));
		if (Mesh)
		{
			AddProperty(Mesh, TEXT("LODForCollision"));
			AddProperty(Mesh, TEXT("ComplexCollisionMesh"));
			if (Mesh->ComplexCollisionMesh)
			{
				if (const UBodySetup* ComplexBody = Mesh->ComplexCollisionMesh->GetBodySetup())
				{
					AddString(ComplexBody->BodySetupGuid.ToString());
					AddProperty(ComplexBody, TEXT("AggGeom"));
					AddProperty(ComplexBody, TEXT("BuildScale3D"));
				}
			}
			AddProperty(Mesh, TEXT("bHasNavigationData"));
			if (const UNavCollisionBase* NavCollision = Mesh->GetNavCollision())
			{
				for (const TCHAR* Name : { TEXT("bIsDynamicObstacle"), TEXT("AreaClass"), TEXT("BoxCollision"),
					TEXT("CylinderCollision"), TEXT("bGatherConvexGeometry") })
					AddProperty(NavCollision, Name);
			}
		}
	}
	if (const UInstancedStaticMeshComponent* Instances = Cast<UInstancedStaticMeshComponent>(Component))
	{
		AddUInt64(Instances->GetInstanceCount());
		for (int32 Index = 0; Index < Instances->GetInstanceCount(); ++Index)
		{
			FTransform Transform;
			if (Instances->GetInstanceTransform(Index, Transform, true)) AddTransform(Transform);
		}
	}
	if (const ULandscapeHeightfieldCollisionComponent* Landscape = Cast<ULandscapeHeightfieldCollisionComponent>(Component))
	{
		AddUInt64(Landscape->CollisionSizeQuads);
		AddUInt64(Landscape->SimpleCollisionSizeQuads);
		AddProperty(Landscape, TEXT("CollisionScale"));
		AddUInt64(Landscape->CollisionQuadFlags.Num());
		AddBytes(Landscape->CollisionQuadFlags.GetData(), Landscape->CollisionQuadFlags.Num());
		const auto AddBulk = [this](const auto& Bulk)
		{
			const int64 Size = Bulk.GetBulkDataSize();
			AddUInt64(Size);
			if (Size > 0)
			{
				const void* Data = Bulk.LockReadOnly();
				AddBytes(Data, Size);
				Bulk.Unlock();
			}
		};
		AddBulk(Landscape->CollisionHeightData);
		// Navigation depends on holes, not the texture/physical-material layer indices.
		// Canonicalize to hole sample positions so ordinary terrain painting stays a hit.
		TArray<int32> Holes;
		const int32 VisibilityIndex = Landscape->ComponentLayerInfos.IndexOfByKey(ALandscapeProxy::VisibilityLayer);
		if (VisibilityIndex != INDEX_NONE && Landscape->DominantLayerData.GetElementCount() > 0)
		{
			const uint8* Layers = static_cast<const uint8*>(Landscape->DominantLayerData.LockReadOnly());
			for (int32 Index = 0; Index < Landscape->DominantLayerData.GetElementCount(); ++Index)
				if (Layers[Index] == VisibilityIndex) Holes.Add(Index);
			Landscape->DominantLayerData.Unlock();
		}
		AddUInt64(Holes.Num());
		for (const int32 Index : Holes) AddUInt64(Index);
	}
}
