#include "GuLiMapAuthoring.h"
#include "Dom/JsonObject.h"
#include "CompGeom/PolygonTriangulation.h"

namespace
{
bool Positive(double V) { return FMath::IsFinite(V) && V > 0.001; }
bool Heights(double A, double B) { return FMath::IsFinite(A) && FMath::IsFinite(B) && B - A > 0.001; }
double Cross(FVector2D A, FVector2D B, FVector2D C) { return (B.X-A.X)*(C.Y-A.Y)-(B.Y-A.Y)*(C.X-A.X); }
bool OnSegment(FVector2D A, FVector2D B, FVector2D P)
{
    return FMath::Abs(Cross(A,B,P)) < 0.000001 && P.X >= FMath::Min(A.X,B.X)-0.000001 &&
        P.X <= FMath::Max(A.X,B.X)+0.000001 && P.Y >= FMath::Min(A.Y,B.Y)-0.000001 && P.Y <= FMath::Max(A.Y,B.Y)+0.000001;
}
bool Intersects(FVector2D A, FVector2D B, FVector2D C, FVector2D D)
{
    const double C1=Cross(A,B,C), C2=Cross(A,B,D), C3=Cross(C,D,A), C4=Cross(C,D,B);
    return ((C1<0 && C2>0 || C1>0 && C2<0) && (C3<0 && C4>0 || C3>0 && C4<0)) ||
        OnSegment(A,B,C) || OnSegment(A,B,D) || OnSegment(C,D,A) || OnSegment(C,D,B);
}
bool PolygonValid(const FGuLiMapPolygonPrism& P)
{
    const int32 N=P.Vertices.Num();
    if (N<3 || N>4096 || !Heights(P.MinZ,P.MaxZ)) return false;
    double Area=0;
    for (int32 I=0; I<N; ++I)
    {
        const FVector2D A=P.Vertices[I], B=P.Vertices[(I+1)%N], C=P.Vertices[(I+2)%N];
        if (!FMath::IsFinite(A.X) || !FMath::IsFinite(A.Y) || (A-B).SizeSquared()<0.000001) return false;
        // Adjacent collinear reversal overlaps even though adjacent endpoints are permitted.
        if (FMath::Abs(Cross(A,B,C))<0.000001 && FVector2D::DotProduct(B-A,C-B)<0) return false;
        Area+=A.X*B.Y-A.Y*B.X;
        for (int32 J=I+1; J<N; ++J)
        {
            if (J==I+1 || (I==0 && J==N-1)) continue;
            if (Intersects(A,B,P.Vertices[J],P.Vertices[(J+1)%N])) return false;
        }
    }
    return FMath::IsFinite(Area) && FMath::Abs(Area)>0.000001;
}
void PrismMesh(const TArray<FVector2D>& Points, double MinZ, double MaxZ, bool Valid, FGuLiMapGeometryMesh& Mesh)
{
    const int32 N=Points.Num();
    for (double Z : {MinZ,MaxZ}) for (const auto& P:Points) Mesh.Vertices.Add(FVector(P.X,P.Y,Z));
    for (int32 I=0; I<N; ++I)
    {
        const int32 J=(I+1)%N;
        Mesh.Lines.Append({Mesh.Vertices[I],Mesh.Vertices[J],Mesh.Vertices[I+N],Mesh.Vertices[J+N],Mesh.Vertices[I],Mesh.Vertices[I+N]});
        if (Valid) Mesh.Triangles.Append({I,J,J+N,I,J+N,I+N});
    }
    if (!Valid) return;
    TArray<UE::Geometry::FIndex3i> Triangles;
    PolygonTriangulation::TriangulateSimplePolygon(Points,Triangles,false);
    for (const auto& T:Triangles) Mesh.Triangles.Append({T.A,T.C,T.B,T.A+N,T.B+N,T.C+N});
}

template<typename T> class TGeometry final : public IGuLiMapGeometryHandler
{
    FName Name;
public:
    explicit TGeometry(FName InName):Name(InName) {}
    FName GetShapeType() const override { return Name; }
    UScriptStruct* GetStruct() const override { return T::StaticStruct(); }
    bool Validate(const FInstancedStruct& Shape, FString& Error) const override
    {
        const T* P=Shape.GetPtr<T>();
        bool Valid=false;
        if (P)
        {
            if constexpr (std::is_same_v<T,FGuLiMapCylinder>) Valid=Positive(P->Radius)&&Heights(P->MinZ,P->MaxZ);
            if constexpr (std::is_same_v<T,FGuLiMapSphere>) Valid=Positive(P->Radius);
            if constexpr (std::is_same_v<T,FGuLiMapBox>) Valid=Positive(P->HalfExtents.X)&&Positive(P->HalfExtents.Y)&&Positive(P->HalfExtents.Z);
            if constexpr (std::is_same_v<T,FGuLiMapPolygonPrism>) Valid=PolygonValid(*P);
        }
        if (!Valid) Error=Name.ToString()+TEXT(": invalid dimensions, non-finite coordinates, or non-simple/degenerate polygon (3..4096 vertices).");
        return Valid;
    }
    TSharedRef<FJsonObject> ToJson(const FInstancedStruct& Shape) const override
    {
        auto J=MakeShared<FJsonObject>(); const T& P=Shape.Get<T>();
        if constexpr (std::is_same_v<T,FGuLiMapCylinder> || std::is_same_v<T,FGuLiMapSphere>) J->SetNumberField(TEXT("radius"),P.Radius);
        if constexpr (std::is_same_v<T,FGuLiMapCylinder> || std::is_same_v<T,FGuLiMapPolygonPrism>)
        { J->SetNumberField(TEXT("min_z"),P.MinZ); J->SetNumberField(TEXT("max_z"),P.MaxZ); }
        if constexpr (std::is_same_v<T,FGuLiMapBox>) J->SetField(TEXT("half_extents"),GuLiMap::VectorJson(P.HalfExtents));
        if constexpr (std::is_same_v<T,FGuLiMapPolygonPrism>)
        {
            TArray<TSharedPtr<FJsonValue>> V;
            for (const auto& Point:P.Vertices) V.Add(MakeShared<FJsonValueArray>(TArray<TSharedPtr<FJsonValue>>{MakeShared<FJsonValueNumber>(Point.X),MakeShared<FJsonValueNumber>(Point.Y)}));
            J->SetArrayField(TEXT("vertices"),V);
        }
        return J;
    }
    bool FromJson(const FJsonObject& Json, FInstancedStruct& Shape, FString& Error) const override
    {
        FInstancedStruct Candidate=FInstancedStruct::Make<T>(); T& P=Candidate.GetMutable<T>();
        TArray<FString> Keys; bool Ok=true;
        if constexpr (std::is_same_v<T,FGuLiMapCylinder> || std::is_same_v<T,FGuLiMapSphere>)
        { Keys.Add(TEXT("radius")); Ok &= Json.HasTypedField<EJson::Number>(TEXT("radius"))&&Json.TryGetNumberField(TEXT("radius"),P.Radius); }
        if constexpr (std::is_same_v<T,FGuLiMapCylinder> || std::is_same_v<T,FGuLiMapPolygonPrism>)
        { Keys.Append({TEXT("min_z"),TEXT("max_z")}); Ok &= Json.HasTypedField<EJson::Number>(TEXT("min_z"))&&Json.TryGetNumberField(TEXT("min_z"),P.MinZ); Ok &= Json.HasTypedField<EJson::Number>(TEXT("max_z"))&&Json.TryGetNumberField(TEXT("max_z"),P.MaxZ); }
        if constexpr (std::is_same_v<T,FGuLiMapBox>)
        { Keys.Add(TEXT("half_extents")); Ok &= GuLiMap::ReadVector(Json.TryGetField(TEXT("half_extents")),P.HalfExtents); }
        if constexpr (std::is_same_v<T,FGuLiMapPolygonPrism>)
        {
            Keys.Add(TEXT("vertices")); const TArray<TSharedPtr<FJsonValue>>* Values=nullptr;
            Ok &= Json.TryGetArrayField(TEXT("vertices"),Values); P.Vertices.Reset();
            if (Values) for (const auto& Value:*Values)
            {
                const TArray<TSharedPtr<FJsonValue>>* XY=nullptr; double X=0,Y=0;
                if (!Value->TryGetArray(XY) || XY->Num()!=2 || (*XY)[0]->Type!=EJson::Number || (*XY)[1]->Type!=EJson::Number || !(*XY)[0]->TryGetNumber(X) || !(*XY)[1]->TryGetNumber(Y)) { Ok=false; break; }
                P.Vertices.Add(FVector2D(X,Y));
            }
        }
        if (!GuLiMap::CheckKeys(Json,Keys,Error)) return false;
        if (!Ok) { Error=TEXT("Missing or incorrectly typed shape parameter."); return false; }
        if (!Validate(Candidate,Error)) return false;
        Shape=MoveTemp(Candidate); return true;
    }
    FGuLiMapGeometryMesh BuildMesh(const FInstancedStruct& Shape) const override
    {
        FGuLiMapGeometryMesh Mesh; const T* P=Shape.GetPtr<T>(); if (!P) return Mesh;
        FString Error; const bool Valid=Validate(Shape,Error);
        if constexpr (std::is_same_v<T,FGuLiMapPolygonPrism>) PrismMesh(P->Vertices,P->MinZ,P->MaxZ,Valid,Mesh);
        if constexpr (std::is_same_v<T,FGuLiMapBox>)
        {
            const FVector H=P->HalfExtents;
            PrismMesh({FVector2D(-H.X,-H.Y),FVector2D(H.X,-H.Y),FVector2D(H.X,H.Y),FVector2D(-H.X,H.Y)},-H.Z,H.Z,Valid,Mesh);
        }
        if constexpr (std::is_same_v<T,FGuLiMapCylinder>)
        {
            TArray<FVector2D> V; for (int32 I=0;I<64;++I) { const double A=2*PI*I/64; V.Add(FVector2D(FMath::Cos(A),FMath::Sin(A))*P->Radius); }
            PrismMesh(V,P->MinZ,P->MaxZ,Valid,Mesh);
        }
        if constexpr (std::is_same_v<T,FGuLiMapSphere>)
        {
            constexpr int32 S=32, R=16;
            for (int32 Y=0;Y<=R;++Y) for (int32 X=0;X<S;++X)
            {
                const double A=2*PI*X/S, B=PI*Y/R;
                Mesh.Vertices.Add(FVector(FMath::Sin(B)*FMath::Cos(A),FMath::Sin(B)*FMath::Sin(A),FMath::Cos(B))*P->Radius);
            }
            for (int32 Y=0;Y<R;++Y) for (int32 X=0;X<S;++X)
            {
                const int32 A=Y*S+X,B=Y*S+(X+1)%S,C=B+S,D=A+S;
                if (Valid) Mesh.Triangles.Append({A,B,C,A,C,D});
                if (Y==R/2 || X%8==0) Mesh.Lines.Append({Mesh.Vertices[A],Mesh.Vertices[B],Mesh.Vertices[A],Mesh.Vertices[D]});
            }
        }
        // Never pass NaNs to the renderer, including during invalid interactive previews.
        for (const auto& V:Mesh.Vertices) if (V.ContainsNaN()) return {};
        return Mesh;
    }
};
}

void GuLiMap::RegisterBuiltInGeometry()
{
    RegisterGeometry(MakeShared<TGeometry<FGuLiMapCylinder>>(TEXT("Cylinder")));
    RegisterGeometry(MakeShared<TGeometry<FGuLiMapSphere>>(TEXT("Sphere")));
    RegisterGeometry(MakeShared<TGeometry<FGuLiMapBox>>(TEXT("Box")));
    RegisterGeometry(MakeShared<TGeometry<FGuLiMapPolygonPrism>>(TEXT("PolygonPrism")));
}
