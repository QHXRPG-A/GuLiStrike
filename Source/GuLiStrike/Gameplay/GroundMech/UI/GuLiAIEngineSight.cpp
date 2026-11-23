#include "Gameplay/GroundMech/UI/GuLiAIEngineSight.h"
#include "Gameplay/GroundMech/Weapons/GuLiGroundMechWeaponComponent.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "Styling/CoreStyle.h"

namespace
{
constexpr double Tau=2.0*UE_PI;
constexpr double ReleaseSeconds=.23;
double Ease(double A,double B,double X)
{
    const double T=FMath::Clamp((X-A)/(B-A),0.,1.);
    return T*T*(3.-2.*T);
}
FVector2D Direction(double A){return {FMath::Cos(A),FMath::Sin(A)};}
FVector2D Rotate(FVector2D P,double A){const auto D=Direction(A);return {P.X*D.X-P.Y*D.Y,P.X*D.Y+P.Y*D.X};}
FColor Ink(FColor Tint,double Alpha){Tint.A=FMath::RoundToInt(255.*FMath::Clamp(Alpha,0.,1.));return Tint;}
const FColor Green(109,227,155), Signal(138,255,169), Hot(198,255,206), Back(4,25,21);
struct FTile { FVector2D Origin,Size; };
const FTile Ring{{6,6},{500,500}},Etch{{518,6},{500,500}},Hex{{6,518},{500,500}},Inner{{518,518},{316,316}},Rail{{518,850},{263,110}};
const FVector2D SolidUV(1018./1024,1012./1024);

// All linework shares one alpha atlas and one Slate draw submission.
struct FOpticsBatch
{
    const FGeometry& Geometry;
    TArray<FSlateVertex> Vertices;
    TArray<SlateIndex> Indices;
    SlateIndex Vertex(FVector2D P,FVector2D UV,FColor C)
    {
        return static_cast<SlateIndex>(Vertices.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(
            Geometry.GetAccumulatedRenderTransform(),FVector2f(P),FVector2f(UV),C)));
    }
    void Quad(FVector2D A,FVector2D B,FVector2D C,FVector2D D,FVector2D UV0,FVector2D UV1,FColor Color)
    {
        const auto I=Vertex(A,UV0,Color);
        Vertex(B,{UV1.X,UV0.Y},Color); Vertex(C,UV1,Color); Vertex(D,{UV0.X,UV1.Y},Color);
        Indices.Append({I,SlateIndex(I+1),SlateIndex(I+2),I,SlateIndex(I+2),SlateIndex(I+3)});
    }
    void Line(FVector2D A,FVector2D B,double Width,FColor C)
    {
        const FVector2D Along=(B-A).GetSafeNormal();
        const FVector2D N=FVector2D(-Along.Y,Along.X)*(.5*Width);
        Quad(A-N,B-N,B+N,A+N,SolidUV,SolidUV,C);
    }
    void Arc(FVector2D P,double Radius,double A,double B,double W,FColor C)
    {
        const int32 Steps=FMath::Max(1,FMath::CeilToInt(FMath::Abs(B-A)*36));
        for(int32 I=0;I<Steps;++I)Line(P+Direction(FMath::Lerp(A,B,double(I)/Steps))*Radius,
            P+Direction(FMath::Lerp(A,B,double(I+1)/Steps))*Radius,W,C);
    }
    // A radial fan clips the authored texture itself. Rotation and translation affect its geometry and preserve UVs.
    void Sector(const FTile& Tile,FVector2D P,double Side,double A,double B,double Angle,FColor C)
    {
        if(B<=A || !C.A)return;
        const auto UV=(Tile.Origin+Tile.Size*.5)/1024.;
        const auto Center=Vertex(P,UV,C);
        const int32 Steps=FMath::Max(1,FMath::CeilToInt((B-A)*16));
        for(int32 I=0;I<=Steps;++I)
        {
            const auto D=Direction(FMath::Lerp(A,B,double(I)/Steps));
            const auto Edge=Vertex(P+Rotate(D,Angle)*(Side*.5),UV+D*Tile.Size*(.5/1024.),C);
            if(I)Indices.Append({Center,SlateIndex(Edge-1),Edge});
        }
    }
};
}

void FGuLiAIEngineSight::Reset()
{
    Radius=0; Entry=-1; Markers.Reset();
}
void FGuLiAIEngineSight::Update(const UGuLiGroundMechWeaponComponent& Weapon,float DPI,double Now,bool PlayEntry)
{
    const auto& sight=Weapon.GetAILockSight();
    if(!Weapon.HasAIEngine() || !sight.bProjected){Reset();return;}
    if(PlayEntry)Entry=Now;
    else if(Entry<0)Entry=Now-1.;
    Center=sight.Center/DPI; Radius=sight.Radius/DPI; DesignRadius=sight.DesignRadius; HexRadius=sight.HexRadius;
    const auto& slots=Weapon.GetLockTargets();
    for(auto it=Markers.CreateIterator();it;++it)
    {
        auto& marker=it.Value();
        if(!slots.ContainsByPredicate([&](const auto& s){return s.Target==it.Key();}))
        {
            if(marker.Released<0){marker.Released=Now;marker.Seated=-1;}
            if(Now-marker.Released>=ReleaseSeconds){it.RemoveCurrent();continue;}
        }
        marker.Impulses.RemoveAll([Now](const FImpulse& i){return Now-i.At>=i.Duration;});
    }
    for(const auto& slot:slots)
    {
        auto& m=Markers.FindOrAdd(slot.Target);
        if(slot.IsLocked() && m.Seated<0)m.Seated=Now;
        if(!slot.IsLocked())m.Seated=-1;
        m.Released=-1; m.Progress=slot.Progress; m.TargetPoint=slot.TargetPoint/DPI;
        const int32 percent=slot.IsLocked()?100:FMath::Min(99,FMath::FloorToInt(slot.Progress*100));
        const int32 meters=FMath::RoundToInt(slot.DistanceMeters);
        if(m.LastPercent!=percent){m.LastPercent=percent;m.PercentLabel=FString::Printf(TEXT("%d%%"),percent);}
        if(m.LastMeters!=meters){m.LastMeters=meters;m.DistanceLabel=FString::Printf(TEXT("%d m"),meters);}
    }
}
void FGuLiAIEngineSight::Shot(double At,float Kick,float Jitter,float Duration)
{
    if(!IsVisible() || Duration<=0)return;
    for(auto& pair:Markers)
    {
        auto& marker=pair.Value;
        if(marker.Released>=0 || marker.Progress<1)continue;
        marker.Impulses.Add({At,Kick,Jitter,Duration,marker.NextSide});
        marker.NextSide=-marker.NextSide;
    }
}
void FGuLiAIEngineSight::Paint(const FGeometry& G,FSlateWindowElementList& Out,int32 Layer,double Now,float MaxKick) const
{
    if(!IsVisible() || !Atlas.GetResourceObject())return;
    FOpticsBatch B{G}; B.Vertices.Reserve(1600); B.Indices.Reserve(3600);
    const double Scale=Radius/DesignRadius,Age=Now-Entry;
    const double Open=Ease(.20,.80,Age),RailOpen=Ease(.16,.61,Age),Ambient=FMath::Max(0.,Age-.8);
    for(const double Sign:{-1.,1.})
    {
        if(Age<.55)
        {
            const double Head=7+(DesignRadius-6)*Ease(.02,.4,Age),Alpha=Ease(0,.055,Age)*(1-Ease(.32,.55,Age));
            const auto Tip=Center+FVector2D(Sign*Head*Scale,0);
            B.Line(Center+FVector2D(Sign*FMath::Max(6.,Head-44)*Scale,0),Tip,1.8*Scale,Ink(Signal,Alpha));
            B.Line(Tip-FVector2D(0,4*Scale),Tip+FVector2D(0,4*Scale),1.2*Scale,Ink(Hot,Alpha));
        }
        const auto Start=Center+FVector2D(Sign*(Radius+10*Scale),0);
        auto RailSlice=[&](double From,double To,FColor Tint)
        {
            From=FMath::Clamp(From,0.,RailOpen); To=FMath::Clamp(To,0.,RailOpen);
            if(To<=From)return;
            const auto Left=Start+FVector2D(Sign*211*Scale*From,-28*Scale),Right=Start+FVector2D(Sign*211*Scale*To,-28*Scale);
            const FVector2D H(0,56*Scale);
            B.Quad(Left,Right,Right+H,Left+H,(Rail.Origin+FVector2D(Rail.Size.X*(1-From),0))/1024.,
                (Rail.Origin+FVector2D(Rail.Size.X*(1-To),Rail.Size.Y))/1024.,Tint);
        };
        RailSlice(0,1,Ink(Green,.78));const double Packet=FMath::Frac(Ambient*(!Markers.IsEmpty()?1.25:.65))*1.4;
        RailSlice(Packet-.30,Packet-.08,Ink(Signal,.40));RailSlice(Packet-.09,Packet,Ink(Hot,.92));
        const double Origin=Sign>0?0.:UE_PI;
        const double Half=.5*UE_PI*Open;
        if(Half<=0)continue;
        B.Sector(Ring,Center,Radius*500./205,Origin-Half,Origin+Half,0,Ink(Green,.61));
        B.Arc(Center,Radius-7*Scale,Origin-Half,Origin+Half,1.1*Scale,Ink(Green,.38));
        B.Arc(Center,Radius-1*Scale,Origin-FMath::Min(.37,Half),Origin+FMath::Min(.37,Half),2.5*Scale,Ink(Signal,.76));
        B.Arc(Center,Radius-20*Scale,Origin-Half,Origin+Half,.85*Scale,Ink(Green,.22));
        if(Open<1)B.Arc(Center,Radius-7*Scale,Origin+Half-.04,Origin+Half,2.4*Scale,Ink(Hot,.95));
    }
    if(Age>.48)
    {
        const double Tint=Ease(.48,1,Age);
        B.Sector(Etch,Center,(Radius+25*Scale)*500./170,0,Tau,.025*FMath::Sin(Ambient*1.8),Ink(Green,.40*Tint));
        const double Sweep=Ambient*.82-.66*UE_PI;
        for(double Offset:{0.,double(UE_PI)})
            B.Sector(Ring,Center,Radius*500./205,Sweep+Offset,Sweep+Offset+.24,0,Ink(Hot,.77*Tint));
    }
    for(const auto& pair:Markers)
    {
        const auto& marker=pair.Value;
        const auto& TargetPoint=marker.TargetPoint;
        const auto& Impulses=marker.Impulses;
        const auto& PercentLabel=marker.PercentLabel;
        const auto& DistanceLabel=marker.DistanceLabel;
        const double Progress=marker.Progress,Released=marker.Released,Seated=marker.Seated;
        const bool active=Released<0;
        const double Release=Released>=0?Ease(0,ReleaseSeconds,Now-Released):0.;
        if((active || (Released>=0 && Release<1)) && Age>.15)
        {
            const double Alpha=(1-Release)*Ease(.15,.36,Age);
            FVector2D Kick=FVector2D::ZeroVector;double Twist=0;
            for(const auto& I:Impulses)
            {
                const double T=(Now-I.At)/I.Duration;
                if(T<0 || T>=1)continue;
                const double Decay=FMath::Square(1.-T),Bounce=Decay*FMath::Cos(Tau*1.4*T),Buzz=I.Jitter*Decay*FMath::Sin(Tau*2.2*T);
                Kick+=FVector2D(I.Side*(I.Kick*Bounce+.35*Buzz),-.84*I.Kick*Bounce+Buzz);
                Twist+=I.Side*Bounce*UE_PI/180.;
            }
            const double Length=Kick.Size();
            if(Length>MaxKick && Length>0)Kick*=MaxKick/Length;
            Twist=FMath::Clamp(Twist,-.028,.028);
            FVector2D P=TargetPoint+Kick*Scale;
            const FVector2D FromCenter=TargetPoint-Center;
            if (active && !Impulses.IsEmpty() && FromCenter.SizeSquared()>FMath::Square(Radius))
            {
                // At the rim, swing the marker around the aperture so recoil preserves its overlap.
                const double Distance=FromCenter.Size();
                const FVector2D Tangent=FVector2D(-FromCenter.Y,FromCenter.X)/Distance;
                Twist=(FVector2D::DotProduct(Kick,Tangent)+HexRadius*Twist)*Scale/Distance;
                P=Center+Rotate(FromCenter,Twist);
            }
            if(active && Progress<1)
            {
                const double Cycles=2*Progress,Phase=Cycles*Tau;
                const double Rot=(Cycles-FMath::Sin(Phase)/Tau)*UE_PI/3;
                const double Size=(HexRadius+21*(1-Ease(0,1,Progress)))*.61*(1+.145*(1-FMath::Cos(Phase)))*316./156*Scale;
                B.Sector(Inner,P,Size,0,Tau,Rot+Twist,Ink(Signal,Alpha*(.8+.16*Progress)));
                B.Sector(Inner,P,Size,-.64*UE_PI,-.35*UE_PI,Rot+Twist,Ink(Hot,Alpha*.70));
            }
            for(int32 I=0;I<6;++I)
            {
                const FGuLiAILockHexSide Side(I,Progress,HexRadius,Release);
                const double A=Side.Angle,ArtSize=Side.Radius*500./220*Scale;
                const auto Edge=P+Direction(A+Twist)*(Side.Outset*Scale);
                B.Sector(Hex,Edge,ArtSize,A-UE_PI/6,A+UE_PI/6,Twist,Ink(Signal,Alpha*(.64+.30*Side.Closure)));
                const double Fill=FMath::Clamp(double(Progress)*6-I,0.,1.);
                B.Sector(Hex,Edge,ArtSize,A-UE_PI/6,A-UE_PI/6+Fill*UE_PI/3,Twist,Ink(Hot,Alpha*.62));
                if(Seated>=0)
                {
                    const auto Tip=P+Direction(A+Twist-UE_PI/6)*HexRadius*Scale;
                    const double Flash=1-Ease(0,.22,Now-Seated);
                    B.Line(Tip-FVector2D(0,1.4*Scale),Tip+FVector2D(0,1.4*Scale),(2.2+Flash*2)*Scale,Ink(Hot,.80+Flash*.2));
                }
            }
        }
        FVector2D Label=FVector2D::ZeroVector;
        if(active && Age>.15)
        {
            const double R=104-42*Progress;
            const auto View=G.GetLocalSize();
            const double Side=TargetPoint.X+(R*.8+96)*Scale>View.X?-1.:1.;
            const auto Bend=TargetPoint+FVector2D(Side*(R*.8+10),R*.32-7)*Scale;
            Label=Bend+FVector2D(Side>0?8:-72,2)*Scale;
            Label.Y=FMath::Clamp(Label.Y,0.,FMath::Max(0.,double(View.Y)-54*Scale));
            const auto Lead=Ink(Signal,.66);
            B.Line(TargetPoint+FVector2D(Side*R*.79,R*.15)*Scale,Bend,1.2*Scale,Lead);
            B.Line(Bend,Bend+FVector2D(Side*33*Scale,0),1.2*Scale,Lead);
        }
        if(active && Age>.15)
        {
            auto DrawLabel=[&](const FString& Text,FVector2D P,bool Strong)
            {
                const auto Font=FCoreStyle::GetDefaultFontStyle(Strong?"Bold":"Regular",Strong?19:14);
                for(int32 Pass=0;Pass<2;++Pass)
                    FSlateDrawElement::MakeText(Out,Layer+1+Pass,G.ToPaintGeometry(FVector2D(92,32),
                        FSlateLayoutTransform(static_cast<float>(Scale),P+FVector2D(0,Pass?0:1.4*Scale))),
                        Text,Font,ESlateDrawEffect::None,FLinearColor(Pass?(Strong?Hot:Green):Back));
            };
            DrawLabel(PercentLabel,Label,true);DrawLabel(DistanceLabel,Label+FVector2D(0,24*Scale),false);
        }
    }
    if(B.Indices.Num())FSlateDrawElement::MakeCustomVerts(Out,Layer,FSlateApplication::Get().GetRenderer()->GetResourceHandle(Atlas),B.Vertices,B.Indices,nullptr,0,0);
}
