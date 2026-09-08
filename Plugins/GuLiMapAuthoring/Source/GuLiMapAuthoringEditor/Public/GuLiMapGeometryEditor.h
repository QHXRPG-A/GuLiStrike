#pragma once
#include "CoreMinimal.h"
#include "GuLiMapTypes.h"

struct FGuLiMapEditHandle
{
    int32 Id=0;
    FVector Position=FVector::ZeroVector;
    bool bEdge=false;
};

// Geometry-local controls. World/region transforms and transactions are owned by the host visualizer.
class GULIMAPAUTHORINGEDITOR_API IGuLiMapGeometryEditor
{
public:
    virtual ~IGuLiMapGeometryEditor() = default;
    virtual TArray<FGuLiMapEditHandle> GetHandles(const FInstancedStruct& Shape) const = 0;
    virtual bool MoveHandle(FInstancedStruct& Shape,int32 HandleId,const FVector& LocalDelta) const = 0;
    virtual bool InsertVertex(FInstancedStruct& Shape,int32 EdgeHandle) const { return false; }
    virtual bool DeleteVertex(FInstancedStruct& Shape,int32 VertexHandle) const { return false; }
    virtual bool ReverseWinding(FInstancedStruct& Shape) const { return false; }
};
namespace GuLiMapEditor
{
    GULIMAPAUTHORINGEDITOR_API bool RegisterGeometryEditor(FName Shape,TSharedRef<IGuLiMapGeometryEditor> Editor);
    GULIMAPAUTHORINGEDITOR_API void UnregisterGeometryEditor(FName Shape);
    GULIMAPAUTHORINGEDITOR_API TSharedPtr<IGuLiMapGeometryEditor> FindGeometryEditor(FName Shape);
}
