#include "Gameplay/GroundMech/UI/GuLiGroundMapWidget.h"
#include "Gameplay/Navigation/Grid/GuLiGridNavigation.h"
#include "Engine/Texture2D.h"

void UGuLiGroundMapWidget::ReleasePlanarTerrain()
{
    PlanarTerrainBrush.SetResourceObject(nullptr);
    if(PlanarTerrainTexture)PlanarTerrainTexture->ReleaseResource();
    PlanarTerrainTexture=nullptr;
}

void UGuLiGroundMapWidget::CachePlanarTerrain()
{
    const int32 Width=Terrain->Cells.X+1,Height=Terrain->Cells.Y+1;
    if(Width<2||Height<2||int64(Width)*Height!=Terrain->Heights.Num())return;
    const auto Heights=Terrain->Heights;
    MinimumHeight=DBL_MAX;MaximumHeight=-DBL_MAX;
    for(const float H:Heights){MinimumHeight=FMath::Min(MinimumHeight,double(H));MaximumHeight=FMath::Max(MaximumHeight,double(H));}
    const double HeightRange=FMath::Max(1.,MaximumHeight-MinimumHeight),Step=GuLiGrid::CellSize;
    const FVector Light=FVector(-.55,-.65,.7).GetSafeNormal();
    auto Sample=[&](int32 X,int32 Y){return double(Heights[FMath::Clamp(Y,0,Height-1)*Width+FMath::Clamp(X,0,Width-1)]);};
    TArray<FColor> Pixels;Pixels.SetNumUninitialized(Heights.Num());
    for(int32 Y=0;Y<Height;++Y)for(int32 X=0;X<Width;++X)
    {
        const double H=Sample(X,Y),DX=(Sample(X+1,Y)-Sample(X-1,Y))/(2*Step),DY=(Sample(X,Y+1)-Sample(X,Y-1))/(2*Step);
        const double Lit=FMath::Clamp(FVector::DotProduct(FVector(-DX*5,-DY*5,1).GetSafeNormal(),Light),0.,1.);
        const double Valley=FMath::Clamp(((Sample(X+3,Y)+Sample(X-3,Y)+Sample(X,Y+3)+Sample(X,Y-3))*.25-H)/Step,0.,.45);
        const double Elevation=(H-MinimumHeight)/HeightRange;
        // Fine, continuous hill shading preserves the source terrain's ridges;
        // the cached contour layer supplies the discrete elevation bands.
        const float Value=float(FMath::Clamp(.055+Lit*.43+Elevation*.08-Valley*.3,.025,.62));
        auto Tint=FMath::Lerp(PanelColor*.45f,SecondaryColor,Value);Tint.A=1;
        Pixels[Y*Width+X]=Tint.ToFColor(true);
    }
    PlanarTerrainTexture=UTexture2D::CreateTransient(Width,Height,PF_B8G8R8A8);
    if(!PlanarTerrainTexture)return;
    PlanarTerrainTexture->SRGB=true;PlanarTerrainTexture->NeverStream=true;
    PlanarTerrainTexture->Filter=TF_Bilinear;PlanarTerrainTexture->AddressX=TA_Clamp;PlanarTerrainTexture->AddressY=TA_Clamp;
    PlanarTerrainTexture->LODGroup=TEXTUREGROUP_UI;
    auto& Mip=PlanarTerrainTexture->GetPlatformData()->Mips[0];
    void* Destination=Mip.BulkData.Lock(LOCK_READ_WRITE);
    FMemory::Memcpy(Destination,Pixels.GetData(),Pixels.Num()*sizeof(FColor));Mip.BulkData.Unlock();
    PlanarTerrainTexture->UpdateResource();
    PlanarTerrainBrush.SetResourceObject(PlanarTerrainTexture);PlanarTerrainBrush.ImageSize=FVector2f(Width,Height);
    PlanarTerrainBrush.DrawAs=ESlateBrushDrawType::Image;

    Contours.Reset();
    auto AddTriangle=[&](const FVector& A,const FVector& B,const FVector& C)
    {
        const double Low=FMath::Min3(A.Z,B.Z,C.Z),High=FMath::Max3(A.Z,B.Z,C.Z);
        for(double Level=FMath::CeilToDouble(Low/ContourStep)*ContourStep;Level<High;Level+=ContourStep)
        {
            FVector Crossings[2];int32 Count=0;const FVector Points[3]{A,B,C};
            for(int32 Edge=0;Edge<3&&Count<2;++Edge)
            {
                const auto& P=Points[Edge];const auto& Q=Points[(Edge+1)%3];
                if((P.Z<=Level&&Q.Z>Level)||(Q.Z<=Level&&P.Z>Level))
                    Crossings[Count++]=FMath::Lerp(P,Q,(Level-P.Z)/(Q.Z-P.Z));
            }
            if(Count==2&&!Crossings[0].Equals(Crossings[1],.01))Contours.Emplace(Crossings[0],Crossings[1]);
        }
    };
    for(int32 Y=0;Y<Height-1;++Y)for(int32 X=0;X<Width-1;++X)
    {
        const FVector2D P=Terrain->Origin+FVector2D(X,Y)*Step;
        const FVector A(P,Sample(X,Y)),B(P+FVector2D(Step,0),Sample(X+1,Y));
        const FVector C(P+FVector2D(Step,Step),Sample(X+1,Y+1)),D(P+FVector2D(0,Step),Sample(X,Y+1));
        // Match the navigation height field's 00->11 diagonal, including saddles.
        AddTriangle(A,B,C);AddTriangle(A,C,D);
    }
    if(MapPan.IsNearlyZero())MapPan=Terrain->Origin+FVector2D(Terrain->Cells)*(Step*.5);
}
